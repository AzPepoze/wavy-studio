#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "audio/AudioEngine.hpp"
#include "audio/Mixer.hpp"
#include "core/Log.hpp"
#include "timeline/Commands.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <doctest/doctest.h>
#include <limits>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <thread>

namespace {
thread_local bool countMemory = false;
thread_local std::size_t allocations = 0, frees = 0;
} // namespace
// These replacements belong only to this standalone test binary.
void* operator new(std::size_t size) {
    if (countMemory)
        ++allocations;
    if (auto p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
    if (countMemory && p)
        ++frees;
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
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _WIN32
    if (countMemory && p)
        ++frees;
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
std::shared_ptr<AudioBuffer> source(unsigned channels = 2, std::size_t frames = 32) {
    return std::make_shared<AudioBuffer>(
        AudioBuffer{48000, channels, std::vector<float>(frames * channels, 1.f)});
}
std::unique_ptr<Snapshot> snapshot(std::shared_ptr<const AudioBuffer> audio, std::int64_t start = 0,
                                   std::int64_t length = 16) {
    timeline::Timeline timeline;
    timeline::AddTrack track("test");
    track.apply(timeline);
    timeline::Clip clip;
    clip.source = "generated";
    clip.start = start;
    clip.length = length;
    timeline::AddClip add(track.trackId(), clip);
    add.apply(timeline);
    return buildSnapshot(timeline, {{"generated", std::move(audio)}});
}
std::array<float, 32> render(Mixer& mixer) {
    std::array<float, 32> out;
    mixer.render(out.data(), 16);
    return out;
}
} // namespace
TEST_CASE("Clip positions, offsets, gains, fades and silence") {
    auto audio = source();
    for (std::size_t i = 0; i < audio->samples.size(); ++i)
        audio->samples[i] = static_cast<float>(i / 2 + 1);
    Mixer mixer;
    auto s = snapshot(audio, 3, 8);
    auto& clip = s->tracks[0].clips[0];
    clip.sourceOffset = 2;
    clip.gain = .5f;
    clip.fadeIn = 2;
    clip.fadeOut = 2;
    s->tracks[0].gain = .5f;
    mixer.publish(std::move(s));
    mixer.transport().play();
    auto out = render(mixer);
    for (int i = 0; i < 16; ++i) {
        float expected = 0;
        if (i >= 3 && i < 11) {
            const int local = i - 3;
            expected =
                (local + 3) * .25f * std::min(1.f, local / 2.f) * std::min(1.f, (7 - local) / 2.f);
        }
        CHECK(out[i * 2] == doctest::Approx(expected));
        CHECK(out[i * 2 + 1] == doctest::Approx(expected));
    }
    mixer.transport().seek(6);
    CHECK(render(mixer)[0] == doctest::Approx(1.5f));
    mixer.transport().pause();
    CHECK(render(mixer)[0] == 0);
    CHECK(mixer.transport().positionFrames() == 22);
    mixer.transport().stop();
    CHECK(mixer.transport().positionFrames() == 0);
    CHECK(render(mixer)[0] == 0);
}
TEST_CASE("Overlaps, mute, solo and channel mapping") {
    for (unsigned channels : {1u, 2u, 4u}) {
        auto audio = source(channels);
        for (std::size_t i = 0; i < audio->samples.size(); ++i)
            audio->samples[i] = static_cast<float>(i % channels + 1);
        Mixer mixer;
        auto s = snapshot(audio);
        s->tracks[0].clips.push_back(s->tracks[0].clips[0]);
        s->tracks.push_back(s->tracks[0]);
        s->tracks[1].muted = true;
        mixer.publish(std::move(s));
        mixer.transport().play();
        auto out = render(mixer);
        CHECK(out[0] == 2);
        CHECK(out[1] == (channels == 1 ? 2 : 4));
        auto solo = snapshot(audio);
        solo->tracks.push_back(solo->tracks[0]);
        solo->tracks[1].solo = true;
        solo->tracks[1].gain = 3;
        solo->anySolo = true;
        mixer.publish(std::move(solo));
        mixer.transport().seek(0);
        CHECK(render(mixer)[0] == 3);
        auto muted = snapshot(audio);
        muted->tracks[0].muted = true;
        mixer.publish(std::move(muted));
        mixer.transport().seek(0);
        CHECK(render(mixer)[0] == 0);
    }
}
TEST_CASE("Loop wrap is sample accurate across blocks") {
    auto audio = source(1);
    for (std::size_t i = 0; i < audio->samples.size(); ++i)
        audio->samples[i] = static_cast<float>(i);
    Mixer mixer;
    mixer.publish(snapshot(audio));
    mixer.transport().setLoop(2, 7);
    mixer.transport().seek(5);
    mixer.transport().play();
    for (int block = 0; block < 2; ++block) {
        auto out = render(mixer);
        for (int i = 0; i < 16; ++i)
            CHECK(out[i * 2] == 2 + (3 + block * 16 + i) % 5);
    }
    CHECK(mixer.transport().positionFrames() == 2);
}
TEST_CASE("Offline engine uses mixer and rendering never allocates or frees") {
    AudioEngine engine(AudioEngine::DeviceMode::NoDevice);
    REQUIRE(engine.start());
    engine.mixer().publish(snapshot(source()));
    engine.mixer().transport().play();
    std::array<float, 32> out;
    engine.renderOffline(out.data(), 16);
    CHECK(out[0] == 1);
    engine.mixer().publish(snapshot(source()));
    engine.mixer().transport().setLoop(0, 16);
    allocations = frees = 0;
    countMemory = true;
    for (int i = 0; i < 1000; ++i)
        engine.renderOffline(out.data(), 16);
    countMemory = false;
    CHECK(allocations == 0);
    CHECK(frees == 0);
}
TEST_CASE("Concurrent publication retires all source owners on the control thread") {
    std::atomic<unsigned> destroyed{0};
    std::atomic<bool> wrongThread{false}, done{false}, finite{true};
    const auto control = std::this_thread::get_id();
    {
        Mixer mixer;
        mixer.transport().play();
        std::thread consumer([&] {
            std::array<float, 1024> out;
            while (!done.load()) {
                mixer.render(out.data(), 512);
                for (auto sample : out)
                    if (!std::isfinite(sample))
                        finite.store(false);
            }
        });
        for (unsigned i = 0; i < 1000; ++i) {
            auto audio = std::shared_ptr<AudioBuffer>(
                new AudioBuffer{48000, 1, std::vector<float>(32, 1)}, [&](AudioBuffer* p) {
                    if (std::this_thread::get_id() != control)
                        wrongThread.store(true);
                    ++destroyed;
                    delete p;
                });
            mixer.publish(snapshot(std::move(audio)));
            mixer.transport().seek(0);
            mixer.transport().setLoop(0, 16);
        }
        done.store(true);
        consumer.join();
        mixer.publish(std::make_unique<Snapshot>());
        render(mixer);
        mixer.collectRetired();
        CHECK(destroyed.load() == 1000);
    }
    CHECK(finite.load());
    CHECK_FALSE(wrongThread.load());
}
TEST_CASE("64 tracks with four overlapping sine clips performance") {
    auto audio = source(2, 512);
    for (std::size_t i = 0; i < audio->samples.size(); ++i)
        audio->samples[i] = .01f * std::sin(static_cast<float>(i / 2) * .1f);
    auto s = snapshot(audio, 0, 512);
    s->tracks[0].clips.resize(4, s->tracks[0].clips[0]);
    s->tracks.resize(64, s->tracks[0]);
    Mixer mixer;
    mixer.publish(std::move(s));
    mixer.transport().setLoop(0, 512);
    mixer.transport().play();
    std::array<float, 1024> out;
    for (int i = 0; i < 10; ++i)
        mixer.render(out.data(), 512);
    std::array<double, 9> rounds;
    for (auto& us : rounds) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i)
            mixer.render(out.data(), 512);
        us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                 .count() /
             20;
    }
    std::sort(rounds.begin(), rounds.end());
    const double us = rounds.front();
    log::info("mixer", "64 tracks / 256 clips: min {} / median {} us per 512-frame block", us,
              rounds[rounds.size() / 2]);
