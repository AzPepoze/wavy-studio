// One ordered slot; controller and trackId route edits; slot supplies chain model roles.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../theme"
import "../timeline"

Rectangle {
    id: root
    required property var controller
    required property int trackId
    required property var slot
    readonly property int slotIndex: slot ? slot.index : -1
    readonly property var slotData: slot || ({name: "", typeId: "", bypassed: true, index: -1})
    required property int chainCount
    property bool active: true
    implicitHeight: content.implicitHeight + Theme.space16 * 2
    color: Theme.surface
    radius: Theme.radius
    border.color: Theme.border
    border.width: Theme.lineWidth
    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.space16
        spacing: Theme.space12
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.slotData.name; color: Theme.textPrimary; font.pixelSize: Theme.fontNormal; font.bold: true; Layout.fillWidth: true }
            ToolButton { text: "↑"; hint: "Move effect up"; enabled: root.slotIndex > 0; onClicked: root.controller.moveEffect(root.trackId, root.slotIndex, root.slotIndex - 1) }
            ToolButton { text: "↓"; hint: "Move effect down"; enabled: root.slotIndex + 1 < root.chainCount; onClicked: root.controller.moveEffect(root.trackId, root.slotIndex, root.slotIndex + 1) }
            ToggleButton { objectName: "bypass-effect"; text: "Bypass"; implicitWidth: Theme.headerWidth / 2; checked: root.slotData.bypassed; onClicked: root.controller.setBypassed(root.trackId, root.slotIndex, checked) }
            ToolButton { objectName: "remove-effect"; text: "×"; hint: "Remove " + root.slotData.name; onClicked: root.controller.removeEffect(root.trackId, root.slotIndex) }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: root.slotData.typeId === "parametric_eq" ? 4 : root.slotData.typeId === "compressor" ? 3 : root.slotData.typeId === "gain_pan" ? 3 : 1
            rows: root.slotData.typeId === "parametric_eq" ? 5 : -1
            flow: root.slotData.typeId === "parametric_eq" ? GridLayout.TopToBottom : GridLayout.LeftToRight
            columnSpacing: Theme.space16
            rowSpacing: Theme.space8
            enabled: !root.slotData.bypassed
            Repeater {
                model: root.slot ? root.controller.parametersModel(root.trackId, root.slotIndex) : null
                delegate: ParameterSlider {
                    required property var model
                    Layout.fillWidth: true
                    controller: root.controller; trackId: root.trackId; slotIndex: root.slotIndex
                    parameter: model
                }
            }
        }
        GainReductionMeter {
            Layout.fillWidth: true
            visible: root.slotData.typeId === "compressor" && !root.slotData.bypassed
            controller: root.controller; trackId: root.trackId; slotIndex: root.slotIndex; active: root.active
        }
    }
}
