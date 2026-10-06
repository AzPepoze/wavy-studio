#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Log.hpp"
#include "effects/DeEsser.hpp"
#include "effects/Limiter.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <memory>
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
constexpr double rate = 48000;
constexpr double pi = std::numbers::pi;
double dbToGain(double db) { return std::pow(10., db / 20); }
void processBlocks(Effect& effect, float* interleaved, std::size_t frames) {
    for (std::size_t offset = 0; offset < frames; offset += 512) {
        const auto n = std::min<std::size_t>(512, frames - offset);
        effect.process(interleaved + offset * 2, n);
    }
}
std::vector<float> tone(double frequency, double amplitude, std::size_t frames) {
    std::vector<float> result(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto sample = static_cast<float>(amplitude * std::sin(2 * pi * frequency * i / rate));
        result[i * 2] = result[i * 2 + 1] = sample;
    }
    return result;
}
double rms(const std::vector<float>& samples, std::size_t startFrame = 0) {
    double sum = 0;
    std::size_t count = 0;
    for (std::size_t i = startFrame; i < samples.size() / 2; ++i) {
        sum += double(samples[i * 2]) * samples[i * 2] +
               double(samples[i * 2 + 1]) * samples[i * 2 + 1];
        count += 2;
    }
    return std::sqrt(sum / count);
}
double peakAbs(const std::vector<float>& samples) {
    double peak = 0;
    for (std::size_t i = 0; i < samples.size() / 2; ++i)
        peak = std::max(
            {peak, std::abs(double(samples[i * 2])), std::abs(double(samples[i * 2 + 1]))});
    return peak;
}
double binAmplitude(const std::vector<float>& samples, double frequency, std::size_t startFrame) {
    std::complex<double> sum{};
    const auto step = std::polar(1., -2 * pi * frequency / rate);
    std::complex<double> phase = std::pow(step, double(startFrame));
    std::size_t count = 0;
    for (std::size_t i = startFrame; i < samples.size() / 2; ++i) {
        sum += double(samples[i * 2]) * phase;
        phase *= step;
        ++count;
    }
    return 2 * std::abs(sum) / count;
}
void checkExtremeSweep(Effect& effect) {
    effect.prepare(rate, 512);
    std::array<float, 1024> block;
    for (int iteration = 0; iteration < 200; ++iteration) {
        for (std::size_t p = 0; p < effect.parameters().size(); ++p) {
            const auto& definition = effect.parameters().definitions()[p];
            effect.parameters().set(p, iteration % 2 ? definition.maximum : definition.minimum);
        }
        block.fill(.3f);
        effect.process(block.data(), 512);
        for (float v : block)
            REQUIRE(std::isfinite(v));
    }
}
void checkNonFiniteInput(Effect& effect) {
    effect.prepare(rate, 512);
    std::array<float, 1024> block;
    for (float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
        block.fill(bad);
        effect.process(block.data(), 512);
        for (float v : block)
            REQUIRE(std::isfinite(v));
    }
    effect.reset();
    block.fill(.5f);
    effect.process(block.data(), 512);
    for (float v : block)
        REQUIRE(std::isfinite(v));
}
} // namespace

TEST_CASE("Limiter and De-esser are registered with vocal defaults") {
    EffectFactory factory;
    auto limiter = factory.create("limiter");
    REQUIRE(limiter);
    CHECK(limiter->parameters().get("ceiling") == doctest::Approx(-1));
    CHECK(limiter->parameters().get("release") == doctest::Approx(100));
    auto deesser = factory.create("de_esser");
    REQUIRE(deesser);
    CHECK(deesser->parameters().get("frequency") == doctest::Approx(6500));
    CHECK(deesser->parameters().get("ratio") == doctest::Approx(3));
    bool limiterFound = false, deEsserFound = false;
    for (const auto& entry : factory.entries()) {
        limiterFound |= entry.id == "limiter" && entry.displayName == "Limiter";
        deEsserFound |= entry.id == "de_esser" && entry.displayName == "De-esser";
    }
    CHECK(limiterFound);
    CHECK(deEsserFound);
}

