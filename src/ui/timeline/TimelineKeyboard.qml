// Window shortcuts for timeline commands, including when a child control owns focus. Requires edit coordinator.
import QtQuick

Item {
    id: root
    required property TimelineEditing editing
    function press(key: int, modifiers: int): void { editing.handle({key: key, modifiers: modifiers, accepted: false}); }
    Shortcut { sequence: "Space"; onActivated: root.press(Qt.Key_Space, Qt.NoModifier) }
    Shortcut { sequence: "Delete"; onActivated: root.press(Qt.Key_Delete, Qt.NoModifier) }
    Shortcut { sequence: "Ctrl+D"; onActivated: root.press(Qt.Key_D, Qt.ControlModifier) }
    Shortcut { sequence: "S"; onActivated: root.press(Qt.Key_S, Qt.NoModifier) }
    Shortcut { sequence: "Home"; onActivated: root.press(Qt.Key_Home, Qt.NoModifier) }
    Shortcut { sequence: "End"; onActivated: root.press(Qt.Key_End, Qt.NoModifier) }
    Shortcut { sequences: ["+", "="]; onActivated: root.press(Qt.Key_Plus, Qt.NoModifier) }
    Shortcut { sequence: "-"; onActivated: root.press(Qt.Key_Minus, Qt.NoModifier) }
    Shortcut { sequence: "Ctrl+0"; onActivated: root.press(Qt.Key_0, Qt.ControlModifier) }
    Shortcut { sequence: "Left"; onActivated: root.press(Qt.Key_Left, Qt.NoModifier) }
    Shortcut { sequence: "Right"; onActivated: root.press(Qt.Key_Right, Qt.NoModifier) }
    Shortcut { sequence: "Up"; onActivated: root.press(Qt.Key_Up, Qt.NoModifier) }
    Shortcut { sequence: "Down"; onActivated: root.press(Qt.Key_Down, Qt.NoModifier) }
    Shortcut { sequence: "Escape"; onActivated: root.press(Qt.Key_Escape, Qt.NoModifier) }
}
