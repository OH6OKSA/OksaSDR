import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A panel's title row, the same in every panel (PanelContent): its name at
// the left, Close at the top right (Larry Averbeck: a tap outside closes it
// too; Close says so), and room between them for a control of the panel's
// own (TNF on / off, Whole band).
RowLayout {
    id: header
    property string title
    // The popup Close closes.
    property Popup panel
    // A long title wraps in this width instead of widening a panel sized by
    // its content (the filter panel's RADE line); unset, one line, elided.
    property real titleWidth: -1
    default property alias extras: extraRow.data
    Layout.fillWidth: true
    spacing: Theme.spacing / 2
    Label {
        text: header.title
        color: Theme.text
        font.pixelSize: Theme.labelFont
        font.bold: true
        elide: header.titleWidth < 0 ? Text.ElideRight : Text.ElideNone
        wrapMode: header.titleWidth < 0 ? Text.NoWrap : Text.Wrap
        Layout.preferredWidth: header.titleWidth
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
    RowLayout {
        id: extraRow
        spacing: Theme.spacing / 2
        visible: children.length > 0
    }
    AppButton {
        objectName: "panelClose"
        text: qsTr("Close")
        focusPolicy: Qt.NoFocus
        Layout.preferredWidth: Theme.buttonWidth * 1.5
        Layout.preferredHeight: Theme.touchTarget
        onClicked: header.panel.close()
    }
}
