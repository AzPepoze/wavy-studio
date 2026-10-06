#pragma once
#include "io/Peaks.hpp"
#include <cstdint>
#include <vector>

namespace wavy {
// Per-column waveform envelope in item pixel coordinates. Column i covers pixels
// [firstX + i * stepX, firstX + (i + 1) * stepX); `columns` may retain capacity from a previous
// call, so the same Envelope can be reused every frame without allocating.
struct WaveformEnvelope {
    std::vector<PeakPair> columns;
    double firstX = 0;
    double stepX = 1;
    std::int64_t beginFrame = 0;
    std::int64_t endFrame = 0;
    int count = 0;
};

// Samples a PeakPyramid into one min/max pair per drawn column for a clip. Item x == 0 is the
// clip's first frame, so `sourceOffset` is the source frame at x == 0 and the clip ends at
// `lengthFrames * pixelsPerFrame`. Only columns overlapping [visibleLeft, visibleRight] are
// produced; the range is clipped to the source so audio past `sourceFrames` is left blank.
// `amplitude` scales both bounds (a negative factor swaps them). Returns `out`.
WaveformEnvelope& buildWaveformEnvelope(const PeakPyramid& peaks, std::int64_t sourceFrames,
                                        std::int64_t sourceOffset, std::int64_t lengthFrames,
                                        double pixelsPerFrame, double visibleLeft,
                                        double visibleRight, float amplitude,
                                        WaveformEnvelope& out);
} // namespace wavy
