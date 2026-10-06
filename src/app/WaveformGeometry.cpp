#include "WaveformGeometry.hpp"
#include <algorithm>
#include <cmath>

namespace wavy {
namespace {
// Bounds one draw pass' column count; the visible pixel span keeps this far lower in practice.
constexpr int kMaximumColumns = 16384;
} // namespace

WaveformEnvelope& buildWaveformEnvelope(const PeakPyramid& peaks, std::int64_t sourceFrames,
                                        std::int64_t sourceOffset, std::int64_t lengthFrames,
                                        double pixelsPerFrame, double visibleLeft,
                                        double visibleRight, float amplitude,
                                        WaveformEnvelope& out) {
    out.firstX = 0;
    out.stepX = 1;
    out.beginFrame = 0;
    out.endFrame = 0;
    out.count = 0;
    out.columns.clear();
    if (!(pixelsPerFrame > 0.0) || lengthFrames <= 0 || sourceFrames <= 0)
        return out;

    const std::int64_t clipBegin = std::max<std::int64_t>(0, sourceOffset);
    const std::int64_t clipEnd = std::min<std::int64_t>(sourceFrames, sourceOffset + lengthFrames);
    if (clipEnd <= clipBegin)
        return out;

    const double clipWidth = static_cast<double>(lengthFrames) * pixelsPerFrame;
    // Where the source runs out, so the uncovered tail of a clip stays blank.
    const double sourceEndX = static_cast<double>(clipEnd - sourceOffset) * pixelsPerFrame;
    const double left = std::max(0.0, visibleLeft);
    const double right = std::min({clipWidth, visibleRight, sourceEndX});
    if (!(right > left))
        return out;

    // At least one column per frame when zoomed in, one per pixel when zoomed out.
    const double framesPerColumn = std::max(1.0, 1.0 / pixelsPerFrame);
    int count = static_cast<int>(std::ceil((right - left) / (framesPerColumn * pixelsPerFrame)));
    count = std::clamp(count, 1, kMaximumColumns);

    const std::int64_t beginFrame =
        sourceOffset + static_cast<std::int64_t>(std::floor(left / pixelsPerFrame));
    const std::int64_t endFrame = std::min(
        clipEnd, sourceOffset + static_cast<std::int64_t>(std::ceil(right / pixelsPerFrame)));
    if (endFrame <= beginFrame)
        return out;

    peaks.query(beginFrame, endFrame, static_cast<std::size_t>(count), out.columns);
    for (auto& column : out.columns) {
        const float low = column.min * amplitude;
        const float high = column.max * amplitude;
        column = low <= high ? PeakPair{low, high} : PeakPair{high, low};
    }
    out.firstX = left;
    out.stepX = (right - left) / count;
    out.beginFrame = beginFrame;
    out.endFrame = endFrame;
    out.count = count;
    return out;
}
} // namespace wavy
