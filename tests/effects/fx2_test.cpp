#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Log.hpp"
#include "effects/Delay.hpp"
#include "effects/Effect.hpp"
#include "effects/Reverb.hpp"
#include "effects/StereoWidth.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <new>
#include <numbers>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

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
using namespace wavy::effects;

std::vector<float> renderImpulse(Effect& effect, std::size_t frames) {
    std::vector<float> buffer(frames * 2, 0);
    buffer[0] = buffer[1] = 1;
    effect.process(buffer.data(), frames);
    return buffer;
}
float noise(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return float(state >> 8) / float(1u << 23) - 1.f;
}
std::vector<double> schroeder(const std::vector<float>& signal, std::size_t frames) {
    std::vector<double> decay(frames, 0);
    double sum = 0;
    for (std::size_t i = frames; i-- > 0;) {
        const double energy =
            double(signal[i * 2]) * signal[i * 2] + double(signal[i * 2 + 1]) * signal[i * 2 + 1];
        sum += energy;
        decay[i] = sum;
    }
    return decay;
}
double rt60FromSchroeder(const std::vector<double>& decay, double rate) {
    if (decay.empty() || decay.front() <= 0)
        return 0;
    const double total = decay.front();
    const auto timeFor = [&](double db) {
        for (std::size_t i = 0; i < decay.size(); ++i)
            if (10 * std::log10(std::max(decay[i], total * 1e-30) / total) <= db)
                return double(i) / rate;
        return double(decay.size()) / rate;
    };
    return 2 * (timeFor(-35) - timeFor(-5));
}
double correlation(const std::vector<float>& signal, std::size_t from, std::size_t to) {
    double lr = 0, ll = 0, rr = 0;
    for (std::size_t i = from; i < to; ++i) {
        lr += double(signal[i * 2]) * signal[i * 2 + 1];
        ll += double(signal[i * 2]) * signal[i * 2];
        rr += double(signal[i * 2 + 1]) * signal[i * 2 + 1];
    }
    const double denominator = std::sqrt(ll * rr);
    return denominator > 0 ? lr / denominator : 1;
}
template <typename T> void checkFiniteUnderSweep() {
    T effect;
    effect.prepare(48000, 512);
    std::array<float, 1024> block;
    std::uint32_t state = 1;
    for (std::size_t p = 0; p < effect.parameters().size(); ++p) {
        const auto& definition = effect.parameters().definitions()[p];
        for (float value : {definition.minimum, definition.maximum, 0.f, -1e30f, 1e30f,
                            std::numeric_limits<float>::quiet_NaN()}) {
            effect.parameters().set(p, value);
            for (std::size_t i = 0; i < 512; ++i)
                block[i * 2] = block[i * 2 + 1] = noise(state);
            effect.process(block.data(), 512);
            for (float v : block)
                REQUIRE(std::isfinite(v));
        }
    }
}
template <typename T> void checkProcessAllocatesNothing() {
    T effect;
    effect.prepare(48000, 512);
    std::array<float, 1024> block{};
    allocations = frees = 0;
    countMemory = true;
    for (int iteration = 0; iteration < 50; ++iteration) {
        for (std::size_t p = 0; p < effect.parameters().size(); ++p) {
            const auto& definition = effect.parameters().definitions()[p];
            effect.parameters().set(p, iteration % 2 ? definition.maximum : definition.minimum);
        }
        block.fill(.1f);
        effect.process(block.data(), 512);
    }
    countMemory = false;
    CHECK(allocations == 0);
    CHECK(frees == 0);
}
template <typename T> void checkLiveParameterEdits() {
    T effect;
    effect.prepare(48000, 512);
    const auto& definitions = effect.parameters().definitions();
    std::atomic<bool> done{false}, valid{true};
    std::thread writer([&] {
        for (int i = 0; i < 200000; ++i) {
            const auto p = std::size_t(i) % definitions.size();
            const float value =
                i % 3 == 0 ? definitions[p].minimum
                           : (i % 3 == 1 ? definitions[p].maximum : definitions[p].defaultValue);
            effect.parameters().set(p, value);
        }
        done.store(true);
    });
    std::array<float, 1024> block{};
    while (!done.load()) {
        block.fill(.1f);
        effect.process(block.data(), 512);
        for (float v : block)
            if (!std::isfinite(v))
                valid.store(false);
    }
    writer.join();
    CHECK(valid.load());
}
struct Chain {
    Reverb reverb;
    Delay delay;
    StereoWidth width;
};
} // namespace

