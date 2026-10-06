#include "UserSettings.hpp"
#include <QSet>
#include <algorithm>

namespace {
// Fixed snap divisions, in the order the settings popup shows them.
const QSet<QString>& divisions() {
    static const QSet<QString> values{
        QStringLiteral("auto"), QStringLiteral("bar"),  QStringLiteral("1/2"),
        QStringLiteral("1/4"),  QStringLiteral("1/8"),  QStringLiteral("1/16"),
        QStringLiteral("1/32"), QStringLiteral("1/2T"), QStringLiteral("1/4T"),
        QStringLiteral("1/8T"), QStringLiteral("1/16T")};
    return values;
}
} // namespace

UserSettings::UserSettings(QObject* parent) : UserSettings(QString(), parent) {}

UserSettings::UserSettings(const QString& settingsFile, QObject* parent) : QObject(parent) {
    QString path = settingsFile;
    if (path.isEmpty())
        path = qEnvironmentVariable("WAVY_SETTINGS_FILE");
    store_ = path.isEmpty() ? std::make_unique<QSettings>()
                            : std::make_unique<QSettings>(path, QSettings::IniFormat);
}

bool UserSettings::validDivision(const QString& division) { return divisions().contains(division); }

bool UserSettings::validRulerMode(const QString& mode) {
    return mode == QStringLiteral("barsBeats") || mode == QStringLiteral("time");
}

bool UserSettings::snapEnabled() const { return store_->value("snap/enabled", true).toBool(); }
void UserSettings::setSnapEnabled(bool enabled) {
    if (snapEnabled() == enabled)
        return;
    store_->setValue("snap/enabled", enabled);
    store_->sync();
    emit changed();
}
QString UserSettings::snapDivision() const {
    return store_->value("snap/division", QStringLiteral("auto")).toString();
}
void UserSettings::setSnapDivision(const QString& division) {
    if (!validDivision(division) || snapDivision() == division)
        return;
    store_->setValue("snap/division", division);
    store_->sync();
    emit changed();
}
bool UserSettings::snapToClipEdges() const {
    return store_->value("snap/clipEdges", true).toBool();
}
void UserSettings::setSnapToClipEdges(bool enabled) {
    if (snapToClipEdges() == enabled)
        return;
    store_->setValue("snap/clipEdges", enabled);
    store_->sync();
    emit changed();
}
bool UserSettings::snapToPlayhead() const { return store_->value("snap/playhead", true).toBool(); }
void UserSettings::setSnapToPlayhead(bool enabled) {
    if (snapToPlayhead() == enabled)
        return;
    store_->setValue("snap/playhead", enabled);
    store_->sync();
    emit changed();
}
int UserSettings::snapTolerancePixels() const {
    return store_->value("snap/tolerancePixels", 8).toInt();
}
void UserSettings::setSnapTolerancePixels(int pixels) {
    pixels = std::clamp(pixels, 1, 64);
    if (snapTolerancePixels() == pixels)
        return;
    store_->setValue("snap/tolerancePixels", pixels);
    store_->sync();
    emit changed();
}
QString UserSettings::rulerMode() const {
    return store_->value("ruler/mode", QStringLiteral("barsBeats")).toString();
}
void UserSettings::setRulerMode(const QString& mode) {
    if (!validRulerMode(mode) || rulerMode() == mode)
        return;
    store_->setValue("ruler/mode", mode);
    store_->sync();
    emit changed();
}
