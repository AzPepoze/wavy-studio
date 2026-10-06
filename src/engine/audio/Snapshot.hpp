#pragma once
#include "effects/Effect.hpp"
#include "io/AudioFile.hpp"
#include "timeline/Timeline.hpp"
#include <map>
#include <memory>
#include <unordered_map>

namespace wavy {
using SourceCache = std::unordered_map<std::string, std::shared_ptr<const AudioBuffer>>;
struct EffectChains {
    std::map<timeline::TrackId, std::vector<effects::EffectSlot>> tracks;
    std::vector<effects::EffectSlot> master;
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
    };
    struct Track {
        float gain;
        bool muted, solo;
        std::vector<Clip> clips;
        std::vector<PreparedEffect> effects;
        std::vector<float> scratch;
    };
    std::vector<Track> tracks;
    bool anySolo = false;
    std::vector<PreparedEffect> masterEffects;
    std::size_t maxBlockFrames = 512;
};
std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline&, const SourceCache&,
                                        const EffectChains& chains = {},
                                        std::size_t maxBlockFrames = 512);
} // namespace wavy
