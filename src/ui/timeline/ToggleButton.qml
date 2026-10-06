// Reusable on/off action. Uses ToolButton's text, hint, checked and clicked API.
import QtQuick

ToolButton {
    checkable: true
    Accessible.role: Accessible.CheckBox
}
