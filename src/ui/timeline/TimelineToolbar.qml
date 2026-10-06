// Timeline navigation and readout. Requires time, snap and ruler state; emits zoom, fit and snap
// requests and writes ruler/snap preferences straight to the injected settings object.
import QtQuick
import QtQuick.Controls as Controls
import "../theme"
import "SnapMath.js" as SnapMath

Rectangle {
    id: root
    required property real playheadFrame
    required property int sampleRate
    required property var settings
    required property bool snapEnabled
    required property real beatsPerMinute
    required property real framesPerBeat
    required property real beatsPerBar
    required property string timeSignatureText
    required property string snapStepLabel
    property bool effectsVisible: false
    signal effectsRequested()
    signal zoomRequested(real factor)
    signal fitRequested()
    signal snapRequested(bool enabled)
    readonly property string timeText: SnapMath.formatTime(root.playheadFrame, root.sampleRate)
    readonly property string barsBeatsText: SnapMath.formatBarsBeats(root.playheadFrame, root.framesPerBeat, root.beatsPerBar)
    height: Theme.toolbarHeight
    color: Theme.background
    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space8
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space8
        ToggleButton { text: "FX"; hint: "Show selected track effects"; checked: root.effectsVisible; onClicked: root.effectsRequested() }
        ZoomControls { onZoomRequested: factor => root.zoomRequested(factor); onFitRequested: root.fitRequested() }
        ToggleButton { text: "Snap"; width: Theme.headerWidth / 3; hint: "Snap edits to the grid"; checked: root.snapEnabled; onClicked: root.snapRequested(checked) }
        ToolButton {
            objectName: "snap-settings"
            text: "▾"
            hint: "Snap settings"
            onClicked: snapPopup.open()
        }
        Text {
            text: root.snapStepLabel
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            width: Theme.snapDivisionButtonWidth
            height: Theme.controlHeight
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: "Snap step " + text
            Accessible.role: Accessible.StaticText
        }
        ToggleButton {
            objectName: "ruler-mode"
            text: root.settings.rulerMode === "time" ? "Time" : "Bars"
            hint: "Ruler display: bars and beats or time"
            checked: root.settings.rulerMode === "time"
            onClicked: root.settings.rulerMode = checked ? "time" : "barsBeats"
        }
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.textSecondary
        font.pixelSize: Theme.fontNormal
        font.family: "monospace"
        text: root.timeText + "   " + root.barsBeatsText
        Accessible.name: "Playhead " + root.timeText + ", " + root.barsBeatsText + "; bars and beats at " + root.beatsPerMinute + " BPM, " + root.timeSignatureText
        Accessible.role: Accessible.StaticText
        Controls.ToolTip.visible: readoutHover.containsMouse
        Controls.ToolTip.delay: Theme.tooltipDelay
        Controls.ToolTip.text: "Space play/pause; Home/End seek; Delete delete; Ctrl+D duplicate; S split; arrows nudge; Escape clear selection; +/− zoom; Ctrl+0 fit; middle mouse or Alt+drag pan; Ctrl+wheel zoom"
        MouseArea { id: readoutHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
    }
    SnapSettings {
        id: snapPopup
        settings: root.settings
        x: root.width / 2 - width / 2
        y: Theme.toolbarHeight + Theme.space4
    }
}
