#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "audio/Mixer.hpp"
#include "core/Log.hpp"
#include "effects/Compressor.hpp"
#include "effects/GainPan.hpp"
#include "effects/ParametricEq.hpp"
#include "timeline/Commands.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <new>
#include <numbers>
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
using namespace wavy::effects;
constexpr double rate = 48000;
void constant(Effect& effect, float level, std::size_t frames = 48000) {
    std::array<float, 1024> block;
    while (frames) {
        const auto n = std::min<std::size_t>(frames, 512);
        block.fill(level);
        effect.process(block.data(), n);
        frames -= n;
    }
}
std::array<double, 5> referenceCoefficients(int type, double frequency, double gain, double q,
                                            double sr) {
    if (gain == 0)
        return {1, 0, 0, 0, 0};
    const double a = std::pow(10., gain / 40), w = 2 * std::numbers::pi * frequency / sr;
    const double c = std::cos(w), alpha = std::sin(w) / (2 * q), beta = 2 * std::sqrt(a) * alpha;
    std::array<double, 3> b, d;
    if (type == 1) {
        b = {1 + alpha * a, -2 * c, 1 - alpha * a};
        d = {1 + alpha / a, -2 * c, 1 - alpha / a};
    } else if (type == 0) {
        b = {a * (a + 1 - (a - 1) * c + beta), 2 * a * (a - 1 - (a + 1) * c),
             a * (a + 1 - (a - 1) * c - beta)};
        d = {a + 1 + (a - 1) * c + beta, -2 * (a - 1 + (a + 1) * c), a + 1 + (a - 1) * c - beta};
    } else {
        b = {a * (a + 1 + (a - 1) * c + beta), -2 * a * (a - 1 + (a + 1) * c),
             a * (a + 1 + (a - 1) * c - beta)};
        d = {a + 1 - (a - 1) * c + beta, 2 * (a - 1 - (a + 1) * c), a + 1 - (a - 1) * c - beta};
    }
    return {b[0] / d[0], b[1] / d[0], b[2] / d[0], d[1] / d[0], d[2] / d[0]};
}
double response(int type, double frequency, double gain, double q, double probe, double sr) {
    const double a = std::pow(10., gain / 40), w = 2 * std::numbers::pi * frequency / sr;
    const double c = std::cos(w), alpha = std::sin(w) / (2 * q), beta = 2 * std::sqrt(a) * alpha;
    std::array<double, 3> b, d;
    if (type == 1) {
        b = {1 + alpha * a, -2 * c, 1 - alpha * a};
        d = {1 + alpha / a, -2 * c, 1 - alpha / a};
    } else if (type == 0) {
        b = {a * (a + 1 - (a - 1) * c + beta), 2 * a * (a - 1 - (a + 1) * c),
             a * (a + 1 - (a - 1) * c - beta)};
        d = {a + 1 + (a - 1) * c + beta, -2 * (a - 1 + (a + 1) * c), a + 1 + (a - 1) * c - beta};
    } else {
        b = {a * (a + 1 + (a - 1) * c + beta), -2 * a * (a - 1 + (a + 1) * c),
             a * (a + 1 + (a - 1) * c - beta)};
        d = {a + 1 - (a - 1) * c + beta, 2 * (a - 1 - (a + 1) * c), a + 1 - (a - 1) * c - beta};
    }
    const auto z = std::polar(1., -2 * std::numbers::pi * probe / sr);
    return 20 * std::log10(
                    std::abs((b[0] + b[1] * z + b[2] * z * z) / (d[0] + d[1] * z + d[2] * z * z)));
}
struct Fixture {
    timeline::Timeline timeline;
    SourceCache cache;
    timeline::TrackId id;
    Fixture() {
        timeline::AddTrack add("voice");
        add.apply(timeline);
        id = add.trackId();
        auto audio = std::make_shared<AudioBuffer>(AudioBuffer{48000, 2, std::vector<float>(1024)});
        for (std::size_t i = 0; i < 512; ++i)
            audio->samples[i * 2] = audio->samples[i * 2 + 1] =
                .1f * std::sin(2 * std::numbers::pi * i / 48);
        cache.emplace("sine", audio);
        timeline::Clip clip;
        clip.source = "sine";
        clip.length = 512;
        timeline::AddClip command(id, clip);
        command.apply(timeline);
    }
};
} // namespace
TEST_CASE("Parameters clamp, resolve ids and serialize shared values") {
    EffectFactory factory;
    auto slot = factory.slot("gain_pan");
    auto& p = *slot.params;
    CHECK(p.set("pan", 2));
    CHECK(p.get("pan") == 1);
    CHECK(p.set("gain", -std::numeric_limits<float>::infinity()));
    CHECK(std::isinf(p.get("gain")));
    CHECK_FALSE(p.set("unknown", 2));
    CHECK(p.get(999u) == 0);
    p.set("pan", std::numeric_limits<float>::quiet_NaN());
    CHECK(p.get("pan") == 0);
    auto values = serializeParameters(slot);
    values["gain"] = 100;
    restoreParameters(slot, values);
    CHECK(p.get("gain") == 24);
    auto effect = factory.create(slot.typeId, slot.params);
    CHECK(&effect->parameters() == slot.params.get());
    CHECK(factory.entries().size() >= 3);
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 100000; ++i)
            p.set("pan", (i % 5) - 2.f);
        done.store(true);
    });
    bool valid = true;
    effect->prepare(rate, 512);
    std::array<float, 1024> block{};
    while (!done.load()) {
        const auto value = p.get("pan");
        valid &= value >= -1 && value <= 1;
        effect->process(block.data(), 512);
    }
    writer.join();
    CHECK(valid);
}
TEST_CASE("Constant power gain/pan and smooth polarity changes") {
    GainPan effect;
    for (float pan : {-1.f, 0.f, 1.f}) {
        effect.parameters().set("pan", pan);
        effect.parameters().set("gain", 6);
        effect.prepare(rate, 512);
        std::array<float, 2> block{1, 1};
        effect.process(block.data(), 1);
        const double angle = (pan + 1) * std::numbers::pi / 4, gain = std::pow(10., .3);
        CHECK(block[0] == doctest::Approx(gain * std::cos(angle)).epsilon(1e-6));
        CHECK(block[1] == doctest::Approx(gain * std::sin(angle)).epsilon(1e-6));
        CHECK(block[0] * block[0] + block[1] * block[1] == doctest::Approx(gain * gain));
    }
    effect.parameters().set("invert", 1);
    std::array<float, 2> first{1, 1};
    effect.process(first.data(), 1);
    CHECK(first[1] > 0);
    constant(effect, 1, 240);
    first = {1, 1};
    effect.process(first.data(), 1);
    CHECK(first[1] < 0);
    effect.parameters().set("gain", -std::numeric_limits<float>::infinity());
    effect.reset();
    first = {1, 1};
    effect.process(first.data(), 1);
    CHECK(first[0] == 0);
    CHECK(first[1] == 0);
}
TEST_CASE("Every EQ band type matches analytic RBJ response") {
    for (double sr : {44100., 48000., 96000.})
        for (float gain : {-12.f, 12.f})
            for (int type = 0; type < 3; ++type)
                for (std::size_t band = 0; band < 4; ++band) {
                    ParametricEq eq;
                    eq.parameters().set(band * 5, float(type));
                    eq.parameters().set(band * 5 + 1, 1200);
                    eq.parameters().set(band * 5 + 2, gain);
                    eq.parameters().set(band * 5 + 3, .8f);
                    eq.prepare(sr, 512);
                    std::vector<float> impulse(32768 * 2, 0);
                    impulse[0] = impulse[1] = 1;
                    eq.process(impulse.data(), 32768);
                    for (double probe : {100., 1200., 10000.}) {
                        std::complex<double> sum{}, phase{1, 0};
                        const auto step = std::polar(1., -2 * std::numbers::pi * probe / sr);
                        for (std::size_t i = 0; i < impulse.size() / 2; ++i) {
                            sum += double(impulse[i * 2]) * phase;
                            phase *= step;
                        }
                        const double measured = 20 * std::log10(std::abs(sum));
                        CHECK(std::abs(measured - response(type, 1200, gain, .8, probe, sr)) < .1);
                    }
                }
}
TEST_CASE("Zero gain EQ is bit transparent and extreme settings remain finite") {
    for (double sr : {44100., 48000., 96000.}) {
        ParametricEq eq;
        eq.prepare(sr, 512);
        std::array<float, 1024> block;
        for (std::size_t i = 0; i < block.size(); ++i)
            block[i] = .5f * std::sin(float(i));
        block[0] = -0.f;
        const auto original = block;
        eq.process(block.data(), 512);
        CHECK(std::memcmp(block.data(), original.data(), sizeof(block)) == 0);
        for (int iteration = 0; iteration < 100; ++iteration) {
            for (std::size_t b = 0; b < 4; ++b) {
                eq.parameters().set(b * 5, float(iteration % 3));
                eq.parameters().set(b * 5 + 1, iteration % 2 ? 40000 : 20);
                eq.parameters().set(b * 5 + 2, iteration % 2 ? 24 : -24);
                eq.parameters().set(b * 5 + 3, iteration % 2 ? 18 : .1f);
                eq.parameters().set(b * 5 + 4, 1);
            }
            block = original;
            eq.process(block.data(), 512);
            for (float v : block)
                REQUIRE(std::isfinite(v));
        }
    }
}
TEST_CASE("Compressor hard and soft knee static curves") {
    Compressor effect;
    effect.parameters().set("attack", .1f);
    effect.parameters().set("release", 1);
    for (float knee : {0.f, 6.f})
        for (float inputDb : {-30.f, -21.f, -19.f, -18.f, -17.f, -15.f, -6.f}) {
            effect.parameters().set("knee", knee);
            effect.prepare(rate, 512);
            const float input = std::pow(10.f, inputDb / 20);
            constant(effect, input);
            std::array<float, 2> block{input, input};
            effect.process(block.data(), 1);
            const double over = inputDb + 18;
            const double reduction = over > knee / 2
                                         ? .75 * over
                                         : (knee > 0 && over > -knee / 2
                                                ? .75 * std::pow(over + knee / 2, 2) / (2 * knee)
                                                : 0);
            CHECK(std::abs(20 * std::log10(block[0]) - (inputDb - reduction)) < .1);
            CHECK(effect.gainReductionDb().load() == doctest::Approx(reduction).epsilon(.001));
        }
}
TEST_CASE("Detector attack and release time constants, stereo link and RMS") {
    for (float rms : {0.f, 1.f}) {
        Compressor effect;
        effect.parameters().set("threshold", -80);
        effect.parameters().set("ratio", 2);
        effect.parameters().set("knee", 0);
        effect.parameters().set("attack", 10);
        effect.parameters().set("release", 100);
        effect.parameters().set("detector", rms);
        effect.prepare(rate, 512);
        constant(effect, .5f, 480);
        const auto envelope = [&] {
            const double db = -80 + 2 * effect.gainReductionDb().load();
            return std::pow(10., db / (rms ? 10 : 20));
        };
        const double full = rms ? .25 : .5;
        CHECK(std::abs(envelope() / (full * (1 - std::exp(-1.))) - 1) < .1);
        constant(effect, .5f, 48000);
        constant(effect, 0, 4800);
        CHECK(std::abs(envelope() / (full * std::exp(-1.)) - 1) < .1);
        std::array<float, 2> linked{.5f, .1f};
        effect.process(linked.data(), 1);
        CHECK(linked[0] / linked[1] == doctest::Approx(5));
    }
    Compressor effect;
    effect.parameters().set("mix", 0);
    effect.prepare(rate, 512);
    std::array<float, 2> dry{.5f, -.2f};
    effect.process(dry.data(), 1);
    CHECK(dry[0] == .5f);
    CHECK(dry[1] == -.2f);
    effect.parameters().set("ratio", 1);
    effect.parameters().set("auto_makeup", 1);
    effect.parameters().set("mix", 1);
    effect.reset();
    dry = {.5f, -.2f};
    effect.process(dry.data(), 1);
    CHECK(dry[0] == .5f);
}
TEST_CASE("Compressor RMS power, auto makeup and parallel gain") {
    Compressor effect;
    effect.parameters().set("attack", .1f);
    effect.parameters().set("release", 1);
    effect.parameters().set("knee", 0);
    for (float detector : {0.f, 1.f}) {
        effect.parameters().set("detector", detector);
        effect.prepare(rate, 512);
        std::array<float, 1024> block;
        for (int n = 0; n < 100; ++n) {
            for (std::size_t i = 0; i < 512; ++i) {
                block[i * 2] = 1;
                block[i * 2 + 1] = 0;
            }
            effect.process(block.data(), 512);
        }
        const double inputDb = detector == 0 ? 0 : -3.01029995664;
        CHECK(std::abs(effect.gainReductionDb().load() - .75 * (inputDb + 18)) < .1);
    }
    effect.parameters().set("detector", 0);
    effect.parameters().set("auto_makeup", 1);
    effect.prepare(rate, 512);
    constant(effect, 1);
    std::array<float, 2> block{1, 1};
    effect.process(block.data(), 1);
    CHECK(std::abs(20 * std::log10(block[0])) < .1);
    effect.parameters().set("auto_makeup", 0);
    effect.parameters().set("makeup", 6);
    effect.parameters().set("mix", .5f);
    effect.reset();
    constant(effect, 1);
    block = {1, 1};
    effect.process(block.data(), 1);
    CHECK(block[0] == doctest::Approx(.5 * (1 + std::pow(10., (6 - 13.5) / 20))).epsilon(.001));
}
TEST_CASE("Effects process and parameter edits allocate and free nothing") {
    EffectFactory factory;
    for (const auto& entry : factory.entries()) {
        auto effect = factory.create(entry.id);
        effect->prepare(rate, 512);
        std::array<float, 1024> block{};
        allocations = frees = 0;
        countMemory = true;
        for (int iteration = 0; iteration < 100; ++iteration) {
            for (std::size_t p = 0; p < effect->parameters().size(); ++p) {
                const auto& definition = effect->parameters().definitions()[p];
                effect->parameters().set(p,
                                         iteration % 2 ? definition.maximum : definition.minimum);
            }
            block.fill(.1f);
            effect->process(block.data(), 512);
        }
        countMemory = false;
        CHECK(allocations == 0);
        CHECK(frees == 0);
        for (float v : block)
            CHECK(std::isfinite(v));
    }
}
TEST_CASE("Snapshot bypass is bit exact and live parameters affect track and master") {
    Fixture fixture;
    EffectFactory factory;
    EffectChains chains;
    auto gain = factory.slot("gain_pan"), eq = factory.slot("parametric_eq");
    gain.bypassed = eq.bypassed = true;
    chains.tracks[fixture.id] = {eq, gain};
    chains.master = {gain};
    Mixer plain, bypass;
    plain.publish(buildSnapshot(fixture.timeline, fixture.cache));
    bypass.publish(buildSnapshot(fixture.timeline, fixture.cache, chains, 127));
    plain.transport().play();
    bypass.transport().play();
    std::array<float, 1024> a{}, b{};
    plain.render(a.data(), 512);
    bypass.render(b.data(), 512);
    CHECK(std::memcmp(a.data(), b.data(), sizeof(a)) == 0);
    eq.bypassed = gain.bypassed = false;
    eq.params->set("band2.frequency", 1000);
    eq.params->set("band2.gain", 6);
    gain.params->set("gain", 6);
    chains.tracks[fixture.id] = {eq, gain};
    chains.master.clear();
    Mixer processed;
    processed.publish(buildSnapshot(fixture.timeline, fixture.cache, chains));
    processed.transport().setLoop(0, 480);
    processed.transport().play();
    for (int i = 0; i < 20; ++i)
        processed.render(b.data(), 480);
    double energy = 0;
    for (std::size_t i = 0; i < 480; ++i)
        energy += b[i * 2] * b[i * 2];
    CHECK(std::abs(20 * std::log10(std::sqrt(energy / 480) / (.1 / std::sqrt(2.))) -
                   (12 - 3.0103)) < .1);
    gain.params->set("gain", 0);
    for (int i = 0; i < 20; ++i)
        processed.render(b.data(), 480);
    energy = 0;
    for (std::size_t i = 0; i < 480; ++i)
        energy += b[i * 2] * b[i * 2];
    CHECK(std::abs(20 * std::log10(std::sqrt(energy / 480) / (.1 / std::sqrt(2.))) - (6 - 3.0103)) <
          .1);
    chains.tracks.clear();
    chains.master = {gain};
    Mixer master;
    master.publish(buildSnapshot(fixture.timeline, fixture.cache, chains));
    master.transport().play();
    master.render(b.data(), 512);
    CHECK(b[2] == doctest::Approx(a[2] / std::sqrt(2.)));
}
TEST_CASE("64 tracks with GainPan EQ Compressor performance and zero mixer allocations") {
    Fixture fixture;
    EffectFactory factory;
    EffectChains chains;
    auto gain = factory.slot("gain_pan"), eq = factory.slot("parametric_eq"),
         compressor = factory.slot("compressor");
    for (int band = 0; band < 4; ++band)
        eq.params->set(std::size_t(band * 5 + 2), 3);
    chains.tracks[fixture.id] = {gain, eq, compressor};
    auto snapshot = buildSnapshot(fixture.timeline, fixture.cache, chains);
    for (int i = 1; i < 64; ++i) {
        auto prepared = buildSnapshot(fixture.timeline, fixture.cache, chains);
        snapshot->tracks.push_back(std::move(prepared->tracks.front()));
    }
    Mixer mixer;
    mixer.publish(std::move(snapshot));
    mixer.transport().setLoop(0, 512);
    mixer.transport().play();
    std::array<float, 1024> block;
    for (int i = 0; i < 10; ++i)
        mixer.render(block.data(), 512);
    allocations = frees = 0;
    countMemory = true;
    std::array<double, 9> rounds;
    for (auto& us : rounds) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i)
            mixer.render(block.data(), 512);
        us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                 .count() /
             20;
    }
    countMemory = false;
    std::sort(rounds.begin(), rounds.end());
    const double us = rounds.front();
    log::info("effects",
              "64 tracks / GainPan + EQ + Compressor: min {} / median {} us per 512-frame block",
              us, rounds[rounds.size() / 2]);
    CHECK(allocations == 0);
    CHECK(frees == 0);
