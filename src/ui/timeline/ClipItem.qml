pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property var clipData
    required property real gridFrames
    required property real pixelsPerFrame
    property bool selected: false

    signal activated()
    signal moved(real deltaFrames, real deltaY)
    signal requested(string operation)
    signal trimmed(real leftDelta, real rightDelta)

    function snap(pixels: real): real {
        return Math.round(pixels / pixelsPerFrame / gridFrames) * gridFrames;
    }

    border.color: selected ? "#f1f5ff" : "#93a5bc"
    border.width: selected ? 2 : 1
    clip: true
    color: clipData.color
    radius: 5

    Text {
        color: "#ffffff"
        elide: Text.ElideRight
        text: root.clipData.name
        width: parent.width - 20
        x: 10
        y: 5
    }
    Row {
        spacing: 3
        x: 10
        y: 29

        Repeater {
            model: Math.min(80, Math.max(0, Math.floor((root.width - 20) / 7)))

            Rectangle {
                required property int index

                color: "#a8cbd5"
                height: 5 + ((index * 17 + root.clipData.clipId * 3) % 25)
                opacity: 0.65
                width: 4
                y: (30 - height) / 2
            }
        }
    }
    MouseArea {
        property point origin

        acceptedButtons: Qt.LeftButton | Qt.RightButton
        anchors.fill: parent
        drag.axis: Drag.XAndYAxis
        drag.target: root
        preventStealing: true

        onPressed: mouse => {
            root.activated();
            origin = mapToItem(null, mouse.x, mouse.y);
            if (mouse.button === Qt.RightButton)
                menu.popup();
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return;
            let end = mapToItem(null, mouse.x, mouse.y);
            root.moved(root.snap(end.x - origin.x), end.y - origin.y);
        }
    }
    Repeater {
        model: 2

        MouseArea {
            required property int index
            property real origin

            cursorShape: Qt.SizeHorCursor
            height: root.height
            width: 8
            x: index === 0 ? 0 : root.width - width

            onPressed: mouse => {
                origin = mapToItem(null, mouse.x, mouse.y).x;
                root.activated();
            }
            onReleased: mouse => {
                let delta = root.snap(mapToItem(null, mouse.x, mouse.y).x - origin);
                root.trimmed(index === 0 ? delta : 0, index === 1 ? delta : 0);
            }
        }
    }
    Menu {
        id: menu

        MenuItem {
            text: "Split at playhead"

            onTriggered: root.requested("split")
        }
        MenuItem {
            text: "Duplicate"

            onTriggered: root.requested("duplicate")
        }
        MenuItem {
            text: "Delete"

            onTriggered: root.requested("delete")
        }
    }
}
