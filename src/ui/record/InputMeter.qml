// Horizontal input meter; level is 0..1, peak is held for Theme.peakHoldDuration.
import QtQuick
import "../theme"

Rectangle {
    id: root
    property real level: 0
    property real heldPeak: 0
    implicitWidth: Theme.meterWidth
    implicitHeight: Theme.meterHeight
    color: Theme.background
    Accessible.role: Accessible.ProgressBar
    Accessible.name: "Input level"
    Accessible.description: Math.round(level * 100) + " percent"
    onLevelChanged: {
        if (level >= heldPeak) {
            heldPeak = level;
            hold.restart();
        }
    }
    Timer { id: hold; interval: Theme.peakHoldDuration; onTriggered: root.heldPeak = root.level }
    Row {
        width: root.width * Math.max(0, Math.min(1, root.level))
        height: root.height
        clip: true
        Rectangle { width: root.width * 0.7; height: root.height; color: Theme.meterGreen }
        Rectangle { width: root.width * 0.2; height: root.height; color: Theme.meterAmber }
        Rectangle { width: root.width * 0.1; height: root.height; color: Theme.record }
    }
    Rectangle {
        x: Math.min(root.width - width, root.heldPeak * root.width)
        width: Theme.lineWidth
        height: root.height
        color: Theme.textPrimary
        visible: root.heldPeak > 0
    }
}
