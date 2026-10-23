import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// FT8 and FT4 on this device: decode the receiver slot by slot, call CQ or
// tap a decode to answer it; the app runs the QSO (reports, RR73 / 73) and
// logs it. Transmit audio goes to the radio in DIGU, past the mic chain.
ColumnLayout {
    id: digi
    spacing: Theme.spacing / 2

    readonly property var qso: Engine.digi
    readonly property real slotSeconds: qso.slotSeconds || 15
    property real slotPos: 0

    Timer {
        interval: 200
        repeat: true
        running: digi.visible && Engine.digiRunning
        onTriggered: digi.slotPos = (Date.now() / 1000) % digi.slotSeconds
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            objectName: "digiDecode"
            text: Engine.digiRunning ? qsTr("Decoding") : qsTr("Decode")
            checked: Engine.digiRunning
            enabled: Engine.connectionState === "connected"
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiRunning = !Engine.digiRunning
        }
        Repeater {
            model: ["FT8", "FT4"]
            delegate: AppButton {
                required property string modelData
                text: modelData
                checked: Engine.digiProtocol === modelData
                Layout.preferredWidth: 64
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiProtocol = modelData
            }
        }
    }

    // The protocol's dial frequency on each band (DIGU).
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: Engine.digiFrequencies
            delegate: AppButton {
                required property var modelData
                text: modelData.band
                checked: Math.abs(Engine.frequencyMhz - modelData.mhz) < 0.0005 && Engine.mode === "DIGU"
                width: Math.max(56, Theme.buttonWidth * 0.8)
                height: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiTune(modelData.mhz)
            }
        }
    }

    // The slot: where in it we are, and whether the next one is ours.
    ProgressBar {
        Layout.fillWidth: true
        visible: Engine.digiRunning
        from: 0
        to: digi.slotSeconds
        value: digi.slotPos
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            objectName: "digiCq"
            text: qsTr("CQ")
            checked: digi.qso.state === "cq"
            enabled: Engine.transmitAvailable
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiCallCq()
        }
        AppButton {
            objectName: "digiHalt"
            text: qsTr("Halt TX")
            enabled: digi.qso.state !== "idle" || digi.qso.transmitting
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiHalt()
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("TX audio %1 Hz").arg(Engine.digiTxHz)
            color: Theme.textDim
            font.pixelSize: 14
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "digiZoom"
            text: qsTr("Zoom")
            Accessible.name: qsTr("Fit the display to the audio band")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiZoom()
        }
        AppButton {
            text: "−"
            Accessible.name: qsTr("Lower TX audio frequency")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiTxHz = Engine.digiTxHz - 50
        }
        AppButton {
            text: "+"
            Accessible.name: qsTr("Raise TX audio frequency")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiTxHz = Engine.digiTxHz + 50
        }
    }

    Label {
        objectName: "digiStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: {
            const s = digi.qso
            let line = s.status || ""
            if (s.state !== "idle" && s.txMsg)
                line += (line ? " · " : "") + (s.transmitting ? qsTr("TX: ") : qsTr("Next (%1): ").arg(s.parity)) + s.txMsg
            return line
        }
        color: digi.qso.transmitting ? Theme.transmit : Theme.text
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.digiRunning && Engine.digiDecodes.length === 0
        text: qsTr("Decodes appear at the end of each %1 s slot. Tap a CQ to answer it; tap the waterfall inside the passband to set the TX audio frequency.").arg(digi.slotSeconds)
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // ---- RADE digital voice (FreeDV) ----
    Label {
        visible: Engine.radeAvailable
        text: qsTr("RADE digital voice")
        color: Theme.textDim
        font.pixelSize: 14
        Layout.topMargin: Theme.spacing
    }
    RowLayout {
        visible: Engine.radeAvailable
        Layout.fillWidth: true
        AppButton {
            objectName: "radeButton"
            text: qsTr("RADE")
            checked: Engine.rade
            enabled: Engine.connectionState === "connected"
            Layout.preferredWidth: 80
            focusPolicy: Qt.NoFocus
            onClicked: Engine.rade = !Engine.rade
        }
        Label {
            Layout.fillWidth: true
            text: {
                const r = Engine.radeStatus
                if (!r.active)
                    return qsTr("FreeDV RADE in the app: talk with PTT as usual; the radio goes to DIGU / DIGL.")
                let line = r.synced ? qsTr("Synced, SNR %1 dB").arg(r.snr) : qsTr("Listening for RADE…")
                if (r.callsign)
                    line += " · " + qsTr("last: %1").arg(r.callsign)
                return line
            }
            color: Engine.radeStatus.synced ? "#00cc66" : Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    // Decodes, newest first: UTC, SNR, DT, audio Hz, message. A tap answers.
    Repeater {
        model: Engine.digiDecodes.slice(0, 40)
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: Math.max(32, decodeText.implicitHeight + 8)
            padding: 2
            Accessible.name: modelData.text
            background: Rectangle {
                color: modelData.toMe ? Qt.rgba(0.96, 0.65, 0.14, 0.18)
                     : modelData.cq ? Qt.rgba(0, 0.8, 0.4, 0.10) : "transparent"
                radius: 4
            }
            contentItem: Label {
                id: decodeText
                text: "%1 %2 %3 %4  %5".arg(modelData.utc.substring(0, 4))
                                       .arg((modelData.snr >= 0 ? "+" : "") + modelData.snr)
                                       .arg(modelData.dt)
                                       .arg(modelData.freq)
                                       .arg(modelData.text)
                color: modelData.toMe ? Theme.warning : Theme.text
                font.family: "monospace"
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            onClicked: Engine.digiAnswer(index)
        }
    }
}
