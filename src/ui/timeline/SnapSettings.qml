// Snap preferences popup. Writes straight to the injected settings object.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.Popup {
    id: root
    required property var settings
    readonly property var divisions: [
        { key: "auto", label: "Auto" },
        { key: "bar", label: "Bar" },
        { key: "1/2", label: "1/2" },
        { key: "1/4", label: "1/4" },
        { key: "1/8", label: "1/8" },
        { key: "1/16", label: "1/16" },
        { key: "1/32", label: "1/32" },
        { key: "1/2T", label: "1/2T" },
        { key: "1/4T", label: "1/4T" },
        { key: "1/8T", label: "1/8T" },
        { key: "1/16T", label: "1/16T" }
    ]
    width: Theme.snapPopupWidth
    padding: Theme.snapPopupPadding
    modal: false
    focus: true
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        border.width: Theme.lineWidth
        radius: Theme.radius
    }
    contentItem: Column {
        spacing: Theme.space8
        TimelineCheckBox {
            text: "Snapping"
            checked: root.settings.snapEnabled
            onToggled: root.settings.snapEnabled = checked
            Accessible.name: text
        }
        Text { text: "Grid"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
        Grid {
            columns: 4
            spacing: Theme.space4
            Repeater {
                model: root.divisions
                ToolButton {
                    required property var modelData
                    text: modelData.label
                    checkable: true
                    checked: root.settings.snapDivision === modelData.key
                    hint: "Snap grid " + modelData.label
                    implicitWidth: Theme.snapDivisionButtonWidth
                    onClicked: root.settings.snapDivision = modelData.key
                }
            }
        }
        Text { text: "Magnets"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
        TimelineCheckBox {
            text: "Snap to clip edges"
            checked: root.settings.snapToClipEdges
            onToggled: root.settings.snapToClipEdges = checked
        }
        TimelineCheckBox {
            text: "Snap to playhead"
            checked: root.settings.snapToPlayhead
            onToggled: root.settings.snapToPlayhead = checked
        }
        Row {
            spacing: Theme.space8
            Controls.Label { text: "Tolerance"; color: Theme.textSecondary; font.pixelSize: Theme.fontNormal }
            Controls.Slider {
                id: tolerance
                from: 1
                to: 64
                stepSize: 1
                width: Theme.snapToleranceSliderWidth
                value: root.settings.snapTolerancePixels
                onMoved: root.settings.snapTolerancePixels = Math.round(value)
                Accessible.name: "Snap tolerance in pixels"
            }
            Controls.Label {
                text: Math.round(tolerance.value) + " px"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontNormal
            }
        }
    }
}