TEST_CASE("Limiter never exceeds the ceiling for any level and waveform") {
    for (float ceiling : {-1.f, -6.f, -20.f})
        for (float levelDb : {-6.f, 0.f, 12.f, 24.f}) {
            const double amplitude = dbToGain(levelDb);
            for (int kind = 0; kind < 3; ++kind) {
                Limiter limiter;
                limiter.parameters().set("ceiling", ceiling);
                limiter.parameters().set("release", 100);
                limiter.prepare(rate, 512);
                const std::size_t frames = 48000;
                std::vector<float> signal(frames * 2, 0);
                std::uint32_t rng = 0x12345678u;
                for (std::size_t i = 0; i < frames; ++i) {
                    double sample = 0;
                    if (kind == 0)
                        sample = amplitude * std::sin(2 * pi * 1000 * i / rate);
                    else if (kind == 1) {
                        rng = rng * 1664525u + 1013904223u;
                        sample = amplitude * (double(rng) / 4294967296.0 * 2 - 1);
                    } else
                        sample = i % 977 == 0 ? amplitude : 0;
                    signal[i * 2] = static_cast<float>(sample);
                    signal[i * 2 + 1] = static_cast<float>(sample * .6);
                }
                processBlocks(limiter, signal.data(), frames);
                CHECK(peakAbs(signal) <= dbToGain(ceiling) * dbToGain(.1));
            }
        }
}

TEST_CASE("Limiter passes below-ceiling audio unchanged after the look-ahead delay") {
    Limiter limiter;
    limiter.parameters().set("ceiling", -1.f);
    limiter.parameters().set("input_gain", 0.f);
    limiter.prepare(rate, 512);
    const std::size_t frames = 4096;
    const auto input = tone(440, dbToGain(-14), frames);
    auto output = input;
    processBlocks(limiter, output.data(), frames);
    const auto latency = limiter.latencyFrames();
    REQUIRE(latency > 0);
    bool exact = true;
    for (std::size_t i = 0; i < frames; ++i)
        if (i < latency)
            exact &= output[i * 2] == 0 && output[i * 2 + 1] == 0;
        else
            exact &= output[i * 2] == input[(i - latency) * 2] &&
                     output[i * 2 + 1] == input[(i - latency) * 2 + 1];
    CHECK(exact);
}

TEST_CASE("Limiter look-ahead matches the reported latency") {
    Limiter limiter;
    limiter.parameters().set("ceiling", -1.f);
    limiter.prepare(rate, 512);
    const std::size_t frames = 1024;
    std::vector<float> signal(frames * 2, 0);
    signal[0] = signal[1] = .5f;
    processBlocks(limiter, signal.data(), frames);
    std::size_t first = frames;
    for (std::size_t i = 0; i < frames; ++i)
        if (signal[i * 2] != 0) {
            first = i;
            break;
        }
    CHECK(first == limiter.latencyFrames());
    CHECK(signal[first * 2] == doctest::Approx(.5).epsilon(1e-6));
}

TEST_CASE("Limiter release time constant matches the setting") {
    Limiter limiter;
    limiter.parameters().set("ceiling", -6.f);
    limiter.parameters().set("release", 100.f);
    limiter.parameters().set("auto_release", 0.f);
    limiter.prepare(rate, 512);
    auto loud = tone(1000, 1., 24000);
    processBlocks(limiter, loud.data(), 24000);
    CHECK(limiter.gainReductionDb().load() == doctest::Approx(6).epsilon(.02));
    const auto latency = limiter.latencyFrames();
    const auto envelopeAfter = [&](std::size_t frames) {
        std::array<float, 2> silent{};
        for (std::size_t i = 0; i < frames; ++i)
            limiter.process(silent.data(), 1);
        return dbToGain(-limiter.gainReductionDb().load());
    };
    const double first = envelopeAfter(latency + 30), second = envelopeAfter(480);
    const double tau = 480 / std::log((first - 1) / (second - 1));
    CHECK(std::abs(tau / (.001 * 100 * rate) - 1) < .15);
}

