#include "effects/Reverb.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace wavy::effects {
namespace {
constexpr std::size_t kCombLengths[]{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::size_t kAllpassLengths[]{556, 441, 341, 225};
constexpr std::size_t kStereoSpread = 23;
constexpr double kInputGain = .013;
// A tap can move at most half a sample per sample while the pre-delay glides.
constexpr double kMaxGlide = .5;
double onePole(double cutoff, double rate) {
    return 1 - std::exp(-2 * std::numbers::pi * std::clamp(cutoff, 1.0, rate * .45) / rate);
}
} // namespace
Reverb::Reverb(std::shared_ptr<ParameterSet> params)
    : params_(sharedParameters(std::move(params),
                               {{"decay", "Decay", "s", .2f, 10, 1.5f, .4f},
                                {"damping", "Damping", "", 0, 1, .3f},
                                {"predelay", "Pre-delay", "ms", 0, 200, 15},
                                {"width", "Width", "", 0, 1, 1},
                                {"lowcut", "Low cut", "Hz", 20, 2000, 120, .4f},
                                {"highcut", "High cut", "Hz", 1000, 20000, 12000, .4f},
                                {"wet", "Wet", "", 0, 1, .2f},
                                {"dry", "Dry", "", 0, 1, 1}})) {}
double Reverb::combProcess(Comb& comb, double input) noexcept {
    const double output = comb.buffer[comb.index];
    comb.store = flush(output * comb.damp2 + comb.store * comb.damp1);
    comb.buffer[comb.index] = static_cast<float>(flush(input + comb.store * comb.feedback));
    if (++comb.index == comb.buffer.size())
        comb.index = 0;
    return output;
}
double Reverb::allpassProcess(Allpass& allpass, double input) noexcept {
    const double buffered = allpass.buffer[allpass.index];
    allpass.buffer[allpass.index] = static_cast<float>(flush(input + buffered * allpass.feedback));
    if (++allpass.index == allpass.buffer.size())
        allpass.index = 0;
    return buffered - input;
}
double Reverb::lineRead(const Line& line, double delay) noexcept {
    double position = double(line.write) - delay;
    while (position < 0)
        position += double(line.buffer.size());
    const auto index = static_cast<std::size_t>(position);
    const double fraction = position - double(index);
    const double a = line.buffer[index];
    const double b = line.buffer[index + 1 >= line.buffer.size() ? 0 : index + 1];
    return a + fraction * (b - a);
}
void Reverb::linePush(Line& line, double value) noexcept {
    line.buffer[line.write] = static_cast<float>(flush(value));
    if (++line.write == line.buffer.size())
        line.write = 0;
}
void Reverb::prepare(double rate, std::size_t) {
    if (!std::isfinite(rate) || rate < 100)
        throw std::invalid_argument("Invalid effect sample rate");
    rate_ = rate;
    const double scale = rate / 44100.;
    const auto spread = std::max<std::size_t>(1, std::lround(kStereoSpread * scale));
    for (std::size_t i = 0; i < combL_.size(); ++i) {
        const auto length = std::max<std::size_t>(1, std::lround(kCombLengths[i] * scale));
        combL_[i].buffer.assign(length, 0);
        combR_[i].buffer.assign(length + spread, 0);
    }
    for (std::size_t i = 0; i < allpassL_.size(); ++i) {
        const auto length = std::max<std::size_t>(1, std::lround(kAllpassLengths[i] * scale));
        allpassL_[i].buffer.assign(length, 0);
        allpassR_[i].buffer.assign(length + spread, 0);
    }
    predelay_.buffer.assign(std::max<std::size_t>(2, std::lround(rate * .2) + 2), 0);
    reset();
}
void Reverb::reset() noexcept {
    for (auto* bank : {&combL_, &combR_})
        for (auto& comb : *bank) {
            std::fill(comb.buffer.begin(), comb.buffer.end(), 0.f);
            comb.index = 0;
            comb.store = 0;
        }
    for (auto* bank : {&allpassL_, &allpassR_})
        for (auto& allpass : *bank) {
            std::fill(allpass.buffer.begin(), allpass.buffer.end(), 0.f);
            allpass.index = 0;
        }
    std::fill(predelay_.buffer.begin(), predelay_.buffer.end(), 0.f);
    predelay_.write = 0;
    lowL_ = lowR_ = highL_ = highR_ = 0;
    targetDelay_ = currentDelay_ =
        std::clamp<double>(params_->get(2) * .001 * rate_, 0, double(predelay_.buffer.size() - 2));
    width_.reset(params_->get(3));
    wet_.reset(params_->get(6));
    dry_.reset(params_->get(7));
}
void Reverb::process(float* stereo, std::size_t frames) noexcept {
    const double decay = std::max<double>(params_->get(0), .05);
    const double damping = std::clamp<double>(params_->get(1), 0, 1) * .4;
    targetDelay_ =
        std::clamp<double>(params_->get(2) * .001 * rate_, 0, double(predelay_.buffer.size() - 2));
    for (std::size_t i = 0; i < combL_.size(); ++i) {
        const double feedback =
            std::pow(10., -3. * double(combL_[i].buffer.size()) / (rate_ * decay));
        combL_[i].feedback = combR_[i].feedback = std::min(feedback, .9999);
        combL_[i].damp1 = combR_[i].damp1 = damping;
        combL_[i].damp2 = combR_[i].damp2 = 1 - damping;
    }
    const double highCoefficient = params_->get(5) >= 20000 ? 1 : onePole(params_->get(5), rate_);
    const double lowCoefficient = params_->get(4) <= 20 ? 0 : onePole(params_->get(4), rate_);
    width_.target(params_->get(3));
    wet_.target(params_->get(6));
    dry_.target(params_->get(7));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double l = finiteOrZero(stereo[frame * 2]), r = finiteOrZero(stereo[frame * 2 + 1]);
        if (currentDelay_ != targetDelay_)
            currentDelay_ += std::clamp(targetDelay_ - currentDelay_, -kMaxGlide, kMaxGlide);
        linePush(predelay_, (l + r) * .5);
        const double input = lineRead(predelay_, currentDelay_ + 1) * kInputGain;
        double outL = 0, outR = 0;
        for (auto& comb : combL_)
            outL += combProcess(comb, input);
        for (auto& comb : combR_)
            outR += combProcess(comb, input);
        for (auto& allpass : allpassL_)
            outL = allpassProcess(allpass, outL);
        for (auto& allpass : allpassR_)
            outR = allpassProcess(allpass, outR);
        lowL_ = flush(lowL_ + highCoefficient * (outL - lowL_));
        outL = lowL_;
        lowR_ = flush(lowR_ + highCoefficient * (outR - lowR_));
        outR = lowR_;
        highL_ = flush(highL_ + lowCoefficient * (outL - highL_));
        outL -= highL_;
        highR_ = flush(highR_ + lowCoefficient * (outR - highR_));
        outR -= highR_;
        const double width = width_.next();
        const double wetL = outL * (width * .5 + .5) + outR * (.5 - width * .5);
        const double wetR = outR * (width * .5 + .5) + outL * (.5 - width * .5);
        const double wet = wet_.next(), dry = dry_.next();
        const double limit = double(std::numeric_limits<float>::max());
        stereo[frame * 2] = static_cast<float>(std::clamp(dry * l + wet * wetL, -limit, limit));
        stereo[frame * 2 + 1] = static_cast<float>(std::clamp(dry * r + wet * wetR, -limit, limit));
    }
    if (!allFinite(stereo, frames * 2)) {
        reset();
        std::fill_n(stereo, frames * 2, 0.f);
    }
}
} // namespace wavy::effects
