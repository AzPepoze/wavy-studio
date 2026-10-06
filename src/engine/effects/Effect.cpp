#include "effects/Effect.hpp"
#include "effects/Compressor.hpp"
#include "effects/DeEsser.hpp"
#include "effects/Delay.hpp"
#include "effects/GainPan.hpp"
#include "effects/Limiter.hpp"
#include "effects/ParametricEq.hpp"
#include "effects/Reverb.hpp"
#include "effects/StereoWidth.hpp"
#include <stdexcept>

namespace wavy::effects {
std::shared_ptr<ParameterSet> sharedParameters(std::shared_ptr<ParameterSet> params,
                                               std::vector<Parameter> definitions) {
    if (!params)
        return std::make_shared<ParameterSet>(std::move(definitions));
    if (params->size() != definitions.size())
        throw std::invalid_argument("Effect parameter schema mismatch");
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const auto& actual = params->definitions()[i];
        const auto& expected = definitions[i];
        if (actual.id != expected.id || actual.minimum != expected.minimum ||
            actual.maximum != expected.maximum)
            throw std::invalid_argument("Effect parameter schema mismatch");
    }
    return params;
}
std::shared_ptr<Effect> EffectSlot::prepared(double sampleRate, std::size_t maxBlockFrames,
                                             const void* chain) const {
    if (!params)
        throw std::invalid_argument("Effect slot requires shared parameters");
    if (!instance_ || sampleRate_ != sampleRate || maxBlockFrames_ != maxBlockFrames ||
        (chain && chain_ && chain != chain_)) {
        auto next = EffectFactory{}.create(typeId, params);
        if (!next)
            throw std::invalid_argument("Unknown effect type");
        next->prepare(sampleRate, maxBlockFrames);
        instance_ = std::move(next);
        sampleRate_ = sampleRate;
        maxBlockFrames_ = maxBlockFrames;
    }
    if (chain)
        chain_ = chain;
    return instance_;
}
ParameterValues serializeParameters(const EffectSlot& slot) {
    ParameterValues result;
    if (slot.params)
        for (std::size_t i = 0; i < slot.params->size(); ++i)
            result.emplace(slot.params->definitions()[i].id, slot.params->get(i));
    return result;
}
void restoreParameters(EffectSlot& slot, const ParameterValues& values) {
    if (!slot.params)
        slot.params = EffectFactory{}.slot(slot.typeId).params;
    if (slot.params)
        for (const auto& [id, value] : values)
            slot.params->set(id, value);
}
EffectFactory::EffectFactory() {
    registerType("gain_pan", "Gain / Pan", [](auto p) { return std::make_unique<GainPan>(p); });
    registerType("parametric_eq", "4-band parametric EQ",
                 [](auto p) { return std::make_unique<ParametricEq>(p); });
    registerType("compressor", "Compressor",
                 [](auto p) { return std::make_unique<Compressor>(p); });
    registerType("reverb", "Reverb", [](auto p) { return std::make_unique<Reverb>(p); });
    registerType("delay", "Delay", [](auto p) { return std::make_unique<Delay>(p); });
    registerType("stereo_width", "Stereo Width",
                 [](auto p) { return std::make_unique<StereoWidth>(p); });
    registerType("limiter", "Limiter", [](auto p) { return std::make_unique<Limiter>(p); });
    registerType("de_esser", "De-esser", [](auto p) { return std::make_unique<DeEsser>(p); });
}
void EffectFactory::registerType(std::string id, std::string displayName, Create create) {
    for (auto& entry : entries_)
        if (entry.id == id) {
            entry = {std::move(id), std::move(displayName), std::move(create)};
            return;
        }
    entries_.push_back({std::move(id), std::move(displayName), std::move(create)});
}
std::unique_ptr<Effect> EffectFactory::create(std::string_view id,
                                              std::shared_ptr<ParameterSet> params) const {
    for (const auto& entry : entries_)
        if (entry.id == id)
            return entry.create(std::move(params));
    return {};
}
EffectSlot EffectFactory::slot(std::string_view id) const {
    auto effect = create(id);
    if (!effect)
        throw std::invalid_argument("Unknown effect type");
    std::vector<Parameter> definitions(effect->parameters().definitions().begin(),
                                       effect->parameters().definitions().end());
    return {std::string(id), false, std::make_shared<ParameterSet>(std::move(definitions))};
}
} // namespace wavy::effects
