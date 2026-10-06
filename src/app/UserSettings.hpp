#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <memory>

// Persisted snap and ruler preferences. Uses the application's QSettings by default; tests and
// callers can point it at an explicit INI file or set WAVY_SETTINGS_FILE to keep user settings
// untouched.
class UserSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY changed)
    Q_PROPERTY(QString snapDivision READ snapDivision WRITE setSnapDivision NOTIFY changed)
    Q_PROPERTY(bool snapToClipEdges READ snapToClipEdges WRITE setSnapToClipEdges NOTIFY changed)
    Q_PROPERTY(bool snapToPlayhead READ snapToPlayhead WRITE setSnapToPlayhead NOTIFY changed)
    Q_PROPERTY(int snapTolerancePixels READ snapTolerancePixels WRITE setSnapTolerancePixels NOTIFY
                   changed)
    Q_PROPERTY(QString rulerMode READ rulerMode WRITE setRulerMode NOTIFY changed)
  public:
    explicit UserSettings(QObject* parent = nullptr);
    explicit UserSettings(const QString& settingsFile, QObject* parent = nullptr);

    bool snapEnabled() const;
    void setSnapEnabled(bool enabled);
    QString snapDivision() const;
    void setSnapDivision(const QString& division);
    bool snapToClipEdges() const;
    void setSnapToClipEdges(bool enabled);
    bool snapToPlayhead() const;
    void setSnapToPlayhead(bool enabled);
    int snapTolerancePixels() const;
    void setSnapTolerancePixels(int pixels);
    QString rulerMode() const;
    void setRulerMode(const QString& mode);

    static bool validDivision(const QString& division);
    static bool validRulerMode(const QString& mode);
  signals:
    void changed();

  private:
    std::unique_ptr<QSettings> store_;
};
