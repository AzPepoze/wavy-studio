#include "effects/DeEsser.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace wavy::effects {
DeEsser::DeEsser(std::shared_ptr<ParameterSet> params)
    : params_(sharedParameters(std::move(params),
                               {{"frequency", "Frequency", "Hz", 3000, 10000, 6500, .3f},
                                {"q", "Q", "", .3f, 8, 1.5f, .3f},
                                {"threshold", "Threshold", "dB", -60, 0, -24},
                                {"ratio", "Ratio", "", 1, 20, 3},
                                {"range", "Range", "dB", 0, 30, 12},
                                {"attack", "Attack", "ms", .1f, 100, 2},
                                {"release", "Release", "ms", 1, 1000, 60},
                                {"listen", "Listen", "", 0, 1, 0},
                                {"mode", "Wide-band", "", 0, 1, 0}})) {}
DeEsser::Coefficients DeEsser::coefficients() const noexcept {
    const double w = 2 * std::numbers::pi *
                     std::clamp<double>(params_->get("frequency"), 1, rate_ * .49) / rate_;
    const double alpha = std::sin(w) / (2 * std::max<double>(params_->get("q"), .05)),
                 a0 = 1 + alpha;
    // RBJ constant-peak-gain band pass: unity gain at the detector centre, so levels match.
    return {alpha / a0, 0, -alpha / a0, -2 * std::cos(w) / a0, (1 - alpha) / a0};
}
void DeEsser::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    for (auto& coefficient : band_.coefficients)
        coefficient.prepare(rate);
    reset();
}
void DeEsser::reset() noexcept {
    band_.state = {};
    const auto current = coefficients();
    for (std::size_t i = 0; i < current.size(); ++i)
        band_.coefficients[i].reset(current[i]);
    energy_ = 0;
    reduction_ = 0;
    reductionDb_.store(0, std::memory_order_relaxed);
}
void DeEsser::process(float* stereo, std::size_t frames) noexcept {
    const double threshold = params_->get("threshold"), slope = 1 - 1. / params_->get("ratio"),
                 range = params_->get("range");
    const double attack =
        std::exp(-1. / (.001 * std::max<double>(params_->get("attack"), .01) * rate_));
    const double release =
        std::exp(-1. / (.001 * std::max<double>(params_->get("release"), .01) * rate_));
    const double level = std::exp(-1. / (.001 * 1. * rate_));
    // 10*log10(2*energy) > threshold is equivalent and avoids the logarithm below the threshold.
    const double thresholdLinear = std::pow(10., threshold / 10);
    const bool listen = params_->get("listen") >= .5f, wide = params_->get("mode") >= .5f;
    const auto target = coefficients();
    for (std::size_t i = 0; i < target.size(); ++i)
        band_.coefficients[i].target(target[i]);
    const bool settled = std::all_of(band_.coefficients.begin(), band_.coefficients.end(),
                                     [](const Smoother& c) { return c.settled(); });
    const auto render = [&](const Coefficients& c, std::size_t frame) {
        std::array<double, 2> dry{}, wet{};
        for (unsigned channel = 0; channel < 2; ++channel) {
            const double x = stereo[frame * 2 + channel];
            dry[channel] = x;
            auto& state = band_.state[channel];
            wet[channel] = c[0] * x + state[0];
            state[0] = c[1] * x - c[3] * wet[channel] + state[1];
            state[1] = c[2] * x - c[4] * wet[channel];
        }
        // Fixed short smoother on the peak power gives a waveform-independent level; the user
        // attack/release then act on the reduction in dB.
        const double power = std::max(wet[0] * wet[0], wet[1] * wet[1]);
        energy_ = power + level * (energy_ - power);
        if (energy_ < 1e-30)
            energy_ = 0;
        const double targetReduction =
            2 * energy_ > thresholdLinear
                ? std::min<double>(range, slope * (10 * std::log10(2 * energy_) - threshold))
                : 0;
        const double coefficient = targetReduction > reduction_ ? attack : release;
        reduction_ = targetReduction + coefficient * (reduction_ - targetReduction);
        // A reduction that has decayed to zero stays there; skip the exponential until it moves.
        const double gain = reduction_ > 0 ? std::exp(-reduction_ * (std::numbers::ln10 / 20)) : 1.;
        const double wetGain = gain - 1;
        for (unsigned channel = 0; channel < 2; ++channel) {
            // Split-band subtracts only the detected band, so the rest of the spectrum is
            // untouched.
            const double out = listen ? wet[channel]
                               : wide ? dry[channel] * gain
                                      : dry[channel] + wetGain * wet[channel];
            stereo[frame * 2 + channel] =
                static_cast<float>(std::clamp(out, -double(std::numeric_limits<float>::max()),
                                              double(std::numeric_limits<float>::max())));
        }
    };
    // A settled filter reads the target coefficients directly instead of stepping five smoothers
    // per sample.
    if (settled)
        for (std::size_t frame = 0; frame < frames; ++frame)
            render(target, frame);
    else
        for (std::size_t frame = 0; frame < frames; ++frame) {
            Coefficients c;
            for (std::size_t i = 0; i < c.size(); ++i)
                c[i] = band_.coefficients[i].next();
            render(c, frame);
        }
    // 1e-30 is inaudible and far above double subnormals; a blow-up shows up as a non-finite
    // block, so the denormal flush and the reset both run once per block.
    for (auto& channel : band_.state)
        for (auto& value : channel)
            if (std::abs(value) < 1e-30)
                value = 0;
    if (!allFinite(stereo, frames * 2)) {
        band_.state = {};
        energy_ = 0;
        reduction_ = 0;
        std::fill_n(stereo, frames * 2, 0.f);
    }
    reductionDb_.store(static_cast<float>(reduction_), std::memory_order_relaxed);
}
} // namespace wavy::effects
