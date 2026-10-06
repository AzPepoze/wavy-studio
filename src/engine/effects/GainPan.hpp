#pragma once
#include "effects/Effect.hpp"
#include <array>
namespace wavy::effects {
class GainPan final : public Effect {
  public:
    explicit GainPan(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "gain_pan"; }

  private:
    std::array<double, 2> gains() const noexcept;
    std::shared_ptr<ParameterSet> params_;
    std::array<Smoother, 2> smooth_;
};
} // namespace wavy::effects
