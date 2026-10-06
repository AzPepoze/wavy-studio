// One virtualized track. Queries clips only on viewport revision or track edits; forwards lane/header actions.
import QtQuick
import "../theme"

Item {
    id: root
    property var recordingController: null
    required property var timelineModel
    required property int trackId
    required property int rowIndex
    required property string name
    required property bool muted
    required property bool solo
    required property real scrollX
    required property real laneWidth
    required property real pixelsPerFrame
    required property real pixelsPerSecond
    required property real gridFrames
    required property real tickSeconds
    required property int selectedClipId
    required property bool snapEnabled
    required property int viewportRevision
    property bool trackSelected: false
    signal trackSelectedRequested()
    property var visibleClips: []
    signal selected(var clip, int trackId, int rowIndex)
    signal cleared()
    signal moved(var clip, int rowIndex, real deltaFrames, real deltaY)
    signal trimmed(var clip, int trackId, real leftDelta, real rightDelta)
    signal dragPreview(real frame, bool active)
    function updateClips(force: bool): void {
        let next = timelineModel.visibleClips(trackId, (scrollX - Theme.cullMargin) / pixelsPerFrame, (scrollX + laneWidth + Theme.cullMargin) / pixelsPerFrame);
        if (!force && next.length === visibleClips.length && next.every((clip, i) => clip.clipId === visibleClips[i].clipId)) return;
        visibleClips = next;
    }
    height: Theme.trackHeight
    Component.onCompleted: updateClips(true)
    onViewportRevisionChanged: updateClips(false)
    onTimelineModelChanged: updateClips(true)
    Connections {
        target: root.timelineModel
        function onClipsChangedForTrack(trackId: int): void { if (trackId === root.trackId) root.updateClips(true); }
    }
    TrackHeader {
        recordingController: root.recordingController; trackId: root.trackId
        selected: root.trackSelected
        onSelectedRequested: root.trackSelectedRequested()
        name: root.name; muted: root.muted; solo: root.solo
        width: Theme.headerWidth; height: root.height - Theme.lineWidth
        onStateRequested: (role, value) => root.timelineModel.setTrackState(root.rowIndex, role, value)
    }
    TrackLane {
        clips: root.visibleClips; scrollX: root.scrollX; pixelsPerFrame: root.pixelsPerFrame
        pixelsPerSecond: root.pixelsPerSecond; gridFrames: root.gridFrames; tickSeconds: root.tickSeconds
        selectedClipId: root.selectedClipId; alternate: root.rowIndex % 2 !== 0; snapEnabled: root.snapEnabled
        x: Theme.headerWidth; width: root.laneWidth; height: root.height - Theme.lineWidth
        onSelected: clip => root.selected(clip, root.trackId, root.rowIndex)
        onCleared: root.cleared()
        onMoved: (clip, deltaFrames, deltaY) => root.moved(clip, root.rowIndex, deltaFrames, deltaY)
        onTrimmed: (clip, leftDelta, rightDelta) => root.trimmed(clip, root.trackId, leftDelta, rightDelta)
        onRequested: (clip, operation) => root.timelineModel.action(clip.clipId, root.trackId, operation)
        onDragPreview: (frame, active) => root.dragPreview(frame, active)
    }
}
