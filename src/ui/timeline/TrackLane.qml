// Culled lane grid and clips. Requires visible clips and viewport state; emits selection, edits and snap preview.
pragma ComponentBehavior: Bound
import QtQuick
import "../theme"

Rectangle {
    id: root
    required property var clips
    required property real scrollX
    required property real pixelsPerFrame
    required property real gridFrames
    required property real tickSeconds
    required property real pixelsPerSecond
    required property int selectedClipId
    required property bool alternate
    required property bool snapEnabled
    signal selected(var clip)
    signal cleared()
    signal trimmed(var clip, real leftDelta, real rightDelta)
    signal requested(var clip, string operation)
    signal clipDragStarted(var clip)
    signal clipDragMoved(real sceneX, real sceneY)
    signal clipDropped(var clip, real sceneX, real sceneY)
    signal clipDragCancelled()
    clip: true
    color: alternate ? Theme.backgroundAlternate : Theme.background
    MouseArea { anchors.fill: parent; onClicked: root.cleared() }
    Repeater {
        model: Math.ceil(root.width / (root.tickSeconds * root.pixelsPerSecond)) + 2
        Rectangle {
            required property int index
            x: (Math.floor(root.scrollX / (root.tickSeconds * root.pixelsPerSecond)) + index) * root.tickSeconds * root.pixelsPerSecond - root.scrollX
            width: Theme.lineWidth
            height: root.height
            color: Theme.surface
        }
    }
    Repeater {
        model: root.clips
        ClipItem {
            required property var modelData
            clipData: modelData
            gridFrames: root.gridFrames
            pixelsPerFrame: root.pixelsPerFrame
            selected: root.selectedClipId === clipData.clipId
            snapEnabled: root.snapEnabled
            width: Math.max(Theme.clipMinimumWidth, clipData.durationFrames * pixelsPerFrame)
            height: root.height - Theme.space8 * 2
            x: clipData.startFrame * pixelsPerFrame - root.scrollX
            y: Theme.space8
            onActivated: root.selected(clipData)
            onTrimmed: (leftDelta, rightDelta) => root.trimmed(clipData, leftDelta, rightDelta)
            onRequested: operation => root.requested(clipData, operation)
            onDragStarted: clip => root.clipDragStarted(clip)
            onDragMoved: (sceneX, sceneY) => root.clipDragMoved(sceneX, sceneY)
            onDragDropped: (clip, sceneX, sceneY) => root.clipDropped(clip, sceneX, sceneY)
            onDragCancelled: root.clipDragCancelled()
        }
    }
}
