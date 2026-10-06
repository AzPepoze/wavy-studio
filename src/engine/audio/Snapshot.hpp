#pragma once
#include "io/AudioFile.hpp"
#include "timeline/Timeline.hpp"
#include <memory>
#include <unordered_map>

namespace wavy {
using SourceCache = std::unordered_map<std::string, std::shared_ptr<const AudioBuffer>>;
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
    };
    std::vector<Track> tracks;
    bool anySolo = false;
};
std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline&, const SourceCache&);
} // namespace wavy
