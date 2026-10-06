#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Log.hpp"
#include "io/SourceLibrary.hpp"
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <doctest/doctest.h>
#include <fstream>
#include <latch>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
struct TempDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("wavy-library-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { std::filesystem::create_directory(path); }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void integer(std::ostream& stream, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
        stream.put(static_cast<char>(value >> (8 * i)));
}
void wav(const std::filesystem::path& path, unsigned rate, unsigned channels,
         const std::vector<float>& samples, bool floating) {
    std::ofstream stream(path, std::ios::binary);
    const unsigned bytes = floating ? 4 : 2;
    const auto size = static_cast<std::uint32_t>(samples.size() * bytes);
    stream.write("RIFF", 4);
    integer(stream, 36 + size, 4);
    stream.write("WAVEfmt ", 8);
    integer(stream, 16, 4);
    integer(stream, floating ? 3 : 1, 2);
    integer(stream, channels, 2);
    integer(stream, rate, 4);
    integer(stream, rate * channels * bytes, 4);
    integer(stream, channels * bytes, 2);
    integer(stream, bytes * 8, 2);
    stream.write("data", 4);
    integer(stream, size, 4);
    for (float sample : samples)
        integer(stream,
                floating ? std::bit_cast<std::uint32_t>(sample)
                         : static_cast<std::uint16_t>(static_cast<std::int16_t>(sample * 32768)),
                bytes);
    REQUIRE(stream.good());
}
} // namespace
TEST_CASE("WAV loading publishes samples and peaks") {
    TempDirectory temp;
    const auto path = (temp.path / "source.wav").string();
    wav(path, 24000, 2, std::vector<float>(480, 0.25f), true);
    wavy::SourceLibrary library(48000);
    CHECK(library.state(path) == wavy::SourceLibrary::State::Unknown);
    CHECK_FALSE(library.get(path));
    library.request(path);
    library.waitIdle();
    const auto source = library.get(path);
    REQUIRE(source);
    CHECK(source->path == path);
    CHECK(source->audio->sampleRate == 48000);
    CHECK(source->audio->channels == 2);
    CHECK(std::abs(source->audio->frames() - 480) <= 1);
    CHECK(source->audio->samples[200] == doctest::Approx(0.25));
    std::vector<wavy::PeakPair> peaks;
    source->peaks->query(128, 256, 1, peaks);
    CHECK(peaks[0].min == doctest::Approx(0.25));
    CHECK(peaks[0].max == doctest::Approx(0.25));
    CHECK(library.memoryBytes() >= source->audio->samples.size() * sizeof(float));
}
TEST_CASE("Concurrent requests deduplicate decoding and callbacks") {
    wavy::SourceLibrary library(48000, 4);
    const std::string path = "generated:sine:440:0.25:1";
    std::atomic<unsigned> callbacks{0}, decodes{0};
    std::atomic<bool> workerCallback{true};
    const auto caller = std::this_thread::get_id();
    static std::atomic<unsigned>* loaded;
    loaded = &decodes;
    wavy::log::setLevel(wavy::log::Level::Info);
    wavy::log::setSink([](std::string_view line) {
        if (line.find("[source-library] loaded ") != line.npos)
            ++*loaded;
    });
    library.setOnReady([&](const std::string& name, bool ok) {
        if (!ok || name != path || std::this_thread::get_id() == caller)
            workerCallback = false;
        ++callbacks;
        library.get(name);
    });
    {
        std::vector<std::jthread> readers;
        for (int i = 0; i < 16; ++i)
            readers.emplace_back([&] {
                for (int j = 0; j < 1000; ++j) {
                    library.request(path);
                    library.get(path);
                    library.state(path);
                    library.error(path);
                    library.memoryBytes();
                }
            });
    }
    library.waitIdle();
    library.request(path);
    library.waitIdle();
    wavy::log::setSink(nullptr);
    CHECK(callbacks == 1);
    CHECK(decodes == 1);
    CHECK(workerCallback);
    CHECK(library.state(path) == wavy::SourceLibrary::State::Ready);
}
TEST_CASE("Failures publish once and bad generated specs fail") {
    TempDirectory temp;
    const auto missing = (temp.path / "missing.wav").string();
    const auto garbage = (temp.path / "garbage.wav").string();
    {
        std::ofstream stream(garbage);
        stream << "garbage";
    }
    wavy::SourceLibrary library(48000, 4);
    std::mutex mutex;
    std::unordered_map<std::string, unsigned> callbacks;
    std::atomic<bool> failed{true};
    library.setOnReady([&](const std::string& path, bool ok) {
        if (ok)
            failed = false;
        std::lock_guard lock(mutex);
        ++callbacks[path];
    });
    const std::vector<std::string> paths = {missing,
                                            garbage,
                                            "generated:bad:440",
                                            "generated:sine",
                                            "generated:sine:nan",
                                            "generated:sine:0",
                                            "generated:sine:24000",
                                            "generated:sine:440:2",
                                            "generated:sine:440:0.2:-1",
                                            "generated:sine:440:",
                                            "generated:sine:440:0.2:1:2",
                                            "generated:sine:440:0.2:1e300"};
    for (const auto& path : paths)
        library.request(path);
    library.waitIdle();
    for (const auto& path : paths) {
        CHECK(library.state(path) == wavy::SourceLibrary::State::Failed);
        CHECK_FALSE(library.get(path));
        CHECK(callbacks[path] == 1);
        library.request(path);
    }
    library.waitIdle();
    CHECK(failed);
    // Double parentheses: doctest would otherwise stringify LoadError through our string_view
    // toString().
    CHECK((library.error(missing) == wavy::LoadError::FileNotFound));
    CHECK((library.error("generated:bad:440") == wavy::LoadError::DecodeFailed));
    for (const auto& path : paths)
        CHECK(callbacks[path] == 1);
}
TEST_CASE("Generated waveforms have stereo samples and deterministic noise") {
    wavy::SourceLibrary library(48000);
    const std::string sine = "generated:sine:1000:0.5:0.1";
    for (const auto& path :
         {sine, std::string("generated:saw:1000:0.5:0.1"),
          std::string("generated:square:1000:0.5:0.1"), std::string("generated:noise:440:0.5:0.1"),
          std::string("generated:noise"), std::string("generated:sine:440")})
        library.request(path);
    library.waitIdle();
    const auto source = library.get(sine);
    REQUIRE(source);
    CHECK(source->audio->frames() == 4800);
    unsigned crossings = 0;
    for (std::size_t i = 0; i < 4800; ++i) {
        const auto sample = source->audio->samples[2 * i];
        CHECK(sample == source->audio->samples[2 * i + 1]);
        CHECK(sample ==
              doctest::Approx(0.5 * std::sin(2 * std::acos(-1) * i / 48)).epsilon(0.00001));
        if (i && sample > 0 && source->audio->samples[2 * (i - 1)] <= 0)
            ++crossings;
    }
    CHECK(crossings == 100);
    CHECK(library.get("generated:saw:1000:0.5:0.1")->audio->samples[0] == -0.5f);
    CHECK(library.get("generated:square:1000:0.5:0.1")->audio->samples[0] == 0.5f);
    auto defaults = library.get("generated:sine:440");
    REQUIRE(defaults);
    CHECK(defaults->audio->frames() == 480000);
    CHECK(defaults->audio->samples[600] == doctest::Approx(-0.2));
    wavy::SourceLibrary other(48000);
    other.request("generated:noise:440:0.5:0.1");
    other.waitIdle();
    CHECK(other.get("generated:noise:440:0.5:0.1")->audio->samples ==
          library.get("generated:noise:440:0.5:0.1")->audio->samples);
}
TEST_CASE("Budget evicts least recently used unowned sources and reloads") {
    const std::string a = "generated:sine:100:0.2:0.1";
    const std::string b = "generated:sine:200:0.2:0.1";
    const std::string c = "generated:sine:300:0.2:0.1";
    wavy::SourceLibrary measure(48000);
    measure.request(a);
    measure.waitIdle();
    const auto bytes = measure.memoryBytes();
    wavy::SourceLibrary library(48000, 1, bytes * 2);
    std::atomic<unsigned> loads{0};
    library.setOnReady([&](const std::string&, bool) { ++loads; });
    library.request(a);
    library.waitIdle();
    library.request(b);
    library.waitIdle();
    library.get(a);
    library.request(c);
    library.waitIdle();
    CHECK(library.state(b) == wavy::SourceLibrary::State::Unknown);
    auto held = library.get(a);
    REQUIRE(held);
    library.request(b);
    library.waitIdle();
    CHECK(library.get(a) == held);
    CHECK(library.state(c) == wavy::SourceLibrary::State::Unknown);
    CHECK(loads == 4);
    auto audio = held->audio;
    held.reset();
    library.request(c);
    library.waitIdle();
    CHECK(library.get(a));
    CHECK(library.memoryBytes() <= bytes * 2);
    wavy::SourceLibrary tiny(48000, 1, bytes);
    tiny.request(a);
    tiny.waitIdle();
    auto pinned = tiny.get(a);
    tiny.request(b);
    tiny.waitIdle();
    CHECK(tiny.get(a) == pinned);
    CHECK_FALSE(tiny.get(b));
    CHECK(tiny.memoryBytes() == bytes);
}
TEST_CASE("Concurrent readers and requesters share many decoding workers") {
    wavy::SourceLibrary library(48000, 4);
    std::atomic<unsigned> callbacks{0};
    library.setOnReady([&](const std::string&, bool) { ++callbacks; });
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < 12; ++t)
            threads.emplace_back([&, t] {
                for (int i = 0; i < 400; ++i) {
                    const auto path =
                        "generated:sine:" + std::to_string(100 + (i + t) % 20) + ":0.2:0.2";
                    library.request(path);
                    if (auto source = library.get(path)) {
                        std::vector<wavy::PeakPair> peaks;
                        source->peaks->query(0, source->audio->frames(), 8, peaks);
                    }
                }
            });
    }
    library.waitIdle();
    CHECK(callbacks == 20);
}
TEST_CASE("Destruction drops queued work and waits for active callbacks") {
    std::atomic<unsigned> callbacks{0};
    for (int repeat = 0; repeat < 10; ++repeat) {
        wavy::SourceLibrary library(48000, 4);
        library.setOnReady([&](const std::string&, bool) { ++callbacks; });
        for (int i = 0; i < 20; ++i)
            library.request("generated:sine:" + std::to_string(100 + i) + ":0.2:60");
    }
    std::latch entered(1), release(1);
    std::atomic<bool> exited{false}, joined{false};
    auto loading = std::make_unique<wavy::SourceLibrary>(48000, 1);
    loading->setOnReady([&](const std::string&, bool) {
        entered.count_down();
        release.wait();
        exited = true;
    });
    loading->request("generated:sine:440:0.2:0.01");
    entered.wait();
    std::jthread destroy([&] {
        loading.reset();
        joined = exited.load();
    });
    release.count_down();
    destroy.join();
    CHECK(joined);
    wavy::SourceLibrary library(48000, 1);
    library.setOnReady([](const std::string&, bool) { throw std::runtime_error("callback"); });
    library.request("generated:sine:440:0.2:0.01");
    library.waitIdle();
    CHECK(library.get("generated:sine:440:0.2:0.01"));
}
TEST_CASE("Twenty sixty-second stereo sources load with four workers") {
    wavy::SourceLibrary library(48000, 4);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
        library.request("generated:sine:" + std::to_string(100 + i) + ":0.2:60");
    library.waitIdle();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    wavy::log::info("source-library", "20 stereo sources (60 s), four workers: {:.3f} s", seconds);
    for (int i = 0; i < 20; ++i) {
        const auto source = library.get("generated:sine:" + std::to_string(100 + i) + ":0.2:60");
        REQUIRE(source);
        CHECK(source->audio->frames() == 60 * 48000);
    }
    CHECK(seconds < 25);
}
