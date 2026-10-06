// Check box with Theme-coloured label, for use in popups outside the window palette.
import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.CheckBox {
    id: root
    contentItem: Text {
        text: root.text
        color: root.enabled ? Theme.textPrimary : Theme.textDisabled
        font.pixelSize: Theme.fontNormal
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
