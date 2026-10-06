#include "EffectsController.hpp"
#include "TimelineModel.hpp"
#include "effects/Compressor.hpp"
#include <cmath>

namespace {
double sliderMinimum(const wavy::effects::Parameter& p) {
    return std::isfinite(p.minimum) ? p.minimum : -80.;
}
} // namespace

EffectsListModel::EffectsListModel(EffectsController& owner, int track, int slot)
    : QAbstractListModel(&owner), owner_(owner), track_(track), slot_(slot) {}
int EffectsListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    const auto* chain = owner_.chain(track_);
    if (!chain)
        return 0;
    if (slot_ < 0)
        return static_cast<int>(chain->size());
    if (slot_ >= static_cast<int>(chain->size()))
        return 0;
    return static_cast<int>((*chain)[slot_].params->size());
}
QVariant EffectsListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto& chain = *owner_.chain(track_);
    if (slot_ < 0) {
        const auto& slot = chain[index.row()];
        if (role == TypeId)
            return QString::fromStdString(slot.typeId);
        if (role == Bypassed)
            return slot.bypassed;
        if (role == SlotIndex)
            return index.row();
        if (role == Name)
            for (const auto& entry : owner_.factory_.entries())
                if (entry.id == slot.typeId)
                    return QString::fromStdString(entry.displayName);
        return {};
    }
    const auto& params = *chain[slot_].params;
    const auto& p = params.definitions()[index.row()];
    switch (role) {
    case Id:
        return QString::fromStdString(p.id);
    case Name:
        return QString::fromStdString(p.name);
    case Unit:
        return QString::fromStdString(p.unit);
    case Minimum:
        return p.minimum;
    case Maximum:
        return p.maximum;
    case Value:
        return params.get(index.row());
    case DefaultValue:
        return p.defaultValue;
    case Skew:
        return p.skew;
    case Normalized:
        return EffectsController::normalized(p, params.get(index.row()));
    default:
        return {};
    }
}
QHash<int, QByteArray> EffectsListModel::roleNames() const {
    return {{Id, "id"},
            {Name, "name"},
            {Unit, "unit"},
            {Minimum, "minimum"},
            {Maximum, "maximum"},
            {Value, "value"},
            {DefaultValue, "defaultValue"},
            {Skew, "skew"},
            {Normalized, "normalized"},
            {TypeId, "typeId"},
            {Bypassed, "bypassed"},
            {SlotIndex, "index"}};
}
void EffectsListModel::refresh() {
    beginResetModel();
    endResetModel();
}
void EffectsListModel::parameterChanged(int row) {
    emit dataChanged(index(row), index(row), {Value, Normalized});
}
EffectsController::EffectsController(TimelineModel& timeline, QObject* parent)
    : QObject(parent), timeline_(timeline) {
    coalesce_.setSingleShot(true);
    coalesce_.setInterval(0);
    connect(&coalesce_, &QTimer::timeout, this, &EffectsController::structureChanged);
    connect(&timeline_, &TimelineModel::timelineChanged, this, [this] {
        std::vector<int> removed;
        for (const auto& [id, chainEntries] : chains_.tracks)
            if (!validTrack(static_cast<int>(id.value)))
                removed.push_back(static_cast<int>(id.value));
        for (int id : removed) {
            chains_.tracks.erase(wavy::timeline::TrackId{static_cast<unsigned>(id)});
            changed(id);
        }
    });
}
bool EffectsController::validTrack(int track) const {
    return track == 0 ||
           (track > 0 && timeline_.timeline().findTrack({static_cast<unsigned>(track)}));
}
std::vector<wavy::effects::EffectSlot>* EffectsController::chain(int track) {
    if (track == 0)
        return &chains_.master;
    auto it = chains_.tracks.find({static_cast<unsigned>(track)});
    return it == chains_.tracks.end() ? nullptr : &it->second;
}
const std::vector<wavy::effects::EffectSlot>* EffectsController::chain(int track) const {
    return const_cast<EffectsController*>(this)->chain(track);
}
wavy::effects::EffectSlot* EffectsController::slot(int track, int index) {
    auto* c = chain(track);
    return c && index >= 0 && index < static_cast<int>(c->size()) ? &(*c)[index] : nullptr;
}
QAbstractItemModel* EffectsController::trackChainModel(int track) {
    return parametersModel(track, -1);
}
QAbstractItemModel* EffectsController::parametersModel(int track, int slot) {
    auto& model = models_[{track, slot}];
    if (!model)
        model = new EffectsListModel(*this, track, slot);
    return model;
}
QVariantList EffectsController::availableEffects() const {
    QVariantList result;
    for (const auto& e : factory_.entries())
        result.push_back(QVariantMap{{"id", QString::fromStdString(e.id)},
                                     {"displayName", QString::fromStdString(e.displayName)}});
    return result;
}
QString EffectsController::trackName(int track) const {
    if (!track)
        return QStringLiteral("Master");
    const auto* t = timeline_.timeline().findTrack({static_cast<unsigned>(track)});
    return t ? QString::fromStdString(t->name) : QString{};
}
void EffectsController::changed(int track) {
    meters_.erase(track);
    for (const auto& [key, model] : models_)
        if (key.first == track)
            model->refresh();
    if (!coalesce_.isActive())
        coalesce_.start();
}
void EffectsController::addEffect(int track, const QString& type) {
    if (!validTrack(track) || !factory_.create(type.toStdString()))
        return;
    if (track && !chain(track))
        chains_.tracks.emplace(wavy::timeline::TrackId{static_cast<unsigned>(track)},
                               std::vector<wavy::effects::EffectSlot>{});
    chain(track)->push_back(factory_.slot(type.toStdString()));
    changed(track);
}
void EffectsController::removeEffect(int track, int index) {
    if (!slot(track, index))
        return;
    auto& c = *chain(track);
    c.erase(c.begin() + index);
    changed(track);
}
void EffectsController::moveEffect(int track, int from, int to) {
    if (from == to || !slot(track, from) || !slot(track, to))
        return;
    auto& c = *chain(track);
    auto moved = std::move(c[from]);
    c.erase(c.begin() + from);
    c.insert(c.begin() + to, std::move(moved));
    changed(track);
}
void EffectsController::setBypassed(int track, int index, bool bypassed) {
    auto* s = slot(track, index);
    if (!s || s->bypassed == bypassed)
        return;
    s->bypassed = bypassed;
    changed(track);
}
void EffectsController::setParameter(int track, int index, const QString& id, double value) {
    auto* s = slot(track, index);
    if (!s)
        return;
    const auto row = s->params->index(id.toStdString());
    if (!s->params->set(row, static_cast<float>(value)))
        return;
    if (auto it = models_.find({track, index}); it != models_.end())
        it->second->parameterChanged(static_cast<int>(row));
}
void EffectsController::resetParameter(int track, int index, const QString& id) {
    auto* s = slot(track, index);
    if (!s)
        return;
    const auto row = s->params->index(id.toStdString());
    if (row < s->params->size())
        setParameter(track, index, id, s->params->definitions()[row].defaultValue);
}
double EffectsController::normalized(const wavy::effects::Parameter& p, double value) {
    if (p.minimum == p.maximum)
        return 0;
    return std::pow(std::clamp((value - sliderMinimum(p)) / (p.maximum - sliderMinimum(p)), 0., 1.),
                    p.skew);
}
double EffectsController::fromNormalized(const wavy::effects::Parameter& p, double value) {
    value = std::clamp(value, 0., 1.);
    if (!value && !std::isfinite(p.minimum))
        return p.minimum;
    return sliderMinimum(p) + (p.maximum - sliderMinimum(p)) * std::pow(value, 1. / p.skew);
}
void EffectsController::setNormalized(int track, int index, const QString& id, double value) {
    auto* s = slot(track, index);
    if (!s || !std::isfinite(value))
        return;
    const auto row = s->params->index(id.toStdString());
    if (row < s->params->size())
        setParameter(track, index, id, fromNormalized(s->params->definitions()[row], value));
}
void EffectsController::observeSnapshot(const wavy::Snapshot& snapshot) {
    meters_.clear();
    auto keep = [this](int id, const auto& prepared) {
        for (const auto& p : prepared)
            meters_[id].push_back(p.effect);
    };
    keep(0, snapshot.masterEffects);
    const auto& tracks = timeline_.timeline().tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i)
        keep(static_cast<int>(tracks[i].id.value), snapshot.tracks[i].effects);
}
double EffectsController::gainReduction(int track, int index) const {
    auto it = meters_.find(track);
    if (it == meters_.end() || index < 0 || index >= static_cast<int>(it->second.size()))
        return 0;
    auto* compressor = dynamic_cast<wavy::effects::Compressor*>(it->second[index].get());
    return compressor ? compressor->gainReductionDb().load(std::memory_order_relaxed) : 0;
}
