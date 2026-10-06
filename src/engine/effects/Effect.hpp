#pragma once
#include "effects/Parameter.hpp"
#include <functional>
#include <map>

namespace wavy::effects {
// True when every sample is finite. A NaN or infinity can stay inside a feedback loop forever, so
// an effect that finds one in its output resets its state and outputs silence for that block.
inline bool allFinite(const float* samples, std::size_t count) noexcept {
    bool finite = true;
    for (std::size_t i = 0; i < count; ++i)
        finite &= std::isfinite(samples[i]);
    return finite;
}
// Input samples that are NaN or infinite are treated as silence so they never enter a filter state.
inline double finiteOrZero(float sample) noexcept {
    return std::isfinite(sample) ? double(sample) : 0.0;
}
class Effect {
  public:
    virtual ~Effect() = default;
    virtual void prepare(double sampleRate, std::size_t maxBlockFrames) = 0;
    virtual void process(float* interleavedStereo, std::size_t frames) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual ParameterSet& parameters() noexcept = 0;
    virtual std::string_view typeId() const noexcept = 0;
    virtual std::size_t latencyFrames() const noexcept { return 0; }
};
struct EffectSlot {
    std::string typeId;
    bool bypassed = false;
    std::shared_ptr<ParameterSet> params;
};
using ParameterValues = std::map<std::string, float>;
ParameterValues serializeParameters(const EffectSlot& slot);
void restoreParameters(EffectSlot& slot, const ParameterValues& values);
class EffectFactory {
  public:
    using Create = std::function<std::unique_ptr<Effect>(std::shared_ptr<ParameterSet>)>;
    struct Entry {
        std::string id, displayName;
        Create create;
    };
    EffectFactory();
    void registerType(std::string id, std::string displayName, Create create);
    std::unique_ptr<Effect> create(std::string_view id,
                                   std::shared_ptr<ParameterSet> params = {}) const;
    EffectSlot slot(std::string_view id) const;
    std::span<const Entry> entries() const noexcept { return entries_; }

  private:
    std::vector<Entry> entries_;
};
// Definitions and ownership stay immutable during rendering; only atomic values are edited live.
std::shared_ptr<ParameterSet> sharedParameters(std::shared_ptr<ParameterSet> params,
                                               std::vector<Parameter> definitions);
} // namespace wavy::effects
