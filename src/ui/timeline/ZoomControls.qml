// Three zoom actions. Emits zoomRequested(factor) or fitRequested; contains no viewport state.
import QtQuick
import "../theme"

Row {
    signal zoomRequested(real factor)
    signal fitRequested()
    id: root
    spacing: Theme.space4
    ToolButton { text: "−"; hint: "Zoom out (−)"; onClicked: root.zoomRequested(1 / Theme.zoomFactor) }
    ToolButton { text: "+"; hint: "Zoom in (+)"; onClicked: root.zoomRequested(Theme.zoomFactor) }
    ToolButton { text: "Fit"; hint: "Zoom to fit (Ctrl+0)"; onClicked: root.fitRequested() }
}
