#include "RecordController.hpp"
#include "EngineController.hpp"
#include "TimelineModel.hpp"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

RecordController::RecordController(TimelineModel& model, EngineController& audio, QObject* parent,
                                   bool offlineInput, QString directory)
    : QObject(parent), model_(model), audio_(audio), directory_(std::move(directory)),
      inputAvailable_(offlineInput), offlineInput_(offlineInput) {
    if (directory_.isEmpty())
        directory_ = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                     QStringLiteral("/Wavy Studio/Recordings");
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &RecordController::updateMeters);
    timer_.start();
}

RecordController::~RecordController() {
    if (worker_.joinable())
        worker_.join();
    audio_.engine().recorder().stop();
    audio_.setControlLocked(false);
}

double RecordController::elapsedSeconds() const { return double(elapsedFrames_) / rate_; }

void RecordController::setError(QString error) {
    lastError_ = std::move(error);
    emit errorChanged();
}

void RecordController::run(std::function<void()> operation) {
    audio_.setControlLocked(true);
    busy_ = true;
    emit stateChanged();
    worker_ = std::jthread([this, operation = std::move(operation)] { operation(); });
}

void RecordController::finish() {
    worker_.join();
    busy_ = false;
    audio_.setControlLocked(false);
}

QString RecordController::safeTrackName(QString name) {
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), "_");
    name = name.left(100).trimmed();
    while (name.endsWith('.') || name.endsWith(' '))
        name.chop(1);
    if (name.isEmpty())
        name = QStringLiteral("Track");
    if (QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                           QRegularExpression::CaseInsensitiveOption)
            .match(name)
            .hasMatch())
        name.prepend('_');
    return name;
}

void RecordController::armTrack(int trackId, bool armed) {
    if (busy_ || recording_)
        return;
    if (!armed) {
        if (armedTrackId_ == trackId)
            armedTrackId_ = -1;
    } else {
        for (const auto& track : model_.timeline().tracks())
            if (int(track.id.value) == trackId)
                armedTrackId_ = trackId;
    }
    emit stateChanged();
}

void RecordController::toggleRecord() {
    if (recording_)
        stopRecording();
    else
        startRecording();
}

void RecordController::startRecording() {
    if (busy_ || recording_)
        return;
    if (armedTrackId_ < 0) {
        setError(QStringLiteral("Arm a track first"));
        return;
    }
    QString name;
    for (const auto& track : model_.timeline().tracks())
        if (int(track.id.value) == armedTrackId_)
            name = QString::fromStdString(track.name);
    if (name.isNull()) {
        setError(QStringLiteral("Arm a track first"));
        return;
    }
    if (selectedInput_ == -1) {
        setError(QStringLiteral("No input device"));
        return;
    }
    audio_.pause();
    audio_.setLoopEnabled(false);
    startFrame_ = audio_.positionFrames();
    elapsedFrames_ = 0;
    droppedFrames_ = 0;
    setError({});
    run([this, name = safeTrackName(name)] {
        QString error;
        auto& engine = audio_.engine();
        if (!engine.isRunning())
            engine.start();
        const bool available = offlineInput_ || engine.inputAvailable();
        const unsigned rate = engine.sampleRate();
        if (!available) {
            error = QStringLiteral("No input device");
        } else if (!QDir().mkpath(directory_)) {
            error = QStringLiteral("Cannot create recordings folder");
        } else {
            QString path;
            for (unsigned take = 1;; ++take) {
                path = QDir(directory_)
                           .filePath(QStringLiteral("%1-take%2.wav")
                                         .arg(name)
                                         .arg(take, 3, 10, QLatin1Char('0')));
                if (!QFile::exists(path))
                    break;
            }
            if (error.isEmpty()) {
                auto& recorder = engine.recorder();
                if (!recorder.arm(std::filesystem::path(path.toStdU16String()), channels_) ||
                    !recorder.startAtFrame(startFrame_)) {
                    error = QString::fromStdString(recorder.error());
                    if (error.isEmpty())
                        error = QStringLiteral("Cannot start recording");
                    recorder.stop();
                }
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, available, rate, error] {
                finish();
                inputAvailable_ = available;
                rate_ = rate ? rate : 48000;
                if (!error.isEmpty()) {
                    setError(error);
                } else {
                    audio_.play();
                    audio_.setControlLocked(true);
                    recording_ = true;
                }
                emit stateChanged();
            },
            Qt::QueuedConnection);
    });
}

