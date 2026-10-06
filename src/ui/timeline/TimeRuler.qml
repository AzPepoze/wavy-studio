// Visible time ticks and draggable playhead target. Requires viewport scale; emits frameRequested.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Rectangle {
    id: root
    required property real scrollX
    required property real pixelsPerSecond
    required property real pixelsPerFrame
    required property real tickSeconds
    signal frameRequested(real frame)
    color: Theme.surface
    clip: true
    height: Theme.rulerHeight
    activeFocusOnTab: true
    Accessible.name: "Time ruler; drag to seek, Home/End to start/end"
    Accessible.role: Accessible.Slider
    border.color: activeFocus ? Theme.accent : Theme.border
    border.width: Theme.lineWidth
    Repeater {
        model: Math.ceil(root.width / (root.tickSeconds * root.pixelsPerSecond)) + 2
        Item {
            id: tick
            required property int index
            readonly property real seconds: (Math.floor(root.scrollX / (root.tickSeconds * root.pixelsPerSecond)) + index) * root.tickSeconds
            x: seconds * root.pixelsPerSecond - root.scrollX
            Rectangle { color: Theme.textDisabled; height: Theme.tickHeight; width: Theme.lineWidth; y: Theme.rulerHeight - height }
            Text { text: tick.seconds + "s"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; x: Theme.space4; y: Theme.space4 }
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        Controls.ToolTip.visible: containsMouse && !pressed
        Controls.ToolTip.delay: Theme.tooltipDelay
        Controls.ToolTip.text: root.Accessible.name
        onPressed: mouse => { root.forceActiveFocus(); root.frameRequested((mouse.x + root.scrollX) / root.pixelsPerFrame); }
        onPositionChanged: mouse => { if (pressed) root.frameRequested((mouse.x + root.scrollX) / root.pixelsPerFrame); }
    }
}
