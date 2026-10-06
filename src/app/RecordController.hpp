#pragma once
#include "audio/AudioEngine.hpp"
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <thread>

class EngineController;
class TimelineModel;

class RecordController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int armedTrackId READ armedTrackId NOTIFY stateChanged)
    Q_PROPERTY(bool inputAvailable READ inputAvailable NOTIFY stateChanged)
    Q_PROPERTY(float inputPeak READ inputPeak NOTIFY meterChanged)
    Q_PROPERTY(qint64 elapsedFrames READ elapsedFrames NOTIFY meterChanged)
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY meterChanged)
    Q_PROPERTY(bool monitorEnabled READ monitorEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(quint64 droppedFrames READ droppedFrames NOTIFY meterChanged)
    Q_PROPERTY(int selectedChannelIndex READ selectedChannelIndex NOTIFY devicesChanged)
    Q_PROPERTY(int selectedInputIndex READ selectedInputIndex NOTIFY devicesChanged)
    Q_PROPERTY(QStringList deviceNames READ inputDeviceNames NOTIFY devicesChanged)
    Q_PROPERTY(QString outputApiName READ outputApiName NOTIFY devicesChanged)
  public:
    explicit RecordController(TimelineModel& model, EngineController& audio,
                              QObject* parent = nullptr, bool offlineInput = false,
                              QString directory = {});
    ~RecordController() override;
    bool recording() const { return recording_; }
    bool busy() const { return busy_; }
    int armedTrackId() const { return armedTrackId_; }
    bool inputAvailable() const { return inputAvailable_; }
    float inputPeak() const { return inputPeak_; }
    qint64 elapsedFrames() const { return elapsedFrames_; }
    double elapsedSeconds() const;
    bool monitorEnabled() const { return monitorEnabled_; }
    QString lastError() const { return lastError_; }
    quint64 droppedFrames() const { return droppedFrames_; }
    int selectedChannelIndex() const { return channels_ == 2 ? 2 : int(firstChannel_); }
    int selectedInputIndex() const { return std::max(0, selectedInput_); }
    QString outputApiName() const { return outputApiName_; }
    Q_INVOKABLE void armTrack(int trackId, bool armed);
    Q_INVOKABLE void toggleRecord();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void setMonitor(bool enabled);
    Q_INVOKABLE QStringList inputDeviceNames() const;
    Q_INVOKABLE void refreshInputDevices();
    Q_INVOKABLE int inputChannelCount(int index) const;
    Q_INVOKABLE void selectInputDevice(int index, int firstChannel, int channelCount);
    static QString safeTrackName(QString name);
  signals:
    void stateChanged();
    void meterChanged();
    void errorChanged();
    void devicesChanged();

  private:
    void run(std::function<void()> operation);
    void finish();
    void setError(QString error);
    void updateMeters();
    TimelineModel& model_;
    EngineController& audio_;
    QTimer timer_;
    std::jthread worker_;
    std::vector<wavy::InputDeviceInfo> devices_;
    QString directory_, lastError_, outputApiName_;
    int armedTrackId_ = -1, selectedInput_ = -2;
    unsigned firstChannel_ = 0, channels_ = 2, rate_ = 48000;
    bool recording_ = false, busy_ = false, inputAvailable_ = false;
    bool monitorEnabled_ = false, offlineInput_ = false;
    float inputPeak_ = 0;
    qint64 startFrame_ = 0, elapsedFrames_ = 0;
    quint64 droppedFrames_ = 0;
};
