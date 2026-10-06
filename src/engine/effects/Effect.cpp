#include "effects/Effect.hpp"
#include "effects/Compressor.hpp"
#include "effects/GainPan.hpp"
#include "effects/ParametricEq.hpp"
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
