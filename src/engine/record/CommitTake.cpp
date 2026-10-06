#include "record/CommitTake.hpp"

namespace wavy::record {
timeline::Result commitTake(timeline::History& history, timeline::TrackId track,
                            const RecordedTake& take) {
    timeline::Clip clip;
    const auto source = take.path.u8string();
    clip.source.assign(source.begin(), source.end());
    clip.start = take.startFrame;
    clip.length = take.lengthFrames;
    return history.execute(std::make_unique<timeline::AddClip>(track, std::move(clip)));
}
} // namespace wavy::record