TEST_CASE("Delay places the impulse at the requested sample time") {
    for (double rate : {44100., 48000., 96000.})
        for (double ms : {10., 30., 100., 250.}) {
            Delay delay;
            delay.parameters().set("time", float(ms));
            delay.parameters().set("feedback", 0);
            delay.parameters().set("mix", 1);
            delay.parameters().set("highcut", 20000);
            delay.parameters().set("lowcut", 20);
            delay.prepare(rate, 512);
            const auto n = std::size_t(std::llround(ms * .001 * rate));
            const auto ir = renderImpulse(delay, n + 2000);
            CHECK(ir[n * 2] == doctest::Approx(1).epsilon(1e-3));
            CHECK(ir[n * 2 + 1] == doctest::Approx(1).epsilon(1e-3));
            double stray = 0;
            for (std::size_t i = 0; i < ir.size(); ++i)
                if (i / 2 != n)
                    stray += std::abs(ir[i]);
            CHECK(stray < 1e-3);
        }
}
TEST_CASE("Delay feedback repeats decay by the feedback factor") {
    constexpr double rate = 48000;
    Delay delay;
    delay.parameters().set("time", 100);
    delay.parameters().set("feedback", .5f);
    delay.parameters().set("mix", 1);
    delay.parameters().set("highcut", 20000);
    delay.parameters().set("lowcut", 20);
    delay.prepare(rate, 512);
    const std::size_t n = 4800;
    const auto ir = renderImpulse(delay, n * 5);
    CHECK(ir[n * 2] == doctest::Approx(1).epsilon(1e-3));
    CHECK(ir[2 * n * 2] == doctest::Approx(.5).epsilon(1e-3));
    CHECK(ir[3 * n * 2] == doctest::Approx(.25).epsilon(1e-3));
    CHECK(ir[4 * n * 2] == doctest::Approx(.125).epsilon(1e-3));
}
TEST_CASE("Delay ping-pong alternates channels") {
    constexpr double rate = 48000;
    Delay delay;
    delay.parameters().set("time", 50);
    delay.parameters().set("feedback", .6f);
    delay.parameters().set("pingpong", 1);
    delay.parameters().set("mix", 1);
    delay.parameters().set("highcut", 20000);
    delay.parameters().set("lowcut", 20);
    delay.prepare(rate, 512);
    const std::size_t n = 2400;
    const auto ir = renderImpulse(delay, n * 4);
    const auto left = [&](std::size_t frame) { return std::abs(ir[frame * 2]); };
    const auto right = [&](std::size_t frame) { return std::abs(ir[frame * 2 + 1]); };
    CHECK(left(n) > .9);
    CHECK(right(n) < 1e-3);
    CHECK(left(2 * n) < 1e-3);
    CHECK(right(2 * n) == doctest::Approx(.6).epsilon(1e-3));
    CHECK(left(3 * n) == doctest::Approx(.36).epsilon(1e-3));
    CHECK(right(3 * n) < 1e-3);
}
TEST_CASE("Delay tempo sync derives the time from note and bpm") {
    constexpr double rate = 48000;
    struct Setting {
        float note;
        float bpm;
        double seconds;
    };
    for (const auto& setting :
         {Setting{3, 120, .5}, Setting{2, 120, .25}, Setting{3, 60, 1.}, Setting{5, 120, 2.}}) {
        Delay delay;
        delay.parameters().set("sync", 1);
        delay.parameters().set("note", setting.note);
        delay.parameters().set("bpm", setting.bpm);
        delay.parameters().set("mix", 1);
        delay.parameters().set("highcut", 20000);
        delay.parameters().set("lowcut", 20);
        delay.prepare(rate, 512);
        const auto n = std::size_t(std::llround(setting.seconds * rate));
        const auto ir = renderImpulse(delay, n + 1000);
        CHECK(ir[n * 2] == doctest::Approx(1).epsilon(1e-3));
    }
}
TEST_CASE("Delay time changes glide without discontinuity") {
    constexpr double rate = 48000;
    Delay delay;
    delay.parameters().set("time", 10);
    delay.parameters().set("mix", 1);
    delay.parameters().set("highcut", 20000);
    delay.parameters().set("lowcut", 20);
    delay.prepare(rate, 512);
    std::array<float, 1024> block;
    double phase = 0, previous = 0, worst = 0;
    bool first = true;
    for (int b = 0; b < 40; ++b) {
        if (b == 10)
            delay.parameters().set("time", 30);
        if (b == 25)
            delay.parameters().set("time", 5);
        for (std::size_t i = 0; i < 512; ++i) {
            block[i * 2] = block[i * 2 + 1] = float(std::sin(phase));
            phase += 2 * std::numbers::pi * 220 / rate;
        }
        delay.process(block.data(), 512);
        for (std::size_t i = 0; i < 512; ++i) {
            if (!first)
                worst = std::max(worst, std::abs(double(block[i * 2]) - previous));
            previous = block[i * 2];
            first = false;
        }
    }
    CHECK(worst < .2);
}
TEST_CASE("Delay modulation thickens without exceeding the glide bound") {
    constexpr double rate = 48000;
    Delay delay;
    delay.parameters().set("time", 30);
    delay.parameters().set("mix", 1);
    delay.parameters().set("mod_depth", 1);
    delay.parameters().set("mod_rate", 1);
    delay.parameters().set("highcut", 20000);
    delay.parameters().set("lowcut", 20);
    delay.prepare(rate, 512);
    std::array<float, 1024> block;
    double phase = 0, worst = 0, previous = 0;
    bool first = true;
    for (int b = 0; b < 200; ++b) {
        for (std::size_t i = 0; i < 512; ++i) {
            block[i * 2] = block[i * 2 + 1] = float(std::sin(phase));
            phase += 2 * std::numbers::pi * 220 / rate;
        }
        delay.process(block.data(), 512);
        for (std::size_t i = 0; i < 512; ++i) {
            if (!first)
                worst = std::max(worst, std::abs(double(block[i * 2]) - previous));
            previous = block[i * 2];
            first = false;
        }
    }
    CHECK(worst < .2);
}

