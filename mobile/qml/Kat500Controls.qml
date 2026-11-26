import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Elecraft's KAT500 tuner (Engine.kat500, MobileEngineElecraft.cpp), over a
// raw TCP serial bridge: its mode, antenna, bypass, SWR (the last it
// measured), band and frequency, as the tuner reports them; with Control on
// (Radio Setup › Amp) its mode (Auto / Manual / Bypass), antenna (1-3), the
// network's bypass, the fault clear, Tune (its full search with the
// radio's TUNE carrier, the KPA500 in STANDBY) and its power (off, it
// answers only its identity and power), each confirmed first, none while
// the radio transmits, and shown only as the tuner reads it back.
ColumnLayout {
    id: kat
    spacing: Theme.spacing / 2

    property var t: Engine.kat500
    readonly property bool reading: t.state === "connected"
    readonly property bool control: !!t.control
    readonly property bool transmitting: Engine.radioTransmitting
    readonly property bool tuneRunning: !!t.tune
    // A command can go: control on, the tuner read and on, the radio in
    // receive, nothing else waiting for the tuner.
    readonly property bool canCommand: control && reading && !transmitting && !t.pending && !tuneRunning && !t.busy && t.power === true
    // Power, also while the tuner is off (as its own power button).
    readonly property bool canPower: control && reading && !transmitting && !t.pending && !tuneRunning && !t.busy && t.power !== undefined
    function num(v, digits, unit) {
        return v === undefined ? "–" : Number(v).toFixed(digits) + (unit ? " " + unit : "")
    }
    function text(v) { return v === undefined || v === "" ? "–" : v }
    function antennaOk(n) {
        const list = t.antennas || []
        return list.length < n || list[n - 1] !== false
    }

    component Info: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        Label {
            text: parent.value
            color: Theme.text
            font.pixelSize: Theme.px(15)
            font.bold: true
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            Layout.maximumWidth: kat.width * 0.6
        }
    }
    component Row3: RowLayout {
        property string label
        Layout.fillWidth: true
        spacing: 4
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
    }

    // ---- Name, mode and tuning (shown, not a button) ----
    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: kat.t.device || "Elecraft KAT500"
            Accessible.role: Accessible.Heading
            color: Theme.text
            font.pixelSize: Theme.px(17)
            font.bold: true
            elide: Text.ElideRight
        }
        Rectangle {
            objectName: "katState"
            visible: kat.reading && (kat.t.power === false || kat.t.mode !== undefined || !!kat.t.tuning)
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Tuner %1").arg(stateText.text)
            radius: 4
            color: kat.t.tuning ? Theme.warning : Theme.surfaceRaised
            border.color: kat.t.tuning ? Theme.warning : Theme.textDim
            implicitWidth: stateText.implicitWidth + 16
            implicitHeight: stateText.implicitHeight + 8
            Label {
                id: stateText
                anchors.centerIn: parent
                text: kat.t.power === false ? qsTr("OFF") : kat.t.tuning ? qsTr("TUNING") : (kat.t.mode || "")
                color: kat.t.tuning ? "black" : Theme.text
                font.pixelSize: Theme.px(14)
                font.bold: true
            }
        }
    }
    Label {
        objectName: "katLink"
        Layout.fillWidth: true
        text: {
            const a = kat.t
            if (!a.on) return qsTr("Reading is off (Radio Setup › Amp).")
            if (a.state === "connected") return !kat.control ? qsTr("Read from %1, read only.").arg(a.target) : qsTr("Read from %1.").arg(a.target)
            if (a.state === "failed") return qsTr("%1 not reached: %2. Trying again…").arg(a.target).arg(a.error)
            if (a.state === "connecting") return a.error ? qsTr("%1: %2. Trying again…").arg(a.target).arg(a.error)
                                                         : qsTr("Connecting to %1…").arg(a.target)
            return qsTr("Read while connected to a radio.")
        }
        color: kat.t.state === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }
    // Off (PS0) the tuner tells nothing more: say so, not only dashes.
    Label {
        objectName: "katOff"
        Layout.fillWidth: true
        visible: kat.reading && kat.t.power === false
        text: kat.control
              ? qsTr("The tuner is off. Power › On below turns it on; its readings come once it is on.")
              : qsTr("The tuner is off. Turn it on with its own power button, or turn on Control the tuner in Radio Setup › Amp to turn it on from here.")
        color: Theme.warning
        font.pixelSize: Theme.px(15)
        font.bold: true
        wrapMode: Text.Wrap
    }
    Label {
        objectName: "katFault"
        Layout.fillWidth: true
        visible: !!kat.t.fault
        text: qsTr("Tuner fault: %1").arg(kat.t.fault || "")
        color: Theme.transmit
        font.pixelSize: Theme.px(15)
        font.bold: true
        wrapMode: Text.Wrap
    }

    // ---- SWR, the last the tuner measured; live while transmitting ----
    BarGauge {
        objectName: "katSwr"
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        kind: qsTr("SWR")
        min: 1
        max: 3
        red: 2
        live: kat.reading && kat.transmitting && kat.t.swr !== undefined
        value: live ? Math.max(1, kat.t.swr) : 1
        text: qsTr("SWR") + " " + kat.num(kat.t.swr, 2, "")
    }
    GridLayout {
        Layout.fillWidth: true
        columns: kat.width >= 560 ? 2 : 1
        columnSpacing: Theme.spacing * 2
        rowSpacing: 2
        // The last SWR the tuner measured, with its match and without it.
        Info { label: qsTr("SWR (last)"); value: kat.num(kat.t.swr, 2, "") }
        Info { label: qsTr("Bypass SWR"); value: kat.num(kat.t.bypassSwr, 2, "") }
        Info { label: qsTr("Antenna"); value: kat.t.antenna === undefined ? "–" : "ANT " + kat.t.antenna }
        Info {
            label: qsTr("Network")
            value: kat.t.bypassed === undefined ? "–" : kat.t.bypassed ? qsTr("Bypassed") : qsTr("In line")
        }
        Info { label: qsTr("Band"); value: kat.text(kat.t.band) }
        Info { label: qsTr("Frequency"); value: kat.t.freq === undefined ? "–" : qsTr("%1 kHz").arg(kat.t.freq) }
        Info { label: qsTr("Firmware"); value: kat.text(kat.t.firmware) }
    }

    // ---- Control (Radio Setup › Amp › Control the tuner) ----
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        visible: kat.control
        spacing: Theme.spacing / 2

        Row3 {
            label: qsTr("Power")
            AppButton {
                objectName: "katPowerOn"
                text: qsTr("On")
                checked: kat.t.power === true
                enabled: kat.canPower
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: if (!checked) confirm.ask("poweron", "", 0)
            }
            AppButton {
                objectName: "katPowerOff"
                text: qsTr("Off")
                checked: kat.t.power === false
                enabled: kat.canPower
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: if (!checked) confirm.ask("poweroff", "", 0)
            }
        }
        Row3 {
            label: qsTr("Mode")
            Repeater {
                model: [{ id: "auto", name: qsTr("Auto"), mode: "AUTO" },
                        { id: "manual", name: qsTr("Manual"), mode: "MANUAL" },
                        { id: "bypass", name: qsTr("Bypass"), mode: "BYPASS" }]
                delegate: AppButton {
                    required property var modelData
                    objectName: "katMode_" + modelData.id
                    text: modelData.name
                    checked: kat.t.mode === modelData.mode
                    enabled: kat.canCommand && kat.t.mode !== undefined
                    Layout.preferredWidth: Math.max(Theme.touchTarget * 1.4, implicitWidth)
                    onClicked: if (!checked) confirm.ask("mode", modelData.id, 0)
                }
            }
        }
        Row3 {
            label: qsTr("Antenna")
            Repeater {
                model: 3
                delegate: AppButton {
                    required property int index
                    readonly property int number: index + 1
                    objectName: "katAnt" + number
                    text: String(number)
                    checked: kat.t.antenna === number
                    // One the tuner has not enabled on this band is dim.
                    enabled: kat.canCommand && kat.antennaOk(number)
                    Accessible.name: qsTr("Antenna %1").arg(number)
                    Layout.preferredWidth: Theme.touchTarget
                    onClicked: if (!checked) confirm.ask("antenna", "", number)
                }
            }
        }
        Row3 {
            label: qsTr("Network")
            AppButton {
                objectName: "katInLine"
                text: qsTr("In line")
                checked: kat.t.bypassed === false
                enabled: kat.canCommand && kat.t.bypassed !== undefined
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: if (!checked) confirm.ask("inline", "", 0)
            }
            AppButton {
                objectName: "katBypass"
                text: qsTr("Bypass")
                checked: kat.t.bypassed === true
                enabled: kat.canCommand && kat.t.bypassed !== undefined
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: if (!checked) confirm.ask("bypass", "", 0)
            }
        }
        Row3 {
            label: qsTr("Tuner")
            AppButton {
                objectName: "katTune"
                text: kat.t.tune === "carrier" ? qsTr("Stop  %1 s").arg(kat.t.tuneLeft)
                    : kat.tuneRunning ? qsTr("Stop") : qsTr("Tune")
                checked: kat.tuneRunning
                enabled: kat.tuneRunning || (kat.canCommand && kat.t.mode !== undefined)
                Accessible.name: kat.tuneRunning ? qsTr("Stop the tuner's tune") : qsTr("Tune the tuner with a carrier")
                Layout.preferredWidth: Math.max(Theme.buttonWidth * 2 + 4, implicitWidth)
                // Outside the tuner's tune power (10-100 W) or in Bypass mode
                // the engine refuses with the reason, no question.
                readonly property bool ready: Engine.tunePower >= 10 && Engine.tunePower <= 100 && kat.t.mode !== "BYPASS"
                onClicked: kat.tuneRunning ? Engine.kat500TuneStop()
                         : !ready ? Engine.kat500Tune()
                         : confirm.ask("tune", "", 0)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            AppButton {
                objectName: "katClearFault"
                text: qsTr("Clear fault")
                enabled: kat.canCommand && !!kat.t.fault
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                onClicked: confirm.ask("clear", "", 0)
            }
            Label {
                objectName: "katCommandState"
                Layout.fillWidth: true
                text: kat.t.tune === "carrier" ? (kat.t.tuning ? qsTr("The tuner is tuning…") : qsTr("Carrier on at %1 W for the tuner.").arg(Engine.tunePower))
                    : kat.t.tune === "standby" ? qsTr("Asking the KPA500 for STANDBY…")
                    : kat.t.tune === "atu" ? qsTr("Bypassing the radio's own ATU…")
                    : kat.t.tune === "frequency" ? qsTr("Setting the tuner to the TX frequency…")
                    : kat.t.tune === "search" ? qsTr("Asking the tuner to search…")
                    : kat.t.tune === "result" ? qsTr("Reading the tune's result…")
                    : kat.t.pending ? qsTr("Asking the tuner…")
                    : kat.transmitting ? qsTr("Not while transmitting.")
                    : kat.t.result || ""
                color: kat.t.tune === "carrier" ? Theme.warning
                     : kat.t.pending || kat.tuneRunning || kat.transmitting ? Theme.textDim
                     : kat.t.resultOk ? Theme.good : Theme.transmit
                font.pixelSize: Theme.px(13)
                wrapMode: Text.Wrap
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        text: kat.control
              ? qsTr("Each change asks you first and none works while the radio transmits; the tab shows only what the tuner reads back. Power turns the tuner on or off as its own power button does. Tune puts a KPA500 in STANDBY, bypasses the radio's own ATU, sets the tuner to the TX frequency and sends the carrier its search needs, 30 s at most.")
              : qsTr("Read only: nothing is sent that changes the tuner. Control in Radio Setup › Amp adds the power, the mode, the antenna, the bypass, Tune and the fault clear.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // One confirmation per change, naming it, as the amplifiers'.
    Dialog {
        id: confirm
        objectName: "katConfirm"
        // Named on its item: Accessible on a Popup itself is refused ("must be
        // attached to an object deriving from Item").
        Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => confirmTitle.text)
        property string action: ""
        property string mode: ""
        property int antenna: 0
        function ask(what, m, n) {
            action = what
            mode = m
            antenna = n
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        readonly property bool stillPossible: action === "poweron" || action === "poweroff" ? kat.canPower : kat.canCommand
        onStillPossibleChanged: if (!stillPossible && opened) close()
        readonly property bool kpaOperate: !!Engine.kpa500.set && Engine.kpa500.operate === "OPERATE"

        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                id: confirmTitle
                objectName: "katConfirmTitle"
                Layout.fillWidth: true
                text: confirm.action === "mode" ? qsTr("Put the tuner in %1?").arg(confirm.mode.toUpperCase())
                    : confirm.action === "antenna" ? qsTr("Switch the tuner to ANT %1?").arg(confirm.antenna)
                    : confirm.action === "bypass" ? qsTr("Bypass the tuner's network?")
                    : confirm.action === "inline" ? qsTr("Put the tuner's network in line?")
                    : confirm.action === "tune" ? qsTr("Tune the KAT500?")
                    : confirm.action === "poweron" ? qsTr("Turn the KAT500 on?")
                    : confirm.action === "poweroff" ? qsTr("Turn the KAT500 off?")
                    : qsTr("Clear the tuner's fault?")
                Accessible.role: Accessible.Heading
                color: Theme.text
                font.pixelSize: Theme.px(18)
                font.bold: true
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: confirm.action === "mode" && confirm.mode === "auto"
                      ? qsTr("The tuner recalls its stored match for the frequency and tunes by itself when the SWR is high.")
                    : confirm.action === "mode" && confirm.mode === "manual"
                      ? qsTr("The tuner keeps its present match and tunes only when asked (Tune).")
                    : confirm.action === "mode"
                      ? qsTr("The tuner's network goes out of line and it does not tune: check that the antenna's SWR suits the power.")
                    : confirm.action === "antenna"
                      ? qsTr("The output moves from ANT %1 to ANT %2. Check that ANT %2 is connected and suits this band.").arg(kat.t.antenna || "–").arg(confirm.antenna)
                    : confirm.action === "bypass"
                      ? qsTr("The antenna is connected straight, without the network: check that its SWR suits the power. In Auto the next tune or recall can put the network back in line.")
                    : confirm.action === "inline"
                      ? qsTr("The tuner's match for this frequency goes in line.")
                    : confirm.action === "tune"
                      ? (confirm.kpaOperate ? qsTr("The KPA500 goes to STANDBY (put it back in OPERATE afterwards). ") : "")
                        + (Engine.hasTuner ? qsTr("The radio's own ATU is bypassed. ") : "")
                        + qsTr("The tuner is set to the TX frequency and starts its full search, and the radio transmits a TUNE carrier at %1 W until the tuner is done, for up to 30 s.").arg(Engine.tunePower)
                        + (Engine.tunePower < 20 ? " " + qsTr("20 W or more matches best (Tune power on the TX tab).") : "")
                    : confirm.action === "poweron"
                      ? qsTr("The tuner starts as with its own power button. Its readings and controls come once it reports that it is on.")
                    : confirm.action === "poweroff"
                      ? qsTr("The tuner stops matching the antenna until it is turned on again, and tells nothing but its power meanwhile. Check the antenna before you transmit.")
                    : qsTr("Clears the KAT500's present fault (%1). Deal with the cause first.").arg(kat.t.fault || "–")
                color: Theme.textDim
                font.pixelSize: Theme.px(15)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    objectName: "katConfirmNo"
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: confirm.close()
                }
                AppButton {
                    objectName: "katConfirmYes"
                    text: confirm.action === "mode" ? confirm.mode.charAt(0).toUpperCase() + confirm.mode.slice(1)
                        : confirm.action === "antenna" ? "ANT " + confirm.antenna
                        : confirm.action === "bypass" ? qsTr("Bypass")
                        : confirm.action === "inline" ? qsTr("In line")
                        : confirm.action === "tune" ? qsTr("Tune")
                        : confirm.action === "poweron" ? qsTr("On")
                        : confirm.action === "poweroff" ? qsTr("Off")
                        : qsTr("Clear")
                    checked: true
                    Layout.fillWidth: true
                    onClicked: {
                        const what = confirm.action
                        const m = confirm.mode
                        const n = confirm.antenna
                        confirm.close()
                        if (what === "mode") Engine.kat500SetMode(m)
                        else if (what === "antenna") Engine.kat500SelectAntenna(n)
                        else if (what === "bypass" || what === "inline") Engine.kat500SetBypass(what === "bypass")
                        else if (what === "tune") Engine.kat500Tune()
                        else if (what === "poweron" || what === "poweroff") Engine.kat500SetPower(what === "poweron")
                        else Engine.kat500ClearFault()
                    }
                }
            }
        }
    }
}
