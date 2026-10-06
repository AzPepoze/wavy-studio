#pragma once
#include "audio/Mixer.hpp"
#include "io/SourceLibrary.hpp"
#include <QObject>
#include <QTimer>
#include <memory>
#include <string>
#include <unordered_set>

class TimelineModel;
class EffectsController;

class SnapshotPublisher final : public QObject {
    Q_OBJECT
  public:
    SnapshotPublisher(TimelineModel& model, wavy::Mixer& mixer, QObject* parent = nullptr);
    ~SnapshotPublisher() override;
    void setEffectsController(EffectsController& effects);
    // Shared source cache for future waveform drawing and import.
    wavy::SourceLibrary& sourceLibrary() { return *library_; }
  signals:
    void published();
    // Emitted on the GUI thread when a source finishes loading (ok false on failure). Forwarded to
    // the waveform bridge; SnapshotPublisher keeps owning the library and its dirty flag.
    void sourceReady(const QString& path, bool ok);

  private:
    void update();
    TimelineModel& model_;
    wavy::Mixer& mixer_;
    QTimer timer_;
    std::unique_ptr<wavy::SourceLibrary> library_;
    std::unordered_set<std::string> reportedFailures_;
    bool dirty_ = true;
    EffectsController* effects_ = nullptr;
};
