// Timeline edit coordinator. Requires view timelineState and track count; owns selection metadata,
// the single tempo-aware snap function and keyboard actions.
import QtQuick
import "../theme"

QtObject {
    id: root
    required property var timelineState
    required property int trackCount
    property var selectedClip: null
    property int selectedTrackId: -1
    property int selectedRow: -1

    // Every snap path (clip move, trim, split and playhead scrub) funnels through here so changing
    // the tempo moves the grid and the magnet targets together.
    // options: { enabled, alt, gridFrames, pixelsPerFrame, tolerancePixels, playhead, clipEdges }.
    function snapFrame(frame: real, options: var): real {
        const opts = options || ({});
        const settings = root.timelineState.settings;
        const alt = root.altHeld(opts.alt);
        const enabled = opts.enabled !== undefined ? opts.enabled : (settings.snapEnabled && !alt);
        if (!enabled)
            return Math.round(frame);
        const pixelsPerFrame = opts.pixelsPerFrame !== undefined ? opts.pixelsPerFrame
                                                                 : root.timelineState.pixelsPerFrame;
        const tolerancePixels = opts.tolerancePixels !== undefined ? opts.tolerancePixels
                                                                   : settings.snapTolerancePixels;
        const tolerance = pixelsPerFrame > 0 ? tolerancePixels / pixelsPerFrame : 0;
        let best = NaN;
        let bestDistance = tolerance;
        if (settings.snapToPlayhead && opts.playhead !== false) {
            const playhead = root.timelineState.timelineModel.playheadFrame;
            const distance = Math.abs(playhead - frame);
            if (distance <= bestDistance) {
                best = playhead;
                bestDistance = distance;
            }
        }
        if (settings.snapToClipEdges && opts.clipEdges) {
            for (const edge of opts.clipEdges) {
                const distance = Math.abs(edge - frame);
                if (distance <= bestDistance) {
                    best = edge;
                    bestDistance = distance;
                }
            }
        }
        if (!isNaN(best))
            return Math.round(best);
        const step = opts.gridFrames !== undefined ? opts.gridFrames : root.timelineState.gridFrames;
        if (!(step > 0))
            return Math.round(frame);
        return Math.round(frame / step) * step;
    }

    // Edges of every clip currently in view except the given one, for the clip-edge magnet.
    function clipEdges(excludeClipId: int): var {
        const state = root.timelineState;
        const model = state.timelineModel;
        const first = (state.scrollX - Theme.cullMargin) / state.pixelsPerFrame;
        const last = (state.scrollX + state.laneWidth + Theme.cullMargin) / state.pixelsPerFrame;
        const edges = [];
        for (let row = 0; row < root.trackCount; ++row) {
            const clips = model.visibleClips(model.trackIdAt(row), first, last);
            for (const clip of clips) {
                if (clip.clipId === excludeClipId)
                    continue;
                edges.push(clip.startFrame);
                edges.push(clip.startFrame + clip.durationFrames);
            }
        }
        return edges;
    }

    // Alt arrives either as a bool (viewport state) or a Qt modifier flag (mouse/key events).
    function altHeld(value: var): bool {
        return value === true || (typeof value === "number" && (value & Qt.AltModifier) !== 0);
    }
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
        if (operation === "split")
            timelineState.timelineModel.playheadFrame =
                snapFrame(timelineState.timelineModel.playheadFrame, { playhead: false, clipEdges: [] });
        timelineState.timelineModel.action(timelineState.selectedClipId, selectedTrackId, operation);
        if (operation === "delete") clear();
    }
    function move(clip: var, targetRow: int, startFrame: real, altModifier: var): void {
        let target = Math.max(0, Math.min(trackCount - 1, targetRow));
        const edges = timelineState.settings.snapToClipEdges ? clipEdges(clip.clipId) : [];
        let start = Math.max(0, snapFrame(startFrame, { alt: altModifier, clipEdges: edges }));
        timelineState.timelineModel.editClip(clip.clipId, timelineState.timelineModel.trackIdAt(target), start, clip.durationFrames);
        if (timelineState.selectedClipId === clip.clipId) {
            selectedRow = target; selectedTrackId = timelineState.timelineModel.trackIdAt(target);
            selectedClip = Object.assign({}, clip, {startFrame: start});
        }
    }
    function trim(clip: var, trackId: int, left: real, right: real, altModifier: var): void {
        const alt = root.altHeld(altModifier);
        const enabled = timelineState.settings.snapEnabled && !alt;
        const minimum = enabled ? timelineState.gridFrames : 1;
        const edges = timelineState.settings.snapToClipEdges ? clipEdges(clip.clipId) : [];
        const options = { alt: alt, clipEdges: edges };
        let start = clip.startFrame;
        let end = clip.startFrame + clip.durationFrames;
        if (left !== 0)
            start = snapFrame(start + left, options);
        if (right !== 0)
            end = snapFrame(end + right, options);
        start = Math.max(0, Math.min(end - minimum, start));
        let duration = Math.max(minimum, end - start);
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
            move(selectedClip, selectedRow + vertical, selectedClip.startFrame + horizontal, event.modifiers & Qt.AltModifier);
        } else event.accepted = false;
    }
}
