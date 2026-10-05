#include "io/Peaks.hpp"
#include <algorithm>
#include <limits>

namespace wavy {
namespace {
PeakPair emptyPeak() {
    return {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
}
void merge(PeakPair& into, PeakPair peak) {
    into.min = std::min(into.min, peak.min);
    into.max = std::max(into.max, peak.max);
}
} // namespace
PeakPyramid::PeakPyramid(const AudioBuffer& buffer) : frames_(buffer.frames()) {
    if (!frames_)
        return;
    auto& base = levels_.emplace_back(static_cast<std::size_t>((frames_ + 63) / 64), emptyPeak());
    // True min/max over all channels: a mono average would cancel out-of-phase audio and halve
    // hard-panned audio.
    for (std::int64_t frame = 0; frame < frames_; ++frame)
        for (unsigned channel = 0; channel < buffer.channels; ++channel) {
            const auto sample =
                buffer.samples[static_cast<std::size_t>(frame) * buffer.channels + channel];
            merge(base[frame / 64], {sample, sample});
        }
    while (levels_.back().size() > 1) {
        const auto index = levels_.size() - 1;
        auto& upper = levels_.emplace_back((levels_[index].size() + 3) / 4, emptyPeak());
        for (std::size_t i = 0; i < levels_[index].size(); ++i)
            merge(upper[i / 4], levels_[index][i]);
    }
}
void PeakPyramid::query(std::int64_t beginFrame, std::int64_t endFrame, std::size_t bucketCount,
                        std::vector<PeakPair>& out) const {
    out.resize(bucketCount);
    if (!bucketCount)
        return;
    const auto begin = std::clamp(beginFrame, std::int64_t{0}, frames_);
    const auto end = std::clamp(endFrame, begin, frames_);
    if (begin == end) {
        std::fill(out.begin(), out.end(), PeakPair{0, 0});
        return;
    }
    const auto width = static_cast<double>(end - begin) / bucketCount;
    std::size_t level = 0;
    std::int64_t stride = 64;
    while (level + 1 < levels_.size() && stride <= width / 4) {
        ++level;
        stride *= 4;
    }
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const auto first = begin + static_cast<std::int64_t>(i * width);
        const auto last =
            std::min(end, std::max(first + 1, begin + static_cast<std::int64_t>((i + 1) * width)));
        auto peak = emptyPeak();
        for (auto j = first / stride; j <= (last - 1) / stride; ++j)
            merge(peak, levels_[level][j]);
        out[i] = peak;
    }
}
} // namespace wavy
