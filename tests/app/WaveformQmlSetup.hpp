#pragma once
#include "WaveformBridge.hpp"
#include "WaveformItem.hpp"
#include "io/SourceLibrary.hpp"
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>

// Registers the C++ waveform module and exposes a library-backed bridge so the QML cases draw real
// peaks. Pure-QML harnesses leave the context property unset and get the flat placeholder instead.
class WaveformQmlSetup : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~WaveformQmlSetup() override { library_.waitIdle(); }
  public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        qmlRegisterType<WaveformItem>("Wavy.Waveform", 1, 0, "WaveformItem");
        bridge_.setLibrary(library_);
        bridge_.subscribe();
        engine->rootContext()->setContextProperty("waveformBridge", &bridge_);
    }

  private:
    wavy::SourceLibrary library_{48000};
    WaveformBridge bridge_;
};
