// Lightweight deterministic placeholder waveform, not decoded audio. Requires seed; bars are bounded.
pragma ComponentBehavior: Bound
import QtQuick
import "../theme"

Item {
    id: root
    required property int seed
    readonly property int barCount: Math.min(Theme.waveformMaximumBars, Math.max(0, Math.floor(width / Theme.waveformStep)))
    Repeater {
        model: root.barCount
        Rectangle {
            required property int index
            color: Theme.waveform
            opacity: Theme.waveformOpacity
            width: Theme.waveformWidth
            height: Theme.waveformMinimum + ((index * 17 + root.seed * 3) % Theme.waveformHeight)
            x: index * root.width / Math.max(1, root.barCount)
            y: (root.height - height) / 2
        }
    }
}
