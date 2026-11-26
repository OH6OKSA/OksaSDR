import QtQuick
import QtQuick.Controls

// The app's button: the Basic style's colours with rounded corners. One
// size everywhere (the band buttons'); only PTT and KEY are larger.
Button {
    id: control
    // Never takes the keyboard focus: a tapped button would otherwise catch
    // the Magic Keyboard's Space (pressing itself instead of PTT) and arrows.
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.buttonFont
    leftPadding: 4
    rightPadding: 4
    // An amber frame: something keeps running from this button (a macro
    // that repeats), so it is never left on unseen.
    property bool warn: false
    // Lit only to stand out (the confirm button of a dialog, the worked
    // call to clear): checked is then a look, not a state, and is not said.
    property bool primary: false

    // What a screen reader says of the state: Qt gives a button's checked
    // to VoiceOver and TalkBack only when the button is checkable, and a
    // checkable AppButton would flip itself on a tap before onClicked (the
    // toggles here bind checked to the radio and read !checked). So the
    // checked look is told as selected: iOS (and the iPad app on a Mac) has
    // the Selected trait for it, which Qt sets from the selected state;
    // Android and Linux have no such state through Qt, so the word goes in
    // the description, which TalkBack reads after the name. A button that
    // sets its own description says its state there itself.
    Accessible.selected: control.checked && !control.primary
    Accessible.description: control.checked && !control.primary && Qt.platform.os !== "ios" ? qsTr("selected") : ""

    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: control.checked ? Theme.buttonTextOn : Theme.buttonText
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        implicitWidth: Theme.buttonWidth
        implicitHeight: Theme.touchTarget
        radius: Theme.radius
        color: control.checked ? (control.down ? Theme.buttonFaceOnDown : Theme.buttonFaceOn)
             : control.down ? Theme.buttonFaceDown : Theme.buttonFace
        opacity: control.enabled ? 1.0 : 0.5
        border.width: control.warn ? 2 : 0
        border.color: Theme.warning
    }
}
