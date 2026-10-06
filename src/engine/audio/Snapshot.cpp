#include "audio/Snapshot.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <limits>

namespace wavy {
std::unique_ptr<Snapshot> buildSnapshot(const timeline::Timeline& timeline,
                                        const SourceCache& cache) {
    auto result = std::make_unique<Snapshot>();
    SourceCache loaded;
    for (const auto& track : timeline.tracks()) {
        Snapshot::Track copy{track.gain, track.muted, track.solo, {}};
        result->anySolo |= track.solo;
        for (const auto& clip : track.clips) {
            std::shared_ptr<const AudioBuffer> source;
            if (auto it = cache.find(clip.source); it != cache.end())
                source = it->second;
            else if (auto it = loaded.find(clip.source); it != loaded.end())
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
                log::error("mixer", "Missing or incompatible source: {}", clip.source);
                continue;
            }
            if (clip.start < 0 || clip.length <= 0 || clip.sourceOffset < 0 ||
                clip.start > std::numeric_limits<std::int64_t>::max() - clip.length)
                continue;
            copy.clips.push_back({clip.start, clip.length, clip.sourceOffset,
                                  std::max<std::int64_t>(0, clip.fadeIn),
                                  std::max<std::int64_t>(0, clip.fadeOut), clip.gain, source});
        }
        std::sort(copy.clips.begin(), copy.clips.end(),
                  [](const auto& a, const auto& b) { return a.start < b.start; });
        result->tracks.push_back(std::move(copy));
    }
    return result;
}
} // namespace wavy