TEST_CASE("Reverb impulse starts after the pre-delay") {
    constexpr double rate = 48000;
    Reverb reverb;
    reverb.parameters().set("decay", .6f);
    reverb.parameters().set("damping", 0);
    reverb.parameters().set("predelay", 50);
    reverb.parameters().set("wet", 1);
    reverb.parameters().set("dry", 0);
    reverb.parameters().set("lowcut", 20);
    reverb.parameters().set("highcut", 20000);
    reverb.prepare(rate, 512);
    const std::size_t onset = 2400;
    const auto ir = renderImpulse(reverb, 48000);
    double early = 0;
    for (std::size_t i = 0; i < onset; ++i)
        early += std::abs(ir[i * 2]) + std::abs(ir[i * 2 + 1]);
    CHECK(early == 0);
    double late = 0;
    for (std::size_t i = onset; i < onset + 4000; ++i)
        late += std::abs(ir[i * 2]);
    CHECK(late > 1e-6);
}
TEST_CASE("Reverb impulse response decays monotonically") {
    constexpr double rate = 48000;
    const std::size_t frames = 2 * std::size_t(rate);
    Reverb reverb;
    reverb.parameters().set("decay", 1.5f);
    reverb.parameters().set("damping", .2f);
    reverb.parameters().set("predelay", 0);
    reverb.parameters().set("wet", 1);
    reverb.parameters().set("dry", 0);
    reverb.parameters().set("lowcut", 20);
    reverb.parameters().set("highcut", 20000);
    reverb.prepare(rate, 512);
    const auto ir = renderImpulse(reverb, frames);
    const auto decay = schroeder(ir, frames);
    bool monotonic = true;
    for (std::size_t i = 1; i < decay.size(); ++i)
        if (decay[i] > decay[i - 1] + 1e-12)
            monotonic = false;
    CHECK(monotonic);
    CHECK(decay.back() < decay.front() * 1e-4);
}
TEST_CASE("Reverb RT60 matches the decay setting") {
    for (double rate : {44100., 48000., 96000.})
        for (double target : {.4, 1.2, 3.}) {
            Reverb reverb;
            reverb.parameters().set("decay", float(target));
            reverb.parameters().set("damping", 0);
            reverb.parameters().set("predelay", 0);
            reverb.parameters().set("wet", 1);
            reverb.parameters().set("dry", 0);
            reverb.parameters().set("width", 1);
            reverb.parameters().set("lowcut", 20);
            reverb.parameters().set("highcut", 20000);
            reverb.prepare(rate, 512);
            const auto frames = std::size_t(rate * (target + .6));
            const auto ir = renderImpulse(reverb, frames);
            const double measured = rt60FromSchroeder(schroeder(ir, frames), rate);
            log::info("fx2", "Reverb RT60 rate {} target {} s measured {} s", rate, target,
                      measured);
            CHECK(std::abs(measured / target - 1) < .25);
        }
}
TEST_CASE("Reverb stays bounded on full-scale noise at maximum settings") {
    for (double rate : {44100., 48000., 96000.}) {
        Reverb reverb;
        reverb.parameters().set("decay", 10);
        reverb.parameters().set("damping", 0);
        reverb.parameters().set("predelay", 200);
        reverb.parameters().set("width", 1);
        reverb.parameters().set("lowcut", 20);
        reverb.parameters().set("highcut", 20000);
        reverb.parameters().set("wet", 1);
        reverb.parameters().set("dry", 1);
        reverb.prepare(rate, 512);
        std::uint32_t state = 0x12345678;
        std::array<float, 1024> block;
        double peak = 0;
        const std::size_t blocks = std::size_t(rate * 60 / 512);
        for (std::size_t b = 0; b < blocks; ++b) {
            for (std::size_t i = 0; i < 512; ++i)
                block[i * 2] = block[i * 2 + 1] = noise(state);
            reverb.process(block.data(), 512);
            for (float v : block)
                peak = std::max(peak, std::abs(double(v)));
        }
        log::info("fx2", "Reverb noise peak at {} Hz: {}", rate, peak);
        CHECK(peak < 4);
    }
}
TEST_CASE("Reverb wet zero is the dry signal and mono input decorrelates") {
    constexpr double rate = 48000;
    Reverb dryReverb;
    dryReverb.parameters().set("wet", 0);
    dryReverb.parameters().set("dry", 1);
    dryReverb.prepare(rate, 512);
    std::uint32_t state = 7;
    std::array<float, 1024> block, original;
    for (std::size_t i = 0; i < 512; ++i)
        block[i * 2] = block[i * 2 + 1] = .5f * noise(state);
    original = block;
    dryReverb.process(block.data(), 512);
    for (std::size_t i = 0; i < block.size(); ++i)
        CHECK(std::abs(block[i] - original[i]) < 1e-7);
    Reverb reverb;
    reverb.parameters().set("decay", 1.5f);
    reverb.parameters().set("damping", .2f);
    reverb.parameters().set("predelay", 0);
    reverb.parameters().set("width", 1);
    reverb.parameters().set("wet", 1);
    reverb.parameters().set("dry", 0);
    reverb.parameters().set("lowcut", 20);
    reverb.parameters().set("highcut", 20000);
    reverb.prepare(rate, 512);
    const auto ir = renderImpulse(reverb, std::size_t(rate));
    CHECK(correlation(ir, 4800, ir.size() / 2) < .9);
}
TEST_CASE("Reverb silent tail is not slowed by denormals") {
    constexpr double rate = 48000;
    const auto measure = [&](bool excited) {
        Reverb reverb;
        reverb.parameters().set("decay", 10);
        reverb.parameters().set("damping", .3f);
        reverb.parameters().set("predelay", 0);
        reverb.parameters().set("wet", 1);
        reverb.parameters().set("dry", 0);
        reverb.prepare(rate, 512);
        std::uint32_t state = 99;
        std::array<float, 1024> block;
        if (excited) {
            for (int i = 0; i < 100; ++i) {
                for (std::size_t n = 0; n < 512; ++n)
                    block[n * 2] = block[n * 2 + 1] = noise(state);
                reverb.process(block.data(), 512);
            }
        }
        block.fill(0);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i)
            reverb.process(block.data(), 512);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const double fresh = measure(false), decayed = measure(true);
    log::info("fx2", "Reverb silent tail fresh {} s decayed {} s", fresh, decayed);
    CHECK(decayed < fresh * 10 + .01);
}