TEST_CASE("Limiter links channels and reports the applied reduction") {
    Limiter limiter;
    limiter.parameters().set("ceiling", -6.f);
    limiter.parameters().set("release", 100.f);
    limiter.prepare(rate, 512);
    const std::size_t frames = 48000;
    std::vector<float> signal(frames * 2, 0);
    for (std::size_t i = 0; i < frames; ++i) {
        signal[i * 2] = 1.f;
        signal[i * 2 + 1] = .25f;
    }
    processBlocks(limiter, signal.data(), frames);
    const double left = signal[(frames - 1) * 2], right = signal[(frames - 1) * 2 + 1];
    CHECK(left / right == doctest::Approx(4).epsilon(1e-4));
    CHECK(left == doctest::Approx(dbToGain(-6)).epsilon(.01));
    CHECK(limiter.gainReductionDb().load() == doctest::Approx(6).epsilon(.02));
}

TEST_CASE("De-esser split-band reduction follows threshold and ratio") {
    DeEsser effect;
    effect.parameters().set("frequency", 6500);
    effect.parameters().set("q", 1.5f);
    effect.parameters().set("threshold", -30.f);
    effect.parameters().set("ratio", 4.f);
    effect.parameters().set("range", 30.f);
    effect.parameters().set("attack", 1.f);
    effect.parameters().set("release", 60.f);
    effect.prepare(rate, 512);
    const std::size_t frames = 24000;
    const auto input = tone(6500, dbToGain(-6), frames);
    auto output = input;
    processBlocks(effect, output.data(), frames);
    const double measured = -20 * std::log10(rms(output, frames / 2) / rms(input, frames / 2));
    const double expected = (-6.0206 + 30) * (1 - 1. / 4);
    CHECK(std::abs(measured - expected) < .5);
}

TEST_CASE("De-esser leaves out-of-band audio bit-exact in split-band mode") {
    DeEsser effect;
    effect.parameters().set("frequency", 6500);
    effect.parameters().set("threshold", -24.f);
    effect.parameters().set("ratio", 4.f);
    effect.parameters().set("range", 30.f);
    effect.parameters().set("attack", 1.f);
    effect.parameters().set("release", 60.f);
    effect.prepare(rate, 512);
    const std::size_t frames = 24000;
    const auto input = tone(500, .2, frames);
    auto output = input;
    processBlocks(effect, output.data(), frames);
    bool exact = true;
    for (std::size_t i = 4096; i < frames; ++i)
        exact &= output[i * 2] == input[i * 2] && output[i * 2 + 1] == input[i * 2 + 1];
    CHECK(exact);
}

TEST_CASE("De-esser listen mode outputs the detection band") {
    DeEsser effect;
    effect.parameters().set("frequency", 6500);
    effect.parameters().set("q", 1.5f);
    effect.parameters().set("threshold", -60.f);
    effect.parameters().set("listen", 1.f);
    effect.prepare(rate, 512);
    const std::size_t frames = 24000;
    const auto bandInput = tone(6500, .5, frames);
    auto bandOutput = bandInput;
    processBlocks(effect, bandOutput.data(), frames);
    CHECK(std::abs(20 * std::log10(rms(bandOutput, frames / 2) / rms(bandInput, frames / 2))) < 1);
    const auto outOfBandInput = tone(500, .5, frames);
    auto outOfBandOutput = outOfBandInput;
    processBlocks(effect, outOfBandOutput.data(), frames);
    CHECK(20 * std::log10(rms(outOfBandOutput, frames / 2) / rms(outOfBandInput, frames / 2)) <
          -15);
}

