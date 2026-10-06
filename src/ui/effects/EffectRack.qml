// Selected track rack; controller exposes chain/parameter models, trackId chooses track (zero is master).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../timeline"

Rectangle {
    id: root
    required property var controller
    property int trackId: -1
    property string trackName: controller.trackName(trackId)
    color: Theme.backgroundAlternate
    border.color: Theme.border
    border.width: Theme.lineWidth
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space12
        spacing: Theme.space8
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.trackId < 0 ? "Select a track to edit effects" : root.trackName + " · Effects"; color: Theme.textPrimary; font.pixelSize: Theme.fontNormal; Layout.fillWidth: true }
            ToolButton {
                objectName: "add-effect"
                text: "Add effect"; implicitWidth: Theme.headerWidth
                enabled: root.trackId >= 0
                onClicked: picker.popup()
                EffectPicker { id: picker; controller: root.controller; onPicked: typeId => root.controller.addEffect(root.trackId, typeId) }
            }
        }
        ListView {
            id: chain
            objectName: "effect-chain"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            spacing: Theme.space8
            model: root.controller.trackChainModel(root.trackId)
            ScrollBar.vertical: ScrollBar {}
            delegate: EffectSlotView {
                required property var model
                width: chain.width
                controller: root.controller; trackId: root.trackId; slot: model; chainCount: chain.count; active: root.visible
            }
            Text {
                anchors.centerIn: parent
                visible: chain.count === 0 && root.trackId >= 0
                text: "No effects · Add an effect to shape this track"
                color: Theme.textSecondary; font.pixelSize: Theme.fontNormal
            }
        }
    }
}
