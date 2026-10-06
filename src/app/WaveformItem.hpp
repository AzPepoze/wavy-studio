#pragma once
#include "WaveformBridge.hpp"
#include "WaveformGeometry.hpp"
#include "io/SourceLibrary.hpp"
#include <QColor>
#include <QQuickItem>
#include <memory>

class QSGFlatColorMaterial;
class QSGGeometry;
class QSGGeometryNode;

// Draws one clip's real waveform as a triangle strip built on the scene graph. It reads peaks from
// the shared SourceLibrary via WaveformBridge, so it never touches audio buffers, and it only
// regenerates the visible slice of the clip. `visibleLeft`/`visibleRight` are in item coordinates.
class WaveformItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(
        qint64 sourceOffset READ sourceOffset WRITE setSourceOffset NOTIFY sourceOffsetChanged)
    Q_PROPERTY(
        qint64 lengthFrames READ lengthFrames WRITE setLengthFrames NOTIFY lengthFramesChanged)
    Q_PROPERTY(qreal pixelsPerFrame READ pixelsPerFrame WRITE setPixelsPerFrame NOTIFY
                   pixelsPerFrameChanged)
    Q_PROPERTY(qreal amplitude READ amplitude WRITE setAmplitude NOTIFY amplitudeChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(qreal visibleLeft READ visibleLeft WRITE setVisibleLeft NOTIFY visibleLeftChanged)
    Q_PROPERTY(
        qreal visibleRight READ visibleRight WRITE setVisibleRight NOTIFY visibleRightChanged)
  public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QString source() const { return sourcePath_; }
    qint64 sourceOffset() const { return sourceOffset_; }
    qint64 lengthFrames() const { return lengthFrames_; }
    qreal pixelsPerFrame() const { return pixelsPerFrame_; }
    qreal amplitude() const { return amplitude_; }
    QColor color() const { return color_; }
    qreal visibleLeft() const { return visibleLeft_; }
    qreal visibleRight() const { return visibleRight_; }

    void setSource(const QString& path);
    void setSourceOffset(qint64 offset);
    void setLengthFrames(qint64 frames);
    void setPixelsPerFrame(qreal pixelsPerFrame);
    void setAmplitude(qreal amplitude);
    void setColor(const QColor& color);
    void setVisibleLeft(qreal x);
    void setVisibleRight(qreal x);

  signals:
    void sourceChanged();
    void sourceOffsetChanged();
    void lengthFramesChanged();
    void pixelsPerFrameChanged();
    void amplitudeChanged();
    void colorChanged();
    void visibleLeftChanged();
    void visibleRightChanged();

  protected:
    void componentComplete() override;
    QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData* data) override;

  private:
    WaveformBridge* bridge() const;
    void fetch();
    void onSourceReady(const QString& path, bool ok);

    QString sourcePath_;
    qint64 sourceOffset_ = 0;
    qint64 lengthFrames_ = 0;
    qint64 sourceFrames_ = 0;
    qreal pixelsPerFrame_ = 1;
    qreal amplitude_ = 1;
    QColor color_;
    qreal visibleLeft_ = 0;
    qreal visibleRight_ = 0;
    std::shared_ptr<const wavy::Source> sourceData_;
    const wavy::PeakPyramid* peaks_ = nullptr;
    wavy::WaveformEnvelope envelope_;
    QSGGeometry* geometry_ = nullptr;
    QSGFlatColorMaterial* material_ = nullptr;
};