TEST_CASE("Stereo width handles the full mid/side range") {
    constexpr double rate = 48000;
    std::uint32_t state = 3;
    std::array<float, 1024> block, original;
    for (std::size_t i = 0; i < 512; ++i) {
        block[i * 2] = noise(state);
        block[i * 2 + 1] = noise(state);
    }
    original = block;
    {
        StereoWidth width;
        width.prepare(rate, 512);
        block = original;
        width.process(block.data(), 512);
        for (std::size_t i = 0; i < block.size(); ++i)
            CHECK(std::abs(block[i] - original[i]) < 1e-6);
    }
    {
        StereoWidth width;
        width.parameters().set("width", 0);
        width.prepare(rate, 512);
        block = original;
        width.process(block.data(), 512);
        for (std::size_t i = 0; i < 512; ++i)
            CHECK(std::abs(block[i * 2] - block[i * 2 + 1]) < 1e-6);
    }
    {
        StereoWidth width;
        width.parameters().set("width", 200);
        width.prepare(rate, 512);
        std::array<float, 2> side{1, -1};
        width.process(side.data(), 1);
        CHECK(side[0] == doctest::Approx(2));
        CHECK(side[1] == doctest::Approx(-2));
        std::array<float, 2> mid{1, 1};
        width.process(mid.data(), 1);
        CHECK(mid[0] == doctest::Approx(1));
        CHECK(mid[1] == doctest::Approx(1));
    }
    {
        StereoWidth width;
        width.prepare(rate, 512);
        for (float w : {0.f, 50.f, 100.f, 200.f}) {
            width.parameters().set("width", w);
            for (std::size_t i = 0; i < 512; ++i)
                block[i * 2] = block[i * 2 + 1] = noise(state);
            original = block;
            width.process(block.data(), 512);
            for (std::size_t i = 0; i < block.size(); ++i)
                CHECK(block[i] == doctest::Approx(original[i]).epsilon(1e-6));
        }
    }
}
TEST_CASE("Stereo width bass mono folds the low band only") {
    constexpr double rate = 48000;
    const auto sideRatio = [&](float bassMono, double frequency) {
        StereoWidth width;
        width.parameters().set("bass_mono", bassMono);
        width.prepare(rate, 512);
        std::array<float, 1024> block;
        double phase = 0, out = 0, in = 0;
        for (int b = 0; b < 200; ++b) {
            for (std::size_t i = 0; i < 512; ++i) {
                const double s = .5 * std::sin(phase);
                block[i * 2] = float(s);
                block[i * 2 + 1] = float(-s);
                phase += 2 * std::numbers::pi * frequency / rate;
            }
            const auto original = block;
            width.process(block.data(), 512);
            if (b > 100)
                for (std::size_t i = 0; i < 512; ++i) {
                    const double o = (double(block[i * 2]) - block[i * 2 + 1]) * .5;
                    const double j = (double(original[i * 2]) - original[i * 2 + 1]) * .5;
                    out += o * o;
                    in += j * j;
                }
        }
        return std::sqrt(out / in);
    };
    CHECK(sideRatio(500, 50) < .2);
    CHECK(sideRatio(0, 5000) > .99);
    CHECK(sideRatio(0, 5000) < 1.01);
}

