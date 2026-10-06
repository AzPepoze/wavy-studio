#pragma once
#include "effects/Effect.hpp"
#include <array>
namespace wavy::effects {
class DeEsser final : public Effect {
  public:
    explicit DeEsser(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "de_esser"; }
    const std::atomic<float>& gainReductionDb() const noexcept { return reductionDb_; }

  private:
    using Coefficients = std::array<double, 5>;
    Coefficients coefficients() const noexcept;
    struct BandPass {
        std::array<Smoother, 5> coefficients;
        std::array<std::array<double, 2>, 2> state{};
    };
    std::shared_ptr<ParameterSet> params_;
    std::atomic<float> reductionDb_{0};
    BandPass band_;
    double rate_ = 48000, energy_ = 0, reduction_ = 0;
};
} // namespace wavy::effects
