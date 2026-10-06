#pragma once
#include "effects/Effect.hpp"
#include "io/AudioFile.hpp"
#include "timeline/Timeline.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <unordered_map>

namespace wavy {
// Precomputed fade laws shared by the render path so it never calls sin/cos/pow per sample.
// Index by timeline::FadeCurve (0 Linear, 1 EqualPower, 2 Exponential). `rise` maps a normalized
// fade position in [0,1] to a rising gain 0->1; `fall` maps it to the complementary falling gain
// 1->0 (Linear 1-t, EqualPower cos, Exponential the exponential decay 1-rise), so a fade-in and
// a crossfade-out of the same curve are consistent.
struct FadeTables {
    static constexpr std::size_t size = 1024;
    std::array<std::array<float, size>, 3> rise{};
    std::array<std::array<float, size>, 3> fall{};
};
const FadeTables& fadeTables();
inline float fadeLookup(const float* table, float phase) {
    phase = std::clamp(phase, 0.f, 1.f);
    const auto x = phase * static_cast<float>(FadeTables::size - 1);
    const auto index = static_cast<std::size_t>(x);
    const auto fraction = x - static_cast<float>(index);
    if (index + 1 >= FadeTables::size)
        return table[FadeTables::size - 1];
    return table[index] + fraction * (table[index + 1] - table[index]);
}
inline const float* fadeTable(timeline::FadeCurve curve, bool rise) {
    const auto index = std::min<std::size_t>(static_cast<std::size_t>(curve), 2);
    return (rise ? fadeTables().rise : fadeTables().fall)[index].data();
}
using SourceCache = std::unordered_map<std::string, std::shared_ptr<const AudioBuffer>>;
struct EffectChains {
    std::map<timeline::TrackId, std::vector<effects::EffectSlot>> tracks;
    std::vector<effects::EffectSlot> master;
    mutable std::map<timeline::TrackId, std::vector<const effects::ParameterSet*>> trackStructures;
    mutable std::vector<const effects::ParameterSet*> masterStructure;
};
struct PreparedEffect {
    std::shared_ptr<effects::Effect> effect;
    bool bypassed = false;
};
struct Snapshot {
    struct Clip {
        std::int64_t start, length, sourceOffset, fadeIn, fadeOut;
        float gain;
        std::shared_ptr<const AudioBuffer> source;
        timeline::FadeCurve fadeInCurve = timeline::FadeCurve::EqualPower;
        timeline::FadeCurve fadeOutCurve = timeline::FadeCurve::EqualPower;
        // Automatic crossfade regions computed on the main thread when two clips overlap in time on
        // the same track (later clip starts strictly after the earlier one). `crossIn` is the
        // longest overlap at this clip's start, where this clip is the later one and ramps in.
        // `crossOut`/`crossOutOffset` describe the longest overlap with a later clip: the earlier
        // clip ramps out over it and, when the later clip ends first, ramps back up over an equal
        // span immediately after. The earlier clip's law is the complement of the later clip's
        // fadeInCurve (`crossOutCurve`).
        std::int64_t crossIn = 0, crossOut = 0, crossOutOffset = 0;
        timeline::FadeCurve crossOutCurve = timeline::FadeCurve::Linear;
        struct Edges {
            const AudioBuffer* source = nullptr;
            std::int64_t offset = 0, length = 0;
            std::vector<float> samples;
        } edges;
    };
    struct Track {
        float gain;
        bool muted, solo;
        std::vector<Clip> clips;
        std::vector<PreparedEffect> effects;
        std::vector<float> scratch;
        timeline::TrackId id;
    };
    std::vector<Track> tracks;
    bool anySolo = false;
    std::vector<PreparedEffect> masterEffects;
    std::size_t maxBlockFrames = 512;
    unsigned sampleRate = 48000;
    std::vector<float> scratch;
};
std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline&, const SourceCache&,
                                        const EffectChains& chains = {},
                                        std::size_t maxBlockFrames = 512);
} // namespace wavy
