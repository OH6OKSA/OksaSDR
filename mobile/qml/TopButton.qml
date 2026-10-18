import QtQuick
import QtQuick.Controls
import AetherMobile

// The top row's buttons: a ToolButton's size in the app's button colours
// (the Basic style's light grey looked disabled next to the dark buttons).
ToolButton {
    id: control
    focusPolicy: Qt.NoFocus
    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.buttonText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1.0 : 0.4
    }
    background: Rectangle {
        implicitWidth: 40
        implicitHeight: 40
        radius: 4
        color: control.down ? Theme.buttonFaceDown : Theme.buttonFace
    }
}
