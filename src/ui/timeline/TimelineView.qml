// Model contract: sampleRate, durationFrames and playheadFrame (int frames), tracks
// (QAbstractListModel roles: trackId int, name string, muted bool, solo bool, gain real dB).
// visibleClips(trackId, firstFrame, lastFrame) returns an array of {clipId int,
// name string, startFrame int, durationFrames int, trackIndex int}.
// editClip(clipId, targetTrackId, startFrame, durationFrames), action(clipId,
// trackId, "split"/"duplicate"/"delete"), setTrackState(row, role, bool),
// setTrackGain(trackId, dB), and trackIdAt(row) returning a track ID are invokables.
// clipsChangedForTrack(trackId) invalidates a lane; standard model notifications
// update track roles. Property notify signals update sample rate, duration, playhead,
// tempoBpm, timeSignatureNumerator/Denominator and framesPerBeat.
// Composes navigation, virtualized tracks and editing; playPauseRequested is wired by the app.
import QtQuick
import "../theme"
import "SnapMath.js" as SnapMath

Rectangle {
    id: root
    property var settings: TimelineSettings {}
    property bool snapEnabled: true
    readonly property real contentWidth: timelineModel.durationFrames * pixelsPerFrame
    readonly property int headerWidth: Theme.headerWidth
    readonly property real laneWidth: Math.max(Theme.lineWidth, width - headerWidth)
    readonly property real beatsPerMinute: timelineModel.tempoBpm !== undefined ? timelineModel.tempoBpm : 120
    readonly property real framesPerBeat: timelineModel.framesPerBeat !== undefined
                                          ? timelineModel.framesPerBeat
                                          : timelineModel.sampleRate * 60 / beatsPerMinute
    readonly property int signatureNumerator: timelineModel.timeSignatureNumerator !== undefined ? timelineModel.timeSignatureNumerator : 4
    readonly property int signatureDenominator: timelineModel.timeSignatureDenominator !== undefined ? timelineModel.timeSignatureDenominator : 4
    readonly property real beatsPerBar: signatureNumerator * 4 / signatureDenominator
    readonly property real framesPerBar: framesPerBeat * beatsPerBar
    readonly property real gridFrames: Math.max(1, Math.round(SnapMath.stepFrames(settings.snapDivision, beatsPerBar, framesPerBeat, pixelsPerFrame, Theme.snapMinimumSpacing)))
    readonly property string snapStepLabel: SnapMath.stepLabel(settings.snapDivision, gridFrames / framesPerBeat, beatsPerBar)
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
    property real zoomAnchorSeconds: 0
    property real zoomAnchorX: 0
    signal playPauseRequested()
    function applySettings(): void {
        root.snapEnabled = root.settings.snapEnabled;
    }
    // Public entry to the single snap path for callers outside the editor (and tests).
    function snapFrame(frame: real, options: var): real {
        return editing.snapFrame(frame, options);
    }
    onSettingsChanged: applySettings()
    Component.onCompleted: applySettings()
    onSnapEnabledChanged: root.settings.snapEnabled = root.snapEnabled
    Connections {
        target: root.settings
        function onChanged() { root.applySettings(); }
    }
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
        playheadFrame: root.timelineModel.playheadFrame
        sampleRate: root.timelineModel.sampleRate
        settings: root.settings
        beatsPerMinute: root.beatsPerMinute
        framesPerBeat: root.framesPerBeat
        beatsPerBar: root.beatsPerBar
        timeSignatureText: root.signatureNumerator + "/" + root.signatureDenominator
        snapStepLabel: root.snapStepLabel
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
        editing: editing
        onSelected: (clip, trackId, rowIndex) => { root.selectedTrackId = trackId; editing.select(clip, trackId, rowIndex); }
        onCleared: editing.clear()
        onMoved: (clip, rowIndex, startFrame, alt) => editing.move(clip, rowIndex, startFrame, alt)
        onTrimmed: (clip, trackId, leftDelta, rightDelta) => editing.trim(clip, trackId, leftDelta, rightDelta, false)
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.transparent
        border.color: Theme.accent
        border.width: Theme.focusWidth
        visible: root.activeFocus
    }
}
