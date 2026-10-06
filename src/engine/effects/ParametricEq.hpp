#pragma once
#include "effects/Effect.hpp"
#include <array>
namespace wavy::effects {
class ParametricEq final : public Effect {
  public:
    enum class Type { LowShelf, Peak, HighShelf };
    explicit ParametricEq(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "parametric_eq"; }

  private:
    using Coefficients = std::array<double, 5>;
    Coefficients coefficients(std::size_t band) const noexcept;
    struct Band {
        std::array<Smoother, 5> coefficients;
        std::array<std::array<double, 2>, 2> state{};
    };
    std::shared_ptr<ParameterSet> params_;
    std::array<Band, 4> bands_;
    double rate_ = 48000;
};
} // namespace wavy::effects
