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
            Accessible.name: qsTr("Lower TX audio frequency by 1 Hz")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            autoRepeat: true
            onClicked: Engine.digiTxHz = Engine.digiTxHz - 1
        }
        AppButton {
            text: "+"
            Accessible.name: qsTr("Raise TX audio frequency by 1 Hz")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            autoRepeat: true
            onClicked: Engine.digiTxHz = Engine.digiTxHz + 1
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

    // WSJT-X's highlighting colours (its defaults, in its priority order):
    // a light ground with dark text, so a row reads at a glance.
    function hlColor(hl) {
        switch (hl) {
        case "mycall": return "#ff6666"
        case "continent": return "#ff0063"
        case "continentBand": return "#ff99c2"
        case "cqzone": return "#ffbf00"
        case "cqzoneBand": return "#ffe499"
        case "dxcc": return "#ff00ff"
        case "dxccBand": return "#ffaaff"
        case "cq": return "#66ff66"
        case "tx": return "#ffff00"
        default: return "transparent"
        }
    }
    // What the list shows; your call, your TX and the station in the QSO
    // always show.
    function passes(row) {
        if (row.hl === "mycall" || row.hl === "tx" || (qso.dx && row.call === qso.dx)) {
            return true
        }
        switch (Engine.digiFilter) {
        case "cq": return row.cq === true
        case "new": return ["continent", "continentBand", "cqzone", "cqzoneBand", "dxcc", "dxccBand"].indexOf(row.hl) >= 0
        case "dxcc": return row.newDxcc > 0
        default: return true
        }
    }
    // The shown rows with their index in Engine.digiDecodes (a tap answers
    // by that index).
    readonly property var shown: {
        const rows = Engine.digiDecodes
        const out = []
        for (let i = 0; i < rows.length && out.length < 40; ++i) {
            if (passes(rows[i])) {
                out.push({ row: rows[i], index: i })
            }
        }
        return out
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("Show")
            color: Theme.textDim
        }
        Repeater {
            model: [{ key: "all", text: qsTr("All") }, { key: "cq", text: qsTr("CQ") },
                    { key: "new", text: qsTr("New") }, { key: "dxcc", text: qsTr("New DXCC") }]
            delegate: AppButton {
                required property var modelData
                objectName: "digiFilter_" + modelData.key
                text: modelData.text
                checked: Engine.digiFilter === modelData.key
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiFilter = modelData.key
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.digiFilter !== "all" && Engine.digiDecodes.length > 0
        text: digi.shown.length > 0
              ? qsTr("%1 of the last %2 decodes").arg(digi.shown.length).arg(Engine.digiDecodes.length)
              : qsTr("None of the last %1 decodes match").arg(Engine.digiDecodes.length)
        color: Theme.textDim
        font.pixelSize: 11
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.digiDecodes.length > 0
        text: qsTr("Green CQ · pink / magenta new DXCC · amber new CQ zone · red new continent or your call · yellow your TX · grey worked before on this band and mode")
        color: Theme.textDim
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    // Decodes from every DIGU slice, newest first: slice, UTC, SNR, DT,
    // audio Hz, message. A tap answers (TX and the display move to that
    // slice).
    Repeater {
        model: digi.shown
        delegate: ItemDelegate {
            required property var modelData
            readonly property var row: modelData.row
            readonly property color ground: digi.hlColor(row.hl)
            readonly property bool lit: row.hl && row.hl !== "b4"
            Layout.fillWidth: true
            implicitHeight: Math.max(32, decodeText.implicitHeight + 8)
            padding: 2
            Accessible.name: row.text
            background: Rectangle {
                color: parent.ground
                radius: 4
            }
            contentItem: Label {
                id: decodeText
                text: "%1 %2 %3 %4 %5  %6".arg(row.slice || " ")
                                          .arg(row.utc.substring(0, 4))
                                          .arg(row.snr === "" ? "   " : ((row.snr >= 0 ? "+" : "") + row.snr))
                                          .arg(row.dt === "" ? "   " : row.dt)
                                          .arg(row.freq)
                                          .arg(row.text)
                color: parent.lit ? "#101010" : row.hl === "b4" ? Theme.textDim : Theme.text
                font.family: "monospace"
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            onClicked: Engine.digiAnswer(modelData.index)
        }
    }
}
