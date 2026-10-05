#include "CommandSupport.hpp"

namespace wavy::timeline {
using namespace detail;

Result AddClip::validate(const Timeline& timeline) const {
    if (!timeline.findTrack(trackId_))
        return Error::UnknownTrack;
    if (!createdClipId_.value && !CommandAccess::canAllocateClip(timeline))
        return Error::IdExhausted;
    if (!validEnd(clip_.start, clip_.length) || !validEnd(clip_.sourceOffset, clip_.length) ||
        !validGain(clip_.gain) || clip_.fadeIn < 0 || clip_.fadeOut < 0 ||
        clip_.fadeIn > clip_.length || clip_.fadeOut > clip_.length)
        return Error::InvalidClip;
    return {};
}
void AddClip::apply(Timeline& timeline) {
    assert(validate(timeline));
    if (!createdClipId_.value)
        createdClipId_ = CommandAccess::allocateClip(timeline);
    Clip clip = clip_;
    clip.id = createdClipId_;
    insertClip(*findTrack(timeline, trackId_), std::move(clip));
    CommandAccess::invalidate(timeline, trackId_);
}
void AddClip::revert(Timeline& timeline) {
    assert(createdClipId_.value);
    auto& track = *findTrack(timeline, trackId_);
    track.clips.erase(findClip(track, createdClipId_));
    CommandAccess::invalidate(timeline, trackId_);
}
Result RemoveClip::validate(const Timeline& timeline) const {
    return timeline.findClip(clipId_) ? Error::None : Error::UnknownClip;
}
void RemoveClip::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto location = findClip(timeline, clipId_);
    if (!originalClip_) {
        originalTrackId_ = location.track.id;
        originalClip_ = takeClip(location.track, clipId_);
    } else
        location.track.clips.erase(findClip(location.track, clipId_));
    CommandAccess::invalidate(timeline, location.track.id);
}
void RemoveClip::revert(Timeline& timeline) {
    assert(originalClip_);
    insertClip(*findTrack(timeline, originalTrackId_), *originalClip_);
    CommandAccess::invalidate(timeline, originalTrackId_);
}
Result MoveClip::validate(const Timeline& timeline) const {
    if (!timeline.findTrack(destinationTrackId_))
        return Error::UnknownTrack;
    const auto location = timeline.findClip(clipId_);
    if (!location)
        return Error::UnknownClip;
    return validEnd(newStart_, location->clip->length) ? Error::None : Error::InvalidPosition;
}
void MoveClip::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto location = findClip(timeline, clipId_);
    if (!previousStart_) {
        originalTrackId_ = location.track.id;
        previousStart_ = location.clip.start;
    }
    Clip moved = takeClip(location.track, clipId_);
    moved.start = newStart_;
    insertClip(*findTrack(timeline, destinationTrackId_), std::move(moved));
    CommandAccess::invalidate(timeline, originalTrackId_);
    CommandAccess::invalidate(timeline, destinationTrackId_);
}
void MoveClip::revert(Timeline& timeline) {
    assert(previousStart_);
    Clip moved = takeClip(*findTrack(timeline, destinationTrackId_), clipId_);
    moved.start = *previousStart_;
    insertClip(*findTrack(timeline, originalTrackId_), std::move(moved));
    CommandAccess::invalidate(timeline, originalTrackId_);
    CommandAccess::invalidate(timeline, destinationTrackId_);
}
Result SplitClip::validate(const Timeline& timeline) const {
    const auto location = timeline.findClip(clipId_);
    if (!location)
        return Error::UnknownClip;
    if (!createdClipId_.value && !CommandAccess::canAllocateClip(timeline))
        return Error::IdExhausted;
    const auto& clip = *location->clip;
    return splitFrame_ > clip.start && splitFrame_ < clip.start + clip.length
               ? Error::None
               : Error::InvalidPosition;
}
void SplitClip::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto location = findClip(timeline, clipId_);
    if (!originalClip_) {
        originalTrackId_ = location.track.id;
        originalClip_ = location.clip;
        createdClipId_ = CommandAccess::allocateClip(timeline);
    }
    auto& left = location.clip;
    Clip right = left;
    const Frames leftLength = splitFrame_ - left.start;
    right.id = createdClipId_;
    right.start = splitFrame_;
    right.sourceOffset += leftLength;
    right.length -= leftLength;
    right.fadeIn = 0;
    right.fadeOut = std::min(right.fadeOut, right.length);
    left.length = leftLength;
    left.fadeIn = std::min(left.fadeIn, leftLength);
    left.fadeOut = 0;
    insertClip(location.track, std::move(right));
    CommandAccess::invalidate(timeline, originalTrackId_);
}
void SplitClip::revert(Timeline& timeline) {
    assert(originalClip_);
    auto& track = *findTrack(timeline, originalTrackId_);
    track.clips.erase(findClip(track, createdClipId_));
    restoreClip(timeline, originalTrackId_, *originalClip_);
}
Result TrimClip::validate(const Timeline& timeline) const {
    return timeline.findClip(clipId_) ? Error::None : Error::UnknownClip;
}
void TrimClip::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto location = findClip(timeline, clipId_);
    if (!originalClip_) {
        originalTrackId_ = location.track.id;
        originalClip_ = location.clip;
    }
    Clip trimmed = takeClip(location.track, clipId_);
    if (edge_ == Edge::Left) {
        const auto minimum = trimmed.start - std::min(trimmed.start, trimmed.sourceOffset);
        const auto start = std::clamp(edgeFrame_, minimum, trimmed.start + trimmed.length - 1);
        const auto delta = start - trimmed.start;
        trimmed.start = start;
        trimmed.sourceOffset += delta;
        trimmed.length -= delta;
    } else {
        const auto maximum =
            std::numeric_limits<Frames>::max() - std::max(trimmed.start, trimmed.sourceOffset);
        const auto end = std::clamp(edgeFrame_, trimmed.start + 1, trimmed.start + maximum);
        trimmed.length = end - trimmed.start;
    }
    trimmed.fadeIn = std::min(trimmed.fadeIn, trimmed.length);
    trimmed.fadeOut = std::min(trimmed.fadeOut, trimmed.length);
    insertClip(location.track, std::move(trimmed));
    CommandAccess::invalidate(timeline, originalTrackId_);
}
void TrimClip::revert(Timeline& timeline) {
    assert(originalClip_);
    restoreClip(timeline, originalTrackId_, *originalClip_);
}
Result DuplicateClip::validate(const Timeline& timeline) const {
    const auto location = timeline.findClip(clipId_);
    if (!location)
        return Error::UnknownClip;
    if (!createdClipId_.value && !CommandAccess::canAllocateClip(timeline))
        return Error::IdExhausted;
    const auto& clip = *location->clip;
    return validEnd(newStart_.value_or(clip.start + clip.length), clip.length)
               ? Error::None
               : Error::InvalidPosition;
}
void DuplicateClip::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto location = findClip(timeline, clipId_);
    if (!createdClipId_.value) {
        originalTrackId_ = location.track.id;
        createdClipId_ = CommandAccess::allocateClip(timeline);
    }
    Clip copy = location.clip;
    copy.id = createdClipId_;
    copy.start = newStart_.value_or(copy.start + copy.length);
    insertClip(location.track, std::move(copy));
    CommandAccess::invalidate(timeline, originalTrackId_);
}
void DuplicateClip::revert(Timeline& timeline) {
    assert(createdClipId_.value);
    auto& track = *findTrack(timeline, originalTrackId_);
    track.clips.erase(findClip(track, createdClipId_));
    CommandAccess::invalidate(timeline, originalTrackId_);
}
Result SetClipGain::validate(const Timeline& timeline) const {
    if (!timeline.findClip(clipId_))
        return Error::UnknownClip;
    return validGain(newGain_) ? Error::None : Error::InvalidGain;
}
void SetClipGain::apply(Timeline& timeline) {
    assert(validate(timeline));
    auto& gain = findClip(timeline, clipId_).clip.gain;
    if (!previousGain_)
        previousGain_ = gain;
    gain = newGain_;
}
void SetClipGain::revert(Timeline& timeline) {
    assert(previousGain_);
    findClip(timeline, clipId_).clip.gain = *previousGain_;
}
bool SetClipGain::mergeWith(const Command& command) {
    const auto* other = dynamic_cast<const SetClipGain*>(&command);
    if (!other || clipId_ != other->clipId_)
        return false;
    newGain_ = other->newGain_;
    return true;
}
} // namespace wavy::timeline
