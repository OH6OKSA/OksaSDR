import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Setup › Phone/CW, this device's part (roadmap 131): the key (straight or
// paddles) and the keyboard's paddles, moved here from the CW tab, which
// keeps what is used while sending. The same Engine calls and values.
ColumnLayout {
    spacing: Theme.spacing / 2
    // Keying in PTT's place: a straight key, or paddles (dit, dah) timed by
    // the app's iambic keyer, which follows the radio's iambic mode and
    // swapped paddles (CW keyer below).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Key"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppButton {
            Accessible.name: qsTr("%1: %2").arg(qsTr("Key")).arg(text)
            text: qsTr("Straight key")
            checked: !Engine.cwPaddles
            Layout.fillWidth: true
            onClicked: Engine.cwPaddles = false
        }
        AppButton {
            Accessible.name: qsTr("%1: %2").arg(qsTr("Key")).arg(text)
            text: qsTr("Paddles")
            checked: Engine.cwPaddles
            Layout.fillWidth: true
            onClicked: Engine.cwPaddles = true
        }
    }
    Label {
        visible: Engine.cwPaddles
        Layout.fillWidth: true
        text: qsTr("Dit and dah take PTT's place; hold both for a squeeze. The keyer follows Iambic A / B and Swap paddles under CW keyer below: B adds one more element after a squeeze, A does not; Swap puts dah on the left.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // Paddles on a hardware keyboard: which two keys are dit and dah.
    Label {
        visible: Engine.cwPaddles
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        text: qsTr("Paddles on a keyboard   (left key dit, right key dah)")
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.cwPaddles
        Repeater {
            model: [{ key: "off", label: qsTr("Off") }, { key: "ctrl", label: "Ctrl" },
                    { key: "alt", label: "Alt ⌥" }, { key: "brackets", label: "[  ]" }]
            delegate: AppButton {
                required property var modelData
                // "[  ]" and "⌥" in words.
                Accessible.name: qsTr("%1: %2").arg(qsTr("Paddles on a keyboard"))
                                 .arg(modelData.key === "brackets" ? qsTr("square brackets")
                                      : modelData.key === "alt" ? qsTr("Alt / Option") : text)
                text: modelData.label
                checked: Engine.cwKeyboardPaddles === modelData.key
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.cwKeyboardPaddles = modelData.key
            }
        }
    }
    Label {
        visible: Engine.cwPaddles
        Layout.fillWidth: true
        text: {
            switch (Engine.cwKeyboardPaddles) {
            case "off": return qsTr("The keyboard does not key CW; use the dit and dah pads on the screen.")
            case "ctrl": return qsTr("Left Ctrl = dit, right Ctrl = dah. USB paddle adapters (Vail, VBand) send these.")
            case "alt": return qsTr("Left Alt / Option = dit, right Alt / Option = dah. For keyboards with no right Ctrl, such as Apple's Magic Keyboard.")
            default: return qsTr("[ = dit, ] = dah. Where [ and ] need AltGr or Option (Finnish, German…), pick Ctrl or Alt.")
            }
        }
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

}
