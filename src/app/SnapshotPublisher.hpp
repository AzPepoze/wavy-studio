#pragma once
#include "audio/Mixer.hpp"
#include "io/SourceLibrary.hpp"
#include <QObject>
#include <QTimer>
#include <memory>
#include <string>
#include <unordered_set>

class TimelineModel;

class SnapshotPublisher final : public QObject {
    Q_OBJECT
  public:
    SnapshotPublisher(TimelineModel& model, wavy::Mixer& mixer, QObject* parent = nullptr);
    ~SnapshotPublisher() override;
    // Shared source cache for future waveform drawing and import.
    wavy::SourceLibrary& sourceLibrary() { return *library_; }
  signals:
    void published();

  private:
    void update();
    TimelineModel& model_;
    wavy::Mixer& mixer_;
    QTimer timer_;
    std::unique_ptr<wavy::SourceLibrary> library_;
    std::unordered_set<std::string> reportedFailures_;
    bool dirty_ = true;
};
