import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "timeline"
import "effects"
import "record"
import "theme"

ApplicationWindow {
    id: window
    readonly property var recordingController: recorder
    property bool closePending: false
    onClosing: close => {
        if (recordingController.recording || recordingController.busy) {
            close.accepted = false;
            closePending = true;
            if (recordingController.recording && !recordingController.busy)
                recordingController.stopRecording();
        }
    }
    property string recordingMessage: ""
    readonly property var timeline: timelineModel
    visible: true
    width: Theme.windowWidth
    height: Theme.windowHeight
    minimumWidth: Theme.minimumWindowWidth
    minimumHeight: Theme.minimumWindowHeight
    title: "Wavy Studio"
    font.pixelSize: Theme.fontNormal
    color: Theme.background
    palette.window: Theme.background
    palette.windowText: Theme.textPrimary
    palette.button: Theme.surface
    palette.buttonText: Theme.textPrimary
    palette.base: Theme.backgroundAlternate
    palette.text: Theme.textPrimary
    palette.highlight: Theme.accent

    header: ToolBar {
        implicitHeight: Theme.toolbarHeight + Theme.space16
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.space8
            Label { text: "WAVY STUDIO"; font.bold: true; Layout.rightMargin: Theme.space16 }
            Button { enabled: !window.recordingController.busy && !window.recordingController.recording; Accessible.name: text; text: audioEngine.playing ? "Pause" : "Play"; onClicked: audioEngine.togglePlay() }
            Button { text: "Stop"; Accessible.name: text; onClicked: window.recordingController.recording ? window.recordingController.stopRecording() : audioEngine.stop() }
            RecordButton { controller: window.recordingController }
            InputMeter { level: window.recordingController.inputPeak }
            Label { visible: window.recordingController.recording; text: window.recordingController.elapsedSeconds.toFixed(1) + " s" }
            TransportBar { timelineModel: window.timeline; Layout.alignment: Qt.AlignVCenter }
            Button { text: "⚙"; Accessible.name: "Audio settings"; onClicked: settings.open() }
            Item { Layout.fillWidth: true }
            Label { text: "Transport"; color: Theme.textSecondary }
        }
    }
    TimelineView {
        id: timelineView
        objectName: "timeline-view"
        effectsVisible: rack.visible
        bottomInset: rack.visible ? rack.height : 0
        onEffectsRequested: rack.visible = !rack.visible
        anchors.fill: parent
        anchors.margins: Theme.space16
        timelineModel: window.timeline
        recordingController: window.recordingController
        settings: userSettings
        onPlayPauseRequested: audioEngine.togglePlay()
    }
    AudioSettings { id: settings; controller: window.recordingController; anchors.centerIn: Overlay.overlay }
    Shortcut {
        sequence: "R"
        enabled: !settings.opened && !(window.activeFocusItem instanceof TextInput) && !(window.activeFocusItem instanceof TextEdit)
        onActivated: window.recordingController.toggleRecord()
    }
    Connections {
        target: window.recordingController
        function onStateChanged(): void {
            if (window.closePending && !window.recordingController.busy) {
                if (window.recordingController.recording)
                    window.recordingController.stopRecording();
                else
                    window.close();
            }
        }
        function onErrorChanged(): void {
            window.recordingMessage = window.recordingController.lastError;
            messageTimer.restart();
        }
    }
    Timer { id: messageTimer; interval: Theme.messageDuration; onTriggered: window.recordingMessage = "" }
    EffectRack {
        id: rack
        objectName: "effect-rack"
        visible: false
        controller: effects
        trackId: timelineView.selectedTrackId
        anchors.left: timelineView.left
        anchors.right: timelineView.right
        anchors.bottom: timelineView.bottom
        height: Math.min(Theme.rackHeight, timelineView.height / 2)
    }
    Connections {
        target: window.timeline
        function onTimelineChanged(): void {
            if (!effects.hasTrack(timelineView.selectedTrackId)) timelineView.selectedTrackId = -1;
        }
    }
    Shortcut { sequence: "Ctrl+Z"; onActivated: timelineModel.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: timelineModel.redo() }
    footer: ToolBar {
        implicitHeight: Theme.toolbarHeight
        background: Rectangle { color: Theme.surface }
        Label {
            anchors.fill: parent
            anchors.margins: Theme.space8
            text: window.recordingMessage || ((window.recordingController.recording ? "Recording" : audioEngine.playbackState) + " • " + (audioEngine.positionFrames / audioEngine.sampleRate).toFixed(2) + " s • " + audioEngine.sampleRate + " Hz • " + (audioEngine.running ? "Device session open" : "Device closed"))
        }
    }
}
