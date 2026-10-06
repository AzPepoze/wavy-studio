// Virtualized track viewport and scroll controls. Requires the timeline timelineState and edit
// coordinator; forwards selection/edit/navigation.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: root
    required property var timelineState
    required property var editing
    property real snapFrame: 0
    property bool dragging: false
    property bool altPressed: false
    property var dragClip: null
    property var dragEdges: []
    property int dragRow: -1
    property real dragFrame: 0
    property bool dragOriginSet: false
    property real dragOriginX: 0
    property int dragSourceRow: -1
    readonly property int trackCount: tracks.count
    readonly property bool draggingClip: dragClip !== null && dragRow >= 0
    signal selected(var clip, int trackId, int rowIndex)
    signal cleared()
    signal moved(var clip, int rowIndex, real startFrame, bool alt)
    signal trimmed(var clip, int trackId, real leftDelta, real rightDelta)
    clip: true
    Keys.onPressed: event => { if (event.key === Qt.Key_Alt) { root.altPressed = true; event.accepted = true; } }
    Keys.onReleased: event => { if (event.key === Qt.Key_Alt) { root.altPressed = false; event.accepted = true; } }
    function rowAt(sceneY: real): int {
        const content = tracks.contentY + (mapFromItem(null, 0, sceneY).y - tracks.y);
        return Math.max(0, Math.min(trackCount - 1, Math.floor(content / Theme.trackHeight)));
    }
    function snapStart(sceneX: real): real {
        if (!dragClip) return 0;
        const raw = (sceneX - dragOriginX) / root.timelineState.pixelsPerFrame;
        return Math.max(0, root.editing.snapFrame(dragClip.startFrame + raw,
                                                  { alt: root.altPressed, clipEdges: root.dragEdges }));
    }
    function beginClipDrag(clip: var): void {
        dragClip = clip;
        dragEdges = root.timelineState.settings.snapToClipEdges ? root.editing.clipEdges(clip.clipId) : [];
        dragRow = -1;
        dragFrame = clip.startFrame;
        dragOriginSet = false;
        dragSourceRow = -1;
    }
    function updateClipDrag(sceneX: real, sceneY: real): void {
        if (!dragClip) return;
        if (!dragOriginSet) {
            dragOriginX = sceneX;
            dragSourceRow = rowAt(sceneY);
            dragOriginSet = true;
        }
        dragRow = rowAt(sceneY);
        dragFrame = snapStart(sceneX);
        snapFrame = dragFrame;
        dragging = true;
    }
    function finishClipDrag(clip: var, sceneX: real, sceneY: real): void {
        if (!dragClip || dragClip.clipId !== clip.clipId) {
            cancelClipDrag();
            return;
        }
        const row = rowAt(sceneY);
        const start = snapStart(sceneX);
        const changed = row !== dragSourceRow || start !== dragClip.startFrame;
        const alt = root.altPressed;
        cancelClipDrag();
        if (changed) root.moved(clip, row, start, alt);
    }
    function cancelClipDrag(): void {
        dragClip = null; dragEdges = []; dragRow = -1; dragOriginSet = false;
        dragSourceRow = -1; dragging = false;
    }
    Text { text: "TRACKS"; x: Theme.space12; y: Theme.space8; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
    TimeRuler {
        x: root.timelineState.headerWidth; width: root.timelineState.laneWidth
        scrollX: root.timelineState.scrollX; pixelsPerSecond: root.timelineState.pixelsPerSecond
        pixelsPerFrame: root.timelineState.pixelsPerFrame; tickSeconds: root.timelineState.tickSeconds
        gridFrames: root.timelineState.gridFrames; framesPerBeat: root.timelineState.framesPerBeat
        framesPerBar: root.timelineState.framesPerBar; beatsPerBar: root.timelineState.beatsPerBar
        sampleRate: root.timelineState.timelineModel.sampleRate
        mode: root.timelineState.settings.rulerMode
        onFrameRequested: (frame, alt) => root.timelineState.timelineModel.playheadFrame =
            Math.round(Math.max(0, Math.min(root.timelineState.timelineModel.durationFrames,
                                            root.editing.snapFrame(frame, { playhead: false, alt: alt }))))
    }
    ListView {
        id: tracks
        y: Theme.rulerHeight; width: root.width; height: root.height - y - Theme.scrollbarHeight
        cacheBuffer: Theme.trackHeight * Theme.trackCacheRows
        clip: true
        model: root.timelineState.timelineModel.tracks
        ScrollBar.vertical: ScrollBar {}
        delegate: TrackRow {
            recordingController: root.timelineState.recordingController
            editing: root.editing
            required property int index
            width: tracks.width
            rowIndex: index; timelineModel: root.timelineState.timelineModel
            scrollX: root.timelineState.scrollX; laneWidth: root.timelineState.laneWidth; pixelsPerFrame: root.timelineState.pixelsPerFrame; gridFrames: root.timelineState.gridFrames
            trackSelected: root.timelineState.selectedTrackId === trackId
            onTrackSelectedRequested: { root.timelineState.selectedTrackId = trackId; }
            selectedClipId: root.timelineState.selectedClipId; snapEnabled: root.timelineState.snapEnabled; viewportRevision: root.timelineState.viewportRevision
            onSelected: (clip, trackId, rowIndex) => root.selected(clip, trackId, rowIndex)
            onCleared: root.cleared()
            onTrimmed: (clip, trackId, leftDelta, rightDelta) => root.trimmed(clip, trackId, leftDelta, rightDelta)
            onClipDragStarted: clip => root.beginClipDrag(clip)
            onClipDragMoved: (sceneX, sceneY) => root.updateClipDrag(sceneX, sceneY)
            onClipDropped: (clip, sceneX, sceneY) => root.finishClipDrag(clip, sceneX, sceneY)
            onClipDragCancelled: root.cancelClipDrag()
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
    Item {
        id: dragLayer
        objectName: "drag-layer"
        x: root.timelineState.headerWidth; y: Theme.rulerHeight
        width: root.timelineState.laneWidth; height: tracks.height
        clip: true
        visible: root.draggingClip
        Rectangle {
            objectName: "drop-highlight"
            x: 0; width: parent.width
            y: root.dragRow * Theme.trackHeight - tracks.contentY
            height: Theme.trackHeight
            color: Theme.laneHighlight
        }
        Rectangle {
            id: ghost
            objectName: "clip-ghost"
            x: root.dragFrame * root.timelineState.pixelsPerFrame - root.timelineState.scrollX
            y: root.dragRow * Theme.trackHeight - tracks.contentY + Theme.space8
            width: Math.max(Theme.clipMinimumWidth, root.dragClip ? root.dragClip.durationFrames * root.timelineState.pixelsPerFrame : 0)
            height: Theme.trackHeight - Theme.lineWidth - Theme.space8 * 2
            radius: Theme.radius
            color: Theme.trackPalette[((root.dragRow % Theme.trackPalette.length) + Theme.trackPalette.length) % Theme.trackPalette.length]
            border.color: Theme.selection
            border.width: Theme.lineWidth
            opacity: 0.92
            Text {
                text: root.dragClip ? root.dragClip.name : ""
                color: Theme.textPrimary; font.pixelSize: Theme.fontNormal
                elide: Text.ElideRight
                x: Theme.space12; y: Theme.space4; width: ghost.width - Theme.space12 * 2
            }
        }
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
            if (event.modifiers & Qt.ControlModifier) {
                const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y * 8;
                root.timelineState.zoomSmooth(Math.pow(Theme.zoomPerWheelUnit, delta), Math.max(0, event.x - root.timelineState.headerWidth));
            } else if ((event.modifiers & Qt.ShiftModifier) || event.angleDelta.x || event.pixelDelta.x) root.timelineState.scrollX = root.timelineState.clampScroll(root.timelineState.scrollX - (event.pixelDelta.x || event.angleDelta.x || event.angleDelta.y));
            else tracks.contentY = Math.max(0, Math.min(Math.max(0, tracks.contentHeight - tracks.height), tracks.contentY - (event.pixelDelta.y || event.angleDelta.y)));
            event.accepted = true;
        }
    }
}
