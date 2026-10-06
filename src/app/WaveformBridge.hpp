#pragma once
#include "io/SourceLibrary.hpp"
#include <QObject>
#include <QString>

// Connects QML waveform items to the shared SourceLibrary. The app points it at the library owned
// by SnapshotPublisher and forwards that publisher's queued ready notifications; a standalone
// setup (for example a QML test) can subscribe directly to the library's worker callback.
class WaveformBridge final : public QObject {
    Q_OBJECT
  public:
    explicit WaveformBridge(QObject* parent = nullptr);
    void setLibrary(wavy::SourceLibrary& library);
    // Installs the queued on-ready callback. Not used by the app, which shares SnapshotPublisher's
    // existing callback instead, and must not clobber it.
    void subscribe();
    wavy::SourceLibrary* library() const { return library_; }
    Q_INVOKABLE void request(const QString& path);
  signals:
    void sourceReady(const QString& path, bool ok);

  private:
    wavy::SourceLibrary* library_ = nullptr;
};
