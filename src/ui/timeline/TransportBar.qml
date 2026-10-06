// Project tempo, time signature and tap tempo controls for the transport bar.
import QtQuick
import "../theme"

Row {
    id: root
    required property var timelineModel
    height: Theme.controlHeight
    spacing: Theme.space8
    Text {
        text: "BPM"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontNormal
        height: root.height
        verticalAlignment: Text.AlignVCenter
    }
    TempoControl { timelineModel: root.timelineModel; height: root.height }
    TimeSignatureControl { timelineModel: root.timelineModel; height: root.height }
    TapTempoButton { timelineModel: root.timelineModel; height: root.height }
}
