// Project time signature readout that opens a numerator/denominator picker.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Item {
    id: root
    required property var timelineModel
    implicitWidth: Theme.buttonWidth + Theme.space8
    implicitHeight: Theme.controlHeight
    activeFocusOnTab: true
    readonly property int numerator: root.timelineModel.timeSignatureNumerator
    readonly property int denominator: root.timelineModel.timeSignatureDenominator
    function choose(n: int, d: int): void {
        root.timelineModel.setTimeSignature(n, d);
        popup.close();
    }
    Accessible.name: "Time signature " + root.numerator + " over " + root.denominator + "; activate to change"
    Accessible.role: Accessible.Button
    // Claim Space from the play/pause shortcut while this control has focus.
    Keys.onShortcutOverride: (event) => { if ([Qt.Key_Space, Qt.Key_Return].includes(event.key)) event.accepted = true; }
    Keys.onReturnPressed: popup.open()
    Keys.onSpacePressed: popup.open()
    Rectangle {
        anchors.fill: parent
        radius: Theme.controlRadius
        color: root.activeFocus ? Theme.hover : Theme.surface
        border.color: root.activeFocus || popup.opened ? Theme.accent : Theme.border
        border.width: root.activeFocus ? Theme.focusWidth : Theme.lineWidth
    }
    Text {
        anchors.centerIn: parent
        text: root.numerator + "/" + root.denominator
        color: Theme.textPrimary
        font.pixelSize: Theme.fontNormal
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: root.forceActiveFocus()
        onClicked: popup.open()
    }
    Controls.Popup {
        id: popup
        width: Theme.timeSignaturePopupWidth
        padding: Theme.snapPopupPadding
        modal: true
        focus: true
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: Theme.lineWidth
            radius: Theme.radius
        }
        contentItem: Column {
            spacing: Theme.space8
            Text { text: "Numerator"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            Grid {
                columns: 8
                spacing: Theme.space4
                Repeater {
                    model: 32
                    ToolButton {
                        required property int index
                        readonly property int value: index + 1
                        text: value.toString()
                        checkable: true
                        checked: root.numerator === value
                        hint: "Numerator " + value
                        implicitWidth: Theme.buttonWidth
                        onClicked: root.choose(value, root.denominator)
                    }
                }
            }
            Text { text: "Denominator"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            Row {
                spacing: Theme.space4
                Repeater {
                    model: [2, 4, 8, 16]
                    ToolButton {
                        required property int modelData
                        text: modelData.toString()
                        checkable: true
                        checked: root.denominator === modelData
                        hint: "Denominator " + modelData
                        implicitWidth: Theme.buttonWidth
                        onClicked: root.choose(root.numerator, modelData)
                    }
                }
            }
        }
    }
}
