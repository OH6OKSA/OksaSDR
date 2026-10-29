import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A popup's content as a column that scrolls where the window is lower than
// the popup (a phone on its side cut off the meter chooser's last row, four
// slices the volume panel's). A scroll bar shows then, so the rows below are
// not missed; otherwise it is a plain column.
Flickable {
    id: scroll
    default property alias content: column.data
    property alias spacing: column.spacing
    // A popup sized by its content grows by the bar, so fixed-width rows
    // (the meter chooser's) keep their room beside it.
    implicitWidth: column.implicitWidth + (overflows ? bar.width + 2 : 0)
    implicitHeight: column.implicitHeight
    contentHeight: column.implicitHeight
    contentWidth: width
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    readonly property bool overflows: column.implicitHeight > height + 1
    // Only then: a slider drag that wanders a little up or down must not be
    // taken for a scroll in a panel that has nothing to scroll.
    interactive: overflows
    ColumnLayout {
        id: column
        // Beside the bar, not under it, while it shows.
        width: scroll.width - (scroll.overflows ? bar.width + 2 : 0)
    }
    ScrollBar.vertical: ScrollBar {
        id: bar
        policy: scroll.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    }
}
