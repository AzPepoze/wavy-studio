// Slim vertical track gain fader in dB. Emits moved(db) while dragging and resetRequested() on reset.
import QtQuick
import "../theme"

Item {
    id: root
    property real value: 0
    property real minimum: Theme.gainMinimumDb
    property real maximum: Theme.gainMaximumDb
    signal moved(real db)
    signal resetRequested()
    readonly property string readout: (cleanValue > 0 ? "+" : "") + cleanValue.toFixed(1) + " dB"
    readonly property real cleanValue: Math.abs(value) < 0.05 ? 0 : value
    readonly property real position: dbToPosition(value)
    implicitWidth: Theme.faderColumnWidth
    implicitHeight: Theme.faderHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.description: readout + "; double-click or Ctrl+click resets to 0 dB"
    function dbToPosition(db: real): real {
        const t = db >= 0 ? 0.9 + db / maximum * 0.1 : 0.9 * Math.pow(10, db / 60);
        return Math.max(0, Math.min(1, (t - 0.09) / 0.91));
    }
    function positionToDb(p: real): real {
        const t = 0.09 + Math.max(0, Math.min(1, p)) * 0.91;
        return t >= 0.9 ? (t - 0.9) / 0.1 * maximum : 60 * Math.log10(t / 0.9);
    }
    function adjust(delta: real): void {
        moved(Math.max(minimum, Math.min(maximum, value + delta)));
    }
    Keys.onShortcutOverride: event => { if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right, Qt.Key_Home, Qt.Key_End].includes(event.key)) event.accepted = true; }
    Keys.onUpPressed: adjust(Theme.gainFineStepDb)
    Keys.onRightPressed: adjust(Theme.gainFineStepDb)
    Keys.onDownPressed: adjust(-Theme.gainFineStepDb)
    Keys.onLeftPressed: adjust(-Theme.gainFineStepDb)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) { moved(minimum); event.accepted = true; }
        else if (event.key === Qt.Key_End) { moved(maximum); event.accepted = true; }
    }
    Rectangle {
        id: groove
        x: (root.width - width) / 2
        width: Theme.space4
        height: root.height - Theme.gainReadoutHeight
        color: Theme.background
        radius: width / 2
        border.color: Theme.border
        border.width: Theme.lineWidth
    }
    Rectangle {
        id: fill
        x: groove.x
        width: groove.width
        y: handle.y + handle.height / 2
        height: Math.max(0, groove.height - y)
        color: Theme.accent
    }
    Rectangle {
        id: handle
        x: (root.width - width) / 2
        width: Theme.faderWidth
        height: Theme.faderHandleHeight
        y: (1 - root.position) * (groove.height - height)
        radius: Theme.controlRadius
        color: root.activeFocus ? Theme.textPrimary : Theme.textSecondary
        border.color: root.activeFocus ? Theme.accent : Theme.border
        border.width: root.activeFocus ? Theme.focusWidth : Theme.lineWidth
    }
    Text {
        anchors.top: groove.bottom
        width: root.width
        text: root.readout
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        horizontalAlignment: Text.AlignHCenter
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function apply(mouse: var): void {
            const span = Math.max(1, groove.height - handle.height);
            const p = 1 - (mouse.y - handle.height / 2) / span;
            root.moved(root.positionToDb(p));
        }
        onPressed: mouse => {
            root.forceActiveFocus();
            if (mouse.modifiers & Qt.ControlModifier) root.resetRequested();
            else apply(mouse);
        }
        onPositionChanged: mouse => { if (pressed) apply(mouse); }
        onDoubleClicked: root.resetRequested();
    }
    WheelHandler {
        onWheel: event => {
            root.adjust(event.angleDelta.y > 0 ? Theme.gainFineStepDb : -Theme.gainFineStepDb);
            event.accepted = true;
        }
    }
}
