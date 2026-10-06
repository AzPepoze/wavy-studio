import QtQuick
import QtTest
import "../../src/ui/timeline"
import "../../src/ui/theme"

Item {
    width: 960
    height: 600

    TimelineView {
        id: view
        anchors.fill: parent
        timelineModel: MockTimelineModel { id: model }
    }

    TestCase {
        name: "Timeline"
        when: windowShown

        function items(node, predicate) {
            let result = predicate(node) ? [node] : [];
            for (let child of node.children || [])
                result = result.concat(items(child, predicate));
            return result;
        }
        function clipItems() { return items(view, item => item instanceof ClipItem); }
        function clipItem(id) { return clipItems().find(item => item.clipData.clipId === id); }
        function clipData(track, id) { return model.clips[track].find(item => item.clipId === id); }
        function select(id) {
            tryVerify(() => clipItem(id) !== undefined);
            let item = clipItem(id);
            mouseClick(item, item.width / 2, Theme.space12);
            compare(view.selectedClipId, id);
        }
        function drag(item, x, dx, dy) {
            let point = item.mapToItem(view, x, Theme.space12);
            mousePress(view, point.x, point.y);
            mouseMove(view, point.x + dx, point.y + dy);
            mouseRelease(view, point.x + dx, point.y + dy);
        }
        function init() {
            model.stress = false;
            model.generate();
            model.playheadFrame = 0;
            view.pixelsPerSecond = Theme.defaultZoom;
            view.scrollX = 0;
            view.snapEnabled = true;
            keyClick(Qt.Key_Escape);
            view.forceActiveFocus();
            tryVerify(() => clipItem(0) !== undefined && clipItem(0).clipData.startFrame === 0 && clipItem(0).clipData.durationFrames === model.sampleRate * 2.4);
            wait(Theme.viewportDelay + 1);
        }

        function test_select_track_header() {
            let header = items(view, item => item instanceof TrackHeader)[0];
            verify(header !== undefined);
            mouseClick(header, Theme.space12, Theme.space12);
            compare(view.selectedTrackId, model.trackIdAt(0));
            view.selectedTrackId = -1;
            header.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(view.selectedTrackId, model.trackIdAt(0));
            verify(header.selected);
        }
        function test_select_delete() {
            select(0);
            keyClick(Qt.Key_Delete);
            compare(model.clips[0].length, 2);
            verify(clipData(0, 0) === undefined);
            compare(view.selectedClipId, -1);
            tryVerify(() => clipItem(0) === undefined);
        }
        function test_duplicate() {
            select(0);
            let original = Object.assign({}, clipData(0, 0));
            let id = model.nextId;
            keyClick(Qt.Key_D, Qt.ControlModifier);
            compare(model.clips[0].length, 4);
            compare(clipData(0, id).startFrame, original.startFrame + original.durationFrames);
            compare(clipData(0, id).durationFrames, original.durationFrames);
        }
        function test_split() {
            select(0);
            let duration = clipData(0, 0).durationFrames;
            model.playheadFrame = duration / 2;
            let id = model.nextId;
            keyClick(Qt.Key_S);
            compare(model.clips[0].length, 4);
            compare(clipData(0, 0).durationFrames, duration / 2);
            compare(clipData(0, id).startFrame, model.playheadFrame);
            compare(clipData(0, id).durationFrames, duration / 2);
        }
        function test_cross_track_drag() {
            let duration = clipData(0, 0).durationFrames;
            drag(clipItem(0), clipItem(0).width / 2, Theme.defaultZoom, Theme.trackHeight);
            verify(clipData(0, 0) === undefined);
            compare(clipData(1, 0).startFrame, model.sampleRate);
            compare(clipData(1, 0).durationFrames, duration);
            compare(view.selectedClipId, 0);
            tryVerify(() => clipItem(0) !== undefined);
        }
        function test_trim_edges() {
            let duration = clipData(0, 0).durationFrames;
            drag(clipItem(0), clipItem(0).width - Theme.space4, Theme.defaultZoom, 0);
            compare(clipData(0, 0).durationFrames, duration + model.sampleRate);
            tryVerify(() => clipItem(0).width === clipData(0, 0).durationFrames * view.pixelsPerFrame);
            drag(clipItem(0), Theme.space4, Theme.defaultZoom, 0);
            compare(clipData(0, 0).startFrame, model.sampleRate);
            compare(clipData(0, 0).durationFrames, duration);
        }
        function test_nudge() {
            select(0);
            keyClick(Qt.Key_Right);
            compare(clipData(0, 0).startFrame, view.gridFrames);
            keyClick(Qt.Key_Down);
            verify(clipData(0, 0) === undefined);
            compare(clipData(1, 0).startFrame, view.gridFrames);
            keyClick(Qt.Key_Up);
            compare(clipData(0, 0).startFrame, view.gridFrames);
            keyClick(Qt.Key_Left);
            compare(clipData(0, 0).startFrame, 0);
            keyClick(Qt.Key_Left);
            compare(clipData(0, 0).startFrame, 0);
        }
        function test_culling() {
            model.stress = true;
            compare(model.tracks.count, 100);
            compare(model.nextId, 20000);
            tryVerify(() => clipItems().length > 0 && clipItems().length < 100);
            view.scrollX = 300 * view.pixelsPerSecond;
            let revision = view.viewportRevision;
            tryVerify(() => view.viewportRevision > revision);
            tryVerify(() => clipItems().length > 0 && clipItems().every(item => {
                let c = item.clipData;
                return c.startFrame * view.pixelsPerFrame < view.scrollX + view.laneWidth + Theme.cullMargin
                    && (c.startFrame + c.durationFrames) * view.pixelsPerFrame > view.scrollX - Theme.cullMargin;
            }));
            verify(clipItems().length < 100);
        }
        function test_scroll_clamps() {
            mouseWheel(view, 500, Theme.toolbarHeight + Theme.space12, 0, 120, Qt.NoButton, Qt.ShiftModifier);
            compare(view.scrollX, 0);
            view.scrollX = view.contentWidth - view.laneWidth - 60;
            mouseWheel(view, 500, Theme.toolbarHeight + Theme.space12, 0, -120, Qt.NoButton, Qt.ShiftModifier);
            tryCompare(view, "scrollX", view.contentWidth - view.laneWidth);
            mouseWheel(view, 500, Theme.toolbarHeight + Theme.space12, 0, 120000, Qt.NoButton, Qt.ShiftModifier);
            compare(view.scrollX, 0);
            let point = Qt.point(600, 400);
            mousePress(view, point.x, point.y, Qt.MiddleButton);
            mouseMove(view, point.x - 100, point.y);
            mouseRelease(view, point.x - 100, point.y, Qt.MiddleButton);
            compare(view.scrollX, 100);
        }
        function test_zoom_anchor_clamps() {
            view.scrollX = 300;
            let cursor = 200;
            let seconds = (view.scrollX + cursor) / view.pixelsPerSecond;
            mouseWheel(view, Theme.headerWidth + cursor, Theme.toolbarHeight + Theme.space12, 0, 120, Qt.NoButton, Qt.ControlModifier);
            tryVerify(() => view.pixelsPerSecond > Theme.defaultZoom);
            fuzzyCompare((view.scrollX + cursor) / view.pixelsPerSecond, seconds, 0.000001);
            view.zoom(100000, cursor);
            compare(view.pixelsPerSecond, Theme.maximumZoom);
            while (view.pixelsPerSecond > Theme.minimumZoom)
                view.zoom(1 / Theme.zoomFactor, cursor);
            view.zoom(0.5, cursor);
            compare(view.pixelsPerSecond, Theme.minimumZoom);
            compare(view.scrollX, 0);
        }
        function test_zoom_to_fit() {
            view.scrollX = 500;
            keyClick(Qt.Key_0, Qt.ControlModifier);
            compare(view.scrollX, 0);
            fuzzyCompare(view.contentWidth, view.laneWidth, 0.000001);
        }
        function test_snap_toggle() {
            let button = items(view, item => item.text === "Snap")[0];
            verify(button !== undefined);
            mouseClick(button, button.width / 2, button.height / 2);
            compare(view.snapEnabled, false);
            drag(clipItem(0), clipItem(0).width / 2, 17, 0);
            compare(clipData(0, 0).startFrame, Math.round(17 / view.pixelsPerFrame));
            mouseClick(button, button.width / 2, button.height / 2);
            compare(view.snapEnabled, true);
        }
    }
}
