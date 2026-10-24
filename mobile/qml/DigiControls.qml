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
    // CQ's slot (even = 0 / 30 s, odd = 15 / 45 s in FT8; Auto = the next)
    // and Keep CQ: back to CQ after each QSO our CQ brought.
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("CQ slot")
            color: Theme.textDim
        }
        Repeater {
            model: [{ key: "auto", text: qsTr("Auto") }, { key: "even", text: qsTr("Even") },
                    { key: "odd", text: qsTr("Odd") }]
            delegate: AppButton {
                required property var modelData
                objectName: "digiCqParity_" + modelData.key
                text: modelData.text
                checked: Engine.digiCqParity === modelData.key
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiCqParity = modelData.key
            }
        }
        AppButton {
            objectName: "digiAutoCq"
            text: qsTr("Keep CQ")
            checked: Engine.digiAutoCq
            Accessible.name: qsTr("Call CQ again after each QSO")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiAutoCq = !Engine.digiAutoCq
        }
    }
    // PSK Reporter: the stations decoded here, reported (off by default).
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Report to PSK Reporter")
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "pskReporterOff"
            text: qsTr("Off")
            checked: !Engine.pskReporter
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.pskReporter = false
        }
        AppButton {
            objectName: "pskReporterOn"
            text: qsTr("On")
            checked: Engine.pskReporter
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.pskReporter = true
        }
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.pskReporter && Engine.pskReporterStatus.length > 0
        text: Engine.pskReporterStatus
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    // The device clock, judged from the decodes' DT: FT8 fails beyond about
    // a second.
    Rectangle {
        Layout.fillWidth: true
        visible: digi.qso.clockWarn === true
        implicitHeight: clockText.implicitHeight + 12
        radius: 6
        color: "#7a1a1d"
        border.color: "#ff5a5f"
        Label {
            id: clockText
            anchors.fill: parent
            anchors.margins: 6
            text: qsTr("This device's clock looks %1 s off (the decodes' DT). FT8 needs it within a second: set Date & Time to automatic.")
                  .arg((digi.qso.clockOffset > 0 ? "+" : "") + Number(digi.qso.clockOffset || 0).toFixed(1))
            color: "#ffffff"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
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
    // The transmit audio level (WSJT-X's Pwr): 0 dB drives the radio fully,
    // so the power out is the TX tab's power; lower it to back off.
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("TX level %1 dB").arg(Engine.digiTxLevelDb)
            color: Theme.textDim
            font.pixelSize: 14
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "digiTxLevelDown"
            text: "−"
            Accessible.name: qsTr("Lower the TX level by 1 dB")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiTxLevelDb > -20
            onClicked: Engine.digiTxLevelDb = Engine.digiTxLevelDb - 1
        }
        AppButton {
            objectName: "digiTxLevelUp"
            text: "+"
            Accessible.name: qsTr("Raise the TX level by 1 dB")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiTxLevelDb < 0
            onClicked: Engine.digiTxLevelDb = Engine.digiTxLevelDb + 1
        }
    }

    // The QSO's state. While a QSO or CQ runs it is a box that stands out:
    // amber while waiting, red while transmitting.
    Rectangle {
        Layout.fillWidth: true
        readonly property bool active: digi.qso.state !== "idle"
        visible: statusText.text.length > 0
        implicitHeight: statusText.implicitHeight + (active ? 14 : 0)
        radius: 6
        color: !active ? "transparent" : digi.qso.transmitting ? "#d7262d" : "#ffbf00"
        Label {
            id: statusText
            objectName: "digiStatus"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: parent.active ? 8 : 0
            anchors.rightMargin: parent.active ? 8 : 0
            text: {
                const s = digi.qso
                let line = s.status || ""
                if (s.state !== "idle" && s.txMsg)
                    line += (line ? "\n" : "") + (s.transmitting ? qsTr("TX: ") : qsTr("Next (%1): ").arg(s.parity)) + s.txMsg
                return line
            }
            color: !parent.active ? Theme.text : digi.qso.transmitting ? "#ffffff" : "#101010"
            font.pixelSize: parent.active ? 15 : 13
            font.bold: parent.active
            wrapMode: Text.WordWrap
        }
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
    // The row tapped last flashes for a second. Kept here, not in the row:
    // the answer changes the QSO, the list is rebuilt and the rows made anew.
    property string flashKey: ""
    property real flashAt: 0
    Timer {
        interval: 1400
        running: digi.flashKey !== ""
        onTriggered: digi.flashKey = ""
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
            readonly property string key: row.utc + "|" + row.freq + "|" + row.text
            Component.onCompleted: {
                const left = 1350 - (Date.now() - digi.flashAt)
                if (digi.flashKey === key && left > 0) {
                    flashAnim.loops = Math.max(1, Math.ceil(left / 450))
                    flashAnim.restart()
                }
            }
            readonly property color ground: digi.hlColor(row.hl)
            readonly property bool lit: row.hl && row.hl !== "b4"
            Layout.fillWidth: true
            implicitHeight: Math.max(32, decodeText.implicitHeight + 8)
            padding: 2
            Accessible.name: row.text
            background: Rectangle {
                color: parent.ground
                radius: 4
                // A tapped row flashes, so the answer shows where the
                // finger is even with the status line scrolled away.
                Rectangle {
                    id: flash
                    anchors.fill: parent
                    radius: 4
                    color: "#ffff00"
                    border.color: "#ffffff"
                    border.width: 2
                    opacity: 0
                }
            }
            SequentialAnimation {
                id: flashAnim
                loops: 3
                NumberAnimation { target: flash; property: "opacity"; to: 0.9; duration: 80 }
                PauseAnimation { duration: 170 }
                NumberAnimation { target: flash; property: "opacity"; to: 0; duration: 200 }
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
            onClicked: {
                digi.flashKey = key
                digi.flashAt = Date.now()
                flashAnim.loops = 3
                flashAnim.restart()
                Engine.digiAnswer(modelData.index)
            }
        }
    }
}
