#pragma once
#include "io/AudioFile.hpp"

namespace wavy {
struct PeakPair {
    float min;
    float max;
};
class PeakPyramid {
  public:
    explicit PeakPyramid(const AudioBuffer& buffer);
    void query(std::int64_t beginFrame, std::int64_t endFrame, std::size_t bucketCount,
               std::vector<PeakPair>& out) const;

  private:
    std::int64_t frames_;
    std::vector<std::vector<PeakPair>> levels_;
};
} // namespace wavy
