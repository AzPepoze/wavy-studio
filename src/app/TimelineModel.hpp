#pragma once
#include "timeline/History.hpp"
#include <QAbstractListModel>
#include <QVariantList>

namespace wavy::record {
struct RecordedTake;
}

class TimelineModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(QAbstractItemModel* tracks READ tracks CONSTANT)
    Q_PROPERTY(qint64 durationFrames READ durationFrames NOTIFY durationFramesChanged)
    Q_PROPERTY(
        qint64 playheadFrame READ playheadFrame WRITE setPlayheadFrame NOTIFY playheadFrameChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  public:
    enum Role { TrackIdRole = Qt::UserRole + 1, NameRole, MutedRole, SoloRole, GainRole };
    explicit TimelineModel(QObject* parent = nullptr);
    const wavy::timeline::Timeline& timeline() const { return timeline_; }
    bool commitTake(int trackId, const wavy::record::RecordedTake& take);
    Q_INVOKABLE void addClip(int trackId, const QString& source, qint64 start, qint64 length);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int sampleRate() const { return static_cast<int>(timeline_.sampleRate); }
    QAbstractItemModel* tracks() { return this; }
    qint64 durationFrames() const;
    qint64 playheadFrame() const { return playheadFrame_; }
    void setPlayheadFrame(qint64 frame);
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    Q_INVOKABLE QVariantList visibleClips(int trackId, qint64 firstFrame, qint64 lastFrame) const;
    Q_INVOKABLE void editClip(int clipId, int targetTrackId, qint64 startFrame,
                              qint64 durationFrames);
    Q_INVOKABLE void action(int clipId, int trackId, const QString& operation);
    Q_INVOKABLE void setTrackState(int row, const QString& role, bool value);
    // Gain is in decibels; the engine stores a linear factor. Repeated calls on the same track
    // merge into one undo step.
    Q_INVOKABLE void setTrackGain(int trackId, double db);
    Q_INVOKABLE int trackIdAt(int row) const;
    Q_INVOKABLE void addTrack(const QString& name);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
  signals:
    void timelineChanged();
    void sampleRateChanged();
    void durationFramesChanged();
    void playheadFrameChanged();
    void historyChanged();
    void clipsChangedForTrack(int trackId);

  private:
    bool execute(std::unique_ptr<wavy::timeline::Command> command);
    void notifyTimeline();
    void notifyAllTracks();
    wavy::timeline::Timeline timeline_;
    wavy::timeline::History history_{timeline_};
    qint64 playheadFrame_ = 0;
};
