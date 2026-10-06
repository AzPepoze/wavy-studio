// Timeline navigation and readout. Requires time and snap state; emits zoom, fit and snap requests.
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Rectangle {
    id: root
    required property real seconds
    required property bool snapEnabled
    property real beatsPerMinute: 120
    signal zoomRequested(real factor)
    signal fitRequested()
    signal snapRequested(bool enabled)
    readonly property int milliseconds: Math.round(seconds * 1000)
    readonly property real beat: seconds * beatsPerMinute / 60
    height: Theme.toolbarHeight
    color: Theme.background
    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space8
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space8
        ZoomControls { onZoomRequested: factor => root.zoomRequested(factor); onFitRequested: root.fitRequested() }
        ToggleButton { text: "Snap"; width: Theme.headerWidth / 3; hint: "Snap edits to the grid"; checked: root.snapEnabled; onClicked: root.snapRequested(checked) }
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.textSecondary
        font.pixelSize: Theme.fontNormal
        font.family: "monospace"
        text: Math.floor(root.milliseconds / 60000).toString().padStart(2, "0") + ":"
              + (Math.floor(root.milliseconds / 1000) % 60).toString().padStart(2, "0") + "."
              + (root.milliseconds % 1000).toString().padStart(3, "0") + "   "
              + (Math.floor(root.beat / 4) + 1) + ":" + (Math.floor(root.beat) % 4 + 1)
        Accessible.name: "Playhead time " + text + "; bars and beats at " + root.beatsPerMinute + " BPM, 4/4"
        Accessible.role: Accessible.StaticText
        Controls.ToolTip.visible: readoutHover.containsMouse
        Controls.ToolTip.delay: Theme.tooltipDelay
        Controls.ToolTip.text: "Space play/pause; Home/End seek; Delete delete; Ctrl+D duplicate; S split; arrows nudge; Escape clear selection; +/− zoom; Ctrl+0 fit; middle mouse or Alt+drag pan; Ctrl+wheel zoom"
        MouseArea { id: readoutHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
    }
}
