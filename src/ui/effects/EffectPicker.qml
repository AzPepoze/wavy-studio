// Effect factory menu; controller supplies entries, picked(typeId) selects one.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"

Menu {
    id: root
    objectName: "effect-picker"
    required property var controller
    palette.window: Theme.surface
    palette.text: Theme.textPrimary
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.hover
    palette.highlightedText: Theme.textPrimary
    signal picked(string typeId)
    Instantiator {
        model: root.controller.availableEffects()
        delegate: MenuItem {
            required property var modelData
            text: modelData.displayName
            font.pixelSize: Theme.fontNormal
            onTriggered: root.picked(modelData.id)
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
