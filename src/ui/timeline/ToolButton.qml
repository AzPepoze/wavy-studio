// Focusable timeline action button. text, hint and checked describe it; clicked invokes it.
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.Button {
    id: root
    property string hint: text
    implicitWidth: Theme.buttonWidth
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: hint
    Accessible.role: Accessible.Button
    Controls.ToolTip.visible: hovered
    Controls.ToolTip.text: hint
    Controls.ToolTip.delay: Theme.tooltipDelay
    contentItem: Text {
        text: root.text
        color: root.enabled ? Theme.textPrimary : Theme.textDisabled
        font.pixelSize: Theme.fontNormal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: Theme.controlRadius
        color: root.down ? Theme.border : root.checked ? Theme.hover : root.hovered ? Theme.hover : Theme.surface
        border.color: root.activeFocus || root.checked ? Theme.accent : Theme.border
        border.width: root.activeFocus ? Theme.focusWidth : Theme.lineWidth
        Behavior on color { enabled: !root.down; ColorAnimation { duration: Theme.hoverDuration } }
    }
}
