#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"
#include "record/CommitTake.hpp"
#include "support/TimingLimit.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <new>
#include <thread>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
thread_local bool countMemory = false;
thread_local std::size_t allocations = 0, frees = 0;
std::atomic<std::int64_t> liveMemory{0};
} // namespace
// These replacements belong only to this standalone test binary.
void* operator new(std::size_t size) {
    if (countMemory)
        ++allocations;
    if (auto p = std::malloc(size ? size : 1)) {
        liveMemory.fetch_add(1, std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
    if (countMemory && p)
        ++frees;
    if (p)
        liveMemory.fetch_sub(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    if (countMemory)
        ++allocations;
    const auto align = static_cast<std::size_t>(alignment);
#ifdef _WIN32
    if (auto p = _aligned_malloc(size ? size : 1, align))
#else
    if (auto p = std::aligned_alloc(align, ((size + align - 1) / align) * align))
#endif
    {
        liveMemory.fetch_add(1, std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _WIN32
    if (countMemory && p)
        ++frees;
    if (p)
        liveMemory.fetch_sub(1, std::memory_order_relaxed);
    _aligned_free(p);
#else
    ::operator delete(p);
#endif
}
void operator delete[](void* p, std::align_val_t alignment) noexcept {
    ::operator delete(p, alignment);
}
void operator delete(void* p, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(p, alignment);
}
void operator delete[](void* p, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(p, alignment);
}

namespace {
using namespace wavy;
using namespace wavy::record;
struct TemporaryFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("wavy-record-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    ~TemporaryFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
} // namespace
TEST_CASE("SPSC streams millions of samples in order") {
    RingBuffer ring(4096, 1);
    constexpr std::size_t total = 4000000;
    std::uint64_t checksum = 0;
    std::jthread producer([&] {
        std::array<float, 257> block;
        std::size_t sent = 0;
        while (sent < total) {
            const auto count = std::min(block.size(), total - sent);
            for (std::size_t i = 0; i < count; ++i)
                block[i] = float(sent + i);
            sent += ring.tryWrite(block.data(), count);
        }
    });
    std::array<float, 193> block;
    std::size_t received = 0;
    bool ordered = true;
    while (received < total) {
        const auto count = ring.tryRead(block.data(), block.size());
        for (std::size_t i = 0; i < count; ++i) {
            ordered &= block[i] == float(received + i);
            checksum += static_cast<std::uint64_t>(block[i]);
        }
        received += count;
    }
    producer.join();
    CHECK(ordered);
    CHECK(checksum == std::uint64_t(total) * (total - 1) / 2);
}
TEST_CASE("Ring overflow counts whole frames and wraps") {
    RingBuffer ring(7, 2);
    CHECK(ring.capacity() == 8);
    const float input[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    float out[12]{};
    CHECK(ring.tryWrite(input, 12) == 8);
    CHECK(ring.droppedFrames() == 2);
    CHECK(ring.tryWrite(input, 4) == 0);
    CHECK(ring.droppedFrames() == 4);
    CHECK(ring.tryRead(out, 6) == 6);
    CHECK(ring.tryWrite(input + 8, 4) == 4);
    CHECK(ring.tryRead(out + 6, 6) == 6);
    for (unsigned i = 0; i < 12; ++i)
        CHECK(out[i] == input[i]);
    ring.reset();
    CHECK(ring.droppedFrames() == 0);
    CHECK(ring.tryRead(out, 12) == 0);
}
TEST_CASE("Float WAV mono and stereo exact samples and crash prefix") {
    for (unsigned channels : {1u, 2u}) {
        TemporaryFile file;
        const float samples[] = {0.25f, -0.5f, 0.75f, 1.f, -1.f, 0.f, 0.125f, -0.125f};
        WavWriter writer;
        REQUIRE(writer.open(file.path, channels, 48000));
        REQUIRE(writer.write(samples, 4));
        REQUIRE(writer.close());
        {
            std::ifstream bytes(file.path, std::ios::binary);
            std::array<unsigned char, 58> header;
            bytes.read(reinterpret_cast<char*>(header.data()), header.size());
            REQUIRE(bytes.gcount() == 58);
            const auto word = [&](unsigned offset) {
                return std::uint32_t(header[offset]) | (std::uint32_t(header[offset + 1]) << 8) |
                       (std::uint32_t(header[offset + 2]) << 16) |
                       (std::uint32_t(header[offset + 3]) << 24);
            };
            CHECK(std::string(reinterpret_cast<char*>(header.data()), 4) == "RIFF");
            CHECK(word(4) == 50 + 4 * channels * 4);
            CHECK(word(16) == 18);
            CHECK(header[20] == 3);
            CHECK(header[22] == channels);
            CHECK(word(24) == 48000);
            CHECK(std::string(reinterpret_cast<char*>(header.data() + 38), 4) == "fact");
            CHECK(word(46) == 4);
            CHECK(std::string(reinterpret_cast<char*>(header.data() + 50), 4) == "data");
            CHECK(word(54) == 4 * channels * 4);
        }
        auto audio = loadAudioFile(file.path, 0);
        REQUIRE(audio);
        CHECK(audio->channels == channels);
        CHECK(audio->sampleRate == 48000);
        REQUIRE(audio->frames() == 4);
        for (unsigned i = 0; i < 4 * channels; ++i)
            CHECK(audio->samples[i] == samples[i]);
        std::filesystem::remove(file.path);
        REQUIRE(writer.open(file.path, channels, 48000));
        REQUIRE(writer.write(samples, 2));
        REQUIRE(writer.patch());
        REQUIRE(writer.write(samples + 2 * channels, 2));
        REQUIRE(writer.close(false));
        audio = loadAudioFile(file.path, 0);
        REQUIRE(audio);
        CHECK(audio->frames() == 2);
        for (unsigned i = 0; i < 2 * channels; ++i)
            CHECK(audio->samples[i] == samples[i]);
    }
}
TEST_CASE("WAV errors preserve a readable prefix and non-ASCII paths") {
    TemporaryFile file;
    file.path = file.path.parent_path() /
                std::filesystem::path(std::u8string(u8"录音-") + file.path.filename().u8string());
    WavWriter writer;
    CHECK_FALSE(writer.open(file.path, 3, 48000));
    REQUIRE(writer.open(file.path, 1, 48000));
    float sample = 0.5f;
    REQUIRE(writer.write(&sample, 1));
    CHECK_FALSE(writer.write(&sample, std::size_t{1} << 30));
    CHECK(writer.error().find("RIFF limit") != std::string::npos);
    CHECK_FALSE(writer.close());
    auto audio = loadAudioFile(file.path, 0);
    REQUIRE(audio);
    CHECK(audio->samples == std::vector<float>{sample});
}
TEST_CASE("Recorder punch-in, latency, peaks and repeated sessions allocate nothing in capture") {
    Recorder recorder;
    recorder.setInputLatencyFrames(3.4);
    TemporaryFile file;
    const float input[] = {0.25f, -0.5f, 0.75f, 1.f, -1.f, 0.f, 0.125f, -0.125f};
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        std::filesystem::remove(file.path);
        REQUIRE(recorder.arm(file.path, 1));
        REQUIRE(recorder.startAtFrame(5));
        allocations = frees = 0;
        countMemory = true;
        recorder.capture(input, 8, 0);
        recorder.capture(input, 8, 8);
        countMemory = false;
        CHECK(allocations == 0);
        CHECK(frees == 0);
        CHECK(recorder.peak(0) == 1.f);
        CHECK(recorder.peak(1) == 0.f);
        auto take = recorder.stop();
        CHECK(take.startFrame == 2);
        CHECK(take.lengthFrames == 11);
        CHECK(take.droppedFrames == 0);
        CHECK(take.channels == 1);
        CHECK_FALSE(recorder.failed());
        auto audio = loadAudioFile(file.path, 0);
        REQUIRE(audio);
        REQUIRE(audio->frames() == 11);
        for (unsigned i = 0; i < 11; ++i)
            CHECK(audio->samples[i] == input[i < 3 ? i + 5 : i - 3]);
    }
    std::filesystem::remove(file.path);
    recorder.setInputLatencyFrames(100);
    REQUIRE(recorder.arm(file.path, 2));
    REQUIRE(recorder.startAtFrame(2));
    recorder.capture(input, 4, 0);
    auto take = recorder.stop();
    CHECK(take.startFrame == 0);
    CHECK(take.lengthFrames == 2);
    auto audio = loadAudioFile(file.path, 0);
    REQUIRE(audio);
    CHECK(audio->samples == std::vector<float>(input + 4, input + 8));
}
TEST_CASE("Repeated recording sessions release all C++ allocations") {
    TemporaryFile file;
    const auto sessions = [&] {
        Recorder recorder;
        bool success = true;
        const float input[512]{};
        for (unsigned cycle = 0; cycle < 20; ++cycle) {
            std::filesystem::remove(file.path);
            if (!recorder.arm(file.path, 2) || !recorder.startAtFrame(0))
                return false;
            recorder.capture(input, 256, 0);
            const auto take = recorder.stop();
            success &= take.lengthFrames == 256 && take.droppedFrames == 0;
        }
        return success;
    };
    REQUIRE(sessions());
    const auto before = liveMemory.load(std::memory_order_relaxed);
    const auto success = sessions();
    const auto after = liveMemory.load(std::memory_order_relaxed);
    CHECK(success);
    CHECK(after == before);
}
TEST_CASE("Concurrent stop closes capture admission before draining") {
    TemporaryFile file;
    Recorder recorder;
    REQUIRE(recorder.arm(file.path, 2));
    REQUIRE(recorder.startAtFrame(0));
    std::atomic<bool> ready{false};
    std::jthread producer([&](std::stop_token token) {
        std::array<float, 512> input{};
        std::int64_t position = 0;
        while (!token.stop_requested()) {
            recorder.capture(input.data(), 256, position);
            position += 256;
            ready.store(true, std::memory_order_release);
        }
    });
    while (!ready.load(std::memory_order_acquire))
        std::this_thread::yield();
    auto take = recorder.stop();
    producer.request_stop();
    producer.join();
    CHECK(take.lengthFrames >= 256);
    CHECK_FALSE(recorder.isArmed());
    auto audio = loadAudioFile(file.path, 0);
    REQUIRE(audio);
    CHECK(audio->frames() == take.lengthFrames);
}
TEST_CASE("Overrun retains a continuous prefix without deadlock") {
    TemporaryFile file;
    Recorder recorder(48000, 8);
    REQUIRE(recorder.arm(file.path, 2));
    REQUIRE(recorder.startAtFrame(0));
    float input[32]{};
    recorder.capture(input, 16, 0);
    recorder.capture(input, 16, 16);
    const auto take = recorder.stop();
    CHECK(take.lengthFrames == 4);
    CHECK(take.droppedFrames == 28);
}
TEST_CASE("Commit take is one validated undo step") {
    timeline::Timeline timeline;
    timeline::History history(timeline);
    auto track = std::make_unique<timeline::AddTrack>("recording");
    auto* command = track.get();
    REQUIRE(history.execute(std::move(track)));
    const auto id = command->trackId();
    RecordedTake take{"take.wav", 42, 100, 2, 48000, 0};
    REQUIRE(commitTake(history, id, take));
    REQUIRE(timeline.findTrack(id)->clips.size() == 1);
    const auto clip = timeline.findTrack(id)->clips[0];
    CHECK(clip.start == 42);
    CHECK(clip.length == 100);
    CHECK(clip.source == "take.wav");
    REQUIRE(history.undo());
    CHECK(timeline.findTrack(id)->clips.empty());
    REQUIRE(history.redo());
    CHECK(timeline.findTrack(id)->clips[0] == clip);
    take.lengthFrames = 0;
    CHECK_FALSE(commitTake(history, id, take));
    CHECK_FALSE(commitTake(history, {999}, take));
}
TEST_CASE("Offline callback captures and monitors mono and stereo without allocations") {
    AudioEngine engine(AudioEngine::DeviceMode::NoDevice);
    REQUIRE(engine.start());
    CHECK(engine.inputDevices().empty());
    CHECK_FALSE(engine.inputAvailable());
    CHECK_FALSE(engine.setInputDevice(0, 0, 3));
    CHECK(engine.inputLatencyFrames() == 0);
    CHECK(engine.outputLatencyFrames() == 0);
    engine.setMonitorGain(0.5f);
    engine.setMonitorEnabled(true);
    const float input[] = {0.25f, -0.5f, 0.75f, 1.f, -1.f, 0.f, 0.125f, -0.125f};
    float output[8]{};
    TemporaryFile file;
    REQUIRE(engine.recorder().arm(file.path, 2));
    REQUIRE(engine.recorder().startAtFrame(2));
    engine.mixer().transport().play();
    allocations = frees = 0;
    countMemory = true;
    engine.feedInputOffline(input, 4, output);
    countMemory = false;
    CHECK(allocations == 0);
    CHECK(frees == 0);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(output[i] == input[i] * 0.5f);
    CHECK(engine.mixer().transport().positionFrames() == 4);
    const auto take = engine.recorder().stop();
    CHECK(take.startFrame == 2);
    CHECK(take.lengthFrames == 2);
    auto audio = loadAudioFile(file.path, 0);
    REQUIRE(audio);
    CHECK(audio->samples == std::vector<float>(input + 4, input + 8));
    engine.feedInputOffline(input, 4, output, 1);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(output[i * 2] == input[i] * 0.5f);
        CHECK(output[i * 2 + 1] == input[i] * 0.5f);
    }
    engine.setMonitorEnabled(false);
    engine.feedInputOffline(input, 4, output);
    for (auto value : output)
        CHECK(value == 0);
    engine.setMonitorEnabled(true);
    engine.setMonitorGain(0);
    engine.feedInputOffline(input, 4, output);
    for (auto value : output)
        CHECK(value == 0);
}
TEST_CASE("Monitoring adds to playback and paused transport does not record") {
    AudioEngine engine(AudioEngine::DeviceMode::NoDevice);
    REQUIRE(engine.start());
    timeline::Timeline timeline;
    timeline::AddTrack track("playback");
    track.apply(timeline);
    timeline::Clip clip;
    clip.source = "generated";
    clip.length = 32;
    timeline::AddClip add(track.trackId(), clip);
    add.apply(timeline);
    engine.mixer().publish(
        buildSnapshot(timeline, {{"generated", std::make_shared<AudioBuffer>(AudioBuffer{
                                                   48000, 1, std::vector<float>(32, 0.25f)})}}));
    TemporaryFile file;
    REQUIRE(engine.recorder().arm(file.path, 1));
    REQUIRE(engine.recorder().startAtFrame(1));
    engine.setMonitorGain(0.5f);
    engine.setMonitorEnabled(true);
    float input[] = {1.f, -0.5f, 0.5f, 0.25f};
    float output[8];
    engine.feedInputOffline(input, 4, output, 1);
    CHECK(engine.mixer().transport().positionFrames() == 0);
    engine.mixer().transport().play();
    engine.feedInputOffline(input, 4, output, 1);
    // Playback ramps in over a few milliseconds after play (declick), so the monitored input is
    // checked exactly on top of a playback part that lies between silence and its final 0.25.
    float previous = 0;
    for (unsigned frame = 0; frame < 4; ++frame) {
        const float playback = output[frame * 2] - input[frame] * 0.5f;
        CHECK(playback >= previous);
        CHECK(playback <= 0.25f);
        previous = playback;
        CHECK(output[frame * 2 + 1] == output[frame * 2]);
    }
    const auto take = engine.recorder().stop();
    CHECK(take.startFrame == 1);
    CHECK(take.lengthFrames == 3);
}
TEST_CASE("Meter decays and missing input preserves only the valid prefix") {
    TemporaryFile file;
    Recorder recorder;
    REQUIRE(recorder.arm(file.path, 1));
    REQUIRE(recorder.startAtFrame(0));
    const float sample = 1.f;
    recorder.capture(&sample, 1, 0);
    CHECK(recorder.peak(0) == 1.f);
    std::vector<float> silence(48000);
    recorder.capture(silence.data(), silence.size(), 1);
    CHECK(recorder.peak(0) == doctest::Approx(std::exp(-1.0 / 0.3)));
    recorder.capture(nullptr, 10, 48001);
    recorder.capture(&sample, 1, 48011);
    auto take = recorder.stop();
    CHECK(take.lengthFrames == 48001);
    CHECK(take.droppedFrames == 11);
    WavWriter writer;
    CHECK_FALSE(writer.open(file.path, 1, 48000));
    auto audio = loadAudioFile(file.path, 0);
    REQUIRE(audio);
    CHECK(audio->frames() == 48001);
}
TEST_CASE("Loop inside an offline callback retains the continuous first pass") {
    AudioEngine engine(AudioEngine::DeviceMode::NoDevice);
    REQUIRE(engine.start());
    TemporaryFile file;
    REQUIRE(engine.recorder().arm(file.path, 2));
    REQUIRE(engine.recorder().startAtFrame(2));
    engine.mixer().transport().seek(2);
    engine.mixer().transport().setLoop(2, 4);
    engine.mixer().transport().play();
    const float input[8]{};
    engine.feedInputOffline(input, 4);
    const auto take = engine.recorder().stop();
    CHECK(take.startFrame == 2);
    CHECK(take.lengthFrames == 2);
    CHECK(take.droppedFrames == 2);
    CHECK(engine.mixer().transport().positionFrames() == 2);
}
TEST_CASE("Ten minutes of stereo streams from ring to disk") {
    TemporaryFile file;
    RingBuffer ring(65536);
    WavWriter writer;
    REQUIRE(writer.open(file.path, 2, 48000));
    std::array<float, 16384> input{}, output{};
    constexpr std::size_t total = 48000 * 600 * 2;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t sent = 0; sent < total;) {
        const auto count = std::min(input.size(), total - sent);
        if (ring.tryWrite(input.data(), count) != count)
            FAIL("Unexpected benchmark overrun");
        const auto read = ring.tryRead(output.data(), output.size());
        if (!writer.write(output.data(), read / 2))
            FAIL(writer.error());
        sent += count;
    }
    REQUIRE(writer.close());
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    log::info("record", "10 minute stereo ring/disk benchmark: {:.3f} s", elapsed);
    CHECK(writer.frames() == 48000 * 600);
    CHECK(elapsed < timingLimit(1.0));
}
TEST_CASE("Optional real input device smoke check") {
    if (!std::getenv("WAVY_RECORD_DEVICE_SMOKE"))
        return;
    AudioEngine engine;
    const auto devices = engine.inputDevices();
    if (devices.empty()) {
        log::info("record", "No input devices; hardware smoke skipped");
        return;
    }
    const auto& input = devices.front();
    REQUIRE(engine.setInputDevice(input.id, 0, std::min(2u, input.inputChannels)));
    REQUIRE(engine.start());
    log::info("record", "Input smoke: available={}, input latency={}, output latency={}",
              engine.inputAvailable(), engine.inputLatencyFrames(), engine.outputLatencyFrames());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();
}