TEST_CASE("Reverb, Delay and StereoWidth stay finite under extreme parameters") {
    checkFiniteUnderSweep<Reverb>();
    checkFiniteUnderSweep<Delay>();
    checkFiniteUnderSweep<StereoWidth>();
}
TEST_CASE("Reverb, Delay and StereoWidth allocate nothing while processing") {
    checkProcessAllocatesNothing<Reverb>();
    checkProcessAllocatesNothing<Delay>();
    checkProcessAllocatesNothing<StereoWidth>();
}
TEST_CASE("Reverb, Delay and StereoWidth accept live parameter edits") {
    checkLiveParameterEdits<Reverb>();
    checkLiveParameterEdits<Delay>();
    checkLiveParameterEdits<StereoWidth>();
}
TEST_CASE("64 tracks of Reverb Delay StereoWidth fit the real-time budget") {
    constexpr double rate = 48000;
    constexpr double budget = 512.0 / 48000 * 1e6;
    std::vector<Chain> tracks(64);
    for (auto& track : tracks) {
        track.reverb.parameters().set("decay", 2.5f);
        track.reverb.parameters().set("wet", .2f);
        track.delay.parameters().set("time", 375);
        track.delay.parameters().set("feedback", .35f);
        track.reverb.prepare(rate, 512);
        track.delay.prepare(rate, 512);
        track.width.prepare(rate, 512);
    }
    std::array<float, 1024> block;
    const auto run = [&] {
        for (auto& track : tracks) {
            block.fill(.1f);
            track.reverb.process(block.data(), 512);
            track.delay.process(block.data(), 512);
            track.width.process(block.data(), 512);
        }
    };
    for (int i = 0; i < 3; ++i)
        run();
    double best = std::numeric_limits<double>::max();
    for (int round = 0; round < 8; ++round) {
        const auto start = std::chrono::steady_clock::now();
        run();
        best = std::min(best, std::chrono::duration<double, std::micro>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
    }
    log::info("fx2", "64 tracks / Reverb + Delay + Stereo Width: {} us per 512-frame block", best);
#ifdef NDEBUG
    CHECK(best < budget * .6);
#endif
    const auto report = [&](const char* name, auto make, auto process) {
        std::vector<decltype(make())> effects;
        effects.reserve(64);
        for (int i = 0; i < 64; ++i)
            effects.push_back(make());
        for (auto& effect : effects)
            effect.prepare(rate, 512);
        std::array<float, 1024> buffer;
        buffer.fill(.1f);
        double fastest = std::numeric_limits<double>::max();
        for (int round = 0; round < 8; ++round) {
            const auto start = std::chrono::steady_clock::now();
            for (auto& effect : effects)
                process(effect, buffer.data());
            fastest = std::min(fastest, std::chrono::duration<double, std::micro>(
                                            std::chrono::steady_clock::now() - start)
                                            .count());
        }
        log::info("fx2", "{}: {} ns per frame", name, fastest * 1000 / (64 * 512));
    };
    report(
        "Reverb", [] { return Reverb{}; },
        [](Reverb& effect, float* buffer) { effect.process(buffer, 512); });
    report(
        "Delay", [] { return Delay{}; },
        [](Delay& effect, float* buffer) { effect.process(buffer, 512); });
    report(
        "StereoWidth", [] { return StereoWidth{}; },
        [](StereoWidth& effect, float* buffer) { effect.process(buffer, 512); });
}
