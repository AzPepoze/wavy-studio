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
    EffectSlot() = default;
    EffectSlot(std::string type, bool bypass, std::shared_ptr<ParameterSet> parameters)
        : typeId(std::move(type)), bypassed(bypass), params(std::move(parameters)) {}
    EffectSlot(const EffectSlot& other) : EffectSlot(other.typeId, other.bypassed, other.params) {}
    EffectSlot& operator=(const EffectSlot& other) {
        if (this != &other) {
            EffectSlot copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    EffectSlot(EffectSlot&&) noexcept = default;
    EffectSlot& operator=(EffectSlot&&) noexcept = default;
    void invalidate() const { instance_.reset(); }
    std::shared_ptr<Effect> prepared(double sampleRate, std::size_t maxBlockFrames,
                                     const void* chain = nullptr) const;

  private:
    // Control-thread cache. A format change replaces the instance rather than preparing a live one.
    mutable std::shared_ptr<Effect> instance_;
    mutable const void* chain_ = nullptr;
    mutable double sampleRate_ = 0;
    mutable std::size_t maxBlockFrames_ = 0;
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
