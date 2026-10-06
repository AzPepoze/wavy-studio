#pragma once
#include "effects/Effect.hpp"
namespace wavy::effects {
// Mid/side stereo width with independent mid and side trims. An optional one-pole low band is
// folded to mono while the high band keeps the width, so the bass stays centred without a
// crossover gap. All live values are smoothed per sample, which keeps a bypassed default exact.
class StereoWidth final : public Effect {
  public:
    explicit StereoWidth(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "stereo_width"; }

  private:
    static double flush(double value) noexcept { return std::abs(value) < 1e-30 ? 0 : value; }
    std::shared_ptr<ParameterSet> params_;
    double rate_ = 48000;
    Smoother width_, mid_, side_, mono_;
    double lowL_ = 0, lowR_ = 0;
};
} // namespace wavy::effects
