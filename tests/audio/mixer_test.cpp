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
std::shared_ptr<AudioBuffer> buffer(std::size_t frames, float value, unsigned channels = 1) {
    return std::make_shared<AudioBuffer>(
        AudioBuffer{48000, channels, std::vector<float>(frames * channels, value)});
}
struct ClipSpec {
    std::string source;
    std::int64_t start = 0, length = 1, sourceOffset = 0, fadeIn = 0, fadeOut = 0;
    float gain = 1.f;
    timeline::FadeCurve inCurve = timeline::FadeCurve::EqualPower;
    timeline::FadeCurve outCurve = timeline::FadeCurve::EqualPower;
    bool muted = false;
};
std::unique_ptr<Snapshot> multiTrack(unsigned rate,
                                     const std::vector<std::vector<ClipSpec>>& tracks,
                                     const SourceCache& cache) {
    timeline::Timeline timeline;
    timeline.sampleRate = rate;
    std::vector<timeline::TrackId> ids;
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        timeline::AddTrack track("t" + std::to_string(t));
        track.apply(timeline);
        ids.push_back(track.trackId());
    }
    for (std::size_t t = 0; t < tracks.size(); ++t)
        for (const auto& c : tracks[t]) {
            timeline::Clip clip;
            clip.source = c.source;
            clip.start = c.start;
            clip.length = c.length;
            clip.sourceOffset = c.sourceOffset;
            clip.fadeIn = c.fadeIn;
            clip.fadeOut = c.fadeOut;
            clip.gain = c.gain;
            clip.fadeInCurve = c.inCurve;
            clip.fadeOutCurve = c.outCurve;
            clip.muted = c.muted;
            timeline::AddClip add(ids[t], clip);
            add.apply(timeline);
        }
    return buildSnapshot(timeline, cache);
}
std::vector<float> renderFrames(Mixer& mixer, std::size_t frames) {
    std::vector<float> out(frames * 2);
    mixer.render(out.data(), frames);
    return out;
}
double riseLaw(timeline::FadeCurve curve, double p) {
    switch (curve) {
    case timeline::FadeCurve::Linear:
        return p;
    case timeline::FadeCurve::EqualPower:
        return std::sin(p * std::acos(-1.) / 2);
    case timeline::FadeCurve::Exponential:
        return (1 - std::exp(-4 * p)) / (1 - std::exp(-4.));
    }
    return p;
}
double fallLaw(timeline::FadeCurve curve, double p) {
    switch (curve) {
    case timeline::FadeCurve::Linear:
        return 1 - p;
    case timeline::FadeCurve::EqualPower:
        return std::cos(p * std::acos(-1.) / 2);
    case timeline::FadeCurve::Exponential:
        return 1 - riseLaw(curve, p);
    }
    return 1 - p;
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
            expected = (local + 3) * .25f * std::min(1.f, local / 48.f) *
                       std::min(1.f, (7 - local) / 48.f) * i / 144.f;
        }
        CHECK(out[i * 2] == doctest::Approx(expected));
        CHECK(out[i * 2 + 1] == doctest::Approx(expected));
    }
    mixer.transport().pause();
    render(mixer);
    CHECK(mixer.transport().positionFrames() == 32);
    mixer.transport().seek(6);
    CHECK(mixer.transport().positionFrames() == 6);
    CHECK(render(mixer)[0] == 0);
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
        CHECK(out[14] == doctest::Approx(2.f * 7 / 48 * 8 / 48 * 7 / 144));
        CHECK(out[15] == doctest::Approx((channels == 1 ? 2.f : 4.f) * 7 / 48 * 8 / 48 * 7 / 144));
        auto solo = snapshot(audio);
        solo->tracks.push_back(solo->tracks[0]);
        solo->tracks[1].solo = true;
        solo->tracks[1].gain = 3;
        solo->anySolo = true;
        Mixer soloMixer;
        soloMixer.publish(std::move(solo));
        soloMixer.transport().play();
        CHECK(render(soloMixer)[14] == doctest::Approx(3.f * 7 / 48 * 8 / 48 * 7 / 144));
        auto muted = snapshot(audio);
        muted->tracks[0].muted = true;
        Mixer mutedMixer;
        mutedMixer.publish(std::move(muted));
        mutedMixer.transport().play();
        CHECK(render(mutedMixer)[14] == 0);
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
        for (int i = 0; i < 16; ++i) {
            const auto local = 2 + (3 + block * 16 + i) % 5;
            const float expected =
                local * (local / 48.f) * ((15 - local) / 48.f) * (block * 16 + i) / 144.f;
            CHECK(out[i * 2] == doctest::Approx(expected));
        }
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
    CHECK(out[0] == 0);
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
    std::atomic<bool> wrongThread{false}, done{false}, finite{true}, memoryClean{true};
    const auto control = std::this_thread::get_id();
    {
        Mixer mixer;
        auto prepared = effects::EffectFactory{}.create("delay");
        prepared->parameters().set("time", 10);
        prepared->prepare(48000, 512);
        auto effect = std::shared_ptr<effects::Effect>(prepared.release(), [&](auto* p) {
            if (std::this_thread::get_id() != control)
                wrongThread.store(true);
            delete p;
        });
        mixer.transport().play();
        std::thread consumer([&] {
            std::array<float, 1024> out;
            while (!done.load()) {
                countMemory = true;
                mixer.render(out.data(), 512);
                countMemory = false;
                for (auto sample : out)
                    if (!std::isfinite(sample))
                        finite.store(false);
            }
            memoryClean.store(allocations == 0 && frees == 0);
        });
        for (unsigned i = 0; i < 1000; ++i) {
            auto audio = std::shared_ptr<AudioBuffer>(
                new AudioBuffer{48000, 1, std::vector<float>(32, 1)}, [&](AudioBuffer* p) {
                    if (std::this_thread::get_id() != control)
                        wrongThread.store(true);
                    ++destroyed;
                    delete p;
                });
            auto next = snapshot(std::move(audio));
            next->tracks[0].effects.push_back({effect, false});
            mixer.publish(std::move(next));
            effect->parameters().set("feedback", float(i % 9) / 10);
            mixer.transport().seek(0);
            mixer.transport().setLoop(0, 16);
        }
        done.store(true);
        consumer.join();
        mixer.publish(std::make_unique<Snapshot>());
        std::array<float, 1024> drain;
        mixer.render(drain.data(), 512);
        mixer.collectRetired();
        CHECK(destroyed.load() == 1000);
    }
    CHECK(finite.load());
    CHECK(memoryClean.load());
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
    CHECK(out[0] == 0);
    CHECK(out[2] == 0);
    CHECK(out[4] == 0);
    CHECK(out[6] == 0);
    for (std::size_t i = 8; i < out.size(); ++i)
        CHECK(out[i] == 0);
    mixer.transport().seek(std::numeric_limits<std::int64_t>::max());
    out = render(mixer);
    for (auto sample : out)
        CHECK(sample == 0);
}
TEST_CASE("Mixer fade regions match the sample-wise reference across seeks and loops") {
    for (unsigned channels : {1u, 2u})
        for (std::int64_t fadeIn : {0, 1, 7, 20, 80})
            for (std::int64_t fadeOut : {0, 1, 8, 20, 90}) {
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
                // This reference is the linear law, so pin both curves instead of the EqualPower
                // default.
                clip.fadeInCurve = timeline::FadeCurve::Linear;
                clip.fadeOutCurve = timeline::FadeCurve::Linear;
                for (std::int64_t seek : {0, 4, 10, 18, 30, 2}) {
                    Mixer mixer;
                    mixer.publish(std::make_unique<Snapshot>(*s));
                    mixer.transport().seek(seek);
                    mixer.transport().play();
                    auto out = render(mixer);
                    for (std::size_t i = 0; i < 16; ++i) {
                        const auto local = seek + static_cast<std::int64_t>(i) - 3;
                        double gain = double(.7f) * .6f;
                        gain *= std::min(1., double(local) / std::max<std::int64_t>(48, fadeIn));
                        gain *=
                            std::min(1., double(16 - local) / std::max<std::int64_t>(48, fadeOut));
                        gain *= double(i) / 144;
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
                Mixer mixer;
                mixer.publish(std::move(s));
                mixer.transport().setLoop(4, 11);
                mixer.transport().seek(4);
                mixer.transport().play();
                auto out = render(mixer);
                for (std::size_t i = 0; i < 16; ++i) {
                    const auto local = 1 + i % 7;
                    double gain = double(.7f) * .6f;
                    gain *= std::min(1., double(local) / std::max<std::int64_t>(48, fadeIn));
                    gain *= std::min(1., double(16 - local) / std::max<std::int64_t>(48, fadeOut));
                    gain *= double(i) / 144;
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

TEST_CASE("Transport and snapshot edits bound sine steps without render allocations") {
    constexpr float amplitude = .5f;
    constexpr double frequency = 100, rate = 48000;
    auto audio = source(1, 200000);
    for (std::size_t i = 0; i < audio->samples.size(); ++i)
        audio->samples[i] = amplitude * std::sin(2 * std::acos(-1.) * frequency * i / rate);
    Mixer mixer;
    mixer.publish(snapshot(audio, 0, 190000));
    mixer.transport().play();
    std::array<float, 128> out;
    float previous = 0, brokenPrevious = 0, maximum = 0, brokenMaximum = 0;
    std::int64_t brokenPosition = 0, brokenOffset = 0, brokenStart = 0;
    bool brokenPlaying = true;
    allocations = frees = 0;
    for (int block = 0; block < 400; ++block) {
        if (block % 11 == 0) {
            brokenPosition = 1000 + (block * 7919) % 100000;
            mixer.transport().seek(brokenPosition);
        }
        if (block % 13 == 0) {
            brokenStart = block % 2 ? 100 : 0;
            brokenOffset = block % 3 ? 250 : 0;
            auto next = snapshot(audio, brokenStart, 190000);
            next->tracks[0].clips[0].sourceOffset = brokenOffset;
            mixer.publish(std::move(next));
        }
        if (block % 31 == 0) {
            mixer.transport().pause();
            brokenPlaying = false;
        } else if (block % 31 == 3) {
            mixer.transport().play();
            brokenPlaying = true;
        }
        if (block % 89 == 0) {
            mixer.transport().stop();
            brokenPosition = 0;
            brokenPlaying = false;
        } else if (block % 89 == 4) {
            mixer.transport().play();
            brokenPlaying = true;
        }
        countMemory = true;
        mixer.render(out.data(), 64);
        countMemory = false;
        for (std::size_t i = 0; i < 64; ++i) {
            maximum = std::max(maximum, std::abs(out[i * 2] - previous));
            previous = out[i * 2];
            const auto local = brokenPosition - brokenStart;
            const float broken = brokenPlaying && local >= 0 && local < 190000
                                     ? audio->samples[local + brokenOffset]
                                     : 0;
            brokenMaximum = std::max(brokenMaximum, std::abs(broken - brokenPrevious));
            brokenPrevious = broken;
            if (brokenPlaying)
                ++brokenPosition;
        }
    }
    const double bound = amplitude * 2 * std::acos(-1.) * frequency / rate + amplitude / 144 +
                         2 * amplitude / 192 + 2 * amplitude / 48;
    log::info("declick", "sine maximum step {} / broken {} / bound {}", maximum, brokenMaximum,
              bound);
    CHECK(maximum < bound);
    CHECK(brokenMaximum > bound * 10);
    CHECK(allocations == 0);
    CHECK(frees == 0);
}

TEST_CASE("Clip edges reach silence within one millisecond and interior samples stay exact") {
    for (unsigned rate : {44100u, 48000u, 96000u}) {
        auto audio = source(2, rate / 10);
        audio->sampleRate = rate;
        timeline::Timeline timeline;
        timeline.sampleRate = rate;
        timeline::AddTrack track("edges");
        track.apply(timeline);
        timeline::Clip clip;
        clip.source = "dc";
        clip.start = rate / 100;
        clip.length = rate / 50;
        timeline::AddClip add(track.trackId(), clip);
        add.apply(timeline);
        Mixer mixer;
        mixer.publish(buildSnapshot(timeline, {{"dc", audio}}));
        mixer.transport().play();
        std::vector<float> out(rate / 10 * 2);
        mixer.render(out.data(), rate / 10);
        const auto fade = rate / 1000;
        CHECK(out[clip.start * 2] == 0);
        CHECK(out[(clip.start + clip.length - 1) * 2] == 0);
        for (unsigned i = 1; i <= fade; ++i) {
            CHECK(out[(clip.start + i) * 2] == doctest::Approx(float(i) / fade));
            CHECK(out[(clip.start + clip.length - 1 - i) * 2] == doctest::Approx(float(i) / fade));
        }
        for (auto i = clip.start + fade; i < clip.start + clip.length - 1 - fade; ++i)
            CHECK(out[i * 2] == 1);
    }
}

TEST_CASE("Transport jumps only at silence and pause position freezes after its ramp") {
    Mixer mixer;
    mixer.publish(snapshot(source(1, 48000), 0, 48000));
    mixer.transport().play();
    std::array<float, 1024> out;
    mixer.render(out.data(), 512);
    CHECK(out[0] == 0);
    CHECK(out[288] == 1);
    mixer.transport().seek(10000);
    mixer.render(out.data(), 144);
    CHECK(out[0] == 1);
    CHECK(out[286] == doctest::Approx(1.f / 144));
    CHECK(mixer.transport().positionFrames() == 10000);
    mixer.render(out.data(), 144);
    CHECK(out[0] == 0);
    CHECK(out[286] == doctest::Approx(143.f / 144));
    mixer.transport().pause();
    mixer.render(out.data(), 72);
    CHECK(mixer.transport().positionFrames() == 10216);
    mixer.render(out.data(), 144);
    CHECK(mixer.transport().positionFrames() == 10288);
    CHECK(out[142] == doctest::Approx(1.f / 144));
    CHECK(out[144] == 0);
    mixer.render(out.data(), 144);
    CHECK(mixer.transport().positionFrames() == 10288);
    mixer.transport().play();
    mixer.render(out.data(), 512);
    mixer.transport().stop();
    mixer.render(out.data(), 144);
    CHECK(out[0] == 1);
    CHECK(mixer.transport().positionFrames() == 0);
    mixer.render(out.data(), 512);
    for (auto sample : out)
        CHECK(sample == 0);
}

TEST_CASE("Render partitioning leaves transport and snapshot fades sample exact") {
    auto audio = source(1, 48000);
    for (std::size_t i = 0; i < audio->samples.size(); ++i)
        audio->samples[i] = .5f * std::sin(float(i) * .01f);
    Mixer blocks, samples;
    auto publish = [&](bool moved) {
        auto a = snapshot(audio, moved ? 30 : 0, 47000);
        auto b = snapshot(audio, moved ? 30 : 0, 47000);
        blocks.publish(std::move(a));
        samples.publish(std::move(b));
    };
    publish(false);
    blocks.transport().play();
    samples.transport().play();
    std::array<float, 1024> a, b;
    for (int block = 0; block < 8; ++block) {
        if (block == 1)
            publish(true);
        if (block == 2) {
            blocks.transport().seek(10000);
            samples.transport().seek(10000);
        }
        if (block == 3) {
            blocks.transport().pause();
            samples.transport().pause();
        }
        if (block == 4) {
            blocks.transport().play();
            samples.transport().play();
        }
        blocks.render(a.data(), 512);
        for (std::size_t i = 0; i < 512; ++i)
            samples.render(b.data() + i * 2, 1);
        for (std::size_t i = 0; i < a.size(); ++i)
            CHECK(a[i] == b[i]);
        CHECK(blocks.transport().positionFrames() == samples.transport().positionFrames());
        blocks.collectRetired();
        samples.collectRetired();
    }
}

TEST_CASE("Muted clips are dropped from the snapshot and render silently") {
    auto audio = buffer(64, 1.f);
    ClipSpec spec;
    spec.source = "a";
    spec.length = 32;
    spec.muted = true;
    auto s = multiTrack(48000, {{spec}}, {{"a", audio}});
    REQUIRE(s->tracks.size() == 1);
    CHECK(s->tracks[0].clips.empty());
    Mixer mixer;
    mixer.publish(std::move(s));
    mixer.transport().play();
    for (float sample : renderFrames(mixer, 32))
        CHECK(sample == 0.f);
}

TEST_CASE("User fade curves follow their documented laws") {
    constexpr std::int64_t fade = 1000, length = 3000, start = 200;
    for (auto curve : {timeline::FadeCurve::Linear, timeline::FadeCurve::EqualPower,
                       timeline::FadeCurve::Exponential}) {
        auto audio = buffer(length, 1.f);
        ClipSpec spec;
        spec.source = "c";
        spec.start = start;
        spec.length = length;
        spec.fadeIn = fade;
        spec.fadeOut = fade;
        spec.inCurve = curve;
        spec.outCurve = curve;
        Mixer mixer;
        mixer.publish(multiTrack(48000, {{spec}}, {{"c", audio}}));
        mixer.transport().play();
        const auto out = renderFrames(mixer, static_cast<std::size_t>(start + length));
        for (std::int64_t i : {0, 1, 100, 250, 500, 750, 999}) {
            const auto p = static_cast<double>(i) / fade;
            CHECK(out[static_cast<std::size_t>(start + i) * 2] ==
                  doctest::Approx(riseLaw(curve, p)).epsilon(1e-4));
            CHECK(out[static_cast<std::size_t>(start + length - 1 - i) * 2] ==
                  doctest::Approx(fallLaw(curve, 1 - p)).epsilon(1e-4));
        }
    }
}

TEST_CASE("Linear crossfade of identical constant clips keeps a constant level") {
    constexpr std::int64_t length = 2000, overlap = 1000;
    constexpr float level = .5f;
    auto audio = buffer(length, level);
    ClipSpec a;
    a.source = "c";
    a.length = length;
    a.inCurve = timeline::FadeCurve::Linear;
    a.outCurve = timeline::FadeCurve::Linear;
    ClipSpec b = a;
    b.start = overlap;
    Mixer mixer;
    mixer.publish(multiTrack(48000, {{a, b}}, {{"c", audio}}));
    mixer.transport().play();
    const auto out = renderFrames(mixer, static_cast<std::size_t>(overlap + length));
    for (std::int64_t i = 0; i < overlap; ++i)
        CHECK(std::abs(out[static_cast<std::size_t>(overlap + i) * 2] - level) < 1e-4f);
    CHECK(std::abs(out[500 * 2] - level) < 1e-4f);
    CHECK(std::abs(out[2500 * 2] - level) < 1e-4f);
}

TEST_CASE("EqualPower crossfade of uncorrelated clips keeps a constant RMS") {
    constexpr std::int64_t length = 2400, overlap = 1440;
    auto sine = [](double frequency) {
        auto audio =
            std::make_shared<AudioBuffer>(AudioBuffer{48000, 1, std::vector<float>(length)});
        for (std::size_t i = 0; i < audio->samples.size(); ++i)
            audio->samples[i] = .5f * float(std::sin(2 * std::acos(-1.) * frequency * i / 48000));
        return audio;
    };
    ClipSpec a;
    a.source = "a";
    a.length = length;
    ClipSpec b = a;
    b.source = "b";
    b.start = length - overlap;
    Mixer mixer;
    mixer.publish(multiTrack(48000, {{a, b}}, {{"a", sine(1000)}, {"b", sine(2500)}}));
    mixer.transport().play();
    const auto out = renderFrames(mixer, static_cast<std::size_t>(overlap + length));
    auto rms = [&](std::size_t from, std::size_t count) {
        double sum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double value = out[(from + i) * 2];
            sum += value * value;
        }
        return std::sqrt(sum / count);
    };
    const double reference = rms(200, 480);
    for (std::size_t window = 0; window + 480 <= overlap; window += 480) {
        const double db = 20 * std::log10(rms(overlap + window, 480) / reference);
        CHECK(std::abs(db) < 0.2);
    }
}

TEST_CASE("Crossfades duck under a nested or short later clip and return") {
    constexpr float level = .5f;
    constexpr std::int64_t length = 1000;
    {
        auto audio = buffer(length, level);
        ClipSpec a;
        a.source = "c";
        a.start = 200;
        a.length = length;
        a.inCurve = timeline::FadeCurve::Linear;
        a.outCurve = timeline::FadeCurve::Linear;
        ClipSpec b = a;
        b.start = 400;
        b.length = 600;
        Mixer mixer;
        mixer.publish(multiTrack(48000, {{a, b}}, {{"c", audio}}));
        mixer.transport().play();
        const auto out = renderFrames(mixer, length + 300);
        CHECK(out[300 * 2] == doctest::Approx(level));
        CHECK(out[700 * 2] == doctest::Approx(level));
        CHECK(std::abs(out[1000 * 2]) < .05f);
        CHECK(out[1100 * 2] ==
              doctest::Approx(level * fallLaw(timeline::FadeCurve::Linear, 1 - 100.0 / 199)));
        CHECK(out[1199 * 2] == doctest::Approx(level));
    }
    {
        auto audio = buffer(length, level);
        ClipSpec a;
        a.source = "c";
        a.start = 200;
        a.length = length;
        a.inCurve = timeline::FadeCurve::Linear;
        a.outCurve = timeline::FadeCurve::Linear;
        ClipSpec b = a;
        b.start = 800;
        b.length = 100;
        Mixer mixer;
        mixer.publish(multiTrack(48000, {{a, b}}, {{"c", audio}}));
        mixer.transport().play();
        const auto out = renderFrames(mixer, length + 300);
        CHECK(out[850 * 2] == doctest::Approx(level));
        CHECK(std::abs(out[900 * 2]) < .1f);
        CHECK(out[950 * 2] > 0.f);
        CHECK(out[950 * 2] < level);
        CHECK(out[1000 * 2] == doctest::Approx(level));
        CHECK(out[1199 * 2] == doctest::Approx(level));
    }
}

TEST_CASE("Three chained clips crossfade to a constant level") {
    constexpr float level = .5f;
    constexpr std::int64_t length = 2000, overlap = 1000;
    auto audio = buffer(length, level);
    ClipSpec a;
    a.source = "c";
    a.length = length;
    a.inCurve = timeline::FadeCurve::Linear;
    a.outCurve = timeline::FadeCurve::Linear;
    ClipSpec b = a;
    b.start = overlap;
    ClipSpec c = a;
    c.start = 2 * overlap;
    Mixer mixer;
    mixer.publish(multiTrack(48000, {{a, b, c}}, {{"c", audio}}));
    mixer.transport().play();
    const auto out = renderFrames(mixer, static_cast<std::size_t>(3 * overlap));
    CHECK(out[500 * 2] == doctest::Approx(level));
    for (std::int64_t i = 0; i < overlap; ++i)
        CHECK(std::abs(out[static_cast<std::size_t>(overlap + i) * 2] - level) < 1e-4f);
    for (std::int64_t i = 0; i < overlap; ++i)
        CHECK(std::abs(out[static_cast<std::size_t>(2 * overlap + i) * 2] - level) < 1e-4f);
    CHECK(out[2500 * 2] == doctest::Approx(level));
}

TEST_CASE("User fades multiply with the crossfade gains") {
    constexpr float level = .5f;
    constexpr std::int64_t length = 2000, overlap = 1000, fade = 1000;
    auto audio = buffer(length, level);
    ClipSpec a;
    a.source = "c";
    a.length = length;
    a.fadeOut = fade;
    a.inCurve = timeline::FadeCurve::Linear;
    a.outCurve = timeline::FadeCurve::Linear;
    ClipSpec b = a;
    b.start = overlap;
    b.fadeIn = fade;
    b.fadeOut = 0;
    Mixer mixer;
    mixer.publish(multiTrack(48000, {{a, b}}, {{"c", audio}}));
    mixer.transport().play();
    const auto out = renderFrames(mixer, static_cast<std::size_t>(overlap + length));
    const auto t = .5;
    const auto aGain = fallLaw(timeline::FadeCurve::Linear, t) *
                       fallLaw(timeline::FadeCurve::Linear, (1500. - 999) / fade);
    const auto bGain = riseLaw(timeline::FadeCurve::Linear, t) *
                       riseLaw(timeline::FadeCurve::Linear, (1500. - 1000) / fade);
    CHECK(out[1500 * 2] == doctest::Approx(level * (aGain + bGain)).epsilon(1e-3));
}

TEST_CASE("A one-frame overlap crossfades without a bump") {
    constexpr float level = .5f;
    auto audio = buffer(200, level);
    ClipSpec a;
    a.source = "c";
    a.start = 200;
    a.length = 100;
    a.inCurve = timeline::FadeCurve::Linear;
    a.outCurve = timeline::FadeCurve::Linear;
    ClipSpec b = a;
    b.start = 299;
    Mixer mixer;
    mixer.publish(multiTrack(48000, {{a, b}}, {{"c", audio}}));
    mixer.transport().play();
    const auto out = renderFrames(mixer, 450);
    CHECK(out[298 * 2] == doctest::Approx(level));
    CHECK(out[299 * 2] == doctest::Approx(level));
    CHECK(out[300 * 2] == doctest::Approx(level));
}

TEST_CASE("Overlapping clips on different tracks still sum") {
    constexpr float one = .5f, two = .25f;
    ClipSpec a;
    a.source = "a";
    a.length = 1000;
    ClipSpec b;
    b.source = "b";
    b.length = 1000;
    Mixer mixer;
    mixer.publish(
        multiTrack(48000, {{a}, {b}}, {{"a", buffer(1000, one)}, {"b", buffer(1000, two)}}));
    mixer.transport().play();
    const auto out = renderFrames(mixer, 1000);
    CHECK(out[500 * 2] == doctest::Approx(one + two));
    CHECK(out[900 * 2] == doctest::Approx(one + two));
}

TEST_CASE("Crossfade boundaries are independent of render block size") {
    constexpr std::int64_t length = 2000, overlap = 1000;
    auto audio = buffer(length, .5f);
    ClipSpec a;
    a.source = "c";
    a.length = length;
    a.fadeIn = 200;
    a.inCurve = timeline::FadeCurve::EqualPower;
    a.outCurve = timeline::FadeCurve::Linear;
    ClipSpec b = a;
    b.start = overlap;
    b.fadeOut = 300;
    auto s = multiTrack(48000, {{a, b}}, {{"c", audio}});
    auto renderBlocks = [](Mixer& mixer, std::size_t total, std::size_t block) {
        std::vector<float> out(total * 2);
        for (std::size_t done = 0; done < total;) {
            const auto count = std::min(block, total - done);
            mixer.render(out.data() + done * 2, count);
            done += count;
        }
        return out;
    };
    Mixer reference;
    reference.publish(std::make_unique<Snapshot>(*s));
    reference.transport().play();
    const auto expected = renderBlocks(reference, overlap + length, 512);
    for (std::size_t block : {1u, 7u, 64u}) {
        Mixer mixer;
        mixer.publish(std::make_unique<Snapshot>(*s));
        mixer.transport().play();
        const auto actual = renderBlocks(mixer, overlap + length, block);
        CHECK(actual == expected);
    }
}

TEST_CASE("Crossfade rendering never allocates or frees") {
    constexpr std::int64_t length = 2000, overlap = 1000;
    auto audio = buffer(length, .5f);
    ClipSpec a;
    a.source = "c";
    a.length = length;
    a.fadeIn = 200;
    a.inCurve = timeline::FadeCurve::EqualPower;
    ClipSpec b = a;
    b.start = overlap;
    b.fadeOut = 300;
    Mixer mixer;
    mixer.publish(multiTrack(48000, {{a, b}}, {{"c", audio}}));
    mixer.transport().play();
    std::array<float, 128> out;
    allocations = frees = 0;
    countMemory = true;
    for (int i = 0; i < 1000; ++i)
        mixer.render(out.data(), 64);
    countMemory = false;
    CHECK(allocations == 0);
    CHECK(frees == 0);
}
