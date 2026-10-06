#include "effects/Compressor.hpp"
#include <stdexcept>
namespace wavy::effects {
Compressor::Compressor(std::shared_ptr<ParameterSet> params)
    : params_(
          sharedParameters(std::move(params), {{"threshold", "Threshold", "dB", -80, 0, -18},
                                               {"ratio", "Ratio", "", 1, 20, 4},
                                               {"attack", "Attack", "ms", .1f, 500, 10},
                                               {"release", "Release", "ms", 1, 5000, 100},
                                               {"knee", "Knee", "dB", 0, 24, 6},
                                               {"makeup", "Makeup", "dB", -24, 24, 0},
                                               {"auto_makeup", "Auto makeup", "", 0, 1, 0},
                                               {"detector", "Detector (peak / RMS)", "", 0, 1, 0},
                                               {"mix", "Mix", "", 0, 1, 1}})) {}
void Compressor::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    makeup_.prepare(rate);
    mix_.prepare(rate);
    reset();
}
void Compressor::reset() noexcept {
    envelope_ = 0;
    rms_ = params_->get(7) >= .5f;
    reductionDb_.store(0, std::memory_order_relaxed);
    const double autoGain =
        params_->get(6) >= .5f ? -params_->get(0) * (1 - 1. / params_->get(1)) : 0;
    makeup_.reset(std::pow(10., (params_->get(5) + autoGain) / 20));
    mix_.reset(params_->get(8));
}
void Compressor::process(float* stereo, std::size_t frames) noexcept {
    const double threshold = params_->get(0), slope = 1 - 1. / params_->get(1),
                 knee = params_->get(4);
    const double attack = std::exp(-1. / (.001 * params_->get(2) * rate_));
    const double release = std::exp(-1. / (.001 * params_->get(3) * rate_));
    const bool rms = params_->get(7) >= .5f;
    if (rms != rms_) {
        envelope_ = rms ? envelope_ * envelope_ : std::sqrt(envelope_);
        rms_ = rms;
    }
    makeup_.target(
        std::pow(10., (params_->get(5) + (params_->get(6) >= .5f ? -threshold * slope : 0)) / 20));
    mix_.target(params_->get(8));
    double reduction = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double l = stereo[i * 2], r = stereo[i * 2 + 1];
        const double detector = rms ? (l * l + r * r) * .5 : std::max(std::abs(l), std::abs(r));
        const double coefficient = detector > envelope_ ? attack : release;
        envelope_ = detector + coefficient * (envelope_ - detector);
        if (envelope_ < 1e-30)
            envelope_ = 0;
        const double level = (rms ? 10 : 20) * std::log10(std::max(envelope_, 1e-30));
        const double over = level - threshold;
        reduction = over > knee / 2
                        ? slope * over
                        : (knee > 0 && over > -knee / 2
                               ? slope * (over + knee / 2) * (over + knee / 2) / (2 * knee)
                               : 0);
        const double wet = std::pow(10., -reduction / 20) * makeup_.next();
        const double mix = mix_.next(), gain = 1 + mix * (wet - 1);
        for (unsigned c = 0; c < 2; ++c)
            stereo[i * 2 + c] = static_cast<float>(
                std::clamp(stereo[i * 2 + c] * gain, -double(std::numeric_limits<float>::max()),
                           double(std::numeric_limits<float>::max())));
    }
    reductionDb_.store(static_cast<float>(reduction), std::memory_order_relaxed);
}
} // namespace wavy::effects
