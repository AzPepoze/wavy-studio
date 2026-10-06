// Non-interactive vertical marker. Its caller sets x, height and visibility; snap selects the marker colour.
import QtQuick
import "../theme"

Rectangle {
    property bool snap: false
    color: snap ? Theme.accent : Theme.playhead
    width: Theme.focusWidth
}
