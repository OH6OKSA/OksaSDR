import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// 4O3A's Power Genius XL and Tuner Genius XL on the Amp tab, as the radio
// reports them and as the app reads them directly (MobileEngine4o3a.cpp).
// The amplifier: OPERATE / STANDBY, its forward power, SWR, drive, current,
// supply and temperatures on the desktop applet's scales, its ports and fan.
// The tuner: OPERATE / BYPASS / STANDBY, TUNE, its relays and antenna switch.
// With control on (Radio Setup › Amp) every change is confirmed here first;
// the devices guard themselves, and the tuner's relays and antenna are not
// moved while the radio transmits.
ColumnLayout {
    id: genius
    spacing: Theme.spacing / 2

    readonly property var p: Engine.pgxl
    readonly property var t: Engine.tgxl
    readonly property bool transmitting: Engine.radioTransmitting
    function num(v, digits, unit) {
        return v === undefined || v === null || v < 0 ? "–" : Number(v).toFixed(digits) + (unit ? " " + unit : "")
    }
    function link(d, name) {
        if (d.direct) return qsTr("%1 read directly at %2.").arg(name).arg(d.ip)
        if (d.authBlocked) return qsTr("%1 at %2 wants its remote code: enter it in Radio Setup › Amp.").arg(name).arg(d.ip)
        if (d.connecting) return qsTr("Connecting to %1 at %2…").arg(name).arg(d.ip)
        if (d.linkError) return qsTr("%1 at %2 not reached directly (%3): the radio's relay only.").arg(name).arg(d.ip || "–").arg(d.linkError)
        return d.radio ? qsTr("%1 through the radio.").arg(name) : ""
    }

    component Info: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
        Label { text: parent.value; color: Theme.text; font.pixelSize: 15; font.bold: true; horizontalAlignment: Text.AlignRight }
    }
    component Chip: Rectangle {
        property string word
        property bool hot
        radius: 4
        color: hot ? Theme.transmit : Theme.surfaceRaised
        border.color: hot ? Theme.transmit : Theme.textDim
        implicitWidth: chipText.implicitWidth + 16
        implicitHeight: chipText.implicitHeight + 8
        Label { id: chipText; anchors.centerIn: parent; text: parent.word; color: parent.hot ? "white" : Theme.text; font.pixelSize: 14; font.bold: true }
    }

    // ================= Power Genius XL =================
    ColumnLayout {
        id: pgxlBox
        visible: !!genius.p.present
        Layout.fillWidth: true
        spacing: Theme.spacing / 2
        readonly property bool canCommand: !!genius.p.control && !!genius.p.radio && !genius.p.pending

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: qsTr("Power Genius XL"); color: Theme.text; font.pixelSize: 17; font.bold: true; elide: Text.ElideRight }
            // IDLE is OPERATE not keyed, as the amplifier's own panel says it.
            Chip {
                objectName: "pgxlState"
                readonly property string st: genius.p.state || ""
                visible: st.length > 0
                word: st === "IDLE" ? qsTr("OPERATE") : st.replace("_", " ")
                hot: st.indexOf("TRANSMIT") === 0
            }
        }
        Label {
            Layout.fillWidth: true
            text: genius.link(genius.p, qsTr("The amplifier"))
            color: Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "pgxlAlert"
            visible: (genius.p.alert || "").length > 0
            Layout.fillWidth: true
            text: genius.p.alert || ""
            color: Theme.transmit
            font.pixelSize: 15
            font.bold: true
            wrapMode: Text.Wrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: genius.width >= 560 ? 3 : 1
            columnSpacing: Theme.spacing
            rowSpacing: 4
            BarGauge {
                objectName: "pgxlForward"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                kind: qsTr("Forward")
                min: 0; max: 2000; red: 1500
                live: !!genius.p.metersLive
                value: genius.p.fwd || 0
                text: qsTr("Forward") + " " + genius.num(genius.p.fwd, 0, "W")
            }
            BarGauge {
                objectName: "pgxlSwr"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                kind: qsTr("SWR")
                min: 1; max: 3; red: 2.5
                live: !!genius.p.metersLive && (genius.p.fwd || 0) >= 5
                value: (genius.p.fwd || 0) >= 5 ? (genius.p.swr || 1) : 1
                text: qsTr("SWR") + " " + ((genius.p.fwd || 0) >= 5 ? genius.num(genius.p.swr, 2, "") : "–")
            }
            BarGauge {
                objectName: "pgxlDrive"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                kind: qsTr("Drive")
                min: 0; max: 100; red: 75
                live: (genius.p.drive || -1) >= 0
                value: Math.max(0, genius.p.drive || 0)
                text: qsTr("Drive") + " " + genius.num(genius.p.drive, 0, "W")
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: genius.width >= 560 ? 2 : 1
            columnSpacing: Theme.spacing * 2
            rowSpacing: 2
            Info { label: qsTr("Current"); value: genius.num(genius.p.amps, 1, "A") }
            Info { label: qsTr("Supply"); value: genius.p.direct ? genius.num(genius.p.volts, 1, "V") + "   " + genius.num(genius.p.mains, 0, "V AC") : "–" }
            Info { label: qsTr("PA temperature"); value: genius.num(genius.p.temp, 0, "°C") }
            Info { label: qsTr("Harmonic load"); value: genius.p.direct ? genius.num(genius.p.hlTemp, 0, "°C") : "–" }
            Info {
                visible: !!genius.p.portsKnown
                label: qsTr("Port A")
                value: genius.p.portA && genius.p.portA.live ? genius.p.portA.band + "  " + genius.p.portA.bias : qsTr("not in use")
            }
            Info {
                visible: !!genius.p.portsKnown
                label: qsTr("Port B")
                value: genius.p.portB && genius.p.portB.live ? genius.p.portB.band + "  " + genius.p.portB.bias : qsTr("not in use")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            AppButton {
                objectName: "pgxlStandby"
                text: qsTr("Standby")
                checked: !genius.p.operate
                enabled: pgxlBox.canCommand
                Layout.preferredWidth: Theme.buttonWidth * 1.6
                onClicked: if (!checked) confirm.ask("pgxlStandby")
            }
            AppButton {
                objectName: "pgxlOperate"
                text: qsTr("Operate")
                checked: !!genius.p.operate
                enabled: pgxlBox.canCommand
                Layout.preferredWidth: Theme.buttonWidth * 1.6
                onClicked: if (!checked) confirm.ask("pgxlOperate")
            }
        }
        // Why the mode buttons are dim with Control on: an amplifier the radio
        // does not report (read only at an address set here: Standby / Operate
        // go through the radio, as on the desktop).
        Label {
            objectName: "pgxlModeNote"
            visible: !!genius.p.control && !genius.p.radio
            Layout.fillWidth: true
            text: qsTr("Standby / Operate go through the radio, and the radio does not report this amplifier: check its connection to the radio.")
            color: Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: !!genius.p.canWriteFan
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Fan"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            Repeater {
                model: [{ v: "STANDARD", l: qsTr("Standard") }, { v: "CONTEST", l: qsTr("Contest") }, { v: "BROADCAST", l: qsTr("Broadcast") }]
                delegate: AppButton {
                    required property var modelData
                    objectName: "pgxlFan" + modelData.v
                    text: modelData.l
                    checked: genius.p.fanMode === modelData.v
                    enabled: !!genius.p.control
                    onClicked: if (!checked) confirm.ask("pgxlFan", modelData.v)
                }
            }
        }
        Label {
            objectName: "pgxlNotice"
            Layout.fillWidth: true
            text: genius.p.notice ? genius.p.notice
                : !genius.p.control ? qsTr("Control is off: turn it on in Radio Setup › Amp to change the amplifier from here.")
                : genius.p.pending ? qsTr("Waiting for the amplifier…") : ""
            visible: text.length > 0
            color: genius.p.notice ? Theme.warning : Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    // ================= Tuner Genius XL =================
    ColumnLayout {
        id: tuner
        visible: !!genius.t.present
        Layout.fillWidth: true
        Layout.topMargin: genius.p.present ? Theme.spacing : 0
        spacing: Theme.spacing / 2
        readonly property bool control: !!genius.t.control
        // The relays and the antenna: the direct link, and the radio in receive.
        readonly property bool canMove: control && !!genius.t.direct && !genius.transmitting && !genius.t.tuning

        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: qsTr("Tuner Genius XL"); color: Theme.text; font.pixelSize: 17; font.bold: true }
            Chip {
                objectName: "tgxlState"
                word: genius.t.tuning ? qsTr("TUNING") : !genius.t.operate ? qsTr("STANDBY") : genius.t.bypass ? qsTr("BYPASS") : qsTr("OPERATE")
                hot: !!genius.t.tuning
            }
        }
        Label {
            Layout.fillWidth: true
            text: genius.link(genius.t, qsTr("The tuner"))
            color: Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "tgxlAlert"
            visible: (genius.t.alert || "").length > 0
            Layout.fillWidth: true
            text: genius.t.alert || ""
            color: Theme.warning
            font.pixelSize: 15
            font.bold: true
            wrapMode: Text.Wrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: genius.width >= 560 ? 2 : 1
            columnSpacing: Theme.spacing
            rowSpacing: 4
            BarGauge {
                objectName: "tgxlForward"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                kind: qsTr("Forward")
                min: 0; max: 2000; red: 1500
                live: true
                value: genius.t.fwd || 0
                text: qsTr("Forward") + " " + genius.num(genius.t.fwd, 0, "W")
            }
            BarGauge {
                objectName: "tgxlSwr"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                kind: qsTr("SWR")
                min: 1; max: 3; red: 2.5
                live: (genius.t.fwd || 0) >= 5
                value: (genius.t.fwd || 0) >= 5 ? Math.max(1, genius.t.swr || 1) : 1
                text: qsTr("SWR") + " " + ((genius.t.fwd || 0) >= 5 ? genius.num(genius.t.swr, 2, "") : "–")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            AppButton {
                objectName: "tgxlStandby"
                text: qsTr("Standby")
                checked: !genius.t.operate
                enabled: tuner.control && !genius.t.tuning
                onClicked: if (!checked) confirm.ask("tgxlStandby")
            }
            AppButton {
                objectName: "tgxlBypass"
                text: qsTr("Bypass")
                checked: !!genius.t.operate && !!genius.t.bypass
                enabled: tuner.control && !genius.t.tuning
                onClicked: if (!checked) confirm.ask("tgxlBypass")
            }
            AppButton {
                objectName: "tgxlOperate"
                text: qsTr("Operate")
                checked: !!genius.t.operate && !genius.t.bypass
                enabled: tuner.control && !genius.t.tuning
                onClicked: if (!checked) confirm.ask("tgxlOperate")
            }
        }
        AppButton {
            objectName: "tgxlTune"
            text: genius.t.tuning ? qsTr("Stop tuning") : qsTr("Tune")
            checked: !!genius.t.tuning
            enabled: tuner.control && !!genius.t.operate
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            // Stopping a tune is never asked.
            onClicked: genius.t.tuning ? Engine.tgxlTune() : confirm.ask("tgxlTune")
        }
        // The relays, a step at a time (the direct link only).
        Repeater {
            model: genius.t.direct ? [{ r: 0, l: "C1", v: genius.t.relayC1 }, { r: 1, l: "L", v: genius.t.relayL }, { r: 2, l: "C2", v: genius.t.relayC2 }] : []
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 4
                Label { text: modelData.l; color: Theme.text; font.pixelSize: 15; Layout.preferredWidth: 32 }
                ProgressBar { from: 0; to: 255; value: modelData.v; Layout.fillWidth: true }
                Label { text: modelData.v; color: Theme.text; font.pixelSize: 15; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 36 }
                AppButton {
                    objectName: "tgxlRelay" + modelData.l + "Down"
                    text: "−"
                    enabled: tuner.canMove
                    Layout.preferredWidth: Theme.touchTarget
                    onClicked: Engine.tgxlAdjustRelay(modelData.r, -1)
                }
                AppButton {
                    objectName: "tgxlRelay" + modelData.l + "Up"
                    text: "+"
                    enabled: tuner.canMove
                    Layout.preferredWidth: Theme.touchTarget
                    onClicked: Engine.tgxlAdjustRelay(modelData.r, 1)
                }
            }
        }
        RowLayout {
            visible: !!genius.t.antennaSwitch && !!genius.t.direct
            Layout.fillWidth: true
            spacing: 4
            Label { text: qsTr("Antenna"); color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
            Repeater {
                model: 3
                delegate: AppButton {
                    required property int index
                    objectName: "tgxlAntenna" + (index + 1)
                    text: index + 1
                    checked: genius.t.antenna === index + 1
                    enabled: tuner.canMove
                    Layout.preferredWidth: Theme.touchTarget
                    onClicked: if (!checked) confirm.ask("tgxlAntenna", index + 1)
                }
            }
        }
        Label {
            objectName: "tgxlNotice"
            Layout.fillWidth: true
            text: genius.t.notice ? genius.t.notice
                : !genius.t.control ? qsTr("Control is off: turn it on in Radio Setup › Amp to change the tuner from here.")
                : genius.transmitting && genius.t.direct ? qsTr("The relays and the antenna wait until the radio is in receive.") : ""
            visible: text.length > 0
            color: genius.t.notice ? Theme.warning : Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    // One confirmation per change, naming it (as the RF2K-S's).
    Dialog {
        id: confirm
        objectName: "geniusConfirm"
        property string action: ""
        property var arg
        function ask(what, a) {
            action = what
            arg = a
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        readonly property var words: ({
            "pgxlOperate": [qsTr("Put the Power Genius XL in OPERATE?"), qsTr("The next transmission goes through the amplifier: check that the radio's power suits it."), qsTr("Operate")],
            "pgxlStandby": [qsTr("Put the Power Genius XL in STANDBY?"), qsTr("The radio's signal goes past the amplifier, not amplified."), qsTr("Standby")],
            "pgxlFan": [qsTr("Set the amplifier's fan to %1?").arg(String(confirm.arg).toLowerCase()), qsTr("Until the amplifier restarts (not saved in it)."), qsTr("Set")],
            "tgxlOperate": [qsTr("Put the Tuner Genius XL in OPERATE?"), qsTr("The tuner's network goes in line with its last solution for this band."), qsTr("Operate")],
            "tgxlBypass": [qsTr("Bypass the Tuner Genius XL?"), qsTr("The signal goes past the tuner's network, untuned."), qsTr("Bypass")],
            "tgxlStandby": [qsTr("Put the Tuner Genius XL in STANDBY?"), qsTr("The tuner stops tuning and passes the signal through."), qsTr("Standby")],
            "tgxlTune": [qsTr("Tune with the Tuner Genius XL?"), qsTr("The tuner keys the radio for a tune carrier itself, and keys or holds its amplifier as its own setting says. Tap Stop tuning to end it."), qsTr("Tune")],
            "tgxlAntenna": [qsTr("Switch the tuner to antenna %1?").arg(confirm.arg), qsTr("Check that antenna %1 is connected and suits this band.").arg(confirm.arg), qsTr("Antenna %1").arg(confirm.arg)]
        })
        readonly property var w: words[action] || ["", "", ""]
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label { Layout.fillWidth: true; text: confirm.w[0]; color: Theme.text; font.pixelSize: 18; font.bold: true; wrapMode: Text.Wrap }
            Label { Layout.fillWidth: true; text: confirm.w[1]; color: Theme.textDim; font.pixelSize: 15; wrapMode: Text.Wrap }
            RowLayout {
                Layout.fillWidth: true
                AppButton { objectName: "geniusConfirmNo"; text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: confirm.close() }
                AppButton {
                    objectName: "geniusConfirmYes"
                    text: confirm.w[2]
                    checked: true
                    Layout.fillWidth: true
                    onClicked: {
                        const a = confirm.action
                        const v = confirm.arg
                        confirm.close()
                        if (a === "pgxlOperate" || a === "pgxlStandby") Engine.pgxlSetOperate(a === "pgxlOperate")
                        else if (a === "pgxlFan") Engine.pgxlSetFanMode(v)
                        else if (a === "tgxlOperate") { Engine.tgxlSetOperate(true); Engine.tgxlSetBypass(false) }
                        else if (a === "tgxlBypass") { Engine.tgxlSetOperate(true); Engine.tgxlSetBypass(true) }
                        else if (a === "tgxlStandby") Engine.tgxlSetOperate(false)
                        else if (a === "tgxlTune") Engine.tgxlTune()
                        else if (a === "tgxlAntenna") Engine.tgxlSetAntenna(v)
                    }
                }
            }
        }
    }
}