#ifdef NDEBUG
    CHECK(us < (512.0 / 48000 * 1e6 * .2));
#endif
    for (auto sample : out)
        CHECK(std::isfinite(sample));
}
TEST_CASE("Source exhaustion and nonfinite inputs leave finite output") {
    auto audio = source(1, 4);
    audio->samples[1] = std::numeric_limits<float>::infinity();
    audio->samples[2] = std::numeric_limits<float>::quiet_NaN();
    Mixer mixer;
    mixer.publish(snapshot(audio));
    mixer.transport().play();
    auto out = render(mixer);
    CHECK(out[0] == 1);
    CHECK(out[2] == 0);
    CHECK(out[4] == 0);
    CHECK(out[6] == 1);
    for (std::size_t i = 8; i < out.size(); ++i)
        CHECK(out[i] == 0);
    mixer.transport().seek(std::numeric_limits<std::int64_t>::max());
    out = render(mixer);
    for (auto sample : out)
        CHECK(sample == 0);
}
TEST_CASE("Mixer fade regions match the sample-wise reference across seeks and loops") {
    for (unsigned channels : {1u, 2u})
        for (std::int64_t fadeIn : {0, 1, 7, 20})
            for (std::int64_t fadeOut : {0, 1, 8, 20}) {
                auto audio = source(channels, 19);
                for (std::size_t i = 0; i < audio->samples.size(); ++i)
                    audio->samples[i] = .1f * float(i + 1);
                auto s = snapshot(audio, 3, 21);
                s->tracks[0].gain = .7f;
                auto& clip = s->tracks[0].clips[0];
                clip.gain = .6f;
                clip.sourceOffset = 2;
                clip.fadeIn = fadeIn;
                clip.fadeOut = fadeOut;
                Mixer mixer;
                mixer.publish(std::move(s));
                mixer.transport().play();
                for (std::int64_t seek : {0, 4, 10, 18, 30, 2}) {
                    mixer.transport().seek(seek);
                    auto out = render(mixer);
                    for (std::size_t i = 0; i < 16; ++i) {
                        const auto local = seek + static_cast<std::int64_t>(i) - 3;
                        double gain = double(.7f) * .6f;
                        if (fadeIn)
                            gain *= std::min(1., double(local) / fadeIn);
                        if (fadeOut)
                            gain *= std::min(1., double(20 - local) / fadeOut);
                        for (unsigned c = 0; c < 2; ++c) {
                            const float expected =
                                local >= 0 && local < 17
                                    ? static_cast<float>(gain *
                                                         audio->samples[(local + 2) * channels +
                                                                        (channels == 1 ? 0 : c)])
                                    : 0;
                            CHECK(out[i * 2 + c] == doctest::Approx(expected).epsilon(1e-6));
                        }
                    }
                }
                mixer.transport().setLoop(4, 11);
                mixer.transport().seek(4);
                auto out = render(mixer);
                for (std::size_t i = 0; i < 16; ++i) {
                    const auto local = 1 + i % 7;
                    double gain = double(.7f) * .6f;
                    if (fadeIn)
                        gain *= std::min(1., double(local) / fadeIn);
                    if (fadeOut)
                        gain *= std::min(1., double(20 - local) / fadeOut);
                    for (unsigned c = 0; c < 2; ++c)
                        CHECK(out[i * 2 + c] ==
                              doctest::Approx(
                                  gain *
                                  audio->samples[(local + 2) * channels + (channels == 1 ? 0 : c)])
                                  .epsilon(1e-6));
                }
            }
}
TEST_CASE("Mixer silences nonfinite mono and stereo samples with master effects") {
    for (unsigned channels : {1u, 2u})
        for (bool processed : {false, true}) {
            auto audio = source(channels, 16);
            audio->samples[0] = std::numeric_limits<float>::quiet_NaN();
            audio->samples[1] = std::numeric_limits<float>::infinity();
            audio->samples[2] = -std::numeric_limits<float>::infinity();
            audio->samples[3] = std::numeric_limits<float>::max();
            auto s = snapshot(audio);
            if (processed) {
                effects::EffectFactory factory;
                for (const auto& entry : factory.entries())
                    s->masterEffects.push_back({factory.create(entry.id), false});
                for (auto& slot : s->masterEffects)
                    slot.effect->prepare(48000, 512);
            }
            Mixer mixer;
            mixer.publish(std::move(s));
            mixer.transport().play();
            const auto out = render(mixer);
            for (float sample : out)
                CHECK(std::isfinite(sample));
        }
}