#ifdef NDEBUG
    CHECK(us < (512.0 / 48000 * 1e6 * .3));
#endif
    for (float v : block)
        CHECK(std::isfinite(v));
}

TEST_CASE("Per-effect settled processing timings") {
    EffectFactory factory;
    for (const auto& entry : factory.entries()) {
        auto effect = factory.create(entry.id);
        if (entry.id == "parametric_eq")
            for (std::size_t b = 0; b < 4; ++b)
                effect->parameters().set(b * 5 + 2, 3);
        effect->prepare(rate, 512);
        std::array<float, 1024> input, block;
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = .1f * std::sin(float(i / 2) * .1f);
        for (int i = 0; i < 10; ++i) {
            block = input;
            effect->process(block.data(), 512);
        }
        std::array<double, 9> rounds;
        for (auto& ns : rounds) {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 20; ++i) {
                block = input;
                effect->process(block.data(), 512);
            }
            ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                     .count() /
                 (20 * 512);
        }
        std::sort(rounds.begin(), rounds.end());
        log::info("effects", "{}: min {} / median {} ns per frame", entry.id, rounds.front(),
                  rounds[rounds.size() / 2]);
        for (float value : block)
            CHECK(std::isfinite(value));
    }
}

TEST_CASE("EQ settled and ramping blocks match the sample-wise reference") {
    for (double sr : {44100., 48000., 96000.}) {
        ParametricEq eq;
        std::array<std::array<Smoother, 5>, 4> smooth;
        std::array<std::array<std::array<double, 2>, 2>, 4> state{};
        auto targets = [&] {
            std::array<std::array<double, 5>, 4> result;
            for (std::size_t b = 0; b < 4; ++b) {
                const auto& p = eq.parameters();
                result[b] =
                    p.get(b * 5 + 4) < .5f
                        ? std::array<double, 5>{1, 0, 0, 0, 0}
                        : referenceCoefficients(int(std::lround(p.get(b * 5))),
                                                std::clamp<double>(p.get(b * 5 + 1), 1, sr * .49),
                                                p.get(b * 5 + 2), p.get(b * 5 + 3), sr);
            }
            return result;
        };
        for (std::size_t b = 0; b < 4; ++b)
            eq.parameters().set(b * 5 + 2, 3);
        eq.prepare(sr, 512);
        const auto initial = targets();
        for (std::size_t b = 0; b < 4; ++b)
            for (std::size_t i = 0; i < 5; ++i) {
                smooth[b][i].prepare(sr);
                smooth[b][i].reset(initial[b][i]);
            }
        for (int block = 0; block < 100; ++block) {
            if (block >= 20 && block < 80 && block % 3 == 0)
                for (std::size_t b = 0; b < 4; ++b) {
                    eq.parameters().set(b * 5, float((block + b) % 3));
                    eq.parameters().set(b * 5 + 1, float(100 + block * 70 + b * 900));
                    eq.parameters().set(b * 5 + 2, float(block % 17 - 8));
                    eq.parameters().set(b * 5 + 3, .7f + float(b) * .1f);
                    eq.parameters().set(b * 5 + 4, block % 9 != 0);
                }
            const auto frames = std::array<std::size_t, 5>{1, 17, 127, 240, 512}[block % 5];
            std::array<float, 1024> actual{}, expected{};
            for (std::size_t i = 0; i < frames * 2; ++i)
                actual[i] = expected[i] = .2f * std::sin(float(block * 1024 + i) * .13f);
            const auto target = targets();
            for (std::size_t b = 0; b < 4; ++b) {
                for (std::size_t i = 0; i < 5; ++i)
                    smooth[b][i].target(target[b][i]);
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    std::array<double, 5> c;
                    for (std::size_t i = 0; i < 5; ++i)
                        c[i] = smooth[b][i].next();
                    for (unsigned channel = 0; channel < 2; ++channel) {
                        auto& z = state[b][channel];
                        const double x = expected[frame * 2 + channel], y = c[0] * x + z[0];
                        z[0] = c[1] * x - c[3] * y + z[1];
                        z[1] = c[2] * x - c[4] * y;
                        for (auto& value : z)
                            if (std::abs(value) < 1e-30)
                                value = 0;
                        expected[frame * 2 + channel] = static_cast<float>(y);
                    }
                }
            }
            eq.process(actual.data(), frames);
            for (std::size_t i = 0; i < frames * 2; ++i)
                REQUIRE(actual[i] == doctest::Approx(expected[i]).epsilon(1e-6).scale(1e-12));
        }
    }
}
TEST_CASE("Effects reject nonfinite samples and recover on the next block") {
    EffectFactory factory;
    for (const auto& entry : factory.entries())
        for (bool moving : {false, true}) {
            auto effect = factory.create(entry.id);
            effect->prepare(rate, 512);
            if (moving)
                for (std::size_t p = 0; p < effect->parameters().size(); ++p)
                    effect->parameters().set(p, effect->parameters().definitions()[p].maximum);
            for (float invalid :
                 {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()}) {
                INFO(entry.id, " moving=", moving, " input=", invalid);
                std::array<float, 1024> block;
                block.fill(.1f);
                block[0] = invalid;
                block[513] = invalid;
                effect->process(block.data(), 512);
                for (float value : block)
                    REQUIRE(std::isfinite(value));
                block.fill(.1f);
                effect->process(block.data(), 512);
                for (float value : block)
                    REQUIRE(std::isfinite(value));
                if (!std::isfinite(invalid))
                    CHECK(std::any_of(block.begin(), block.end(), [](float v) { return v != 0; }));
            }
        }
}
TEST_CASE("GainPan settled and moving gains match the sample-wise reference") {
    GainPan effect;
    effect.prepare(rate, 512);
    std::array<Smoother, 2> smooth;
    for (auto& s : smooth) {
        s.prepare(rate);
        s.reset(std::sqrt(.5));
    }
    for (int block = 0; block < 80; ++block) {
        if (block >= 10 && block < 60) {
            effect.parameters().set("gain", float(block % 25 - 12));
            effect.parameters().set("pan", float(block % 21 - 10) / 10);
            effect.parameters().set("invert", block % 2);
        }
        const auto& p = effect.parameters();
        const double gain = std::pow(10., p.get(0) / 20.) * (p.get(2) >= .5f ? -1 : 1);
        const double angle = (p.get(1) + 1) * std::numbers::pi / 4;
        smooth[0].target(gain * std::cos(angle));
        smooth[1].target(gain * std::sin(angle));
        std::array<float, 1024> actual, expected;
        for (std::size_t i = 0; i < actual.size(); ++i)
            actual[i] = expected[i] = .2f * std::sin(float(i) * .13f);
        actual[1] = expected[1] = std::numeric_limits<float>::max();
        actual[2] = expected[2] = -std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < 512; ++i)
            for (unsigned c = 0; c < 2; ++c)
                expected[i * 2 + c] =
                    static_cast<float>(std::clamp(expected[i * 2 + c] * smooth[c].next(),
                                                  -double(std::numeric_limits<float>::max()),
                                                  double(std::numeric_limits<float>::max())));
        effect.process(actual.data(), 512);
        for (std::size_t i = 0; i < actual.size(); ++i)
            REQUIRE(actual[i] == doctest::Approx(expected[i]).epsilon(1e-6).scale(1e-12));
    }
}
TEST_CASE("Compressor optimization matches the original curve and parameter sweeps") {
    for (bool rms : {false, true}) {
        Compressor effect;
        effect.parameters().set("detector", rms);
        effect.prepare(rate, 512);
        double envelope = 0;
        Smoother makeup, mix;
        makeup.prepare(rate);
        mix.prepare(rate);
        makeup.reset(1);
        mix.reset(1);
        for (int block = 0; block < 100; ++block) {
            if (block >= 20 && block < 80) {
                effect.parameters().set("threshold", float(-10 - block % 50));
                effect.parameters().set("knee", float(block % 25));
                effect.parameters().set("ratio", float(1 + block % 20));
                effect.parameters().set("makeup", float(block % 20 - 10));
                effect.parameters().set("auto_makeup", block % 2);
                effect.parameters().set("mix", float(block % 11) / 10);
            }
            const auto& p = effect.parameters();
            const double threshold = p.get(0), slope = 1 - 1. / p.get(1), knee = p.get(4);
            const double attack = std::exp(-1. / (.001 * p.get(2) * rate));
            const double release = std::exp(-1. / (.001 * p.get(3) * rate));
            makeup.target(
                std::pow(10., (p.get(5) + (p.get(6) >= .5f ? -threshold * slope : 0)) / 20));
            mix.target(p.get(8));
            std::array<float, 1024> actual, expected;
            for (std::size_t i = 0; i < actual.size(); ++i)
                actual[i] = expected[i] = .8f * std::sin(float(block * 1024 + i) * .13f);
            double reduction = 0;
            for (std::size_t i = 0; i < 512; ++i) {
                const double l = expected[i * 2], r = expected[i * 2 + 1];
                const double detector =
                    rms ? (l * l + r * r) * .5 : std::max(std::abs(l), std::abs(r));
                const double coefficient = detector > envelope ? attack : release;
                envelope = detector + coefficient * (envelope - detector);
                if (envelope < 1e-30)
                    envelope = 0;
                const double level = (rms ? 10 : 20) * std::log10(std::max(envelope, 1e-30));
                const double over = level - threshold;
                reduction = over > knee / 2
                                ? slope * over
                                : (knee > 0 && over > -knee / 2
                                       ? slope * (over + knee / 2) * (over + knee / 2) / (2 * knee)
                                       : 0);
                const double wet = std::pow(10., -reduction / 20) * makeup.next();
                const double gain = 1 + mix.next() * (wet - 1);
                expected[i * 2] = static_cast<float>(l * gain);
                expected[i * 2 + 1] = static_cast<float>(r * gain);
            }
            effect.process(actual.data(), 512);
            CHECK(std::abs(effect.gainReductionDb().load() - reduction) < .01);
            for (std::size_t i = 0; i < actual.size(); ++i)
                REQUIRE(actual[i] == doctest::Approx(expected[i]).epsilon(1e-5).scale(1e-12));
        }
    }
}
