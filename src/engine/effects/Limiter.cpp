#include "effects/Limiter.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace wavy::effects {
Limiter::Limiter(std::shared_ptr<ParameterSet> params)
    : params_(
          sharedParameters(std::move(params), {{"input_gain", "Input gain", "dB", -24, 24, 0},
                                               {"ceiling", "Ceiling", "dBFS", -20, 0, -1},
                                               {"release", "Release", "ms", 10, 1000, 100},
                                               {"auto_release", "Auto release", "", 0, 1, 0}})) {}
void Limiter::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    lookahead_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(rate * .0015)));
    for (auto& channel : delay_)
        channel.assign(lookahead_, 0);
    windowValue_.assign(lookahead_ + 1, 1);
    windowIndex_.assign(lookahead_ + 1, 0);
    reset();
}
void Limiter::reset() noexcept {
    for (auto& channel : delay_)
        std::fill(channel.begin(), channel.end(), 0);
    write_ = 0;
    windowHead_ = 0;
    windowCount_ = 0;
    position_ = 0;
    envelope_ = 1;
    reductionDb_.store(0, std::memory_order_relaxed);
}
void Limiter::process(float* stereo, std::size_t frames) noexcept {
    const double inputGain = std::pow(10., params_->get("input_gain") / 20);
    const double limit = std::pow(10., (params_->get("ceiling") - params_->get("input_gain")) / 20);
    const double releaseMs = params_->get("auto_release") >= .5f ? 250. : params_->get("release");
    const double release = std::exp(-1. / (.001 * releaseMs * rate_));
    const double attack = std::exp(-4. / lookahead_);
    const std::size_t capacity = lookahead_ + 1;
    double reduction = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        double l = stereo[i * 2], r = stereo[i * 2 + 1];
        if (!std::isfinite(l))
            l = 0;
        if (!std::isfinite(r))
            r = 0;
        l *= inputGain;
        r *= inputGain;
        const double peak = std::max(std::abs(l), std::abs(r));
        const double need = peak > 0 ? std::min(1., limit / peak) : 1.;
        // Monotonic deque keeps the minimum required gain over the look-ahead window, so a peak
        // lowers the gain before the delayed sample reaches the output.
        while (windowCount_ && windowIndex_[windowHead_] + lookahead_ < position_) {
            windowHead_ = (windowHead_ + 1) % capacity;
            --windowCount_;
        }
        while (windowCount_ && windowValue_[(windowHead_ + windowCount_ - 1) % capacity] >= need)
            --windowCount_;
        windowValue_[(windowHead_ + windowCount_) % capacity] = need;
        windowIndex_[(windowHead_ + windowCount_) % capacity] = position_;
        ++windowCount_;
        const double windowMin = windowValue_[windowHead_];
        const double coefficient = windowMin < envelope_ ? attack : release;
        envelope_ = windowMin + coefficient * (envelope_ - windowMin);
        const double delayedL = delay_[0][write_], delayedR = delay_[1][write_];
        delay_[0][write_] = l;
        delay_[1][write_] = r;
        write_ = (write_ + 1) % lookahead_;
        const double delayedPeak = std::max(std::abs(delayedL), std::abs(delayedR));
        double gain = envelope_;
        // The smoother lags on a fast attack, so clamp the emitted sample to its own requirement.
        if (delayedPeak > limit)
            gain = std::min(gain, limit / delayedPeak);
        if (!std::isfinite(gain))
            gain = 0;
        reduction = gain > 0 ? -20. * std::log10(gain) : 0;
        stereo[i * 2] = static_cast<float>(std::clamp(delayedL * gain,
                                                      -double(std::numeric_limits<float>::max()),
                                                      double(std::numeric_limits<float>::max())));
        stereo[i * 2 + 1] = static_cast<float>(
            std::clamp(delayedR * gain, -double(std::numeric_limits<float>::max()),
                       double(std::numeric_limits<float>::max())));
        ++position_;
    }
    reductionDb_.store(static_cast<float>(reduction), std::memory_order_relaxed);
}
} // namespace wavy::effects
