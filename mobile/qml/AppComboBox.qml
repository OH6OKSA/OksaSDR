import QtQuick
import QtQuick.Controls
import AetherMobile

// A ComboBox whose list stays inside the window's safe area and scrolls by
// a bar that stays. Basic's list kept 6 px from the window's edges, so a
// long list (the global and mic profiles) ran under a phone's status and
// navigation bars, its first and last entries out of reach in portrait
// (Larry Averbeck, a Galaxy S26 Ultra), and its thin indicator faded.
ComboBox {
    id: box
    popup: Popup {
        // The window's insets, as the overlay (the whole window) has them.
        readonly property var insets: box.Overlay.overlay ? box.Overlay.overlay.SafeArea.margins : null
        y: box.height
        width: box.width
        topMargin: Theme.spacing + (insets ? insets.top : 0)
        bottomMargin: Theme.spacing + (insets ? insets.bottom : 0)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         (box.Window.height > 0 ? box.Window.height : 800) - topMargin - bottomMargin)
        padding: 1
        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            readonly property bool overflows: contentHeight > height + 1
            ScrollBar.vertical: AppScrollBar { policy: list.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        }
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
    }
}
