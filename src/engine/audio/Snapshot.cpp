#include "audio/Snapshot.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wavy {
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
                                  {}});
            auto& copyClip = copy.clips.back();
            const auto playable = std::min(clip.length, source->frames() - clip.sourceOffset);
            const auto minimum = std::max<std::int64_t>(1, timeline.sampleRate / 1000);
            // Include the unity endpoint so the entire boundary region uses the cached SIMD path.
            const auto count = std::min(minimum + 1, playable);
            if (count > 0 && clip.fadeIn <= minimum && clip.fadeOut <= minimum) {
                copyClip.edges = {source.get(), clip.sourceOffset, playable, {}};
                copyClip.edges.samples.resize(count * 4);
                for (std::int64_t edge = 0; edge < 2; ++edge)
                    for (std::int64_t i = 0; i < count; ++i) {
                        const auto local = edge ? playable - count + i : i;
                        const double ramp = std::min(1., double(local) / minimum) *
                                            std::min(1., double(playable - 1 - local) / minimum);
                        for (unsigned channel = 0; channel < 2; ++channel)
                            copyClip.edges.samples[(edge * count + i) * 2 + channel] =
                                static_cast<float>(
                                    ramp *
                                    source->samples[(clip.sourceOffset + local) * source->channels +
                                                    (source->channels == 1 ? 0 : channel)]);
                    }
            }
        }
        std::sort(copy.clips.begin(), copy.clips.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
        result->tracks.push_back(std::move(copy));
    }
    return result;
}
} // namespace wavy
