#pragma once
#include "effects/Effect.hpp"
#include <array>
#include <cstdint>
#include <vector>
namespace wavy::effects {
class Limiter final : public Effect {
  public:
    explicit Limiter(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "limiter"; }
    std::size_t latencyFrames() const noexcept override { return lookahead_; }
    const std::atomic<float>& gainReductionDb() const noexcept { return reductionDb_; }

  private:
    std::shared_ptr<ParameterSet> params_;
    std::atomic<float> reductionDb_{0};
    double rate_ = 48000;
    std::size_t lookahead_ = 1, write_ = 0;
    std::array<std::vector<double>, 2> delay_;
    std::vector<double> needDelay_;
    std::vector<double> windowValue_;
    std::vector<std::uint64_t> windowIndex_;
    std::size_t windowHead_ = 0, windowCount_ = 0, windowMask_ = 0;
    std::uint64_t position_ = 0;
    double envelope_ = 1;
};
} // namespace wavy::effects