TEST_CASE("De-esser range limits the maximum reduction") {
    DeEsser effect;
    effect.parameters().set("frequency", 6500);
    effect.parameters().set("threshold", -60.f);
    effect.parameters().set("ratio", 20.f);
    effect.parameters().set("range", 6.f);
    effect.parameters().set("attack", 1.f);
    effect.parameters().set("release", 60.f);
    effect.prepare(rate, 512);
    const std::size_t frames = 24000;
    const auto input = tone(6500, 1., frames);
    auto output = input;
    processBlocks(effect, output.data(), frames);
    const double measured = -20 * std::log10(rms(output, frames / 2) / rms(input, frames / 2));
    CHECK(std::abs(measured - 6) < .5);
}

TEST_CASE("De-esser attack and release time constants match the settings") {
    DeEsser effect;
    effect.parameters().set("frequency", 6500);
    effect.parameters().set("q", 1.5f);
    effect.parameters().set("threshold", -60.f);
    effect.parameters().set("ratio", 2.f);
    effect.parameters().set("range", 40.f);
    effect.parameters().set("attack", 10.f);
    effect.parameters().set("release", 100.f);
    effect.prepare(rate, 512);
    const double amplitude = .5, target = (20 * std::log10(amplitude) + 60) * .5;
    auto attackSegment = tone(6500, amplitude, static_cast<std::size_t>(.001 * 10 * rate));
    processBlocks(effect, attackSegment.data(), attackSegment.size() / 2);
    const double attack = effect.gainReductionDb().load();
    CHECK(std::abs(attack / target - (1 - std::exp(-1.))) < .2 * (1 - std::exp(-1.)));
    auto settle = tone(6500, amplitude, 24000);
    processBlocks(effect, settle.data(), 24000);
    CHECK(effect.gainReductionDb().load() == doctest::Approx(target).epsilon(.02));
    std::vector<float> silence(static_cast<std::size_t>(.1 * rate) * 2, 0);
    processBlocks(effect, silence.data(), silence.size() / 2);
    const double release = effect.gainReductionDb().load();
    CHECK(std::abs(release / target - std::exp(-1.)) < .2 * std::exp(-1.));
}

TEST_CASE("De-esser wide-band mode ducks the whole signal, split-band does not") {
    const std::size_t frames = 24000;
    std::vector<float> input(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        input[i * 2] = input[i * 2 + 1] = static_cast<float>(
            .3 * std::sin(2 * pi * 500 * i / rate) + .5 * std::sin(2 * pi * 6500 * i / rate));
    const double lowInput = binAmplitude(input, 500, frames / 2);
    for (float wide : {0.f, 1.f}) {
        DeEsser effect;
        effect.parameters().set("frequency", 6500);
        effect.parameters().set("threshold", -30.f);
        effect.parameters().set("ratio", 4.f);
        effect.parameters().set("range", 30.f);
        effect.parameters().set("attack", 1.f);
        effect.parameters().set("release", 60.f);
        effect.parameters().set("mode", wide);
        effect.prepare(rate, 512);
        auto output = input;
        processBlocks(effect, output.data(), frames);
        const double lowOutput = binAmplitude(output, 500, frames / 2);
        const double ratio = lowOutput / lowInput;
        if (wide >= .5f)
            CHECK(ratio < .25f);
        else
            CHECK(ratio == doctest::Approx(1).epsilon(.05));
    }
}

TEST_CASE("Both effects survive extreme parameters and non-finite input") {
    Limiter limiter;
    DeEsser deesser;
    checkExtremeSweep(limiter);
    checkExtremeSweep(deesser);
    checkNonFiniteInput(limiter);
    checkNonFiniteInput(deesser);
}

TEST_CASE("Both effects allocate and free nothing during process") {
    Limiter limiter;
    DeEsser deesser;
    limiter.prepare(rate, 512);
    deesser.prepare(rate, 512);
    std::array<float, 1024> block;
    block.fill(.3f);
    allocations = frees = 0;
    countMemory = true;
    for (int iteration = 0; iteration < 200; ++iteration) {
        limiter.process(block.data(), 512);
        deesser.process(block.data(), 512);
    }
    countMemory = false;
    CHECK(allocations == 0);
    CHECK(frees == 0);
}

TEST_CASE("Live parameter edits from another thread are data-race free") {
    Limiter limiter;
    DeEsser deesser;
    limiter.prepare(rate, 512);
    deesser.prepare(rate, 512);
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; !done.load(); ++i) {
            limiter.parameters().set("ceiling", -1.f - float(i % 20));
            limiter.parameters().set("release", 10.f + float(i % 990));
            limiter.parameters().set("auto_release", float(i % 2));
            deesser.parameters().set("frequency", 3000.f + float(i % 7000));
            deesser.parameters().set("threshold", -30.f + float(i % 20));
            deesser.parameters().set("ratio", 1.f + float(i % 19));
            deesser.parameters().set("mode", float(i % 2));
        }
    });
    std::array<float, 1024> block;
    for (int iteration = 0; iteration < 2000; ++iteration)
        for (Effect* effect : {static_cast<Effect*>(&limiter), static_cast<Effect*>(&deesser)}) {
            block.fill(.2f);
            effect->process(block.data(), 512);
            for (float v : block)
                REQUIRE(std::isfinite(v));
        }
    done.store(true);
    writer.join();
}

