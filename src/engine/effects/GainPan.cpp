#include "effects/GainPan.hpp"
#include <numbers>
#include <stdexcept>
namespace wavy::effects {
GainPan::GainPan(std::shared_ptr<ParameterSet> params)
    : params_(sharedParameters(std::move(params), {{"gain", "Gain", "dB",
                                                    -std::numeric_limits<float>::infinity(), 24, 0},
                                                   {"pan", "Pan", "", -1, 1, 0},
                                                   {"invert", "Invert polarity", "", 0, 1, 0}})) {}
std::array<double, 2> GainPan::gains() const noexcept {
    const double gain = std::pow(10., params_->get(0) / 20.) * (params_->get(2) >= .5f ? -1 : 1);
    const double angle = (params_->get(1) + 1) * std::numbers::pi / 4;
    return {gain * std::cos(angle), gain * std::sin(angle)};
}
void GainPan::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    for (auto& s : smooth_)
        s.prepare(rate);
    reset();
}
void GainPan::reset() noexcept {
    const auto g = gains();
    for (unsigned i = 0; i < 2; ++i)
        smooth_[i].reset(g[i]);
}
void GainPan::process(float* stereo, std::size_t frames) noexcept {
    const auto g = gains();
    for (unsigned i = 0; i < 2; ++i)
        smooth_[i].target(g[i]);
    for (std::size_t i = 0; i < frames; ++i)
        for (unsigned c = 0; c < 2; ++c)
            stereo[i * 2 + c] = static_cast<float>(std::clamp(
                stereo[i * 2 + c] * smooth_[c].next(), -double(std::numeric_limits<float>::max()),
                double(std::numeric_limits<float>::max())));
}
} // namespace wavy::effects
