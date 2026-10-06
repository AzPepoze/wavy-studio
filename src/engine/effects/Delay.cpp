#include "effects/Delay.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace wavy::effects {
namespace {
constexpr double kMaxSeconds = 2.0;
constexpr double kMaxModSeconds = .008;
// A tap can move at most half a sample per sample while gliding, which bounds the pitch shift.
constexpr double kMaxGlide = .5;
constexpr std::size_t kDivisionCount = 7;
constexpr double kDivisionBeats[kDivisionCount]{.125, .25, .5, 1, 2, 4, 8};
double onePole(double cutoff, double rate) {
    return 1 - std::exp(-2 * std::numbers::pi * std::clamp(cutoff, 1.0, rate * .45) / rate);
}
} // namespace
Delay::Delay(std::shared_ptr<ParameterSet> params)
    : params_(sharedParameters(std::move(params),
                               {{"time", "Time", "ms", 1, 2000, 375, .4f},
                                {"sync", "Sync", "", 0, 1, 0},
                                {"note", "Note", "", 0, 6, 2},
                                {"bpm", "BPM", "", 20, 300, 120},
                                {"feedback", "Feedback", "", 0, .95f, .35f},
                                {"pingpong", "Ping-pong", "", 0, 1, 0},
                                {"lowcut", "Low cut", "Hz", 20, 2000, 100, .4f},
                                {"highcut", "High cut", "Hz", 1000, 20000, 8000, .4f},
                                {"mix", "Mix", "", 0, 1, .35f},
                                {"mod_depth", "Modulation", "", 0, 1, 0},
                                {"mod_rate", "Mod rate", "Hz", .05f, 5, .3f, .4f}})) {}
double Delay::lineRead(const Line& line, double delay) noexcept {
    double position = double(line.write) - delay;
    while (position < 0)
        position += double(line.buffer.size());
    const auto index = static_cast<std::size_t>(position);
    const double fraction = position - double(index);
    const double a = line.buffer[index];
    const double b = line.buffer[index + 1 >= line.buffer.size() ? 0 : index + 1];
    return a + fraction * (b - a);
}
void Delay::linePush(Line& line, double value) noexcept {
    line.buffer[line.write] = static_cast<float>(flush(value));
    if (++line.write == line.buffer.size())
        line.write = 0;
}
double Delay::feedbackFilter(double input, double& low, double& high, double lowCoefficient,
                             double highCoefficient) noexcept {
    if (highCoefficient >= 1)
        low = input;
    else
        low = flush(low + highCoefficient * (input - low));
    input = low;
    if (lowCoefficient <= 0)
        high = 0;
    else
        high = flush(high + lowCoefficient * (input - high));
    return flush(input - high);
}
double Delay::desiredDelaySeconds() const noexcept {
    const bool sync = params_->get(1) >= .5f;
    const auto note = static_cast<std::size_t>(
        std::clamp<double>(std::lround(params_->get(2)), 0, kDivisionCount - 1));
    const double bpm = std::clamp<double>(params_->get(3), 20, 300);
    const double seconds = sync ? kDivisionBeats[note] * 60. / bpm : params_->get(0) * .001;
    return std::clamp(seconds, .001, kMaxSeconds);
}
void Delay::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    const auto length = std::max<std::size_t>(
        8, static_cast<std::size_t>((kMaxSeconds + kMaxModSeconds) * rate) + 4);
    left_.buffer.assign(length, 0);
    right_.buffer.assign(length, 0);
    reset();
}
void Delay::reset() noexcept {
    std::fill(left_.buffer.begin(), left_.buffer.end(), 0.f);
    std::fill(right_.buffer.begin(), right_.buffer.end(), 0.f);
    left_.write = right_.write = 0;
    phase_ = 0;
    lowL_ = lowR_ = highL_ = highR_ = 0;
    targetDelay_ = currentDelay_ = desiredDelaySeconds() * rate_;
    mix_.reset(params_->get(8));
}
void Delay::process(float* stereo, std::size_t frames) noexcept {
    targetDelay_ = desiredDelaySeconds() * rate_;
    const double feedback = params_->get(4);
    const bool pingpong = params_->get(5) >= .5f;
    const auto bypassHigh = params_->get(7) >= 20000;
    const auto bypassLow = params_->get(6) <= 20;
    const double highCoefficient = bypassHigh ? 1 : onePole(params_->get(7), rate_);
    const double lowCoefficient = bypassLow ? 0 : onePole(params_->get(6), rate_);
    const double modulation = params_->get(9) * kMaxModSeconds * rate_;
    const double increment = 2 * std::numbers::pi * params_->get(10) / rate_;
    mix_.target(params_->get(8));
    const double maximum = double(left_.buffer.size() - 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double l = stereo[frame * 2], r = stereo[frame * 2 + 1];
        if (currentDelay_ != targetDelay_)
            currentDelay_ += std::clamp(targetDelay_ - currentDelay_, -kMaxGlide, kMaxGlide);
        const double wobble = modulation * std::sin(phase_);
        phase_ += increment;
        if (phase_ >= 2 * std::numbers::pi)
            phase_ -= 2 * std::numbers::pi;
        const double delay = std::clamp(currentDelay_ + wobble, 1., maximum);
        const double dl = lineRead(left_, delay);
        const double dr = lineRead(right_, delay);
        if (pingpong) {
            const double mono = (l + r) * .5;
            linePush(left_, mono + feedback * feedbackFilter(dr, lowL_, highL_, lowCoefficient,
                                                             highCoefficient));
            linePush(right_,
                     feedback * feedbackFilter(dl, lowR_, highR_, lowCoefficient, highCoefficient));
        } else {
            linePush(left_, l + feedback * feedbackFilter(dl, lowL_, highL_, lowCoefficient,
                                                          highCoefficient));
            linePush(right_, r + feedback * feedbackFilter(dr, lowR_, highR_, lowCoefficient,
                                                           highCoefficient));
        }
        const double mix = mix_.next();
        const double limit = double(std::numeric_limits<float>::max());
        stereo[frame * 2] = static_cast<float>(std::clamp((1 - mix) * l + mix * dl, -limit, limit));
        stereo[frame * 2 + 1] =
            static_cast<float>(std::clamp((1 - mix) * r + mix * dr, -limit, limit));
    }
}
} // namespace wavy::effects
