#include "effects/StereoWidth.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace wavy::effects {
StereoWidth::StereoWidth(std::shared_ptr<ParameterSet> params)
    : params_(sharedParameters(std::move(params),
                               {{"width", "Width", "%", 0, 200, 100},
                                {"mid_gain", "Mid gain", "dB", -24, 24, 0},
                                {"side_gain", "Side gain", "dB", -24, 24, 0},
                                {"bass_mono", "Bass mono", "Hz", 0, 1000, 0, .4f}})) {}
void StereoWidth::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    width_.prepare(rate);
    mid_.prepare(rate);
    side_.prepare(rate);
    mono_.prepare(rate);
    reset();
}
void StereoWidth::reset() noexcept {
    lowL_ = lowR_ = 0;
    width_.reset(params_->get(0) / 100.);
    mid_.reset(std::pow(10., params_->get(1) / 20.));
    side_.reset(std::pow(10., params_->get(2) / 20.));
    mono_.reset(params_->get(3) > 0 ? 1. : 0.);
}
void StereoWidth::process(float* stereo, std::size_t frames) noexcept {
    const double bassMono = std::clamp<double>(params_->get(3), 0, rate_ * .45);
    const double coefficient = 1 - std::exp(-2 * std::numbers::pi * bassMono / rate_);
    width_.target(params_->get(0) / 100.);
    mid_.target(std::pow(10., params_->get(1) / 20.));
    side_.target(std::pow(10., params_->get(2) / 20.));
    mono_.target(bassMono > 0 ? 1. : 0.);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double l = finiteOrZero(stereo[frame * 2]), r = finiteOrZero(stereo[frame * 2 + 1]);
        const double mid = (l + r) * .5, side = (l - r) * .5;
        lowL_ = flush(lowL_ + coefficient * (l - lowL_));
        lowR_ = flush(lowR_ + coefficient * (r - lowR_));
        const double widthGain = width_.next();
        const double midGain = mid_.next(), sideGain = side_.next() * widthGain,
                     amount = mono_.next();
        double outL = mid * midGain + side * sideGain;
        double outR = mid * midGain - side * sideGain;
        if (amount > 0) {
            // Replace the low band's side component with its mid so only the high band keeps width.
            const double lowMid = (lowL_ + lowR_) * .5, lowSide = (lowL_ - lowR_) * .5;
            outL += amount * (lowMid * (1 - midGain) - lowSide * sideGain);
            outR += amount * (lowMid * (1 - midGain) + lowSide * sideGain);
        }
        const double limit = double(std::numeric_limits<float>::max());
        stereo[frame * 2] = static_cast<float>(std::clamp(outL, -limit, limit));
        stereo[frame * 2 + 1] = static_cast<float>(std::clamp(outR, -limit, limit));
    }
    if (!allFinite(stereo, frames * 2)) {
        reset();
        std::fill_n(stereo, frames * 2, 0.f);
    }
}
} // namespace wavy::effects
