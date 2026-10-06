import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "timeline"

ApplicationWindow {
    id: window
    readonly property var timeline: timelineModel
    visible: true
    width: 960
    height: 600
    minimumWidth: 480
    minimumHeight: 320
    title: "Wavy Studio"
    color: "#181b22"
    palette.window: "#181b22"
    palette.windowText: "#e1e5ee"
    palette.button: "#303644"
    palette.buttonText: "#e1e5ee"
    palette.base: "#222630"
    palette.text: "#e1e5ee"
    palette.highlight: "#739cec"

    header: ToolBar {
        background: Rectangle { color: "#252a35" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label { text: "WAVY STUDIO"; font.bold: true; Layout.rightMargin: 20 }
            Button { text: audioEngine.playing ? "Pause" : "Play"; onClicked: audioEngine.togglePlay() }
            Button { text: "Stop"; onClicked: audioEngine.stop() }
            Item { Layout.fillWidth: true }
            Label { text: "Transport"; color: "#9099ac" }
        }
    }
    TimelineView {
        anchors.fill: parent
        anchors.margins: 16
        timelineModel: window.timeline
        onPlayPauseRequested: audioEngine.togglePlay()
    }
    Shortcut { sequence: "Ctrl+Z"; onActivated: timelineModel.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: timelineModel.redo() }
    footer: ToolBar {
        background: Rectangle { color: "#252a35" }
        Label {
            anchors.fill: parent
            anchors.margins: 8
            text: audioEngine.playbackState + " • " + (audioEngine.positionFrames / audioEngine.sampleRate).toFixed(2) + " s • " + audioEngine.sampleRate + " Hz • " + (audioEngine.running ? "Device session open" : "Device closed")
        }
    }
}
