// Visible bar/beat or time ruler and draggable playhead target. Requires viewport scale and tempo;
// emits frameRequested(frame, alt) so the viewport can snap the scrub through the single snap path.
// Lines are drawn on one Canvas so the tick count stays cheap; only the labels are items.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import "../theme"
import "SnapMath.js" as SnapMath

Rectangle {
    id: root
    required property real scrollX
    required property real pixelsPerSecond
    required property real pixelsPerFrame
    required property real tickSeconds
    required property real gridFrames
    required property real framesPerBeat
    required property real framesPerBar
    required property real beatsPerBar
    required property int sampleRate
    property string mode: "barsBeats"
    signal frameRequested(real frame, bool alt)
    readonly property bool barsMode: mode !== "time"
    readonly property real timeStepFrames: Math.max(1, tickSeconds * sampleRate)
    readonly property real tickStepFrames: barsMode ? gridFrames : timeStepFrames
    readonly property real tickStepPixels: Math.max(1, tickStepFrames * pixelsPerFrame)
    readonly property int tickCount: Math.ceil(width / tickStepPixels) + 2
    readonly property real firstTickFrame: Math.floor(scrollX / pixelsPerFrame / tickStepFrames) * tickStepFrames
    readonly property real barPixels: Math.max(0.0001, framesPerBar * pixelsPerFrame)
    readonly property int barStride: {
        let stride = 1;
        while (stride * barPixels < Theme.rulerLabelSpacing)
            stride *= 2;
        return stride;
    }
    readonly property int barCount: Math.ceil(width / Math.max(1, barStride * barPixels)) + 2
    readonly property real firstBarIndex: Math.floor(Math.floor(scrollX / pixelsPerFrame / framesPerBar) / barStride) * barStride
    color: Theme.surface
    clip: true
    height: Theme.rulerHeight
    activeFocusOnTab: true
    Accessible.name: barsMode ? "Bars and beats ruler; drag to seek, Home/End to start/end"
                              : "Time ruler; drag to seek, Home/End to start/end"
    Accessible.role: Accessible.Slider
    border.color: activeFocus ? Theme.accent : Theme.border
    border.width: Theme.lineWidth
    onScrollXChanged: canvas.requestPaint()
    onPixelsPerSecondChanged: canvas.requestPaint()
    onGridFramesChanged: canvas.requestPaint()
    onModeChanged: canvas.requestPaint()
    onFramesPerBeatChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            ctx.lineWidth = Theme.lineWidth;
            const bottom = root.height;
            if (root.barsMode) {
                for (let i = 0; i < root.tickCount; ++i) {
                    const frame = root.firstTickFrame + i * root.tickStepFrames;
                    const x = Math.round(frame * root.pixelsPerFrame - root.scrollX) + 0.5;
                    if (x < 0 || x > width)
                        continue;
                    const beats = frame / root.framesPerBeat;
                    const beat = Math.abs(beats - Math.round(beats)) * root.framesPerBeat * root.pixelsPerFrame < Theme.lineWidth * 2;
                    ctx.strokeStyle = beat ? Theme.textSecondary : Theme.textDisabled;
                    const tickHeight = beat ? Theme.rulerBeatTickHeight : Theme.tickHeight;
                    ctx.beginPath();
                    ctx.moveTo(x, bottom - tickHeight);
                    ctx.lineTo(x, bottom);
                    ctx.stroke();
                }
                ctx.strokeStyle = Theme.textPrimary;
                for (let i = 0; i < root.barCount; ++i) {
                    const barIndex = root.firstBarIndex + i * root.barStride;
                    const x = Math.round(barIndex * root.framesPerBar * root.pixelsPerFrame - root.scrollX) + 0.5;
                    if (x < -Theme.rulerLabelSpacing || x > width)
                        continue;
                    ctx.beginPath();
                    ctx.moveTo(x, bottom - Theme.rulerBarTickHeight);
                    ctx.lineTo(x, bottom);
                    ctx.stroke();
                }
            } else {
                ctx.strokeStyle = Theme.textDisabled;
                for (let i = 0; i < root.tickCount; ++i) {
                    const frame = root.firstTickFrame + i * root.timeStepFrames;
                    const x = Math.round(frame * root.pixelsPerFrame - root.scrollX) + 0.5;
                    if (x < 0 || x > width)
                        continue;
                    ctx.beginPath();
                    ctx.moveTo(x, bottom - Theme.tickHeight);
                    ctx.lineTo(x, bottom);
                    ctx.stroke();
                }
            }
        }
    }
    Repeater {
        model: root.barsMode ? root.barCount : 0
        Text {
            required property int index
            readonly property real barIndex: root.firstBarIndex + index * root.barStride
            readonly property real position: barIndex * root.framesPerBar * root.pixelsPerFrame - root.scrollX
            text: (barIndex + 1).toString()
            visible: position > -Theme.rulerLabelSpacing && position < root.width
            x: position + Theme.space4
            y: Theme.space4
            color: Theme.textPrimary
            font.pixelSize: Theme.fontSmall
        }
    }
    Repeater {
        model: root.barsMode ? 0 : root.tickCount
        Text {
            required property int index
            readonly property real frame: root.firstTickFrame + index * root.timeStepFrames
            readonly property real position: frame * root.pixelsPerFrame - root.scrollX
            text: SnapMath.formatTime(frame, root.sampleRate)
            visible: position > -Theme.rulerLabelSpacing && position < root.width
            x: position + Theme.space4
            y: Theme.space4
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        Controls.ToolTip.visible: containsMouse && !pressed
        Controls.ToolTip.delay: Theme.tooltipDelay
        Controls.ToolTip.text: root.Accessible.name
        onPressed: mouse => { root.forceActiveFocus(); root.frameRequested((mouse.x + root.scrollX) / root.pixelsPerFrame, mouse.modifiers & Qt.AltModifier); }
        onPositionChanged: mouse => { if (pressed) root.frameRequested((mouse.x + root.scrollX) / root.pixelsPerFrame, mouse.modifiers & Qt.AltModifier); }
    }
}
