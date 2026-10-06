#pragma once
#include "effects/Effect.hpp"
#include <array>
#include <vector>
namespace wavy::effects {
// Freeverb-style bank: eight damped feedback combs feed four allpass diffusers per channel.
// Comb lengths are a 44.1 kHz design scaled by the sample rate, and each comb's feedback is
// derived from the requested RT60 so every resonator decays at the same rate. The +23-sample
// right-channel spread decorrelates a mono source; a one-pole pre-delay, wet low/high cuts and
// a per-sample width blend sit around the network. Denormals are flushed inside the loops.
class Reverb final : public Effect {
  public:
    explicit Reverb(std::shared_ptr<ParameterSet> params = {});
    void prepare(double sampleRate, std::size_t maxBlockFrames) override;
    void process(float* stereo, std::size_t frames) noexcept override;
    void reset() noexcept override;
    ParameterSet& parameters() noexcept override { return *params_; }
    std::string_view typeId() const noexcept override { return "reverb"; }

  private:
    struct Comb {
        std::vector<float> buffer;
        std::size_t index = 0;
        double store = 0, feedback = 0, damp1 = 0, damp2 = 1;
    };
    struct Allpass {
        std::vector<float> buffer;
        std::size_t index = 0;
        double feedback = .5;
    };
    struct Line {
        std::vector<float> buffer;
        std::size_t write = 0;
    };
    static double combProcess(Comb& comb, double input) noexcept;
    static double allpassProcess(Allpass& allpass, double input) noexcept;
    static double lineRead(const Line& line, double delay) noexcept;
    static void linePush(Line& line, double value) noexcept;
    static double flush(double value) noexcept { return std::abs(value) < 1e-30 ? 0 : value; }
    std::shared_ptr<ParameterSet> params_;
    std::array<Comb, 8> combL_, combR_;
    std::array<Allpass, 4> allpassL_, allpassR_;
    Line predelay_;
    double rate_ = 48000, currentDelay_ = 0, targetDelay_ = 0;
    double lowL_ = 0, lowR_ = 0, highL_ = 0, highR_ = 0;
    Smoother width_, wet_, dry_;
};
} // namespace wavy::effects
