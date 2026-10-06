#include "WaveformBridge.hpp"
#include <QMetaObject>
#include <string>

WaveformBridge::WaveformBridge(QObject* parent) : QObject(parent) {}

void WaveformBridge::setLibrary(wavy::SourceLibrary& library) { library_ = &library; }

void WaveformBridge::subscribe() {
    if (!library_)
        return;
    library_->setOnReady([this](const std::string& path, bool ok) {
        const QString text = QString::fromStdString(path);
        QMetaObject::invokeMethod(
            this, [this, text, ok] { emit sourceReady(text, ok); }, Qt::QueuedConnection);
    });
}

void WaveformBridge::request(const QString& path) {
    if (library_ && !path.isEmpty())
        library_->request(path.toStdString());
}
