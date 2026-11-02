import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The amplifier: an RF-KIT RF2K-S read over its own REST API, read only
// (MobileEngineAmp.cpp). Its state, forward and reflected power, SWR, the
// PA's temperature, voltage and current, band, frequency, tuner, antenna and
// interface, as the amplifier reports them. Set up in Radio Setup › Amp; the
// tab shows once an amplifier is set up there.
ColumnLayout {
    id: ampTab
    spacing: Theme.spacing / 2

    readonly property var a: Engine.amp
    readonly property bool reading: a.state === "connected"
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
    Label {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        text: qsTr("Read only: this version shows what the amplifier reports and changes nothing on it. Operate / Standby, the antenna and the error reset come once tested on a real RF2K-S.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
