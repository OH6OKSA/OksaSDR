import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The amplifier: an RF-KIT RF2K-S over its own REST API
// (MobileEngineAmp.cpp). Its state, forward and reflected power, SWR, the
// PA's temperature, voltage and current, band, frequency, tuner, antenna and
// interface, as the amplifier reports them; with Control on (Radio Setup ›
// Amp), Operate / Standby, the internal antennas and the error reset, each
// confirmed first, none while the radio transmits, and shown only as the
// amplifier reads them back; and Tune, a carrier for the amplifier's own
// tuner (Engine.ampTune). The tab shows once an amplifier is set up.
ColumnLayout {
    id: ampTab
    spacing: Theme.spacing / 2

    readonly property var a: Engine.amp
    readonly property bool reading: a.state === "connected"
    readonly property bool control: !!a.control
    readonly property bool transmitting: Engine.radioTransmitting
    // A command can go: control on, the amplifier read, the radio in
    // receive and no other command waiting for its answer.
    readonly property bool canCommand: control && reading && !transmitting && !a.pending
    // The tune carrier for the amplifier's tuner is on, or STANDBY is
    // being asked for it.
    readonly property bool tuneRunning: !!a.tune
    function antennaState(n) {
        const list = a.antennaList || []
        for (let i = 0; i < list.length; ++i)
            if (list[i].type === "INTERNAL" && list[i].number === n) return list[i].state
        return ""
    }
    readonly property var external: {
        const list = a.antennaList || []
        for (let i = 0; i < list.length; ++i)
            if (list[i].type === "EXTERNAL") return list[i]
        return null
    }

    // Forward peak, as the desktop applet's forward gauge: the highest
    // reading of the last 3 s, the marker falling back over the full scale
    // in about 3.7 s. Between the amplifier's readings (half a second in
    // receive, 250 ms while transmitting) it keeps the peaks in sight.
    readonly property real fwdNow: reading && a.fwd !== undefined ? a.fwd : 0
    property var fwdSamples: []
    property real fwdPeak: 0
    function recordForward() {
        const now = Date.now()
        fwdSamples = fwdSamples.filter(x => now - x[0] <= 3000).concat([[now, fwdNow]])
        if (fwdNow >= fwdPeak) fwdPeak = fwdNow
    }
    onFwdNowChanged: Qt.callLater(recordForward)
    onReadingChanged: { fwdSamples = []; fwdPeak = 0 }
    Timer {
        interval: 100
        repeat: true
        running: ampTab.fwdPeak > ampTab.fwdNow
        onTriggered: {
            const now = Date.now()
            ampTab.fwdSamples = ampTab.fwdSamples.filter(x => now - x[0] <= 3000)
            const windowMax = ampTab.fwdSamples.reduce((m, x) => Math.max(m, x[1]), ampTab.fwdNow)
            ampTab.fwdPeak = Math.max(windowMax, ampTab.fwdPeak - 1600 / 3.7 * 0.1)
        }
    }
    function num(v, digits, unit) {
        return v === undefined ? "–" : Number(v).toFixed(digits) + (unit ? " " + unit : "")
    }
    function text(v) { return v === undefined || v === "" ? "–" : v }

    component Info: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
        Label {
            text: parent.value
            color: Theme.text
            font.pixelSize: 15
            font.bold: true
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            Layout.maximumWidth: ampTab.width * 0.6
        }
    }

    // ---- Name, link and Operate / Standby (shown, not a button) ----
    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: (ampTab.a.device || "RF2K-S") + (ampTab.a.name ? "  " + ampTab.a.name : "")
            color: Theme.text
            font.pixelSize: 17
            font.bold: true
            elide: Text.ElideRight
        }
        Rectangle {
            objectName: "ampOperate"
            visible: ampTab.reading && ampTab.a.operate !== undefined
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Amplifier %1").arg(ampTab.a.operate || "")
            radius: 4
            color: ampTab.a.operate === "OPERATE" ? Theme.transmit : Theme.surfaceRaised
            border.color: ampTab.a.operate === "OPERATE" ? Theme.transmit : Theme.textDim
            implicitWidth: operateText.implicitWidth + 16
            implicitHeight: operateText.implicitHeight + 8
            Label {
                id: operateText
                anchors.centerIn: parent
                text: ampTab.a.operate || ""
                color: ampTab.a.operate === "OPERATE" ? "white" : Theme.text
                font.pixelSize: 14
                font.bold: true
            }
        }
    }
    Label {
        objectName: "ampState"
        Layout.fillWidth: true
        text: {
            const a = ampTab.a
            if (!a.on) return qsTr("Reading is off (Radio Setup › Amp).")
            if (a.state === "connected") return qsTr("Read from %1, every half second.").arg(a.target)
            if (a.state === "failed") return qsTr("%1 not reached: %2. Trying again…").arg(a.target).arg(a.error)
            if (a.state === "connecting") return qsTr("Connecting to %1…").arg(a.target)
            return qsTr("Read while connected to a radio.")
        }
        color: ampTab.reading ? Theme.textDim : (ampTab.a.state === "failed" ? Theme.transmit : Theme.textDim)
        font.pixelSize: 13
        wrapMode: Text.Wrap
    }

    // ---- Power and SWR on the amplifier's own scales (upstream's applet:
    // forward 0-1600 W, reflected 0-200 W; SWR red from 2:1) ----
    GridLayout {
        Layout.fillWidth: true
        columns: ampTab.width >= 560 ? 3 : 1
        columnSpacing: Theme.spacing
        rowSpacing: 4
        BarGauge {
            objectName: "ampForward"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            kind: qsTr("Forward")
            min: 0
            max: 1600
            red: 1500
            live: ampTab.reading && ampTab.a.fwd !== undefined
            value: ampTab.a.fwd || 0
            text: qsTr("Forward") + " " + ampTab.num(ampTab.a.fwd, 0, "W")
            peak: ampTab.fwdPeak
            showPeak: true
        }
        BarGauge {
            objectName: "ampReflected"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            kind: qsTr("Reflected")
            min: 0
            max: 200
            // 2:1 SWR at the present forward power is 11 % reflected; no
            // red zone in receive.
            red: (ampTab.a.fwd || 0) >= 1 ? Math.max(1, ampTab.a.fwd * 0.111) : 1e9
            live: ampTab.reading && ampTab.a.refl !== undefined
            value: ampTab.a.refl || 0
            text: qsTr("Reflected") + " " + ampTab.num(ampTab.a.refl, 0, "W")
        }
        BarGauge {
            objectName: "ampSwr"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            kind: qsTr("SWR")
            min: 1
            max: 3
            red: 2
            // Held at 1 below 1 W forward, as the amplifier applets do.
            live: ampTab.reading && ampTab.a.swr !== undefined
            value: (ampTab.a.fwd || 0) >= 1 ? (ampTab.a.swr || 1) : 1
            text: qsTr("SWR") + " " + ((ampTab.a.fwd || 0) >= 1 ? ampTab.num(ampTab.a.swr, 2, "") : "–")
        }
    }

    // ---- The rest, as label and value ----
    GridLayout {
        Layout.fillWidth: true
        columns: ampTab.width >= 560 ? 2 : 1
        columnSpacing: Theme.spacing * 2
        rowSpacing: 2
        Info { label: qsTr("Temperature"); value: ampTab.num(ampTab.a.temp, 0, "°C") }
        Info { label: qsTr("Voltage"); value: ampTab.num(ampTab.a.volts, 1, "V") }
        Info { label: qsTr("Current"); value: ampTab.num(ampTab.a.amps, 1, "A") }
        Info { label: qsTr("Band"); value: ampTab.text(ampTab.a.band) }
        Info { label: qsTr("Frequency"); value: ampTab.text(ampTab.a.freq) }
        Info { label: qsTr("Antenna"); value: ampTab.text(ampTab.a.antenna) }
        Info {
            label: qsTr("Tuner")
            value: ampTab.a.tuner === undefined ? "–"
                 : ampTab.a.tuner + (ampTab.a.tunerSetup && ampTab.a.tunerSetup !== "–" ? " (" + ampTab.a.tunerSetup + ")" : "")
        }
        Info { label: qsTr("Interface"); value: ampTab.text(ampTab.a.iface) }
        Info { label: qsTr("Status"); value: ampTab.text(ampTab.a.status) }
        Info { label: qsTr("Firmware"); value: ampTab.text(ampTab.a.firmware) }
    }
    Label {
        objectName: "ampError"
        Layout.fillWidth: true
        visible: !!ampTab.a.ifaceError
        text: qsTr("Amplifier error: %1").arg(ampTab.a.ifaceError || "")
        color: Theme.transmit
        font.pixelSize: 15
        font.bold: true
        wrapMode: Text.Wrap
    }

    // ---- Control (Radio Setup › Amp › Control the amplifier) ----
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        visible: ampTab.control
        spacing: Theme.spacing / 2

        // Operate / Standby as a pair, the amplifier's present mode the dark
        // one; the other asks first.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            AppButton {
                objectName: "ampStandby"
                text: qsTr("Standby")
                checked: ampTab.a.operate === "STANDBY"
                enabled: ampTab.canCommand && ampTab.a.operate !== undefined
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: if (!checked) confirm.ask("standby", 0)
            }
            AppButton {
                objectName: "ampOperateButton"
                text: qsTr("Operate")
                checked: ampTab.a.operate === "OPERATE"
                enabled: ampTab.canCommand && ampTab.a.operate !== undefined
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: if (!checked) confirm.ask("operate", 0)
            }
        }
        // The internal antennas 1-4, the active one dark; one the amplifier
        // has not set up is dim. The external antenna is shown, not switched.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Antenna"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            Repeater {
                // A fixed model: the buttons stay while the readings change.
                model: 4
                delegate: AppButton {
                    required property int index
                    readonly property int number: index + 1
                    readonly property string antState: ampTab.antennaState(number)
                    objectName: "ampAntenna" + number
                    visible: antState !== ""
                    text: number
                    checked: antState === "ACTIVE"
                    // The active one stays bright (it is the present state);
                    // a tap on it asks nothing.
                    enabled: ampTab.canCommand && antState !== "DISABLED"
                    Accessible.name: qsTr("Antenna %1").arg(number)
                    Layout.preferredWidth: Theme.touchTarget
                    onClicked: if (!checked) confirm.ask("antenna", number)
                }
            }
            Rectangle {
                visible: ampTab.external !== null
                readonly property bool active: !!ampTab.external && ampTab.external.state === "ACTIVE"
                Accessible.role: Accessible.StaticText
                Accessible.name: active ? qsTr("External antenna, active") : qsTr("External antenna, not active")
                radius: Theme.radius
                color: active ? Theme.buttonFaceOn : "transparent"
                border.color: Theme.textDim
                implicitWidth: extText.implicitWidth + 12
                implicitHeight: Theme.touchTarget
                Label {
                    id: extText
                    anchors.centerIn: parent
                    text: ampTab.external ? ampTab.external.label.replace(/^External/i, "EXT") : ""
                    color: parent.active ? Theme.buttonTextOn : Theme.textDim
                    font.pixelSize: Theme.buttonFont
                }
            }
        }
        // Tune: the amplifier's own tuner needs a carrier and its Tune
        // pressed (on its screen, or over VNC); this sends the carrier, in
        // STANDBY, and keeps it while the app is away, 20 s at most.
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Tuner"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            AppButton {
                objectName: "ampTune"
                text: ampTab.a.tune === "carrier" ? qsTr("Stop  %1 s").arg(ampTab.a.tuneLeft)
                    : ampTab.tuneRunning ? qsTr("Stop") : qsTr("Tune")
                checked: ampTab.tuneRunning
                enabled: ampTab.tuneRunning || (ampTab.canCommand && ampTab.a.operate !== undefined)
                Accessible.name: ampTab.tuneRunning ? qsTr("Stop the tune carrier") : qsTr("Tune carrier for the amplifier's tuner")
                Layout.preferredWidth: Math.max(Theme.buttonWidth * 2 + 4, implicitWidth)
                // Outside the amplifier's 4-39 W the engine refuses with the
                // reason, no question first.
                onClicked: ampTab.tuneRunning ? Engine.ampTuneStop()
                         : Engine.tunePower < 4 || Engine.tunePower > 39 ? Engine.ampTune()
                         : confirm.ask("tune", 0)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            AppButton {
                objectName: "ampReset"
                text: qsTr("Reset error")
                enabled: ampTab.canCommand
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: confirm.ask("reset", 0)
            }
            Label {
                objectName: "ampCommandState"
                Layout.fillWidth: true
                text: ampTab.a.tune === "carrier"
                      ? (ampTab.a.tuner === "TUNING" ? qsTr("The amplifier is tuning…")
                         : qsTr("Carrier on at %1 W: press Tune on the amplifier's screen (VNC).").arg(Engine.tunePower))
                    : ampTab.a.pending ? qsTr("Asking the amplifier…")
                    : ampTab.transmitting ? qsTr("Not while transmitting.")
                    : ampTab.a.result || ""
                color: ampTab.a.tune === "carrier" ? Theme.warning
                     : ampTab.a.pending || ampTab.transmitting ? Theme.textDim
                     : ampTab.a.resultOk ? "#00cc66" : Theme.transmit
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        text: ampTab.control
              ? qsTr("Each change asks you first and none works while the radio transmits; the tab shows only what the amplifier reads back. Tune sends the carrier the amplifier's tuner needs; its Tune is pressed on the amplifier's own screen.")
              : qsTr("Read only: nothing is sent that changes the amplifier. Control in Radio Setup › Amp adds Operate / Standby, the antenna and the error reset.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // One confirmation per change, naming it, as the app's other offers
    // (SetupSuggestion); it closes by itself when the change could no
    // longer go (the radio keyed, the link lost, control turned off).
    Dialog {
        id: confirm
        objectName: "ampConfirm"
        property string action: ""
        property int antenna: 0
        function ask(what, n) {
            action = what
            antenna = n
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        readonly property bool stillPossible: ampTab.canCommand
        onStillPossibleChanged: if (!stillPossible && opened) close()
        // "antenna 1" for an internal one, the external one by its label.
        readonly property string activeAntenna: {
            const label = ampTab.a.antenna || ""
            return /^[0-9]+$/.test(label) ? qsTr("antenna %1").arg(label) : (label || qsTr("the present antenna"))
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                objectName: "ampConfirmTitle"
                Layout.fillWidth: true
                text: confirm.action === "operate" ? qsTr("Put the amplifier in OPERATE?")
                    : confirm.action === "standby" ? qsTr("Put the amplifier in STANDBY?")
                    : confirm.action === "antenna" ? qsTr("Switch the amplifier to antenna %1?").arg(confirm.antenna)
                    : confirm.action === "tune" ? qsTr("Send a tune carrier for the amplifier?")
                    : qsTr("Reset the amplifier's error?")
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: confirm.action === "operate"
                      ? qsTr("The next transmission goes through the amplifier: check that the radio's power suits it.")
                    : confirm.action === "standby"
                      ? qsTr("The radio's signal goes past the amplifier, not amplified.")
                    : confirm.action === "antenna"
                      ? qsTr("The amplifier's output moves from %1 to antenna %2. Check that antenna %2 is connected and suits this band.").arg(confirm.activeAntenna).arg(confirm.antenna)
                    : confirm.action === "tune"
                      ? qsTr("The amplifier goes to STANDBY and the radio transmits a TUNE carrier at %1 W for up to 20 s, also while you open the amplifier's own screen (VNC) to press its Tune. It stops by itself once the amplifier has tuned, or after 20 s.").arg(Engine.tunePower)
                      : qsTr("Clear the RF-KIT's error state? Make sure the cause of the error has been resolved first.")
                color: Theme.textDim
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    objectName: "ampConfirmNo"
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: confirm.close()
                }
                AppButton {
                    objectName: "ampConfirmYes"
                    text: confirm.action === "operate" ? qsTr("Operate")
                        : confirm.action === "standby" ? qsTr("Standby")
                        : confirm.action === "antenna" ? qsTr("Antenna %1").arg(confirm.antenna)
                        : confirm.action === "tune" ? qsTr("Tune")
                        : qsTr("Reset")
                    checked: true
                    Layout.fillWidth: true
                    onClicked: {
                        const what = confirm.action
                        const n = confirm.antenna
                        confirm.close()
                        if (what === "operate" || what === "standby") Engine.ampSetOperate(what === "operate")
                        else if (what === "antenna") Engine.ampSelectAntenna(n)
                        else if (what === "reset") Engine.ampResetError()
                        else if (what === "tune") Engine.ampTune()
                    }
                }
            }
        }
    }
}
