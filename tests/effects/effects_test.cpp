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
    CHECK(factory.entries().size() == 3);
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
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i)
        mixer.render(block.data(), 512);
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
            .count() /
        100;
    countMemory = false;
    log::info("effects", "64 tracks / GainPan + EQ + Compressor: {} us per 512-frame block", us);
    CHECK(allocations == 0);
    CHECK(frees == 0);
#ifdef NDEBUG
    CHECK(us < (512.0 / 48000 * 1e6 * .3));
#endif
    for (float v : block)
        CHECK(std::isfinite(v));
}
