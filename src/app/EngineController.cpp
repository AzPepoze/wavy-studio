#include "EngineController.hpp"
#include "TimelineModel.hpp"
#include <QScopedValueRollback>

EngineController::EngineController(TimelineModel& model, QObject* parent,
                                   wavy::AudioEngine::DeviceMode mode)
    : QObject(parent), model_(model), engine_(mode), transport_(engine_.mixer().transport()) {
    transport_.seek(model_.playheadFrame());
    connect(&model_, &TimelineModel::playheadFrameChanged, this, [this] {
        if (!updatingPosition_)
            seek(model_.playheadFrame());
    });
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &EngineController::updatePosition);
}

QString EngineController::playbackState() const {
    return playing()  ? QStringLiteral("Playing")
           : stopped_ ? QStringLiteral("Stopped")
                      : QStringLiteral("Paused");
}

void EngineController::play() {
    if (playing())
        return;
    if (!engine_.isRunning() && !engine_.start())
        return;
    transport_.play();
    stopped_ = false;
    timer_.start();
    emit stateChanged();
}

void EngineController::pause() {
    transport_.pause();
    timer_.stop();
    stopped_ = false;
    updatePosition();
    emit stateChanged();
}

void EngineController::togglePlay() {
    if (playing())
        pause();
    else
        play();
}

void EngineController::stop() {
    transport_.stop();
    timer_.stop();
    stopped_ = true;
    updatePosition();
    emit stateChanged();
}

void EngineController::seek(qint64 frame) {
    transport_.seek(frame);
    updatePosition();
}

void EngineController::updatePosition() {
    QScopedValueRollback guard(updatingPosition_, true);
    model_.setPlayheadFrame(positionFrames());
    emit positionChanged();
}

void EngineController::setLoop(qint64 begin, qint64 end) {
    loopBegin_ = begin;
    loopEnd_ = end;
    setLoopEnabled(begin >= 0 && end > begin);
}

void EngineController::setLoopEnabled(bool enabled) {
    loopEnabled_ = enabled && loopBegin_ >= 0 && loopEnd_ > loopBegin_;
    transport_.setLoop(loopBegin_, loopEnd_, loopEnabled_);
    emit stateChanged();
}
