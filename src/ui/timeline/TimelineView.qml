// Model contract: sampleRate, durationFrames and playheadFrame (int frames), tracks
// (QAbstractListModel roles: trackId int, name string, muted bool, solo bool, gain real dB).
// visibleClips(trackId, firstFrame, lastFrame) returns an array of {clipId int,
// name string, startFrame int, durationFrames int, trackIndex int}.
// editClip(clipId, targetTrackId, startFrame, durationFrames), action(clipId,
// trackId, "split"/"duplicate"/"delete"), setTrackState(row, role, bool),
// setTrackGain(trackId, dB), and trackIdAt(row) returning a track ID are invokables.
// clipsChangedForTrack(trackId) invalidates a lane; standard model notifications
// update track roles. Property notify signals update sample rate, duration and playhead.
// Composes navigation, virtualized tracks and editing; playPauseRequested is wired by the app.
import QtQuick
import "../theme"

Rectangle {
    id: root
    readonly property real contentWidth: timelineModel.durationFrames * pixelsPerFrame
    readonly property real gridFrames: tickSeconds * timelineModel.sampleRate / 4
    readonly property int headerWidth: Theme.headerWidth
    readonly property real laneWidth: Math.max(Theme.lineWidth, width - headerWidth)
    readonly property real pixelsPerFrame: pixelsPerSecond / timelineModel.sampleRate
    property real pixelsPerSecond: Theme.defaultZoom
    property real scrollX: 0
    property int selectedTrackId: -1
    property int bottomInset: 0
    property bool effectsVisible: false
    signal effectsRequested()
    property int selectedClipId: -1
    readonly property real tickSeconds: Math.pow(2, Math.ceil(Math.log(Theme.tickSpacing / pixelsPerSecond) / Math.LN2))
    property var recordingController: null
    property var timelineModel: MockTimelineModel {}
    readonly property int trackHeight: Theme.trackHeight
    property int viewportRevision: 0
    property bool snapEnabled: true
    property real zoomAnchorSeconds: 0
    property real zoomAnchorX: 0
    signal playPauseRequested()
    function clampScroll(value: real): real { return Math.max(0, Math.min(value, Math.max(0, contentWidth - laneWidth))); }
    function zoom(factor: real, cursorX: real): void {
        zoomAnimation.stop();
        let seconds = (scrollX + cursorX) / pixelsPerSecond;
        pixelsPerSecond = Math.max(Theme.minimumZoom, Math.min(Theme.maximumZoom, pixelsPerSecond * factor));
        scrollX = clampScroll(seconds * pixelsPerSecond - cursorX);
    }
    function zoomSmooth(factor: real, cursorX: real): void {
        zoomAnchorSeconds = (scrollX + cursorX) / pixelsPerSecond;
        zoomAnchorX = cursorX;
        const base = zoomAnimation.running ? zoomAnimation.to : pixelsPerSecond;
        const target = Math.max(Theme.minimumZoom, Math.min(Theme.maximumZoom, base * factor));
        zoomAnimation.stop();
        zoomAnimation.from = pixelsPerSecond;
        zoomAnimation.to = target;
        zoomAnimation.start();
    }
    function zoomIn(): void { zoomSmooth(Theme.zoomFactor, laneWidth / 2); }
    function zoomOut(): void { zoomSmooth(1 / Theme.zoomFactor, laneWidth / 2); }
    function zoomToFit(): void {
        zoomAnimation.stop();
        pixelsPerSecond = Math.max(Theme.minimumZoom, Math.min(Theme.maximumZoom, laneWidth * timelineModel.sampleRate / Math.max(1, timelineModel.durationFrames)));
        scrollX = 0;
    }
    clip: true
    color: Theme.background
    focus: true
    activeFocusOnTab: true
    Accessible.name: "Timeline; Space play/pause; Home/End seek; middle mouse or Alt+drag pan; Ctrl+wheel zoom"
    Accessible.role: Accessible.Pane
    onPixelsPerSecondChanged: {
        if (zoomAnimation.running) scrollX = clampScroll(zoomAnchorSeconds * pixelsPerSecond - zoomAnchorX);
        refresh.start();
    }
    onScrollXChanged: refresh.start()
    onWidthChanged: refresh.start()
    NumberAnimation {
        id: zoomAnimation
        target: root; property: "pixelsPerSecond"
        duration: Theme.zoomDuration; easing.type: Easing.OutCubic
    }
    Timer { id: refresh; interval: Theme.viewportDelay; onTriggered: root.viewportRevision++ }
    TimelineEditing { id: editing; timelineState: root; trackCount: viewport.trackCount }
    TimelineKeyboard { editing: editing; onPlayPauseRequested: root.playPauseRequested() }
    TimelineToolbar {
        width: root.width
        effectsVisible: root.effectsVisible
        onEffectsRequested: root.effectsRequested()
        seconds: root.timelineModel.playheadFrame / root.timelineModel.sampleRate
        snapEnabled: root.snapEnabled
        onZoomRequested: factor => root.zoomSmooth(factor, root.laneWidth / 2)
        onFitRequested: root.zoomToFit()
        onSnapRequested: enabled => root.snapEnabled = enabled
    }
    TimelineViewport {
        id: viewport
        objectName: "timeline-viewport"
        y: Theme.toolbarHeight; width: root.width; height: root.height - y - root.bottomInset
        timelineState: root
        onSelected: (clip, trackId, rowIndex) => { root.selectedTrackId = trackId; editing.select(clip, trackId, rowIndex); }
        onCleared: editing.clear()
        onMoved: (clip, rowIndex, startFrame) => editing.move(clip, rowIndex, startFrame)
        onTrimmed: (clip, trackId, leftDelta, rightDelta) => editing.trim(clip, trackId, leftDelta, rightDelta)
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.transparent
        border.color: Theme.accent
        border.width: Theme.focusWidth
        visible: root.activeFocus
    }
}
