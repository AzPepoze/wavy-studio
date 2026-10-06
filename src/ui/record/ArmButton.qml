// Track arm toggle; controller provides armedTrackId and armTrack(id, armed).
import QtQuick
import QtQuick.Controls
import "../theme"

AbstractButton {
    id: root
    property var controller: null
    required property int trackId
    required property string trackName
    implicitWidth: Theme.buttonWidth
    implicitHeight: Theme.controlHeight
    enabled: controller !== null && !controller.busy && !controller.recording
    checked: controller !== null && controller.armedTrackId === trackId
    Accessible.name: "Arm " + trackName + " for recording"
    ToolTip.visible: hovered
    ToolTip.text: Accessible.name
    ToolTip.delay: Theme.tooltipDelay
    onClicked: controller.armTrack(trackId, !checked)
    background: Rectangle {
        color: root.hovered ? Theme.hover : Theme.surface
        radius: Theme.controlRadius
        border.color: root.activeFocus ? Theme.accent : Theme.border
        border.width: Theme.lineWidth
    }
    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: Theme.space12
            height: width
            radius: width / 2
            color: root.checked ? Theme.record : Theme.textDisabled
        }
    }
}
