#include "audio/Snapshot.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wavy {
namespace {
constexpr double kPi = 3.14159265358979323846;
// Exponential fade span: a = 4 makes the curve reach most of its range early (fast), matching the
// "fast, perceptually-tuned" description while staying gentle enough to avoid audible kinks.
constexpr double kExponentialRate = 4.0;
FadeTables buildFadeTables() {
    FadeTables tables;
    const double tail = std::exp(-kExponentialRate);
    for (std::size_t i = 0; i < FadeTables::size; ++i) {
        const double p = static_cast<double>(i) / (FadeTables::size - 1);
        const double linear = p;
        const double equalPower = std::sin(p * kPi / 2);
        const double exponential = (1.0 - std::exp(-kExponentialRate * p)) / (1.0 - tail);
        tables.rise[0][i] = static_cast<float>(linear);
        tables.fall[0][i] = static_cast<float>(1.0 - linear);
        tables.rise[1][i] = static_cast<float>(equalPower);
        tables.fall[1][i] = static_cast<float>(std::cos(p * kPi / 2));
        tables.rise[2][i] = static_cast<float>(exponential);
        tables.fall[2][i] = static_cast<float>(1.0 - exponential);
    }
    return tables;
}
const FadeTables kFadeTables = buildFadeTables();
} // namespace
const FadeTables& fadeTables() { return kFadeTables; }

