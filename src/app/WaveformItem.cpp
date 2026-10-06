#include "WaveformItem.hpp"
#include <QQmlContext>
#include <QQmlEngine>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <algorithm>
#include <cmath>

WaveformItem::WaveformItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(QQuickItem::ItemHasContents);
}

void WaveformItem::componentComplete() {
    QQuickItem::componentComplete();
    if (auto* bridge = this->bridge())
        connect(bridge, &WaveformBridge::sourceReady, this, &WaveformItem::onSourceReady);
    fetch();
}

void WaveformItem::setSource(const QString& path) {
    if (sourcePath_ == path)
        return;
    sourcePath_ = path;
    emit sourceChanged();
    if (isComponentComplete())
        fetch();
}

void WaveformItem::setSourceOffset(qint64 offset) {
    if (sourceOffset_ == offset)
        return;
    sourceOffset_ = offset;
    emit sourceOffsetChanged();
    update();
}

void WaveformItem::setLengthFrames(qint64 frames) {
    if (lengthFrames_ == frames)
        return;
    lengthFrames_ = frames;
    emit lengthFramesChanged();
    update();
}

void WaveformItem::setPixelsPerFrame(qreal pixelsPerFrame) {
    if (qFuzzyCompare(pixelsPerFrame_, pixelsPerFrame))
        return;
    pixelsPerFrame_ = pixelsPerFrame;
    emit pixelsPerFrameChanged();
    update();
}

void WaveformItem::setAmplitude(qreal amplitude) {
    if (qFuzzyCompare(amplitude_, amplitude))
        return;
    amplitude_ = amplitude;
    emit amplitudeChanged();
    update();
}

void WaveformItem::setColor(const QColor& color) {
    if (color_ == color)
        return;
    color_ = color;
    emit colorChanged();
    update();
}

void WaveformItem::setVisibleLeft(qreal x) {
    if (qFuzzyCompare(visibleLeft_, x))
        return;
    visibleLeft_ = x;
    emit visibleLeftChanged();
    update();
}

void WaveformItem::setVisibleRight(qreal x) {
    if (qFuzzyCompare(visibleRight_, x))
        return;
    visibleRight_ = x;
    emit visibleRightChanged();
    update();
}

WaveformBridge* WaveformItem::bridge() const {
    auto* engine = qmlEngine(this);
    if (!engine)
        return nullptr;
    const QVariant value = engine->rootContext()->contextProperty(QStringLiteral("waveformBridge"));
    return qobject_cast<WaveformBridge*>(value.value<QObject*>());
}

void WaveformItem::fetch() {
    sourceData_.reset();
    peaks_ = nullptr;
    sourceFrames_ = 0;
    if (auto* bridge = this->bridge()) {
        if (auto* library = bridge->library(); library && !sourcePath_.isEmpty()) {
            library->request(sourcePath_.toStdString());
            sourceData_ = library->get(sourcePath_.toStdString());
            if (sourceData_ && sourceData_->peaks) {
                peaks_ = sourceData_->peaks.get();
                sourceFrames_ = sourceData_->audio ? sourceData_->audio->frames() : 0;
            }
        }
    }
    update();
}

void WaveformItem::onSourceReady(const QString& path, bool) {
    if (path == sourcePath_)
        fetch();
}

QSGNode* WaveformItem::updatePaintNode(QSGNode* node, UpdatePaintNodeData*) {
    auto* geometryNode = static_cast<QSGGeometryNode*>(node);
    if (!geometryNode) {
        geometryNode = new QSGGeometryNode;
        geometry_ = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry_->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        geometry_->setVertexDataPattern(QSGGeometry::DynamicPattern);
        geometryNode->setGeometry(geometry_);
        geometryNode->setFlag(QSGNode::OwnsGeometry);
        material_ = new QSGFlatColorMaterial;
        geometryNode->setMaterial(material_);
        geometryNode->setFlag(QSGNode::OwnsMaterial);
    }
    if (material_->color() != color_) {
        material_->setColor(color_);
        geometryNode->markDirty(QSGNode::DirtyMaterial);
    }

    int columns = 0;
    if (peaks_) {
        wavy::buildWaveformEnvelope(*peaks_, sourceFrames_, sourceOffset_, lengthFrames_,
                                    pixelsPerFrame_, visibleLeft_, visibleRight_,
                                    static_cast<float>(amplitude_), envelope_);
        columns = envelope_.count;
    } else {
        envelope_.count = 0;
        envelope_.columns.clear();
    }

    const bool placeholder = columns == 0;
    const int vertexCount = placeholder ? 4 : 2 * (columns + 1);
    // QSGGeometry::allocate keeps the buffer when the count is unchanged and only reallocates on
    // a size change; setVertexCount exists only in newer Qt than the CI's 6.8.
    if (geometry_->vertexCount() != vertexCount)
        geometry_->allocate(vertexCount);

    auto* vertices = geometry_->vertexDataAsPoint2D();
    const float half = static_cast<float>(height()) / 2.f;
    const float widthF = static_cast<float>(width());
    if (placeholder) {
        const float top = std::max(0.f, half - 0.5f);
        const float bottom = std::min(static_cast<float>(height()), half + 0.5f);
        vertices[0].set(0.f, top);
        vertices[1].set(0.f, bottom);
        vertices[2].set(widthF, top);
        vertices[3].set(widthF, bottom);
    } else {
        for (int i = 0; i <= columns; ++i) {
            const auto& column =
                envelope_.columns[static_cast<std::size_t>(std::min(i, columns - 1))];
            const float x = static_cast<float>(envelope_.firstX + i * envelope_.stepX);
            vertices[2 * i].set(x, half - std::clamp(column.max, -1.f, 1.f) * half);
            vertices[2 * i + 1].set(x, half - std::clamp(column.min, -1.f, 1.f) * half);
        }
    }
    geometry_->markVertexDataDirty();
    geometryNode->markDirty(QSGNode::DirtyGeometry);
    return geometryNode;
}
