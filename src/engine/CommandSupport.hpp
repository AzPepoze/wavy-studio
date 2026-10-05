#pragma once
#include "Commands.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace wavy::timeline::detail {
template <typename Id> Id allocateId(Id& next) {
    assert(next.value != 0);
    const Id allocated = next;
    // Zero marks exhaustion without wrapping into a previously allocated id.
    next.value = next.value == std::numeric_limits<std::uint32_t>::max() ? 0 : next.value + 1;
    return allocated;
}

struct CommandAccess {
    static std::vector<Track>& tracks(Timeline& timeline) { return timeline.tracks_; }
    static bool canAllocateTrack(const Timeline& timeline) {
        return timeline.nextTrack_.value != 0;
    }
    static bool canAllocateClip(const Timeline& timeline) { return timeline.nextClip_.value != 0; }
    static TrackId allocateTrack(Timeline& timeline) { return allocateId(timeline.nextTrack_); }
    static ClipId allocateClip(Timeline& timeline) { return allocateId(timeline.nextClip_); }
    static void invalidate(Timeline& timeline, TrackId id) { timeline.invalidate(id); }
};
inline bool validEnd(Frames start, Frames length) {
    return start >= 0 && length > 0 && start <= std::numeric_limits<Frames>::max() - length;
}
inline bool validGain(float gain) { return std::isfinite(gain) && gain >= 0; }
inline bool clipLess(const Clip& a, const Clip& b) {
    return a.start == b.start ? a.id < b.id : a.start < b.start;
}
inline void insertClip(Track& track, Clip clip) {
    const auto it = std::lower_bound(track.clips.begin(), track.clips.end(), clip, clipLess);
    track.clips.insert(it, std::move(clip));
}
inline auto findTrack(Timeline& timeline, TrackId id) {
    auto& tracks = CommandAccess::tracks(timeline);
    const auto it = std::find_if(tracks.begin(), tracks.end(),
                                 [=](const Track& track) { return track.id == id; });
    assert(it != tracks.end());
    return it;
}
inline auto findClip(Track& track, ClipId id) {
    const auto it = std::find_if(track.clips.begin(), track.clips.end(),
                                 [=](const Clip& clip) { return clip.id == id; });
    assert(it != track.clips.end());
    return it;
}
struct MutableClipLocation {
    Track& track;
    Clip& clip;
};
inline MutableClipLocation findClip(Timeline& timeline, ClipId id) {
    const auto location = timeline.findClip(id);
    assert(location);
    auto& track = *findTrack(timeline, location->track->id);
    return {track, *findClip(track, id)};
}
inline Clip takeClip(Track& track, ClipId id) {
    const auto it = findClip(track, id);
    Clip clip = std::move(*it);
    track.clips.erase(it);
    return clip;
}
inline void moveTrack(Timeline& timeline, TrackId id, size_t index) {
    auto& tracks = CommandAccess::tracks(timeline);
    const auto it = findTrack(timeline, id);
    Track track = std::move(*it);
    tracks.erase(it);
    tracks.insert(tracks.begin() + index, std::move(track));
}
inline void restoreClip(Timeline& timeline, TrackId trackId, const Clip& original) {
    auto& track = *findTrack(timeline, trackId);
    track.clips.erase(findClip(track, original.id));
    insertClip(track, original);
    CommandAccess::invalidate(timeline, trackId);
}
} // namespace wavy::timeline::detail
