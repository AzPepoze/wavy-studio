// Polls controller's atomic compressor meter at 30 Hz while visible.
import QtQuick
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: root
    required property var controller
    required property int trackId
    required property int slotIndex
    property bool active: true
    property real reduction: 0
    spacing: Theme.space8
    Text { text: "Reduction " + root.reduction.toFixed(1) + " dB"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.space8; color: Theme.border; radius: Theme.controlRadius
        Rectangle { width: parent.width * Math.min(1, root.reduction / 24); height: parent.height; color: Theme.accent; radius: Theme.controlRadius }
    }
    Timer { interval: 1000 / 30; repeat: true; running: root.active && root.visible; onTriggered: root.reduction = root.controller.gainReduction(root.trackId, root.slotIndex) }
}
