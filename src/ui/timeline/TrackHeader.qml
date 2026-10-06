// Track identity, selection and mute/solo. Emits selectedRequested() and stateRequested(role, value).
import QtQuick
import "../theme"
import "../record"

Rectangle {
    id: root
    property var recordingController: null
    property int trackId: -1
    required property string name
    required property bool muted
    required property bool solo
    property bool selected: false
    signal selectedRequested()
    activeFocusOnTab: true
    Accessible.name: "Select track " + name
    Accessible.role: Accessible.Button
    Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Space || event.key === Qt.Key_Return) event.accepted = true; }
    Keys.onReturnPressed: selectedRequested()
    Keys.onSpacePressed: selectedRequested()
    TapHandler { onTapped: { root.forceActiveFocus(); root.selectedRequested(); } }
    signal stateRequested(string role, bool value)
    color: Theme.surface
    border.color: selected || activeFocus ? Theme.accent : Theme.border
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
        ArmButton {
            controller: root.recordingController
            trackId: root.trackId
            trackName: root.name
        }
        ToggleButton { text: "S"; hint: "Solo " + root.name; checked: root.solo; onClicked: root.stateRequested("solo", checked) }
    }
}
