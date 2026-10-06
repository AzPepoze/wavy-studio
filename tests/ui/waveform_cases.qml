import QtQuick
import QtTest
import "../../src/ui/timeline"
import "../../src/ui/theme"

// Run by tests/app/waveform_qml_test.cpp, which registers Wavy.Waveform and the bridge. It is named
// without the tst_ prefix so the plain qmltestrunner harness (no C++ types) does not collect it.
Item {
    width: 960
    height: 600

    MockTimelineModel { id: model }
    TimelineView {
        id: view
        anchors.fill: parent
        timelineModel: model
    }

    TestCase {
        name: "Waveform"
        when: windowShown

        function items(node, predicate) {
            let result = predicate(node) ? [node] : [];
            for (let child of node.children || [])
                result = result.concat(items(child, predicate));
            return result;
        }
        function clipItems() { return items(view, item => item instanceof ClipItem); }
        function clipItem(id) { return clipItems().find(item => item.clipData.clipId === id); }
        // grabImage() crops from the window origin rather than the item, so grab the whole view
        // and scan the clip's mapped rectangle.
        function clipRect(id) {
            const item = clipItem(id);
            if (!item) return null;
            const origin = item.mapToItem(view, 0, 0);
            const body = item.mapToItem(view, Theme.space12, Theme.controlHeight);
            const corner = item.mapToItem(view, item.width - Theme.space12, item.height - Theme.space4);
            return { x: body.x, y: body.y, right: corner.x, bottom: corner.y };
        }
        function shot() {
            let image = null;
            for (let attempt = 0; attempt < 20 && image === null; ++attempt) {
                try { image = grabImage(view); } catch (error) { image = null; }
                if (image === null) wait(20);
            }
            return image;
        }
        function spannedColumns(image, rect) {
            if (!image || !rect) return 0;
            let spanned = 0;
            for (let x = rect.x + 2; x < rect.right - 2; x += 2) {
                let top = -1;
                let bottom = -1;
                for (let y = rect.y + 1; y < rect.bottom - 1; ++y) {
                    if (isWaveform(image, x, y)) {
                        if (top < 0) top = y;
                        bottom = y;
                    }
                }
                if (top >= 0 && bottom - top >= 4) ++spanned;
            }
            return spanned;
        }
        function isWaveform(image, x, y) {
            return image.red(x, y) > 150 && image.green(x, y) > 150 && image.blue(x, y) > 150;
        }
        function pixelDiff(a, b, rect) {
            if (!a || !b || !rect) return 1;
            let diff = 0;
            let total = 0;
            for (let y = rect.y + 1; y < rect.bottom - 1; y += 2)
                for (let x = rect.x + 2; x < rect.right - 2; x += 2) {
                    ++total;
                    const delta = Math.abs(a.red(x, y) - b.red(x, y))
                        + Math.abs(a.green(x, y) - b.green(x, y))
                        + Math.abs(a.blue(x, y) - b.blue(x, y));
                    if (delta > 40) ++diff;
                }
            return total ? diff / total : 0;
        }
        function init() {
            model.stress = false;
            model.generate();
            view.pixelsPerSecond = Theme.defaultZoom;
            view.scrollX = 0;
            view.forceActiveFocus();
            wait(Theme.viewportDelay + 1);
        }

        function test_generated_tone_draws_waveform() {
            const clip = model.clips[0][0];
            let spanned = 0;
            tryVerify(() => {
                spanned = spannedColumns(shot(), clipRect(clip.clipId));
                return spanned > 10;
            }, 8000);
            verify(spanned > 10);
        }

        function test_trimmed_clip_shows_a_different_part() {
            // A low-frequency square has a phase-dependent envelope at this zoom, unlike a tone.
            const clip = model.clips[0][0];
            clip.source = "generated:square:8:0.2:4.8";
            model.clipsChangedForTrack(0);
            wait(Theme.viewportDelay + 1);
            const rect = clipRect(clip.clipId);
            let before = null;
            tryVerify(() => {
                before = shot();
                return before !== null && spannedColumns(before, rect) > 5;
            }, 8000);
            clip.sourceOffset = 12345;
            model.clipsChangedForTrack(0);
            let after = null;
            tryVerify(() => {
                after = shot();
                return after !== null && pixelDiff(before, after, rect) > 0.05;
            }, 5000);
        }

        function test_missing_source_shows_placeholder() {
            const clip = model.clips[0][0];
            clip.source = "/nonexistent/wavy-missing-source.wav";
            model.clipsChangedForTrack(0);
            wait(Theme.viewportDelay + 1);
            wait(300);
            compare(spannedColumns(shot(), clipRect(clip.clipId)), 0);
        }
    }
}
