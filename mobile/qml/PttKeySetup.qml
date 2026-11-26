import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Setup › Transmit, this device's part (roadmap 131): the learned PTT key,
// moved here from the TX tab. Not in CW, where the key is the CW key.
ColumnLayout {
    spacing: Theme.spacing / 2
    // A learned PTT key (roadmap 121, Rob Metallo VA3MET's UNIWA F400, whose
    // side key sends F2): the next key pressed after Learn becomes PTT,
    // held as long as the key (Tap on / off: a press toggles), wherever the
    // keyboard is; it no longer acts as itself. Space stays PTT beside it,
    // unless switched off (the page's next row, roadmap 209).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("PTT key"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        Label {
            objectName: "pttKeyName"
            Layout.fillWidth: true
            text: Engine.pttKeyLearning ? qsTr("Press the key now…")
                  : Engine.pttKey !== 0 ? Engine.pttKeyName : qsTr("None")
            color: Engine.pttKeyLearning ? Theme.warning : Theme.text
            font.pixelSize: Theme.px(15)
            elide: Text.ElideRight
        }
        AppButton {
            objectName: "pttKeyLearn"
            text: Engine.pttKeyLearning ? qsTr("Cancel") : qsTr("Learn")
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.pttKeyLearning ? Engine.cancelPttKeyLearn() : Engine.learnPttKey()
        }
        AppButton {
            objectName: "pttKeyClear"
            visible: Engine.pttKey !== 0 && !Engine.pttKeyLearning
            text: qsTr("Clear")
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.clearPttKey()
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("For a button that sends a key (a handset's side PTT key, a Bluetooth button or foot switch): tap Learn, then press it. Held, it keys as PTT; it no longer does what the key did before. Space is PTT beside it (below).")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

}