void RecordController::stopRecording() {
    if (busy_ || !recording_)
        return;
    audio_.setControlLocked(false);
    audio_.pause();
    elapsedFrames_ = std::max<qint64>(0, audio_.positionFrames() - startFrame_);
    run([this, track = armedTrackId_] {
        const auto take = audio_.engine().recorder().stop();
        const auto error = QString::fromStdString(audio_.engine().recorder().error());
        QMetaObject::invokeMethod(
            this,
            [this, take, track, error] {
                finish();
                recording_ = false;
                droppedFrames_ = take.droppedFrames;
                if (take.lengthFrames > 0 && !model_.commitTake(track, take))
                    setError(QStringLiteral("Cannot add recorded take to track"));
                else if (!error.isEmpty())
                    setError(error);
                else if (take.droppedFrames)
                    setError(QStringLiteral("Warning: %1 recording frames dropped")
                                 .arg(take.droppedFrames));
                if (take.lengthFrames > 0)
                    audio_.seek(take.startFrame + take.lengthFrames);
                emit stateChanged();
                emit meterChanged();
            },
            Qt::QueuedConnection);
    });
}

void RecordController::updateMeters() {
    if (busy_)
        return;
    const auto& recorder = audio_.engine().recorder();
    const auto elapsed = std::max<qint64>(0, audio_.positionFrames() - startFrame_);
    const float peak =
        recording_ && elapsed != elapsedFrames_ ? std::max(recorder.peak(0), recorder.peak(1)) : 0;
    inputPeak_ = std::clamp(std::max(peak, inputPeak_ * 0.85f), 0.f, 1.f);
    if (recording_) {
        elapsedFrames_ = elapsed;
        droppedFrames_ = recorder.droppedFrames();
    }
    emit meterChanged();
}

void RecordController::setMonitor(bool enabled) {
    if (busy_)
        return;
    monitorEnabled_ = enabled;
    audio_.engine().setMonitorGain(1);
    audio_.engine().setMonitorEnabled(enabled);
    emit stateChanged();
}

QStringList RecordController::inputDeviceNames() const {
    QStringList names{QStringLiteral("None")};
    for (const auto& device : devices_)
        names.append(QString::fromStdString(device.name));
    return names;
}

int RecordController::inputChannelCount(int index) const {
    return index > 0 && size_t(index) <= devices_.size() ? devices_[index - 1].inputChannels : 0;
}

void RecordController::refreshInputDevices() {
    if (busy_ || recording_)
        return;
    audio_.pause();
    run([this] {
        auto devices = audio_.engine().inputDevices();
        const auto api = QString::fromStdString(audio_.engine().outputApiName());
        QMetaObject::invokeMethod(
            this,
            [this, devices = std::move(devices), api] {
                finish();
                devices_ = devices;
                outputApiName_ = api;
                if (selectedInput_ == -2) {
                    selectedInput_ = -1;
                    for (size_t i = 0; i < devices_.size(); ++i)
                        if (devices_[i].isDefault) {
                            selectedInput_ = int(i + 1);
                            channels_ = std::min(2u, devices_[i].inputChannels);
                            break;
                        }
                }
                inputAvailable_ = offlineInput_ || selectedInput_ > 0;
                emit devicesChanged();
                emit stateChanged();
            },
            Qt::QueuedConnection);
    });
}

void RecordController::selectInputDevice(int index, int firstChannel, int channelCount) {
    if (busy_ || recording_ || index < 0 || size_t(index) > devices_.size())
        return;
    if (index == 0) {
        selectedInput_ = -1;
        inputAvailable_ = false;
        setMonitor(false);
        emit stateChanged();
        emit devicesChanged();
        return;
    }
    const auto device = devices_[index - 1];
    if (firstChannel < 0 || (channelCount != 1 && channelCount != 2) ||
        unsigned(firstChannel + channelCount) > device.inputChannels) {
        setError(QStringLiteral("Invalid input channels"));
        return;
    }
    audio_.pause();
    run([this, index, device, firstChannel, channelCount] {
        auto& engine = audio_.engine();
        engine.stop();
        const bool selected = engine.setInputDevice(device.id, firstChannel, channelCount);
        if (selected)
            engine.start();
        const bool available = selected && engine.inputAvailable();
        const auto api = QString::fromStdString(engine.outputApiName());
        QMetaObject::invokeMethod(
            this,
            [this, index, firstChannel, channelCount, available, api] {
                finish();
                selectedInput_ = index;
                outputApiName_ = api;
                firstChannel_ = firstChannel;
                channels_ = channelCount;
                emit devicesChanged();
                inputAvailable_ = available;
                if (!available)
                    setError(QStringLiteral("No input device"));
                emit stateChanged();
            },
            Qt::QueuedConnection);
    });
}
