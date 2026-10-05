// Model contract: sampleRate, durationFrames and playheadFrame (int frames), tracks
// (QAbstractListModel roles: trackId int, name string, muted bool, solo bool).
// visibleClips(trackId, firstFrame, lastFrame) returns an array of {clipId int,
// name string, startFrame int, durationFrames int, color QColor/string}.
// editClip(clipId, targetTrackId, startFrame, durationFrames), action(clipId,
// trackId, "split"/"duplicate"/"delete"), setTrackState(row, role, bool), and
// trackIdAt(row) returning a track ID are invokables.
// clipsChangedForTrack(trackId) invalidates a lane; standard model notifications
// update track roles. Property notify signals update sample rate, duration and playhead.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    readonly property real contentWidth: timelineModel.durationFrames * pixelsPerFrame
    readonly property real gridFrames: tickSeconds * timelineModel.sampleRate / 4
    readonly property int headerWidth: 180
    readonly property real laneWidth: Math.max(1, width - headerWidth)
    readonly property real pixelsPerFrame: pixelsPerSecond / timelineModel.sampleRate
    property real pixelsPerSecond: 90
    property real scrollX: 0
    property int selectedClipId: -1
    readonly property real tickSeconds: Math.pow(2, Math.ceil(Math.log(70 / pixelsPerSecond) / Math.LN2))
    property var timelineModel: MockTimelineModel {
    }
    readonly property int trackHeight: 82
    property int viewportRevision: 0

    function clampScroll(value: real): real {
        return Math.max(0, Math.min(value, Math.max(0, contentWidth - laneWidth)));
    }

    clip: true
    color: "#181b22"

    onPixelsPerSecondChanged: refresh.start()
    onScrollXChanged: refresh.start()
    onWidthChanged: refresh.start()

    Timer {
        id: refresh

        interval: 40

        onTriggered: root.viewportRevision++
    }
    Rectangle {
        clip: true
        color: "#252a35"
        height: 32
        width: root.laneWidth
        x: root.headerWidth

        Repeater {
            model: Math.ceil(root.laneWidth / (root.tickSeconds * root.pixelsPerSecond)) + 2

            Item {
                required property int index
                readonly property real seconds: (Math.floor(root.scrollX / (root.tickSeconds * root.pixelsPerSecond)) + index) * root.tickSeconds

                x: seconds * root.pixelsPerSecond - root.scrollX

                Rectangle {
                    color: "#9099ac"
                    height: 10
                    width: 1
                    y: 22
                }
                Text {
                    color: "#b7c1d4"
                    font.pixelSize: 11
                    text: parent.seconds + "s"
                    x: 4
                    y: 4
                }
            }
        }
        MouseArea {
            anchors.fill: parent

            onClicked: mouse => root.timelineModel.playheadFrame = Math.round((mouse.x + root.scrollX) / root.pixelsPerFrame)
        }
    }
    Text {
        color: "#b7c1d4"
        font.pixelSize: 11
        text: "TRACKS"
        x: 12
        y: 8
    }
    ListView {
        id: tracks

        cacheBuffer: root.trackHeight * 2
        clip: true
        height: root.height - 46
        model: root.timelineModel.tracks
        width: root.width
        y: 32

        ScrollBar.vertical: ScrollBar {
        }
        delegate: Item {
            id: row

            required property int index
            required property bool muted
            required property string name
            required property bool solo
            required property int trackId
            property var visibleClips: []

            function updateClips(force: bool): void {
                let next = root.timelineModel.visibleClips(trackId, (root.scrollX - 150) / root.pixelsPerFrame, (root.scrollX + root.laneWidth + 150) / root.pixelsPerFrame);
                if (!force && next.length === visibleClips.length && next.every((clip, i) => clip.clipId === visibleClips[i].clipId))
                    return;
                visibleClips = next;
            }

            height: root.trackHeight
            width: tracks.width

            Component.onCompleted: updateClips()

            Connections {
                function onViewportRevisionChanged(): void {
                    row.updateClips();
                }

                target: root
            }
            Connections {
                function onClipsChangedForTrack(trackId: int): void {
                    if (trackId === row.trackId)
                        row.updateClips(true);
                }

                target: root.timelineModel
            }
            Rectangle {
                color: "#252a35"
                height: parent.height - 1
                width: root.headerWidth

                Text {
                    color: "#e1e5ee"
                    text: row.name
                    x: 12
                    y: 10
                }
                Row {
                    spacing: 6
                    x: 10
                    y: 34

                    Button {
                        checkable: true
                        checked: row.muted
                        height: 30
                        text: "M"
                        width: 36

                        onClicked: root.timelineModel.setTrackState(row.index, "muted", checked)
                    }
                    Button {
                        checkable: true
                        checked: row.solo
                        height: 30
                        text: "S"
                        width: 36

                        onClicked: root.timelineModel.setTrackState(row.index, "solo", checked)
                    }
                }
            }
            Rectangle {
                clip: true
                color: row.index % 2 ? "#202530" : "#1d222c"
                height: parent.height - 1
                width: root.laneWidth
                x: root.headerWidth

                Item {
                    height: parent.height
                    width: root.contentWidth
                    x: -root.scrollX

                    Repeater {
                        model: row.visibleClips

                        ClipItem {
                            required property var modelData

                            clipData: modelData
                            gridFrames: root.gridFrames
                            height: 64
                            pixelsPerFrame: root.pixelsPerFrame
                            selected: root.selectedClipId === clipData.clipId
                            width: Math.max(2, clipData.durationFrames * pixelsPerFrame)
                            x: clipData.startFrame * pixelsPerFrame
                            y: 8

                            onMoved: (deltaFrames, deltaY) => {
                                let targetRow = Math.max(0, Math.min(tracks.count - 1, row.index + Math.round(deltaY / root.trackHeight)));
                                root.timelineModel.editClip(clipData.clipId, root.timelineModel.trackIdAt(targetRow), Math.round((clipData.startFrame + deltaFrames) / root.gridFrames) * root.gridFrames, clipData.durationFrames);
                            }
                            onRequested: operation => root.timelineModel.action(clipData.clipId, row.trackId, operation)
                            onActivated: root.selectedClipId = clipData.clipId
                            onTrimmed: (leftDelta, rightDelta) => {
                                let start = Math.max(0, Math.min(clipData.startFrame + clipData.durationFrames - root.gridFrames, clipData.startFrame + leftDelta));
                                root.timelineModel.editClip(clipData.clipId, row.trackId, start, Math.max(root.gridFrames, clipData.durationFrames + clipData.startFrame - start + rightDelta));
                            }
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        color: "#f4b36e"
        height: root.height - 14
        visible: x >= root.headerWidth && x < root.width
        width: 2
        x: root.headerWidth + root.timelineModel.playheadFrame * root.pixelsPerFrame - root.scrollX
        y: 0
    }
    ScrollBar {
        height: 14
        orientation: Qt.Horizontal
        position: root.scrollX / root.contentWidth
        size: Math.min(1, root.laneWidth / root.contentWidth)
        width: root.laneWidth
        x: root.headerWidth
        y: root.height - 14

        onPositionChanged: if (pressed)
            root.scrollX = root.clampScroll(position * root.contentWidth)
    }
    WheelHandler {
        acceptedModifiers: Qt.KeyboardModifierMask

        onWheel: event => {
            if (event.modifiers & Qt.ControlModifier) {
                let mouseX = Math.max(0, event.x - root.headerWidth);
                let seconds = (root.scrollX + mouseX) / root.pixelsPerSecond;
                root.pixelsPerSecond = Math.max(12, Math.min(720, root.pixelsPerSecond * Math.pow(1.2, event.angleDelta.y / 120)));
                root.scrollX = root.clampScroll(seconds * root.pixelsPerSecond - mouseX);
            } else {
                root.scrollX = root.clampScroll(root.scrollX - (event.pixelDelta.x || event.pixelDelta.y || event.angleDelta.x || event.angleDelta.y));
            }
            event.accepted = true;
        }
    }
}
