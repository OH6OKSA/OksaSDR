import QtQuick
import QtQuick.Controls

// A scrolling page or tab that shows when it scrolls: while its content is
// taller than the view, a scroll bar stays at the right (the usability
// review: the controls tabs, Radio Setup and the connect page scrolled with
// no sign of it until touched; Michael Whaley: a scroll must look like one)
// and the content keeps clear of it; otherwise no bar and no margin.
ScrollView {
    id: scroll
    readonly property bool overflows: contentHeight > availableHeight + 1
    clip: true
    contentWidth: availableWidth
    rightPadding: overflows ? ScrollBar.vertical.width + 2 : 0
    ScrollBar.vertical.policy: overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
}
