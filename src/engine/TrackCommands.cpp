#include "CommandSupport.hpp"

namespace wavy::timeline {
using namespace detail;

Result AddTrack::validate(const Timeline& timeline) const {
    return createdTrackId_.value || CommandAccess::canAllocateTrack(timeline) ? Error::None
                                                                              : Error::IdExhausted;
}
void AddTrack::apply(Timeline& timeline) {
    assert(validate(timeline));
    if (!createdTrackId_.value)
        createdTrackId_ = CommandAccess::allocateTrack(timeline);
    CommandAccess::tracks(timeline).push_back({createdTrackId_, name_, {}});
}
void AddTrack::revert(Timeline& timeline) {
    assert(createdTrackId_.value);
    auto& tracks = CommandAccess::tracks(timeline);
    tracks.erase(findTrack(timeline, createdTrackId_));
    CommandAccess::invalidate(timeline, createdTrackId_);
}
Result RemoveTrack::validate(const Timeline& timeline) const {
    return timeline.findTrack(trackId_) ? Error::None : Error::UnknownTrack;
}
void RemoveTrack::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto& tracks = CommandAccess::tracks(timeline);
    const auto it = findTrack(timeline, trackId_);
    if (!removedTrack_) {
        previousIndex_ = it - tracks.begin();
        removedTrack_ = std::move(*it);
    }
    tracks.erase(it);
    CommandAccess::invalidate(timeline, trackId_);
}
void RemoveTrack::revert(Timeline& timeline) {
    assert(removedTrack_);
    auto& tracks = CommandAccess::tracks(timeline);
    tracks.insert(tracks.begin() + previousIndex_, *removedTrack_);
    CommandAccess::invalidate(timeline, trackId_);
}
Result MoveTrack::validate(const Timeline& timeline) const {
    if (!timeline.findTrack(trackId_))
        return Error::UnknownTrack;
    return destinationIndex_ < timeline.tracks().size() ? Error::None : Error::InvalidPosition;
}
void MoveTrack::apply(Timeline& timeline) {
    assert(validate(timeline));
    if (!previousIndex_)
        previousIndex_ = findTrack(timeline, trackId_) - CommandAccess::tracks(timeline).begin();
    moveTrack(timeline, trackId_, destinationIndex_);
}
void MoveTrack::revert(Timeline& timeline) {
    assert(previousIndex_);
    moveTrack(timeline, trackId_, *previousIndex_);
}
Result SetTrackGain::validate(const Timeline& timeline) const {
    if (!timeline.findTrack(trackId_))
        return Error::UnknownTrack;
    return validGain(newGain_) ? Error::None : Error::InvalidGain;
}
void SetTrackGain::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto& gain = findTrack(timeline, trackId_)->gain;
    if (!previousGain_)
        previousGain_ = gain;
    gain = newGain_;
}
void SetTrackGain::revert(Timeline& timeline) {
    assert(previousGain_);
    findTrack(timeline, trackId_)->gain = *previousGain_;
}
bool SetTrackGain::mergeWith(const Command& command) {
    const auto* other = dynamic_cast<const SetTrackGain*>(&command);
    if (!other || trackId_ != other->trackId_)
        return false;
    newGain_ = other->newGain_;
    return true;
}
Result SetTrackMute::validate(const Timeline& timeline) const {
    if (!timeline.findTrack(trackId_))
        return Error::UnknownTrack;
    return {};
}
void SetTrackMute::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto& muted = findTrack(timeline, trackId_)->muted;
    if (!previousMuted_)
        previousMuted_ = muted;
    muted = newMuted_;
}
void SetTrackMute::revert(Timeline& timeline) {
    assert(previousMuted_);
    findTrack(timeline, trackId_)->muted = *previousMuted_;
}
bool SetTrackMute::mergeWith(const Command& command) {
    const auto* other = dynamic_cast<const SetTrackMute*>(&command);
    if (!other || trackId_ != other->trackId_)
        return false;
    newMuted_ = other->newMuted_;
    return true;
}
Result SetTrackSolo::validate(const Timeline& timeline) const {
    if (!timeline.findTrack(trackId_))
        return Error::UnknownTrack;
    return {};
}
void SetTrackSolo::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto& solo = findTrack(timeline, trackId_)->solo;
    if (!previousSolo_)
        previousSolo_ = solo;
    solo = newSolo_;
}
void SetTrackSolo::revert(Timeline& timeline) {
    assert(previousSolo_);
    findTrack(timeline, trackId_)->solo = *previousSolo_;
}
bool SetTrackSolo::mergeWith(const Command& command) {
    const auto* other = dynamic_cast<const SetTrackSolo*>(&command);
    if (!other || trackId_ != other->trackId_)
        return false;
    newSolo_ = other->newSolo_;
    return true;
}
} // namespace wavy::timeline
