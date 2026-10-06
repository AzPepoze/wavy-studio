// Focusable clip with hover, drag, trim and edit menu. Requires clip data and scale; emits edits and dragPreview.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Rectangle {
    id: root
    required property var clipData
    required property real gridFrames
    required property real pixelsPerFrame
    property bool selected: false
    property bool snapEnabled: true
    property real dragX: 0
    property real dragY: 0
    signal activated()
    signal moved(real deltaFrames, real deltaY)
    signal requested(string operation)
    signal trimmed(real leftDelta, real rightDelta)
    signal dragPreview(real frame, bool active)
    function snap(pixels: real): real {
        let frames = pixels / pixelsPerFrame;
        return snapEnabled ? Math.round(frames / gridFrames) * gridFrames : Math.round(frames);
    }
    color: clipData.color
    radius: Theme.radius
    border.color: selected ? Theme.selection : activeFocus ? Theme.accent : body.containsMouse ? Theme.textPrimary : Theme.border
    border.width: selected || activeFocus ? Theme.focusWidth : Theme.lineWidth
    Behavior on border.color { ColorAnimation { duration: Theme.selectionDuration } }
    clip: true
    activeFocusOnTab: true
    Accessible.name: clipData.name + "; Delete, Ctrl+D duplicate, S split, arrows nudge, Escape clear selection"
    Accessible.role: Accessible.Button
    transform: Translate { x: root.dragX; y: root.dragY }
    Text { text: root.clipData.name; color: Theme.textPrimary; font.pixelSize: Theme.fontNormal; elide: Text.ElideRight; x: Theme.space12; y: Theme.space4; width: root.width - Theme.space12 * 2 }
    WaveformPreview { seed: root.clipData.clipId; x: Theme.space12; y: Theme.controlHeight; width: root.width - Theme.space12 * 2; height: root.height - y - Theme.space4 }
    MouseArea {
        id: body
        property point origin
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        preventStealing: true
        Controls.ToolTip.visible: containsMouse && !pressed
        Controls.ToolTip.text: root.Accessible.name
        Controls.ToolTip.delay: Theme.tooltipDelay
        onPressed: mouse => {
            root.forceActiveFocus(); root.activated(); origin = mapToItem(null, mouse.x, mouse.y);
            if (mouse.button === Qt.RightButton) menu.popup();
        }
        onPositionChanged: mouse => {
            if (!pressed || pressedButtons !== Qt.LeftButton) return;
            let end = mapToItem(null, mouse.x, mouse.y);
            root.dragX += end.x - origin.x; root.dragY += end.y - origin.y; origin = end;
            root.dragPreview(root.clipData.startFrame + root.snap(root.dragX), true);
        }
        onReleased: mouse => {
            if (mouse.button === Qt.LeftButton && (root.dragX !== 0 || root.dragY !== 0)) root.moved(root.snap(root.dragX), root.dragY);
            root.dragX = 0; root.dragY = 0; root.dragPreview(0, false);
        }
        onCanceled: { root.dragX = 0; root.dragY = 0; root.dragPreview(0, false); }
    }
    Repeater {
        model: 2
        MouseArea {
            required property int index
            property real origin
            cursorShape: Qt.SizeHorCursor
            height: root.height
            width: Theme.space8
            x: index === 0 ? 0 : root.width - width
            onPressed: mouse => { origin = mapToItem(null, mouse.x, mouse.y).x; root.forceActiveFocus(); root.activated(); }
            onReleased: mouse => {
                let delta = root.snap(mapToItem(null, mouse.x, mouse.y).x - origin);
                if (delta !== 0) root.trimmed(index === 0 ? delta : 0, index === 1 ? delta : 0);
            }
        }
    }
    Controls.Menu {
        id: menu
        Controls.MenuItem { text: "Split at playhead (S)"; onTriggered: root.requested("split") }
        Controls.MenuItem { text: "Duplicate (Ctrl+D)"; onTriggered: root.requested("duplicate") }
        Controls.MenuItem { text: "Delete (Delete)"; onTriggered: root.requested("delete") }
    }
}
