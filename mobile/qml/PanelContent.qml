import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The base every panel is built on, as its contentItem: the title row
// (PanelHeader: the name, Close at the top right) and under it the panel's
// own rows, which scroll where the window is lower than the panel
// (PopupScroll). The title row stays put, so Close is always in sight.
ColumnLayout {
    id: panelContent
    property alias title: header.title
    property alias panel: header.panel
    property alias titleWidth: header.titleWidth
    // Controls of the panel's own in the title row, between the name and
    // Close (TNF on / off, Whole band).
    property alias extras: header.extras
    // Between the rows under the title.
    property alias rowSpacing: scroll.spacing
    default property alias content: scroll.content
    spacing: Theme.spacing / 2
    PanelHeader { id: header }
    PopupScroll {
        id: scroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
    }
}
