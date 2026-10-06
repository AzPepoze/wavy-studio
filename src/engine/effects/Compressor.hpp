#pragma once
#include "effects/Effect.hpp"
namespace wavy::effects {
class Compressor final : public Effect {
  public:
    explicit Compressor(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "compressor"; }
    const std::atomic<float>& gainReductionDb() const noexcept { return reductionDb_; }

  private:
    std::shared_ptr<ParameterSet> params_;
    std::atomic<float> reductionDb_{0};
    double rate_ = 48000, envelope_ = 0;
    bool rms_ = false;
    Smoother makeup_, mix_;
};
} // namespace wavy::effects