std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline& timeline,
                                        const SourceCache& cache, const EffectChains& chains,
                                        std::size_t maxBlockFrames) {
    if (!maxBlockFrames)
        throw std::invalid_argument("Empty effect block capacity");
    auto result = std::make_unique<Snapshot>();
    result->maxBlockFrames = maxBlockFrames;
    result->sampleRate = timeline.sampleRate;
    result->scratch.resize(maxBlockFrames * 2);
    auto prepare = [&](const auto& slots, auto& previous) {
        std::vector<const effects::ParameterSet*> structure;
        for (const auto& slot : slots)
            structure.push_back(slot.params.get());
        // Crossfading a structural edit requires disjoint state in the two chain versions.
        if (!previous.empty() && previous != structure)
            for (const auto& slot : slots)
                slot.invalidate();
        previous = std::move(structure);
        std::vector<PreparedEffect> chain;
        for (const auto& slot : slots)
            chain.push_back(
                {slot.prepared(timeline.sampleRate, maxBlockFrames, &slots), slot.bypassed});
        return chain;
    };
    result->masterEffects = prepare(chains.master, chains.masterStructure);
    SourceCache loaded;
    for (const auto& track : timeline.tracks()) {
        Snapshot::Track copy{track.gain, track.muted, track.solo, {}, {}, {}, track.id};
        if (const auto it = chains.tracks.find(track.id); it != chains.tracks.end()) {
            copy.effects = prepare(it->second, chains.trackStructures[track.id]);
        }
        copy.scratch.resize(maxBlockFrames * 2);
        result->anySolo |= track.solo;
        for (const auto& clip : track.clips) {
            // Muted clips never reach the render path: they cost no snapshot memory and no mixing.
            if (clip.muted)
                continue;
            std::shared_ptr<const AudioBuffer> source;
            bool loading = false;
            if (auto it = cache.find(clip.source); it != cache.end()) {
                source = it->second;
                // A null cache entry is the caller's way of saying "still loading, stay silent".
                loading = !source;
            } else if (auto it = loaded.find(clip.source); it != loaded.end())
                source = it->second;
            else {
                auto audio = loadAudioFile(clip.source, timeline.sampleRate);
                if (audio)
                    source = std::make_shared<AudioBuffer>(std::move(*audio));
                else
                    log::error("mixer", "Cannot load {}: {}", clip.source, toString(audio.error()));
                loaded.emplace(clip.source, source);
            }
            if (!source || source->sampleRate != timeline.sampleRate || !source->channels) {
                if (loading)
                    log::debug("mixer", "Source still loading: {}", clip.source);
                else
                    log::error("mixer", "Missing or incompatible source: {}", clip.source);
                continue;
            }
            if (clip.start < 0 || clip.length <= 0 || clip.sourceOffset < 0 ||
                clip.start > std::numeric_limits<std::int64_t>::max() - clip.length)
                continue;
            copy.clips.push_back({clip.start,
                                  clip.length,
                                  clip.sourceOffset,
                                  std::max<std::int64_t>(0, clip.fadeIn),
                                  std::max<std::int64_t>(0, clip.fadeOut),
                                  clip.gain,
                                  source,
                                  clip.fadeInCurve,
                                  clip.fadeOutCurve,
                                  0,
                                  0,
                                  0,
                                  timeline::FadeCurve::Linear,
                                  {}});
        }
        std::sort(copy.clips.begin(), copy.clips.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
        const auto minimum = std::max<std::int64_t>(1, timeline.sampleRate / 1000);
        for (std::size_t i = 0; i < copy.clips.size(); ++i) {
            auto& clip = copy.clips[i];
            // Only a strictly later start makes a crossfade meaningful; simultaneous clips layer.
            std::int64_t crossIn = 0;
            for (std::size_t j = 0; j < i; ++j) {
                const auto& earlier = copy.clips[j];
                if (earlier.start >= clip.start)
                    continue;
                const auto overlap =
                    std::min(clip.start + clip.length, earlier.start + earlier.length) - clip.start;
                crossIn = std::max(crossIn, overlap);
            }
            clip.crossIn = std::clamp(crossIn, std::int64_t{0}, clip.length);
            std::int64_t crossOut = 0, crossOutOffset = 0;
            timeline::FadeCurve crossOutCurve = timeline::FadeCurve::Linear;
            for (std::size_t k = i + 1; k < copy.clips.size(); ++k) {
                const auto& later = copy.clips[k];
                if (later.start <= clip.start)
                    continue;
                if (later.start >= clip.start + clip.length)
                    break;
                const auto overlap =
                    std::min(clip.start + clip.length, later.start + later.length) - later.start;
                if (overlap > crossOut) {
                    crossOut = overlap;
                    crossOutOffset = later.start - clip.start;
                    crossOutCurve = later.fadeInCurve;
                }
            }
            clip.crossOut = std::clamp(crossOut, std::int64_t{0}, clip.length - crossOutOffset);
            clip.crossOutOffset = crossOutOffset;
            clip.crossOutCurve = crossOutCurve;
            const auto playable = std::min(clip.length, clip.source->frames() - clip.sourceOffset);
            const auto count = std::min(minimum + 1, playable);
            // The cached edges are only the 1 ms declick, so any longer user fade or crossfade
            // needs the general path (which applies the curve and the crossfade ramps).
            if (count > 0 && clip.fadeIn <= minimum && clip.fadeOut <= minimum &&
                clip.crossIn == 0 && clip.crossOut == 0) {
                clip.edges = {clip.source.get(), clip.sourceOffset, playable, {}};
                clip.edges.samples.resize(count * 4);
                for (std::int64_t edge = 0; edge < 2; ++edge)
                    for (std::int64_t i = 0; i < count; ++i) {
                        const auto local = edge ? playable - count + i : i;
                        const double ramp = std::min(1., double(local) / minimum) *
                                            std::min(1., double(playable - 1 - local) / minimum);
                        for (unsigned channel = 0; channel < 2; ++channel)
                            clip.edges
                                .samples[(edge * count + i) * 2 + channel] = static_cast<float>(
                                ramp *
                                clip.source
                                    ->samples[(clip.sourceOffset + local) * clip.source->channels +
                                              (clip.source->channels == 1 ? 0 : channel)]);
                    }
            }
        }
        result->tracks.push_back(std::move(copy));
    }
    return result;
}
} // namespace wavy
