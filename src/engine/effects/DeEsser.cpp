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
    const bool listen = params_->get("listen") >= .5f, wide = params_->get("mode") >= .5f;
    const auto target = coefficients();
    for (std::size_t i = 0; i < target.size(); ++i)
        band_.coefficients[i].target(target[i]);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        Coefficients c;
        for (std::size_t i = 0; i < c.size(); ++i)
            c[i] = band_.coefficients[i].next();
        std::array<double, 2> dry{}, wet{};
        for (unsigned channel = 0; channel < 2; ++channel) {
            double x = stereo[frame * 2 + channel];
            if (!std::isfinite(x))
                x = 0;
            dry[channel] = x;
            auto& state = band_.state[channel];
            wet[channel] = c[0] * x + state[0];
            state[0] = c[1] * x - c[3] * wet[channel] + state[1];
            state[1] = c[2] * x - c[4] * wet[channel];
            for (auto& z : state)
                if (!std::isfinite(z) || std::abs(z) < 1e-30)
                    z = 0;
            if (!std::isfinite(wet[channel])) {
                wet[channel] = 0;
                state = {};
            }
        }
        // Fixed short smoother on the peak power gives a waveform-independent level; the user
        // attack/release then act on the reduction in dB.
        const double power = std::max(wet[0] * wet[0], wet[1] * wet[1]);
        energy_ = power + level * (energy_ - power);
        if (energy_ < 1e-30)
            energy_ = 0;
        const double over = 10 * std::log10(std::max(2 * energy_, 1e-30)) - threshold;
        const double targetReduction = over > 0 ? std::min<double>(range, slope * over) : 0;
        const double coefficient = targetReduction > reduction_ ? attack : release;
        reduction_ = targetReduction + coefficient * (reduction_ - targetReduction);
        const double gain = std::pow(10., -reduction_ / 20);
        for (unsigned channel = 0; channel < 2; ++channel) {
            // Split-band subtracts only the detected band, so the rest of the spectrum is
            // untouched.
            const double out = listen ? wet[channel]
                               : wide ? dry[channel] * gain
                                      : dry[channel] + (gain - 1) * wet[channel];
            stereo[frame * 2 + channel] =
                static_cast<float>(std::clamp(out, -double(std::numeric_limits<float>::max()),
                                              double(std::numeric_limits<float>::max())));
        }
    }
    reductionDb_.store(static_cast<float>(reduction_), std::memory_order_relaxed);
}
} // namespace wavy::effects