TEST_CASE("64 tracks of Limiter plus De-esser stay inside half the real-time budget") {
    constexpr int tracks = 64;
    constexpr std::size_t block = 512;
    std::vector<std::unique_ptr<Limiter>> limiters;
    std::vector<std::unique_ptr<DeEsser>> deessers;
    std::vector<std::vector<float>> buffers(tracks, std::vector<float>(block * 2));
    for (int track = 0; track < tracks; ++track) {
        auto limiter = std::make_unique<Limiter>();
        limiter->parameters().set("ceiling", -1.f);
        limiter->parameters().set("release", 100.f);
        limiter->prepare(rate, block);
        auto deesser = std::make_unique<DeEsser>();
        deesser->parameters().set("frequency", 6500.f);
        deesser->prepare(rate, block);
        for (std::size_t i = 0; i < block; ++i)
            buffers[track][i * 2] = buffers[track][i * 2 + 1] =
                static_cast<float>(.3 * std::sin(2 * pi * (200 + 8 * track) * i / rate) +
                                   .2 * std::sin(2 * pi * 7000 * i / rate));
        limiters.push_back(std::move(limiter));
        deessers.push_back(std::move(deesser));
    }
    const auto measure = [](auto&& body) {
        double best = std::numeric_limits<double>::infinity();
        for (int round = 0; round < 8; ++round) {
            const auto start = std::chrono::steady_clock::now();
            body();
            best = std::min(best, std::chrono::duration<double, std::micro>(
                                      std::chrono::steady_clock::now() - start)
                                      .count());
        }
        return best;
    };
    const double limiterUs = measure([&] {
        for (int t = 0; t < tracks; ++t)
            limiters[t]->process(buffers[t].data(), block);
    });
    const double deEsserUs = measure([&] {
        for (int t = 0; t < tracks; ++t)
            deessers[t]->process(buffers[t].data(), block);
    });
    allocations = frees = 0;
    countMemory = true;
    const double combinedUs = measure([&] {
        for (int t = 0; t < tracks; ++t) {
            limiters[t]->process(buffers[t].data(), block);
            deessers[t]->process(buffers[t].data(), block);
        }
    });
    countMemory = false;
    log::info("effects", "64-track Limiter+De-esser: {} us per 512-frame block", combinedUs);
    log::info("effects", "Limiter {} ns/frame, De-esser {} ns/frame across 64 tracks",
              limiterUs * 1000 / block, deEsserUs * 1000 / block);
    CHECK(allocations == 0);
    CHECK(frees == 0);
#ifdef NDEBUG
    CHECK(combinedUs < block / rate * 1e6 * .5);
#endif
    for (const auto& buffer : buffers)
        for (float v : buffer)
            CHECK(std::isfinite(v));
}
