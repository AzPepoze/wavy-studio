#pragma once
#include "audio/Snapshot.hpp"
#include <QAbstractListModel>
#include <QTimer>
#include <QVariantList>

class TimelineModel;
class EffectsController;

class EffectsListModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role {
        Id = Qt::UserRole + 1,
        Name,
        Unit,
        Minimum,
        Maximum,
        Value,
        DefaultValue,
        Skew,
        Normalized,
        TypeId,
        Bypassed,
        SlotIndex
    };
    EffectsListModel(EffectsController& owner, int track, int slot);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void refresh();
    void parameterChanged(int row);

  private:
    EffectsController& owner_;
    int track_, slot_;
};

class EffectsController final : public QObject {
    Q_OBJECT
  public:
    explicit EffectsController(TimelineModel& timeline, QObject* parent = nullptr);
    const wavy::EffectChains& chains() const { return chains_; }
    // Track id zero addresses the master chain.
    Q_INVOKABLE QAbstractItemModel* trackChainModel(int trackId);
    Q_INVOKABLE QAbstractItemModel* parametersModel(int trackId, int slotIndex);
    Q_INVOKABLE QVariantList availableEffects() const;
    Q_INVOKABLE QString trackName(int trackId) const;
    Q_INVOKABLE bool hasTrack(int trackId) const { return validTrack(trackId); }
    Q_INVOKABLE void addEffect(int trackId, const QString& typeId);
    Q_INVOKABLE void removeEffect(int trackId, int slotIndex);
    Q_INVOKABLE void moveEffect(int trackId, int from, int to);
    Q_INVOKABLE void setBypassed(int trackId, int slotIndex, bool bypassed);
    Q_INVOKABLE void setParameter(int trackId, int slotIndex, const QString& id, double value);
    Q_INVOKABLE void setNormalized(int trackId, int slotIndex, const QString& id, double value);
    Q_INVOKABLE void resetParameter(int trackId, int slotIndex, const QString& id);
    Q_INVOKABLE double gainReduction(int trackId, int slotIndex) const;
    void observeSnapshot(const wavy::Snapshot& snapshot);
    static double normalized(const wavy::effects::Parameter& p, double value);
    static double fromNormalized(const wavy::effects::Parameter& p, double value);
  signals:
    void structureChanged();

  private:
    friend class EffectsListModel;
    bool validTrack(int track) const;
    std::vector<wavy::effects::EffectSlot>* chain(int track);
    const std::vector<wavy::effects::EffectSlot>* chain(int track) const;
    wavy::effects::EffectSlot* slot(int track, int index);
    void changed(int track);
    TimelineModel& timeline_;
    wavy::effects::EffectFactory factory_;
    wavy::EffectChains chains_;
    QTimer coalesce_;
    std::map<std::pair<int, int>, EffectsListModel*> models_;
    std::map<int, std::vector<std::shared_ptr<wavy::effects::Effect>>> meters_;
};
