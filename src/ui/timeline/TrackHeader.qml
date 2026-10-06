// Track identity and mute/solo actions. Requires name and states; emits stateRequested(role, value).
import QtQuick
import "../theme"

Rectangle {
    id: root
    required property string name
    required property bool muted
    required property bool solo
    signal stateRequested(string role, bool value)
    color: Theme.surface
    border.color: Theme.border
    border.width: Theme.lineWidth
    Text {
        x: Theme.space12
        y: Theme.space8
        width: root.width - Theme.space12 * 2
        text: root.name
        elide: Text.ElideRight
        color: Theme.textPrimary
        font.pixelSize: Theme.fontNormal
    }
    Row {
        x: Theme.space12
        y: Theme.rulerHeight + Theme.space4
        spacing: Theme.space4
        ToggleButton { text: "M"; hint: "Mute " + root.name; checked: root.muted; onClicked: root.stateRequested("muted", checked) }
        ToggleButton { text: "S"; hint: "Solo " + root.name; checked: root.solo; onClicked: root.stateRequested("solo", checked) }
    }
}
