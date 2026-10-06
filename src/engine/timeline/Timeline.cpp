#include "timeline/Timeline.hpp"
#include <algorithm>
#include <limits>

namespace wavy::timeline {
const Track* Timeline::findTrack(TrackId id) const {
    auto it =
        std::find_if(tracks_.begin(), tracks_.end(), [=](const Track& t) { return t.id == id; });
    return it == tracks_.end() ? nullptr : &*it;
}
std::optional<Timeline::ClipLocation> Timeline::findClip(ClipId id) const {
    for (const auto& track : tracks_)
        for (const auto& clip : track.clips)
            if (clip.id == id)
                return ClipLocation{&track, &clip};
    return std::nullopt;
}
void Timeline::invalidate(TrackId id) {
    std::erase_if(indexes_, [=](const Index& index) { return index.id == id; });
}
Frames Timeline::endFrame() const {
    Frames end = 0;
    for (const auto& track : tracks_)
        for (const auto& clip : track.clips)
            end = std::max(end, clip.start + clip.length);
    return end;
}
std::vector<const Clip*> Timeline::clipsInRange(TrackId id, Frames begin, Frames end) const {
    std::vector<const Clip*> result;
    const auto* track = findTrack(id);
    if (!track || begin >= end || track->clips.empty())
        return result;
    auto it = std::find_if(indexes_.begin(), indexes_.end(),
                           [=](const Index& index) { return index.id == id; });
    if (it == indexes_.end()) {
        indexes_.push_back({id, std::vector<Frames>(track->clips.size() * 4)});
        it = std::prev(indexes_.end());
        auto build = [&](auto&& self, size_t node, size_t left, size_t right) -> Frames {
            if (right - left == 1)
                return it->ends[node] = track->clips[left].start + track->clips[left].length;
            const auto middle = (left + right) / 2;
            return it->ends[node] = std::max(self(self, node * 2, left, middle),
                                             self(self, node * 2 + 1, middle, right));
        };
        build(build, 1, 0, track->clips.size());
    }
    const auto limit =
        std::lower_bound(track->clips.begin(), track->clips.end(), end,
                         [](const Clip& clip, Frames frame) { return clip.start < frame; }) -
        track->clips.begin();
    // Maximum-end pruning keeps long overlapping clips visible without scanning the track.
    auto query = [&](auto&& self, size_t node, size_t left, size_t right) -> void {
        if (left >= static_cast<size_t>(limit) || it->ends[node] <= begin)
            return;
        if (right - left == 1) {
            result.push_back(&track->clips[left]);
            return;
        }
        const auto middle = (left + right) / 2;
        self(self, node * 2, left, middle);
        self(self, node * 2 + 1, middle, right);
    };
    query(query, 1, 0, track->clips.size());
    return result;
}
void Timeline::Restorer::addTrack(Track track) {
    std::sort(track.clips.begin(), track.clips.end(), [](const Clip& a, const Clip& b) {
        return a.start == b.start ? a.id < b.id : a.start < b.start;
    });
    tracks_.push_back(std::move(track));
}
void Timeline::Restorer::setCounters(TrackId nextTrack, ClipId nextClip) {
    nextTrack_ = nextTrack;
    nextClip_ = nextClip;
}
Timeline Timeline::Restorer::build() {
    Timeline timeline;
    timeline.sampleRate = sampleRate_;
    timeline.tracks_ = std::move(tracks_);
    timeline.nextTrack_ = nextTrack_;
    timeline.nextClip_ = nextClip_;
    return timeline;
}
} // namespace wavy::timeline
