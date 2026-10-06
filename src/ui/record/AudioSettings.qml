// Audio settings popup; controller supplies device names, channels, selection and monitor state.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Popup {
    id: root
    required property var controller
    width: Theme.settingsWidth
    padding: Theme.space16
    modal: true
    onOpened: controller.refreshInputDevices()
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
    contentItem: ColumnLayout {
        spacing: Theme.space8
        Label { text: "Audio Settings"; color: Theme.textPrimary; font.pixelSize: Theme.fontNormal }
        ComboBox {
            id: devices
            Layout.fillWidth: true
            model: root.controller.deviceNames
            currentIndex: root.controller.selectedInputIndex
            enabled: !root.controller.busy && !root.controller.recording
            Accessible.name: "Input device"
            onActivated: {
                root.controller.selectInputDevice(currentIndex, 0, 1);
            }
        }
        ComboBox {
            id: channels
            Layout.fillWidth: true
            model: root.controller.inputChannelCount(devices.currentIndex) > 1
                ? ["Mono left", "Mono right", "Stereo"] : ["Mono left"]
            currentIndex: Math.min(count - 1, root.controller.selectedChannelIndex)
            enabled: devices.currentIndex > 0 && !root.controller.busy && !root.controller.recording
            Accessible.name: "Input channels"
            onActivated: root.controller.selectInputDevice(devices.currentIndex, currentIndex === 1 ? 1 : 0, currentIndex === 2 ? 2 : 1)
        }
        Switch {
            text: "Monitor input"
            checked: root.controller.monitorEnabled
            enabled: !root.controller.busy
            Accessible.name: text
            onClicked: root.controller.setMonitor(checked)
        }
        Label {
            Layout.fillWidth: true
            text: "Monitoring includes audio device latency."
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
        }
        Label {
            Layout.fillWidth: true
            text: "Output API: " + root.controller.outputApiName
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
        }
    }
}
