#pragma once
#include "AudioEngine.hpp"
#include <QObject>

class EngineController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    Q_PROPERTY(unsigned int sampleRate READ sampleRate NOTIFY stateChanged)
  public:
    explicit EngineController(QObject* parent = nullptr) : QObject(parent) {}
    bool isRunning() const { return engine_.isRunning(); }
    unsigned int sampleRate() const { return engine_.sampleRate(); }
    Q_INVOKABLE bool start() {
        const bool result = engine_.start();
        emit stateChanged();
        return result;
    }
    Q_INVOKABLE void stop() {
        engine_.stop();
        emit stateChanged();
    }
  signals:
    void stateChanged();

  private:
    wavy::AudioEngine engine_;
};
