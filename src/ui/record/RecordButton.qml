// Transport record toggle; controller provides recording, busy and toggleRecord().
import QtQuick
import QtQuick.Controls
import "../theme"

AbstractButton {
    id: root
    required property var controller
    implicitWidth: Theme.buttonWidth
    implicitHeight: Theme.buttonWidth
    enabled: !controller.busy
    Accessible.name: controller.recording ? "Stop recording" : "Record"
    ToolTip.visible: hovered
    ToolTip.text: "Record (R)"
    ToolTip.delay: Theme.tooltipDelay
    onClicked: controller.toggleRecord()
    background: Rectangle {
        color: root.activeFocus ? Theme.accent : Theme.surface
        radius: width / 2
    }
    contentItem: Item {
        Rectangle {
            id: light
            anchors.centerIn: parent
            width: Theme.space16
            height: Theme.space16
            radius: root.controller.recording ? Theme.controlRadius : width / 2
            color: Theme.record
            SequentialAnimation on opacity {
                running: root.controller.recording
                loops: Animation.Infinite
                NumberAnimation { to: Theme.pulseOpacity; duration: Theme.pulseDuration }
                NumberAnimation { to: 1; duration: Theme.pulseDuration }
                onStopped: light.opacity = 1
            }
        }
    }
}
