import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's IP address on the app's own keypad (as frequency entry), so it
// can always be entered: on-screen or hardware keyboard alike. Typed digits
// and dots work too. ABC switches to the device's keyboard for a host name
// (Sebastian Schu DG5CST: a dynamic DNS name); a hardware keyboard types one
// on the keypad as well.
Dialog {
    id: dialog
    objectName: "addressDialog"
    modal: true
    anchors.centerIn: parent
    width: Math.min(420, parent.width - 24)
    contentHeight: column.implicitHeight
    height: Math.min(contentHeight + 2 * padding, parent.height - 16)
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
    readonly property int keyHeight: Math.max(36, Math.min(60, (parent.height - 16 - 2 * padding - 90 - 5 * Theme.spacing) / 5))

    property string entry: ""
    // The device's keyboard for a name instead of the keypad.
    property bool letters: false
    signal connectRequested(string address)

    readonly property bool ipLike: /^[0-9.]*$/.test(entry)
    function add(c) {
        if (entry.length >= 253) return
        if (c === "." && (entry.length === 0 || entry.endsWith(".") || (ipLike && entry.split(".").length > 3))) return
        entry += c
    }
    readonly property bool complete: ipLike
        ? /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(entry) && entry.split(".").every(p => Number(p) <= 255)
        : /^(?=.{1,253}$)[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*\.?$/.test(entry)
    function commit() {
        if (!complete) return
        dialog.close()
        dialog.connectRequested(entry)
    }
    function focusEntry() {
        if (letters) {
            nameField.text = entry
            nameField.forceActiveFocus()
        } else {
            keyCatcher.forceActiveFocus()
        }
    }

    onAboutToShow: {
        entry = Engine.lastAddress
        letters = !ipLike
    }
    onOpened: focusEntry()
    onLettersChanged: if (opened) focusEntry()

    Item {
        id: keyCatcher
        // The operate page leaves the keyboard focus here (see OperatePage).
        readonly property bool keepsKeyboard: true
        Keys.onPressed: (event) => {
            if ((event.key >= Qt.Key_0 && event.key <= Qt.Key_9) || (event.key >= Qt.Key_A && event.key <= Qt.Key_Z)
                    || event.key === Qt.Key_Minus) {
                dialog.add(event.text)
            } else if (event.key === Qt.Key_Period || event.key === Qt.Key_Comma) {
                dialog.add(".")
            } else if (event.key === Qt.Key_Backspace) {
                dialog.entry = dialog.entry.slice(0, -1)
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                dialog.commit()
            } else {
                return
            }
            event.accepted = true
        }
    }

    ColumnLayout {
        id: column
        width: dialog.availableWidth
        spacing: Theme.spacing

        Label {
            text: qsTr("Radio IP address or host name")
            color: Theme.textDim
            font.pixelSize: 15
        }
        Label {
            visible: !dialog.letters
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignRight
            text: dialog.entry.length > 0 ? dialog.entry : qsTr("e.g. 192.168.1.50")
            color: dialog.entry.length > 0 ? (dialog.complete ? Theme.text : Theme.warning) : Theme.textDim
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 18
            font.pixelSize: 34
            font.family: "monospace"
        }
        TextField {
            id: nameField
            objectName: "addressNameField"
            visible: dialog.letters
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            font.pixelSize: 22
            placeholderText: qsTr("e.g. radio.example.org")
            color: dialog.complete || text.length === 0 ? Theme.text : Theme.warning
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                              | Qt.ImhPreferLowercase
            EnterKey.type: Qt.EnterKeyGo
            onTextEdited: dialog.entry = text.trim()
            onAccepted: dialog.commit()
        }
        GridLayout {
            visible: !dialog.letters
            columns: 3
            Layout.fillWidth: true
            Repeater {
                // Phone order (1 2 3 on top), as the devices' own number pads.
                model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "⌫"]
                delegate: AppButton {
                    required property string modelData
                    text: modelData === "⌫" ? "" : modelData
                    font.pixelSize: 26
                    Layout.fillWidth: true
                    Layout.preferredHeight: dialog.keyHeight
                    BackspaceIcon {
                        anchors.centerIn: parent
                        visible: parent.modelData === "⌫"
                    }
                    onClicked: modelData === "⌫" ? dialog.entry = dialog.entry.slice(0, -1) : dialog.add(modelData)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.keyHeight
                text: qsTr("Cancel")
                font.pixelSize: 20
                onClicked: dialog.close()
            }
            AppButton {
                objectName: "addressLetters"
                Layout.preferredHeight: dialog.keyHeight
                leftPadding: 14
                rightPadding: 14
                text: dialog.letters ? "123" : "ABC"
                font.pixelSize: 20
                focusPolicy: Qt.NoFocus
                Accessible.name: dialog.letters ? qsTr("Number keypad") : qsTr("Keyboard for a host name")
                onClicked: dialog.letters = !dialog.letters
            }
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.keyHeight
                text: qsTr("Connect")
                font.pixelSize: 22
                enabled: dialog.complete
                onClicked: dialog.commit()
            }
        }
    }
}
