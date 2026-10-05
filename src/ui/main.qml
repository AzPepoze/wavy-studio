import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
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
            Button { text: "Play"; enabled: !audioEngine.running; onClicked: audioEngine.start() }
            Button { text: "Stop"; enabled: audioEngine.running; onClicked: audioEngine.stop() }
            Item { Layout.fillWidth: true }
            Label { text: "Transport"; color: "#9099ac" }
        }
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16
        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: "#222630"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Label { text: "Tracks"; font.bold: true }
                Label { text: "No tracks yet"; color: "#9099ac" }
                Item { Layout.fillHeight: true }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: "Your workspace"
            color: "#9099ac"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    footer: ToolBar {
        background: Rectangle { color: "#252a35" }
        Label {
            anchors.fill: parent
            anchors.margins: 8
            text: audioEngine.running ? "Running • " + audioEngine.sampleRate + " Hz • Silent output" : "Stopped"
        }
    }
}
