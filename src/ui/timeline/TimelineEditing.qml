// Timeline edit coordinator. Requires view timelineState and track count; owns selection metadata and keyboard actions.
import QtQuick

QtObject {
    id: root
    required property var timelineState
    required property int trackCount
    property var selectedClip: null
    property int selectedTrackId: -1
    property int selectedRow: -1
    function select(clip: var, trackId: int, row: int): void {
        selectedClip = clip; selectedTrackId = trackId; selectedRow = row;
        timelineState.selectedClipId = clip.clipId;
    }
    function clear(): void { selectedClip = null; timelineState.selectedClipId = -1; timelineState.forceActiveFocus(); }
    function refreshSelection(): void {
        if (!selectedClip || timelineState.selectedClipId < 0) return;
        let clips = timelineState.timelineModel.visibleClips(selectedTrackId, 0, timelineState.timelineModel.durationFrames);
        selectedClip = clips.find(clip => clip.clipId === timelineState.selectedClipId) || null;
        if (!selectedClip) clear();
    }
    function action(operation: string): void {
        refreshSelection();
        if (timelineState.selectedClipId < 0 || !selectedClip) return;
        timelineState.timelineModel.action(timelineState.selectedClipId, selectedTrackId, operation);
        if (operation === "delete") clear();
    }
    function move(clip: var, targetRow: int, startFrame: real): void {
        let target = Math.max(0, Math.min(trackCount - 1, targetRow));
        let start = Math.max(0, startFrame);
        if (timelineState.snapEnabled) start = Math.round(start / timelineState.gridFrames) * timelineState.gridFrames;
        timelineState.timelineModel.editClip(clip.clipId, timelineState.timelineModel.trackIdAt(target), start, clip.durationFrames);
        if (timelineState.selectedClipId === clip.clipId) {
            selectedRow = target; selectedTrackId = timelineState.timelineModel.trackIdAt(target);
            selectedClip = Object.assign({}, clip, {startFrame: start});
        }
    }
    function trim(clip: var, trackId: int, left: real, right: real): void {
        let minimum = timelineState.snapEnabled ? timelineState.gridFrames : 1;
        let start = Math.max(0, Math.min(clip.startFrame + clip.durationFrames - minimum, clip.startFrame + left));
        let duration = Math.max(minimum, clip.durationFrames + clip.startFrame - start + right);
        timelineState.timelineModel.editClip(clip.clipId, trackId, start, duration);
        if (timelineState.selectedClipId === clip.clipId) selectedClip = Object.assign({}, clip, {startFrame: start, durationFrames: duration});
    }
    function handle(event: var): void {
        refreshSelection();
        event.accepted = true;
        if (event.key === Qt.Key_Space) timelineState.playPauseRequested();
        else if (event.key === Qt.Key_Delete) action("delete");
        else if (event.key === Qt.Key_D && event.modifiers & Qt.ControlModifier) action("duplicate");
        else if (event.key === Qt.Key_S) action("split");
        else if (event.key === Qt.Key_Home) timelineState.timelineModel.playheadFrame = 0;
        else if (event.key === Qt.Key_End) timelineState.timelineModel.playheadFrame = timelineState.timelineModel.durationFrames;
        else if (event.key === Qt.Key_Escape) clear();
        else if (event.key === Qt.Key_0 && event.modifiers & Qt.ControlModifier) timelineState.zoomToFit();
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) timelineState.zoomIn();
        else if (event.key === Qt.Key_Minus) timelineState.zoomOut();
        else if (selectedClip && timelineState.selectedClipId >= 0 && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].includes(event.key)) {
            let horizontal = event.key === Qt.Key_Left ? -timelineState.gridFrames : event.key === Qt.Key_Right ? timelineState.gridFrames : 0;
            let vertical = event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_Down ? 1 : 0;
            move(selectedClip, selectedRow + vertical, selectedClip.startFrame + horizontal);
        } else event.accepted = false;
    }
}
