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
    needDelay_.assign(lookahead_, 1);
    // A power-of-two ring lets the monotonic deque index with a mask instead of a division.
    std::size_t capacity = 1;
    while (capacity < lookahead_ + 1)
        capacity <<= 1;
    windowMask_ = capacity - 1;
    windowValue_.assign(capacity, 1);
    windowIndex_.assign(capacity, 0);
    reset();
}
void Limiter::reset() noexcept {
    for (auto& channel : delay_)
        std::fill(channel.begin(), channel.end(), 0);
    std::fill(needDelay_.begin(), needDelay_.end(), 1);
    write_ = 0;
    windowHead_ = 0;
    windowCount_ = 0;
    position_ = 0;
    envelope_ = 1;
    reductionDb_.store(0, std::memory_order_relaxed);
}
void Limiter::process(float* stereo, std::size_t frames) noexcept {
    const double inputGain = std::pow(10., params_->get("input_gain") / 20);
    const double limit = std::pow(10., params_->get("ceiling") / 20) / inputGain;
    const double maxFloat = double(std::numeric_limits<float>::max());
    // With nothing pending in the window or release envelope, a block that stays below the ceiling
    // needs no gain reduction at all: the gain is exactly one, so the serial smoother is skipped.
    if (windowCount_ == 0 && envelope_ == 1.) {
        double blockPeak = 0;
        bool finiteInput = true;
        for (std::size_t i = 0; i < frames * 2; ++i) {
            finiteInput &= std::isfinite(stereo[i]);
            blockPeak = std::max(blockPeak, std::abs(double(stereo[i])));
        }
        if (finiteInput && blockPeak * inputGain <= limit) {
            for (std::size_t i = 0; i < frames; ++i) {
                const double l = double(stereo[i * 2]) * inputGain;
                const double r = double(stereo[i * 2 + 1]) * inputGain;
                const double delayedL = delay_[0][write_], delayedR = delay_[1][write_];
                delay_[0][write_] = l;
                delay_[1][write_] = r;
                needDelay_[write_] = 1.;
                if (++write_ == lookahead_)
                    write_ = 0;
                stereo[i * 2] = static_cast<float>(std::clamp(delayedL, -maxFloat, maxFloat));
                stereo[i * 2 + 1] = static_cast<float>(std::clamp(delayedR, -maxFloat, maxFloat));
            }
            position_ += frames;
            reductionDb_.store(0, std::memory_order_relaxed);
            return;
        }
    }
    const double releaseMs = params_->get("auto_release") >= .5f ? 250. : params_->get("release");
    const double release = std::exp(-1. / (.001 * releaseMs * rate_));
    const double attack = std::exp(-4. / double(lookahead_));
    const std::size_t mask = windowMask_;
    double lastGain = 1;
    for (std::size_t i = 0; i < frames; ++i) {
        const double l = double(stereo[i * 2]) * inputGain;
        const double r = double(stereo[i * 2 + 1]) * inputGain;
        const double peak = std::max(std::abs(l), std::abs(r));
        const double need = peak > limit ? limit / peak : 1.;
        // Monotonic deque keeps the minimum required gain over the look-ahead window, so a peak
        // lowers the gain before the delayed sample reaches the output. A gain of exactly one
        // (no limiting) can never lower the window minimum, so it is not stored.
        while (windowCount_ && windowIndex_[windowHead_] + lookahead_ < position_) {
            windowHead_ = (windowHead_ + 1) & mask;
            --windowCount_;
        }
        if (need < 1.) {
            while (windowCount_ && windowValue_[(windowHead_ + windowCount_ - 1) & mask] >= need)
                --windowCount_;
            windowValue_[(windowHead_ + windowCount_) & mask] = need;
            windowIndex_[(windowHead_ + windowCount_) & mask] = position_;
            ++windowCount_;
        }
        const double windowMin = windowCount_ ? windowValue_[windowHead_] : 1.;
        const double coefficient = windowMin < envelope_ ? attack : release;
        envelope_ = windowMin + coefficient * (envelope_ - windowMin);
        const double delayedL = delay_[0][write_], delayedR = delay_[1][write_];
        // The required gain travels with the audio, so clamping the lagging smoother to the
        // delayed sample's own requirement needs no second division.
        const double delayedNeed = needDelay_[write_];
        delay_[0][write_] = l;
        delay_[1][write_] = r;
        needDelay_[write_] = need;
        if (++write_ == lookahead_)
            write_ = 0;
        const double gain = std::min(envelope_, delayedNeed);
        lastGain = gain;
        stereo[i * 2] = static_cast<float>(std::clamp(delayedL * gain, -maxFloat, maxFloat));
        stereo[i * 2 + 1] = static_cast<float>(std::clamp(delayedR * gain, -maxFloat, maxFloat));
        ++position_;
    }
    // Only the block's final gain feeds the meter, so its dB conversion runs once per block.
    reductionDb_.store(static_cast<float>(lastGain > 0 ? -20. * std::log10(lastGain) : 0),
                       std::memory_order_relaxed);
    // A NaN or infinity from a pathological input can enter the delay and deque state, so check
    // once per block and reset rather than branch per sample.
    bool finite = true;
    for (std::size_t i = 0; i < frames * 2; ++i)
        finite &= std::isfinite(stereo[i]);
    if (!finite) {
        for (auto& channel : delay_)
            std::fill(channel.begin(), channel.end(), 0);
        std::fill(needDelay_.begin(), needDelay_.end(), 1);
        write_ = 0;
        windowHead_ = 0;
        windowCount_ = 0;
        position_ = 0;
        envelope_ = 1;
        std::fill_n(stereo, frames * 2, 0.f);
    }
}
} // namespace wavy::effects
