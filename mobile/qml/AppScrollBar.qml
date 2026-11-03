import QtQuick
import QtQuick.Controls
import AetherMobile

// The app's scroll bar: wide enough to take with a finger or a stylus, and
// kept so. Basic's bar narrowed from 6 to 2 px and stopped taking presses
// once it was touched, so on a phone in portrait it shrank or faded and the
// list had to be dragged from inside, moving a setting on the way (Larry
// Averbeck, a Galaxy S26 Ultra with a stylus).
ScrollBar {
    id: bar
    interactive: true
    padding: 2
    minimumSize: 0.1
    contentItem: Rectangle {
        implicitWidth: bar.orientation === Qt.Vertical ? 8 : 100
        implicitHeight: bar.orientation === Qt.Horizontal ? 8 : 100
        radius: 4
        color: bar.pressed ? Theme.accent : Theme.textDim
        opacity: bar.policy === ScrollBar.AlwaysOn || (bar.active && bar.size < 1.0) ? 0.8 : 0.0
    }
}
