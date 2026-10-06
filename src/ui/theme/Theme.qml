pragma Singleton
import QtQuick

// Shared visual tokens for the timeline. Import ../theme and use Theme properties.
QtObject {
    readonly property color record: "#ed4257"
    readonly property color meterGreen: "#53c58a"
    readonly property color meterAmber: "#efba55"
    readonly property int meterWidth: 120
    readonly property int meterHeight: 12
    readonly property int pulseDuration: 650
    readonly property real pulseOpacity: 0.4
    readonly property int peakHoldDuration: 1000
    readonly property int messageDuration: 5000
    readonly property int settingsWidth: 360
    readonly property int windowWidth: 960
    readonly property int windowHeight: 600
    readonly property int minimumWindowWidth: 480
    readonly property int minimumWindowHeight: 320
    readonly property color background: "#161a22"
    readonly property color backgroundAlternate: "#1b202a"
    readonly property color surface: "#252c38"
    readonly property color border: "#455267"
    readonly property color textPrimary: "#f1f5fc"
    readonly property color textSecondary: "#b9c5d8"
    readonly property color textDisabled: "#8290a6"
    readonly property color accent: "#8ab4ff"
    readonly property color playhead: "#ffc080"
    readonly property color selection: "#ffffff"
    readonly property color hover: "#344156"
    readonly property color danger: "#ff929e"
    readonly property color transparent: "transparent"
    readonly property color waveform: "#d8e5f5"
    readonly property var trackPalette: ["#365d7b", "#655082", "#326759", "#785633", "#774c60", "#435e87", "#686131", "#3b666e"]
    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int radius: 5
    readonly property int controlRadius: 4
    readonly property int fontSmall: 11
    readonly property int fontNormal: 13
    readonly property int trackHeight: 82
    readonly property int headerWidth: 180
    readonly property int rulerHeight: 32
    readonly property int toolbarHeight: 40
    readonly property int scrollbarHeight: 14
    readonly property int controlHeight: 28
    readonly property int buttonWidth: 36
    readonly property int readoutWidth: 210
    readonly property int lineWidth: 1
    readonly property int focusWidth: 2
    readonly property int tickHeight: 10
    readonly property int clipMinimumWidth: 2
    readonly property int waveformWidth: 3
    readonly property int waveformStep: 7
    readonly property int waveformHeight: 26
    readonly property int waveformMinimum: 4
    readonly property int waveformMaximumBars: 16
    readonly property real waveformOpacity: 0.65
    readonly property int hoverDuration: 90
    readonly property int selectionDuration: 100
    readonly property int tooltipDelay: 500
    readonly property int viewportDelay: 40
    readonly property int trackCacheRows: 0
    readonly property int cullMargin: 150
    readonly property int tickSpacing: 70
    readonly property real defaultZoom: 90
    readonly property real minimumZoom: 0.1
    readonly property real maximumZoom: 720
    readonly property real zoomFactor: 1.2
    readonly property int wheelStep: 120
}
