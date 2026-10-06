// Focusable clip with hover, drag, trim and edit menu. Requires clip data and scale; emits edits and drag lifecycle.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Rectangle {
    id: root
    required property var clipData
    required property real gridFrames
    required property real pixelsPerFrame
    property real laneWidth: 0
    property bool selected: false
    property bool snapEnabled: true
    property bool dragActive: false
    readonly property int trackIndex: clipData.trackIndex !== undefined ? clipData.trackIndex : 0
    readonly property bool waveformAvailable: (typeof waveformBridge !== "undefined" && waveformBridge !== null)
    readonly property real waveformX: Theme.space12
    readonly property real waveformY: Theme.controlHeight
    readonly property real waveformWidth: Math.max(0, root.width - Theme.space12 * 2)
    readonly property real waveformHeight: Math.max(0, root.height - waveformY - Theme.space4)
    // The lane shows display x in [0, laneWidth]; item x is display x minus the clip's lane x.
    readonly property real waveformVisibleLeft: laneWidth > 0 ? Math.max(0, -root.x - waveformX) : 0
    readonly property real waveformVisibleRight: laneWidth > 0 ? Math.min(waveformWidth, laneWidth - root.x - waveformX) : waveformWidth
    readonly property int paletteSize: Theme.trackPalette.length
    signal activated()
    signal requested(string operation)
    signal trimmed(real leftDelta, real rightDelta)
    signal dragStarted(var clip)
    signal dragMoved(real sceneX, real sceneY)
    signal dragDropped(var clip, real sceneX, real sceneY)
    signal dragCancelled()
    function snap(pixels: real): real {
        let frames = pixels / pixelsPerFrame;
        return snapEnabled ? Math.round(frames / gridFrames) * gridFrames : Math.round(frames);
    }
    color: Theme.trackPalette[((trackIndex % paletteSize) + paletteSize) % paletteSize]
    opacity: dragActive ? Theme.dragSourceOpacity : 1
    radius: Theme.radius
    border.color: selected ? Theme.selection : activeFocus ? Theme.accent : body.containsMouse ? Theme.textPrimary : Theme.border
    border.width: selected || activeFocus ? Theme.focusWidth : Theme.lineWidth
    Behavior on border.color { ColorAnimation { duration: Theme.selectionDuration } }
    clip: true
    activeFocusOnTab: true
    Accessible.name: clipData.name + "; Delete, Ctrl+D duplicate, S split, arrows nudge, Escape clear selection"
    Accessible.role: Accessible.Button
    Text { text: root.clipData.name; color: Theme.textPrimary; font.pixelSize: Theme.fontNormal; elide: Text.ElideRight; x: Theme.space12; y: Theme.space4; width: root.width - Theme.space12 * 2 }
    Rectangle {
        id: placeholder
        x: root.waveformX
        y: root.waveformY
        width: root.waveformWidth
        height: root.waveformHeight
        color: Theme.transparent
        visible: root.waveformWidth > 0 && root.waveformHeight > 0
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: Theme.lineWidth; color: Theme.waveform; opacity: Theme.waveformOpacity }
    }
    Item {
        id: waveformHolder
        x: root.waveformX
        y: root.waveformY
        width: root.waveformWidth
        height: root.waveformHeight
        // Created only when the C++ module is registered, so pure-QML harnesses keep working.
        Component.onCompleted: {
            if (!root.waveformAvailable) return;
            try {
                const item = Qt.createQmlObject('import QtQuick; import Wavy.Waveform 1.0; WaveformItem {}', waveformHolder, "clip-waveform");
                item.width = Qt.binding(() => root.waveformWidth);
                item.height = Qt.binding(() => root.waveformHeight);
                item.source = Qt.binding(() => root.clipData.source !== undefined ? root.clipData.source : "");
                item.sourceOffset = Qt.binding(() => root.clipData.sourceOffset !== undefined ? root.clipData.sourceOffset : 0);
                item.lengthFrames = Qt.binding(() => root.clipData.durationFrames);
                item.pixelsPerFrame = Qt.binding(() => root.pixelsPerFrame);
                item.amplitude = Qt.binding(() => root.clipData.gain !== undefined ? root.clipData.gain : 1);
                item.color = Qt.binding(() => Theme.waveform);
                item.visibleLeft = Qt.binding(() => root.waveformVisibleLeft);
                item.visibleRight = Qt.binding(() => root.waveformVisibleRight);
            } catch (error) {
                console.warn("waveform unavailable:", error);
            }
        }
    }
    MouseArea {
        id: body
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        preventStealing: true
        Controls.ToolTip.visible: containsMouse && !pressed
        Controls.ToolTip.text: root.Accessible.name
        Controls.ToolTip.delay: Theme.tooltipDelay
        onPressed: mouse => {
            root.forceActiveFocus(); root.activated();
            if (mouse.button === Qt.RightButton) {
                menu.popup();
                return;
            }
            root.dragActive = true;
            const scene = mapToItem(null, mouse.x, mouse.y);
            root.dragStarted(root.clipData);
            root.dragMoved(scene.x, scene.y);
        }
        onPositionChanged: mouse => {
            if (!pressed || pressedButtons !== Qt.LeftButton) return;
            const scene = mapToItem(null, mouse.x, mouse.y);
            root.dragMoved(scene.x, scene.y);
        }
        onReleased: mouse => {
            if (mouse.button === Qt.LeftButton && root.dragActive) {
                const scene = mapToItem(null, mouse.x, mouse.y);
                root.dragActive = false;
                root.dragDropped(root.clipData, scene.x, scene.y);
            }
        }
        onCanceled: {
            root.dragActive = false;
            root.dragCancelled();
        }
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
