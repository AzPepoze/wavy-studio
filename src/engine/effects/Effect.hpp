#pragma once
#include "effects/Parameter.hpp"
#include <functional>
#include <map>

namespace wavy::effects {
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
