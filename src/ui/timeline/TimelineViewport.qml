// Virtualized track viewport and scroll controls. Requires the timeline timelineState; forwards selection/edit/navigation.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: root
    required property var timelineState
    property real snapFrame: 0
    property bool dragging: false
    readonly property int trackCount: tracks.count
    signal selected(var clip, int trackId, int rowIndex)
    signal cleared()
    signal moved(var clip, int rowIndex, real deltaFrames, real deltaY)
    signal trimmed(var clip, int trackId, real leftDelta, real rightDelta)
    clip: true
    Text { text: "TRACKS"; x: Theme.space12; y: Theme.space8; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
    TimeRuler {
        x: root.timelineState.headerWidth; width: root.timelineState.laneWidth
        scrollX: root.timelineState.scrollX; pixelsPerSecond: root.timelineState.pixelsPerSecond
        pixelsPerFrame: root.timelineState.pixelsPerFrame; tickSeconds: root.timelineState.tickSeconds
        onFrameRequested: frame => root.timelineState.timelineModel.playheadFrame = Math.round(Math.max(0, Math.min(root.timelineState.timelineModel.durationFrames, frame)))
    }
    ListView {
        id: tracks
        y: Theme.rulerHeight; width: root.width; height: root.height - y - Theme.scrollbarHeight
        cacheBuffer: Theme.trackHeight * Theme.trackCacheRows
        clip: true
        model: root.timelineState.timelineModel.tracks
        ScrollBar.vertical: ScrollBar {}
        delegate: TrackRow {
            required property int index
            width: tracks.width
            rowIndex: index; timelineModel: root.timelineState.timelineModel
            scrollX: root.timelineState.scrollX; laneWidth: root.timelineState.laneWidth; pixelsPerFrame: root.timelineState.pixelsPerFrame
            pixelsPerSecond: root.timelineState.pixelsPerSecond; tickSeconds: root.timelineState.tickSeconds; gridFrames: root.timelineState.gridFrames
            selectedClipId: root.timelineState.selectedClipId; snapEnabled: root.timelineState.snapEnabled; viewportRevision: root.timelineState.viewportRevision
            onSelected: (clip, trackId, rowIndex) => root.selected(clip, trackId, rowIndex)
            onCleared: root.cleared()
            onMoved: (clip, rowIndex, deltaFrames, deltaY) => root.moved(clip, rowIndex, deltaFrames, deltaY)
            onTrimmed: (clip, trackId, leftDelta, rightDelta) => root.trimmed(clip, trackId, leftDelta, rightDelta)
            onDragPreview: (frame, active) => { root.snapFrame = frame; root.dragging = active; }
        }
    }
    Playhead {
        x: root.timelineState.headerWidth + root.timelineState.timelineModel.playheadFrame * root.timelineState.pixelsPerFrame - root.timelineState.scrollX
        height: root.height - Theme.scrollbarHeight
        visible: x >= root.timelineState.headerWidth && x < root.width
    }
    Playhead {
        snap: true; y: Theme.rulerHeight; height: tracks.height
        x: root.timelineState.headerWidth + root.snapFrame * root.timelineState.pixelsPerFrame - root.timelineState.scrollX
        visible: root.dragging && root.timelineState.snapEnabled && x >= root.timelineState.headerWidth && x < root.width
    }
    ScrollBar {
        orientation: Qt.Horizontal
        x: root.timelineState.headerWidth; y: root.height - height; width: root.timelineState.laneWidth; height: Theme.scrollbarHeight
        position: root.timelineState.scrollX / Math.max(1, root.timelineState.contentWidth)
        size: Math.min(1, root.timelineState.laneWidth / Math.max(1, root.timelineState.contentWidth))
        onPositionChanged: if (pressed) root.timelineState.scrollX = root.timelineState.clampScroll(position * root.timelineState.contentWidth)
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        property point origin
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: mouse => {
            if (mouse.button !== Qt.MiddleButton && !(mouse.modifiers & Qt.AltModifier)) { mouse.accepted = false; return; }
            origin = Qt.point(mouse.x, mouse.y);
        }
        onPositionChanged: mouse => {
            if (!pressed) return;
            root.timelineState.scrollX = root.timelineState.clampScroll(root.timelineState.scrollX + origin.x - mouse.x);
            tracks.contentY = Math.max(0, Math.min(Math.max(0, tracks.contentHeight - tracks.height), tracks.contentY + origin.y - mouse.y));
            origin = Qt.point(mouse.x, mouse.y);
        }
    }
    WheelHandler {
        acceptedModifiers: Qt.KeyboardModifierMask
        onWheel: event => {
            if (event.modifiers & Qt.ControlModifier) root.timelineState.zoom(Math.pow(Theme.zoomFactor, event.angleDelta.y / Theme.wheelStep), Math.max(0, event.x - root.timelineState.headerWidth));
            else if ((event.modifiers & Qt.ShiftModifier) || event.angleDelta.x || event.pixelDelta.x) root.timelineState.scrollX = root.timelineState.clampScroll(root.timelineState.scrollX - (event.pixelDelta.x || event.angleDelta.x || event.angleDelta.y));
            else tracks.contentY = Math.max(0, Math.min(Math.max(0, tracks.contentHeight - tracks.height), tracks.contentY - (event.pixelDelta.y || event.angleDelta.y)));
            event.accepted = true;
        }
    }
}
