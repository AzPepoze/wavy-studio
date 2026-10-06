// Tap tempo: averages the intervals between the last few taps and resets after a pause.
import QtQuick
import "../theme"

ToolButton {
    id: root
    required property var timelineModel
    text: "Tap"
    hint: "Tap tempo; averages the last few taps"
    property var taps: []
    property real lastTap: 0
    Timer {
        id: reset
        interval: Theme.tapTempoWindow
        onTriggered: root.taps = []
    }
    function tap(): void {
        const now = Date.now();
        let taps = now - root.lastTap > Theme.tapTempoWindow ? [] : root.taps.slice();
        taps.push(now);
        const maximumTaps = Theme.tapTempoMaximumIntervals + 1;
        if (taps.length > maximumTaps)
            taps = taps.slice(taps.length - maximumTaps);
        root.taps = taps;
        root.lastTap = now;
        reset.restart();
        if (taps.length < 2)
            return;
        let total = 0;
        for (let i = 1; i < taps.length; ++i)
            total += taps[i] - taps[i - 1];
        const bpm = 60000 / (total / (taps.length - 1));
        root.timelineModel.setTempo(Math.max(Theme.tempoMinimum, Math.min(Theme.tempoMaximum, bpm)));
    }
    onClicked: tap()
}
