#pragma once
#include "effects/Effect.hpp"
#include <vector>
namespace wavy::effects {
// Stereo delay line with linear-interpolated taps, so the read pointer can glide between times
// without clicks. A rate-limited glide bounds the pitch shift while the time control moves; the
// same lines carry a light LFO for chorus-like thickening. Feedback can run through one-pole
// low/high shelves, and ping-pong crosses the two taps while summing the input to mono.
class Delay final : public Effect {
  public:
    explicit Delay(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "delay"; }
    std::size_t latencyFrames() const noexcept override { return 0; }

  private:
    struct Line {
        std::vector<float> buffer;
        std::size_t write = 0;
    };
    static double lineRead(const Line& line, double delay) noexcept;
    static void linePush(Line& line, double value) noexcept;
    static double feedbackFilter(double input, double& low, double& high, double lowCoefficient,
                                 double highCoefficient) noexcept;
    static double flush(double value) noexcept { return std::abs(value) < 1e-30 ? 0 : value; }
    double desiredDelaySeconds() const noexcept;
    std::shared_ptr<ParameterSet> params_;
    Line left_, right_;
    double rate_ = 48000, currentDelay_ = 0, targetDelay_ = 0, phase_ = 0;
    double lowL_ = 0, lowR_ = 0, highL_ = 0, highR_ = 0;
    Smoother mix_;
};
} // namespace wavy::effects
