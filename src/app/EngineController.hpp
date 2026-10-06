#pragma once
#include "audio/AudioEngine.hpp"
#include <QObject>
#include <QTimer>

class TimelineModel;

class EngineController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    Q_PROPERTY(unsigned int sampleRate READ sampleRate NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionFrames READ positionFrames NOTIFY positionChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString playbackState READ playbackState NOTIFY stateChanged)
  public:
    explicit EngineController(
        TimelineModel& model, QObject* parent = nullptr,
        wavy::AudioEngine::DeviceMode mode = wavy::AudioEngine::DeviceMode::Default);
    bool isRunning() const { return engine_.isRunning(); }
    unsigned int sampleRate() const { return engine_.isRunning() ? engine_.sampleRate() : 48000; }
    bool playing() const { return transport_.isPlaying(); }
    qint64 positionFrames() const { return transport_.positionFrames(); }
    bool loopEnabled() const { return loopEnabled_; }
    QString playbackState() const;
    wavy::AudioEngine& engine() { return engine_; }
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 frame);
    Q_INVOKABLE void setLoop(qint64 begin, qint64 end);
    void setLoopEnabled(bool enabled);
  signals:
    void stateChanged();
    void positionChanged();

  private:
    void updatePosition();
    TimelineModel& model_;
    wavy::AudioEngine engine_;
    wavy::Transport& transport_;
    QTimer timer_;
    qint64 loopBegin_ = 0, loopEnd_ = 0;
    bool loopEnabled_ = false;
    bool stopped_ = true;
    bool updatingPosition_ = false;
};
