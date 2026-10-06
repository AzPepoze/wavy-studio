#include "timeline/CommandSupport.hpp"

namespace wavy::timeline {
using namespace detail;

Result SetTempo::validate(const Timeline&) const {
    return validTempo(newBpm_) ? Error::None : Error::InvalidTempo;
}
void SetTempo::apply(Timeline& timeline) {
    assert(validate(timeline));
    if (!previousBpm_)
        previousBpm_ = timeline.tempoBpm;
    timeline.tempoBpm = newBpm_;
}
void SetTempo::revert(Timeline& timeline) {
    assert(previousBpm_);
    timeline.tempoBpm = *previousBpm_;
}
bool SetTempo::mergeWith(const Command& command) {
    const auto* other = dynamic_cast<const SetTempo*>(&command);
    if (!other)
        return false;
    newBpm_ = other->newBpm_;
    return true;
}
Result SetTimeSignature::validate(const Timeline&) const {
    return validTimeSignature(newNumerator_, newDenominator_) ? Error::None
                                                              : Error::InvalidTimeSignature;
}
void SetTimeSignature::apply(Timeline& timeline) {
    assert(validate(timeline));
    if (!previousSignature_)
        previousSignature_ =
            std::pair{timeline.timeSignatureNumerator, timeline.timeSignatureDenominator};
    timeline.timeSignatureNumerator = newNumerator_;
    timeline.timeSignatureDenominator = newDenominator_;
}
void SetTimeSignature::revert(Timeline& timeline) {
    assert(previousSignature_);
    timeline.timeSignatureNumerator = previousSignature_->first;
    timeline.timeSignatureDenominator = previousSignature_->second;
}
} // namespace wavy::timeline
