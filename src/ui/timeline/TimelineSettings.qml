// In-memory default settings. TimelineView binds its settings to the persistent UserSettings context
// property in the app; tests and previews fall back to this object.
import QtQuick

QtObject {
    id: root
    property bool snapEnabled: true
    property string snapDivision: "auto"
    property bool snapToClipEdges: true
    property bool snapToPlayhead: true
    property int snapTolerancePixels: 8
    property string rulerMode: "barsBeats"
    signal changed()
    onSnapEnabledChanged: root.changed()
    onSnapDivisionChanged: root.changed()
    onSnapToClipEdgesChanged: root.changed()
    onSnapToPlayheadChanged: root.changed()
    onSnapTolerancePixelsChanged: root.changed()
    onRulerModeChanged: root.changed()
}
