import QtQuick
import QtTest
import "../../src/ui/timeline"
import "../../src/ui/theme"

Item {
    id: harness
    width: 960
    height: 600

    TimelineView {
        id: view
        objectName: "timeline-view"
        anchors.fill: parent
        timelineModel: MockTimelineModel { id: model }
    }

    TempoControl {
        id: tempo
        x: 0
        y: 0
        width: Theme.tempoFieldWidth
        height: Theme.controlHeight
        timelineModel: model
    }
    TapTempoButton {
        id: tap
        x: 200
        y: 0
        timelineModel: model
    }
    TimeSignatureControl {
        id: signature
        x: 300
        y: 0
        timelineModel: model
    }
    SnapSettings { id: snapPopup; settings: view.settings }

    TestCase {
        name: "SnapTempo"
        when: windowShown

        function items(node, predicate) {
            let result = predicate(node) ? [node] : [];
            for (let child of node.children || [])
                result = result.concat(items(child, predicate));
            return result;
        }
        function ruler() { return items(view, item => item instanceof TimeRuler)[0]; }
        function rulerTexts(predicate) { return items(ruler(), item => item instanceof Text && predicate(item)); }
        function typeText(text) {
            const keys = { "0": Qt.Key_0, "1": Qt.Key_1, "4": Qt.Key_4, "5": Qt.Key_5, "9": Qt.Key_9 };
            for (const character of text)
                keyClick(keys[character]);
        }
        function init() {
            model.stress = false;
            model.generate();
            model.tempoBpm = 120;
            model.timeSignatureNumerator = 4;
            model.timeSignatureDenominator = 4;
            model.playheadFrame = 0;
            view.pixelsPerSecond = Theme.defaultZoom;
            view.scrollX = 0;
            view.settings.snapEnabled = true;
            view.settings.snapDivision = "auto";
            view.settings.snapToClipEdges = true;
            view.settings.snapToPlayhead = true;
            view.settings.snapTolerancePixels = 8;
            view.settings.rulerMode = "barsBeats";
            tap.taps = [];
            tap.lastTap = 0;
            wait(Theme.viewportDelay + 1);
        }

        function test_auto_snap_gets_finer_when_zoomed_in() {
            view.pixelsPerSecond = 90;
            const coarse = view.gridFrames;
            view.pixelsPerSecond = 900;
            const fine = view.gridFrames;
            verify(fine < coarse);
            view.pixelsPerSecond = Theme.maximumZoom;
            verify(view.gridFrames < fine);
            // At the zoom ceiling Auto reaches its finest candidate, a 1/64 beat.
            fuzzyCompare(view.gridFrames, view.framesPerBeat / 64, 0.001);
        }

        function test_auto_snap_gets_coarser_when_zoomed_out() {
            view.pixelsPerSecond = 90;
            const near = view.gridFrames;
            view.pixelsPerSecond = 1;
            verify(view.gridFrames > near);
            verify(view.gridFrames > view.framesPerBeat);
        }

        function test_fixed_quarter_tracks_tempo() {
            view.settings.snapDivision = "1/4";
            model.tempoBpm = 120;
            compare(view.gridFrames, 6000);
            compare(view.snapFrame(5999, {}), 6000);
            model.tempoBpm = 90;
            compare(view.gridFrames, 8000);
            compare(view.snapFrame(7999, {}), 8000);
        }

        function test_clip_edge_magnet() {
            view.settings.snapDivision = "1/4";
            view.settings.snapToPlayhead = false;
            view.settings.snapToClipEdges = true;
            compare(view.snapFrame(144000 - 100, { clipEdges: [144000] }), 144000);
            // Outside the magnet tolerance the grid wins.
            verify(view.snapFrame(144000 - 9000, { clipEdges: [144000] }) !== 144000);
        }

        function test_playhead_magnet() {
            view.settings.snapDivision = "1/4";
            view.settings.snapToClipEdges = false;
            view.settings.snapToPlayhead = true;
            model.playheadFrame = 100000;
            compare(view.snapFrame(100150, { clipEdges: [] }), 100000);
            verify(view.snapFrame(100150, { clipEdges: [], playhead: false }) !== 100000);
        }

        function test_alt_disables_snapping() {
            view.settings.snapDivision = "1/4";
            view.settings.snapToPlayhead = false;
            view.settings.snapToClipEdges = false;
            compare(view.snapFrame(1234, {}), 0);
            compare(view.snapFrame(1234, { alt: true }), 1234);
            // Ruler and nudge pass the raw Qt modifier flag.
            compare(view.snapFrame(1234, { alt: Qt.AltModifier }), 1234);
            compare(view.snapFrame(1234, { alt: Qt.NoModifier }), 0);
        }

        function test_ruler_bars_beats_120_four_four() {
            model.tempoBpm = 120;
            view.settings.rulerMode = "barsBeats";
            const labels = rulerTexts(item => /^\d+$/.test(item.text));
            const numbers = labels.map(item => Number(item.text));
            verify(numbers.includes(1) && numbers.includes(2) && numbers.includes(3));
            // A bar is 96000 frames = 180 px at the default zoom.
            const second = labels.find(item => item.text === "2");
            verify(second !== undefined);
            fuzzyCompare(second.x - Theme.space4, 180, 0.5);
        }

        function test_ruler_bars_beats_120_three_four() {
            model.tempoBpm = 120;
            model.timeSignatureNumerator = 3;
            model.timeSignatureDenominator = 4;
            view.settings.rulerMode = "barsBeats";
            const labels = rulerTexts(item => /^\d+$/.test(item.text));
            const numbers = labels.map(item => Number(item.text));
            verify(numbers.includes(1) && numbers.includes(2) && numbers.includes(3));
            const second = labels.find(item => item.text === "2");
            verify(second !== undefined);
            // A bar is 3 beats = 72000 frames = 135 px.
            fuzzyCompare(second.x - Theme.space4, 135, 0.5);
        }

        function test_ruler_time_mode() {
            view.settings.rulerMode = "time";
            const labels = rulerTexts(item => /\d\d:\d\d\.\d\d\d/.test(item.text));
            verify(labels.length > 0);
        }

        function test_ruler_mode_toggle() {
            const toggle = items(view, item => item.objectName === "ruler-mode")[0];
            verify(toggle !== undefined);
            view.settings.rulerMode = "barsBeats";
            mouseClick(toggle);
            compare(view.settings.rulerMode, "time");
            mouseClick(toggle);
            compare(view.settings.rulerMode, "barsBeats");
        }

        function test_tempo_drag_and_wheel() {
            model.tempoBpm = 120;
            mousePress(tempo, tempo.width / 2, tempo.height / 2);
            mouseMove(tempo, tempo.width / 2, tempo.height / 2 - 10);
            mouseRelease(tempo, tempo.width / 2, tempo.height / 2 - 10);
            tryCompare(model, "tempoBpm", 130);
            mouseWheel(tempo, tempo.width / 2, tempo.height / 2, 0, 120, Qt.NoButton, Qt.ShiftModifier);
            tryCompare(model, "tempoBpm", 130.1);
        }

        function test_tempo_keyboard_arrows_beat_nudge_shortcuts() {
            model.tempoBpm = 120;
            tempo.forceActiveFocus();
            keyClick(Qt.Key_Up);
            tryCompare(model, "tempoBpm", 121);
            keyClick(Qt.Key_Down);
            tryCompare(model, "tempoBpm", 120);
        }

        function test_time_signature_control_updates_ruler() {
            signature.choose(3, 4);
            compare(model.timeSignatureNumerator, 3);
            compare(model.timeSignatureDenominator, 4);
            compare(view.beatsPerBar, 3);
            signature.choose(5, 8);
            compare(model.timeSignatureNumerator, 5);
            compare(model.timeSignatureDenominator, 8);
            fuzzyCompare(view.beatsPerBar, 5 * 4 / 8, 0.0001);
        }

        function test_tempo_double_click_starts_typing() {            model.tempoBpm = 120;
            mouseDoubleClickSequence(tempo, tempo.width / 2, tempo.height / 2);
            verify(tempo.typing);
            keyClick(Qt.Key_Escape);
            verify(!tempo.typing);
        }

        function test_tempo_typed_entry_commit_and_escape() {
            model.tempoBpm = 120;
            tempo.beginTyping();
            typeText("140");
            keyClick(Qt.Key_Return);
            tryCompare(model, "tempoBpm", 140);
            verify(!tempo.typing);
            tempo.beginTyping();
            typeText("90");
            keyClick(Qt.Key_Escape);
            tryCompare(model, "tempoBpm", 140);
            verify(!tempo.typing);
        }

        function test_tempo_clamps_typed_values() {
            model.tempoBpm = 120;
            tempo.beginTyping();
            typeText("5000");
            keyClick(Qt.Key_Return);
            tryCompare(model, "tempoBpm", Theme.tempoMaximum);
            tempo.beginTyping();
            typeText("5");
            keyClick(Qt.Key_Return);
            tryCompare(model, "tempoBpm", Theme.tempoMinimum);
        }

        function test_tap_tempo_averages_intervals() {
            model.tempoBpm = 60;
            tap.tap();
            wait(400);
            tap.tap();
            wait(400);
            tap.tap();
            tryVerify(() => Math.abs(model.tempoBpm - 150) < 25, 2000);
            compare(tap.taps.length >= 3, true);
        }

        function test_zz_settings_popup_changes_behaviour() {
            view.settings.snapDivision = "auto";
            view.settings.snapEnabled = true;
            const gridBefore = view.gridFrames;
            snapPopup.open();
            tryVerify(() => snapPopup.contentItem !== null);
            const division = items(snapPopup.contentItem, item => item.text === "1/16")[0];
            verify(division !== undefined);
            mouseClick(division);
            compare(view.settings.snapDivision, "1/16");
            verify(view.gridFrames !== gridBefore);
            const snapping = items(snapPopup.contentItem, item => item.text === "Snapping")[0];
            verify(snapping !== undefined);
            mouseClick(snapping);
            compare(view.settings.snapEnabled, false);
            compare(view.snapEnabled, false);
            // With snapping off the snap path only rounds.
            compare(view.snapFrame(1234, {}), 1234);
            snapPopup.close();
        }

        function test_toolbar_reports_snap_step_and_gear() {
            view.settings.snapDivision = "1/4";
            tryVerify(() => items(view, item => item.text === "1/4").length > 0);
            const gear = items(view, item => item.objectName === "snap-settings")[0];
            verify(gear !== undefined);
        }
    }
}
