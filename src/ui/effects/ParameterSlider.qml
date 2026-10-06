// Schema parameter control; controller, trackId, slotIndex and parameter route atomic edits.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../timeline"

ColumnLayout {
    id: root
    required property var controller
    required property int trackId
    required property int slotIndex
    required property var parameter
    readonly property bool toggle: parameter.id === "invert" || parameter.id === "auto_makeup" || parameter.id.endsWith(".enabled")
    readonly property var choices: parameter.id.endsWith(".type") ? ["Low shelf", "Peak", "High shelf"] : parameter.id === "detector" ? ["Peak", "RMS"] : []
    readonly property string readout: (isFinite(parameter.value) ? Number(parameter.value).toFixed(parameter.unit === "Hz" ? 0 : parameter.maximum <= 20 ? 2 : 1) : "−∞") + (parameter.unit ? " " + parameter.unit : "")
    Keys.onShortcutOverride: event => { if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End, Qt.Key_Space].includes(event.key)) event.accepted = true; }
    spacing: Theme.space4
    RowLayout {
        Layout.fillWidth: true
        Text { text: root.parameter.id.endsWith(".type") ? "Band " + root.parameter.id.slice(4, 5) + " · " + root.parameter.name : root.parameter.name; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
        Text { text: root.readout; visible: !root.toggle && !root.choices.length; color: Theme.textPrimary; font.pixelSize: Theme.fontSmall }
    }
    ToggleButton {
        visible: root.toggle
        Layout.fillWidth: true
        text: root.parameter.name
        checked: root.parameter.value >= 0.5
        onClicked: root.controller.setParameter(root.trackId, root.slotIndex, root.parameter.id, checked ? 1 : 0)
    }
    ComboBox {
        visible: root.choices.length > 0
        Layout.fillWidth: true
        palette.button: Theme.surface
        palette.buttonText: Theme.textPrimary
        palette.window: Theme.surface
        palette.text: Theme.textPrimary
        palette.base: Theme.surface
        palette.highlight: Theme.hover
        palette.highlightedText: Theme.textPrimary
        model: root.choices
        currentIndex: Math.round(root.parameter.value)
        Accessible.name: root.parameter.name
        onActivated: root.controller.setParameter(root.trackId, root.slotIndex, root.parameter.id, currentIndex)
        font.pixelSize: Theme.fontNormal
        implicitHeight: Theme.controlHeight
    }
    Slider {
        id: slider
        objectName: "parameter-" + root.parameter.id
        visible: !root.toggle && !root.choices.length
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        from: 0; to: 1; stepSize: 0.005
        value: root.parameter.normalized
        Accessible.name: root.parameter.name + " " + root.readout
        Accessible.role: Accessible.Slider
        Accessible.description: "Arrows adjust; double-click or Ctrl+click resets"
        onMoved: root.controller.setNormalized(root.trackId, root.slotIndex, root.parameter.id, value)
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth; height: Theme.space4
            radius: Theme.controlRadius; color: Theme.border
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: Theme.accent }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: Theme.space12; height: Theme.space16
            radius: Theme.controlRadius; color: Theme.textPrimary
            border.color: slider.activeFocus ? Theme.accent : Theme.border
            border.width: Theme.focusWidth
        }
        MouseArea {
            anchors.fill: parent
            function adjust(mouse: var): void {
                if (mouse.modifiers & Qt.ControlModifier) root.controller.resetParameter(root.trackId, root.slotIndex, root.parameter.id);
                else root.controller.setNormalized(root.trackId, root.slotIndex, root.parameter.id, Math.max(0, Math.min(1, (mouse.x - slider.leftPadding) / slider.availableWidth)));
            }
            onPressed: mouse => { slider.forceActiveFocus(); adjust(mouse); }
            onPositionChanged: mouse => { if (pressed) adjust(mouse); }
            onDoubleClicked: root.controller.resetParameter(root.trackId, root.slotIndex, root.parameter.id)
        }
        WheelHandler {
            onWheel: event => {
                root.controller.setNormalized(root.trackId, root.slotIndex, root.parameter.id, root.parameter.normalized + event.angleDelta.y / Theme.wheelStep * slider.stepSize);
                event.accepted = true;
            }
        }
    }
}
