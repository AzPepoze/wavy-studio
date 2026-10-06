// Compact BPM field: drag vertically, wheel (Shift for fine steps), or double-click/Enter to type.
import QtQuick
import "../theme"

Item {
    id: root
    required property var timelineModel
    implicitWidth: Theme.tempoFieldWidth
    implicitHeight: Theme.controlHeight
    activeFocusOnTab: true
    property bool typing: false
    readonly property real tempo: root.timelineModel.tempoBpm
    function clamp(bpm: real): real {
        return Math.max(Theme.tempoMinimum, Math.min(Theme.tempoMaximum, bpm));
    }
    function apply(bpm: real): void {
        if (isFinite(bpm))
            root.timelineModel.setTempo(root.clamp(bpm));
    }
    function beginTyping(): void {
        root.typing = true;
        editor.text = root.tempo.toFixed(2);
        editor.selectAll();
        editor.forceActiveFocus();
    }
    function commitTyping(): void {
        const parsed = Number(editor.text);
        if (editor.text.trim() !== "" && isFinite(parsed))
            root.apply(parsed);
        root.typing = false;
        root.forceActiveFocus();
    }
    Accessible.name: "Tempo " + root.tempo.toFixed(2) + " BPM; drag vertically, wheel to change, Enter to type"
    Accessible.role: Accessible.SpinBox
    Keys.onUpPressed: (event) => root.apply(root.tempo + (event.modifiers & Qt.ShiftModifier ? Theme.tempoFineStep : Theme.tempoWheelStep))
    Keys.onDownPressed: (event) => root.apply(root.tempo - (event.modifiers & Qt.ShiftModifier ? Theme.tempoFineStep : Theme.tempoWheelStep))
    // Claim the arrows from the timeline nudge shortcuts while this field has focus.
    Keys.onShortcutOverride: (event) => {
        if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_F2].includes(event.key))
            event.accepted = true;
    }
    Keys.onPressed: (event) => { if (event.key === Qt.Key_F2) { root.beginTyping(); event.accepted = true; } }
    Rectangle {
        anchors.fill: parent
        radius: Theme.controlRadius
        color: Theme.surface
        border.color: root.activeFocus || root.typing ? Theme.accent : Theme.border
        border.width: root.activeFocus ? Theme.focusWidth : Theme.lineWidth
    }
    Text {
        anchors.centerIn: parent
        visible: !root.typing
        text: root.tempo.toFixed(2)
        color: Theme.textPrimary
        font.pixelSize: Theme.fontNormal
        font.family: "monospace"
    }
    TextInput {
        id: editor
        anchors.fill: parent
        anchors.margins: Theme.space4
        visible: root.typing
        color: Theme.textPrimary
        font.pixelSize: Theme.fontNormal
        font.family: "monospace"
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        onAccepted: root.commitTyping()
        Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Escape) event.accepted = true; }
        Keys.onEscapePressed: { root.typing = false; root.forceActiveFocus(); }
    }
    MouseArea {
        id: drag
        anchors.fill: parent
        enabled: !root.typing
        cursorShape: Qt.SizeVerCursor
        hoverEnabled: true
        property real startValue: 0
        property real startY: 0
        onPressed: mouse => { root.forceActiveFocus(); drag.startValue = root.tempo; drag.startY = mouse.y; }
        onPositionChanged: mouse => { if (pressed) root.apply(drag.startValue - (mouse.y - drag.startY) * Theme.tempoWheelStep); }
        onDoubleClicked: root.beginTyping()
    }
    WheelHandler {
        enabled: !root.typing
        onWheel: event => {
            const step = event.modifiers & Qt.ShiftModifier ? Theme.tempoFineStep : Theme.tempoWheelStep;
            root.apply(root.tempo + (event.angleDelta.y > 0 ? step : -step));
        }
    }
}
