import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Radio Setup, after SmartSDR's and the desktop's: the radio's own settings
// a radio needs on first use (identity, remote, transmit limits and timing,
// CW keyer and microphone, line out and headphones). Every change is written
// to the radio; values are re-read once a second while the page is open.
Popup {
    id: setup
    objectName: "radioSetup"
    parent: Overlay.overlay
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width * 0.95 : 600, 640)
    height: parent ? parent.height * 0.9 : 800
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius }

    property var d: ({})
    property var xvtrs: []
    property int section: 0
    // Another tab starts at its top (a long tab's scroll left a short one
    // out of sight).
    onSectionChanged: scroll.contentItem.contentY = 0
    // The TX band settings' band buttons: re-made only when the radio's
    // band list changes (not on every read, which would drop a tap).
    property var txBandList: []
    property string txBandKey: ""
    property int txBandId: -1
    readonly property var txBand: {
        const list = d.txBands || []
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === txBandId) return list[i]
        return null
    }
    function bandLabel(name) { return /^[0-9]+$/.test(name) ? name + "m" : name }
    // USB cables: the cable buttons and the sources' antenna and slice
    // choices re-made only when they change, as the band buttons.
    property var usb: ({ cables: [], antennas: [], slices: [] })
    property var usbList: []
    property string usbKey: ""
    property string usbSn: ""
    property int usbBit: 0
    property var antennaChoices: []
    property var sliceChoices: []
    readonly property var cable: {
        const list = usb.cables || []
        for (let i = 0; i < list.length; ++i)
            if (list[i].sn === usbSn) return list[i]
        return null
    }
    function setUsb(key, value) { Engine.setUsbCable(usbSn, key, String(value)); refreshSoon.restart() }
    function setUsbBit(key, value) { Engine.setUsbCableBit(usbSn, usbBit, key, String(value)); refreshSoon.restart() }
    function refreshUsb() {
        usb = Engine.usbCables()
        const list = usb.cables || []
        const key = list.map(c => c.sn + ":" + c.name + ":" + c.present).join(",")
            + "|" + (usb.antennas || []).join(",") + "|" + (usb.slices || []).length
        if (key !== usbKey) {
            usbKey = key
            usbList = list.map(c => ({ sn: c.sn, label: (c.name || c.sn) + (c.present ? "" : qsTr(" (unplugged)")) }))
            antennaChoices = (usb.antennas || []).map(a => ({ label: a, value: a }))
            sliceChoices = (usb.slices || []).map(i => ({ label: String.fromCharCode(65 + i), value: String(i) }))
            if (!list.some(c => c.sn === usbSn)) usbSn = list.length > 0 ? list[0].sn : ""
        }
    }
    function refresh() {
        d = Engine.radioSetup()
        xvtrs = Engine.transverters()
        refreshUsb()
        const list = d.txBands || []
        const key = list.map(b => b.id + ":" + b.name).join(",")
        if (key !== txBandKey) {
            txBandKey = key
            txBandList = list.map(b => ({ id: b.id, name: b.name }))
            // The band the radio is on, else the first.
            if (!list.some(b => b.id === txBandId)) {
                const here = list.find(b => bandLabel(b.name) === Engine.activeBand)
                txBandId = here ? here.id : (list.length > 0 ? list[0].id : -1)
            }
        }
    }
    function set(key, value) { Engine.setRadioSetup(key, value); refreshSoon.restart() }
    function setBand(field, value) { set("txBand", { id: txBandId, field: field, value: value }) }
    // A change that could key the radio by itself asks first (a TX REQ
    // input read active high: an open input then requests transmit), and
    // so does one that can lose the radio or its frequency (a restart, an
    // address, the reference, a calibration).
    function setRisky(title, text, apply, yes) {
        risky.heading = title
        risky.text = text
        risky.apply = apply
        risky.yes = yes || qsTr("Switch")
        risky.open()
    }
    onOpened: refresh()
    Timer { interval: 1000; repeat: true; running: setup.opened; onTriggered: setup.refresh() }
    Timer { id: refreshSoon; interval: 300; onTriggered: setup.refresh() }

    // ---- pieces ----
    component Heading: Label {
        color: Theme.textDim
        font.pixelSize: 14
        Layout.topMargin: Theme.spacing / 2
    }
    component Info: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
        Label { text: parent.value.length > 0 ? parent.value : "–"; color: Theme.text; font.pixelSize: 15; font.bold: true }
    }
    // On/off as two of the app's buttons, the current one dark.
    component OnOff: RowLayout {
        id: onOff
        property string label
        property bool on
        signal toggled(bool on)
        Layout.fillWidth: true
        Label { text: onOff.label; color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
        AppButton {
            objectName: onOff.objectName.length > 0 ? onOff.objectName + "Off" : ""
            text: qsTr("Off"); checked: !onOff.on; focusPolicy: Qt.NoFocus; Layout.preferredWidth: 64; onClicked: onOff.toggled(false)
        }
        AppButton {
            objectName: onOff.objectName.length > 0 ? onOff.objectName + "On" : ""
            text: qsTr("On"); checked: onOff.on; focusPolicy: Qt.NoFocus; Layout.preferredWidth: 64; onClicked: onOff.toggled(true)
        }
    }
    // One of a few values as a row (or rows) of the app's buttons, the
    // current one dark; options [{label, value}].
    component Choice: ColumnLayout {
        id: choice
        property string label
        property var options: []
        property var current
        property int columns: 4
        signal chosen(var value)
        Layout.fillWidth: true
        spacing: 2
        Label { visible: choice.label.length > 0; text: choice.label; color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
        GridLayout {
            Layout.fillWidth: true
            columns: choice.columns
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: choice.options
                delegate: AppButton {
                    required property var modelData
                    objectName: choice.objectName.length > 0 ? choice.objectName + String(modelData.value).replace(/[^A-Za-z0-9]/g, "") : ""
                    text: modelData.label
                    checked: String(choice.current) === String(modelData.value)
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onClicked: if (!checked) choice.chosen(modelData.value)
                }
            }
        }
    }
    // Where a USB cable's frequency or band comes from (FlexLib's sources),
    // and which antenna or slice for the ones that name one.
    component SourceChoice: ColumnLayout {
        id: source
        property var item: ({})
        signal setKey(string key, string value)
        Layout.fillWidth: true
        spacing: Theme.spacing / 2
        Choice {
            objectName: source.objectName.length > 0 ? source.objectName : ""
            label: qsTr("Frequency from")
            columns: 4
            options: [{ label: qsTr("None"), value: "None" }, { label: qsTr("TX pan"), value: "tx_pan" },
                      { label: qsTr("TX slice"), value: "tx_slice" }, { label: qsTr("Active slice"), value: "active_slice" },
                      { label: qsTr("TX antenna"), value: "tx_ant" }, { label: qsTr("RX antenna"), value: "rx_ant" },
                      { label: qsTr("Slice"), value: "ordinal_slice" }]
            current: source.item.source || "None"
            onChosen: (v) => source.setKey("source", v)
        }
        Choice {
            visible: source.item.source === "tx_ant" || source.item.source === "rx_ant"
            label: source.item.source === "tx_ant" ? qsTr("Transmit antenna") : qsTr("Receive antenna")
            columns: 4
            options: setup.antennaChoices
            current: source.item.source === "tx_ant" ? source.item.sourceTxAnt : source.item.sourceRxAnt
            onChosen: (v) => source.setKey(source.item.source === "tx_ant" ? "source_tx_ant" : "source_rx_ant", v)
        }
        Choice {
            visible: source.item.source === "ordinal_slice"
            label: qsTr("Slice")
            columns: 8
            options: setup.sliceChoices
            current: source.item.sourceSlice
            onChosen: (v) => source.setKey("source_slice", v)
        }
    }
    // A serial port's settings (CAT and Passthrough cables).
    component SerialSettings: ColumnLayout {
        id: serial
        property var item: ({})
        signal setKey(string key, string value)
        Layout.fillWidth: true
        spacing: Theme.spacing / 2
        Choice {
            objectName: "usbSpeed"
            label: qsTr("Speed (baud)")
            columns: 5
            options: [300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
                     .map(b => ({ label: String(b), value: String(b) }))
            current: String(serial.item.speed || "")
            onChosen: (v) => serial.setKey("speed", v)
        }
        Choice {
            label: qsTr("Data bits")
            columns: 4
            options: [{ label: "7", value: "7" }, { label: "8", value: "8" }]
            current: String(serial.item.dataBits || "")
            onChosen: (v) => serial.setKey("data_bits", v)
        }
        Choice {
            label: qsTr("Parity")
            columns: 5
            options: ["none", "odd", "even", "mark", "space"].map(p => ({ label: p, value: p }))
            current: serial.item.parity || ""
            onChosen: (v) => serial.setKey("parity", v)
        }
        Choice {
            label: qsTr("Stop bits")
            columns: 4
            options: [{ label: "1", value: "1" }, { label: "2", value: "2" }]
            current: String(serial.item.stopBits || "")
            onChosen: (v) => serial.setKey("stop_bits", v)
        }
        Choice {
            label: qsTr("Flow control")
            columns: 4
            options: [{ label: qsTr("None"), value: "none" }, { label: "RTS/CTS", value: "rts_cts" },
                      { label: "DTR/DSR", value: "dtr_dsr" }, { label: "XON/XOFF", value: "xon_xoff" }]
            current: serial.item.flow || ""
            onChosen: (v) => serial.setKey("flow_control", v)
        }
    }
    // A slider that writes when released (not a command per pixel).
    component Level: ColumnLayout {
        id: level
        property string label
        property string unit: ""
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property real current: 0
        signal committed(real value)
        Layout.fillWidth: true
        spacing: 0
        Label {
            text: level.label + "  " + Math.round(slider.busy ? slider.shown : level.current) + level.unit
            color: Theme.text
            font.pixelSize: 15
        }
        // Sent on release, each − / + step on its own; follows the radio
        // except while a finger is on it.
        FineSlider {
            id: slider
            objectName: level.objectName.length > 0 ? level.objectName + "Slider" : ""
            accessibleName: level.label
            Layout.fillWidth: true
            from: level.from
            to: level.to
            stepSize: level.stepSize
            liveDrag: false
            value: level.current
            onMoved: (v) => level.committed(Math.round(v))
        }
    }
    // A text value with its own Save (written only on Save).
    component TextSetting: ColumnLayout {
        id: textSetting
        property string label
        property string current
        property string hint
        property bool upper: true
        signal saved(string text)
        Layout.fillWidth: true
        spacing: 2
        Label { text: textSetting.label; color: Theme.text; font.pixelSize: 15 }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: field
                objectName: textSetting.objectName.length > 0 ? textSetting.objectName + "Field" : ""
                Accessible.name: textSetting.label
                Layout.fillWidth: true
                font.pixelSize: 17
                placeholderText: textSetting.current.length > 0 ? textSetting.current : textSetting.hint
                inputMethodHints: textSetting.upper ? (Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText) : Qt.ImhNoPredictiveText
                onAccepted: saveButton.clicked()
            }
            AppButton {
                id: saveButton
                objectName: textSetting.objectName.length > 0 ? textSetting.objectName + "Save" : ""
                text: qsTr("Save")
                enabled: field.text.trim().length > 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    textSetting.saved(textSetting.upper ? field.text.trim().toUpperCase() : field.text.trim())
                    field.text = ""
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Radio Setup"); color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            AppButton { text: qsTr("Close"); focusPolicy: Qt.NoFocus; onClicked: setup.close() }
        }
        Label {
            Layout.fillWidth: true
            text: setup.section === 6
                  ? qsTr("The amplifier is not a setting of the radio: this device reaches it directly, and the address stays on this device.")
                  : qsTr("These are the radio's own settings, as in SmartSDR's Radio Setup: changes are written to the radio.")
            color: Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        // Nine in a row where each gets 84 px ("Phone/CW" whole), else 5 + 4,
        // 3 + 3 + 3 on the narrowest.
        GridLayout {
            Layout.fillWidth: true
            columns: width >= 9 * 84 ? 9 : width >= 5 * 68 ? 5 : 3
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: [qsTr("Radio"), qsTr("Transmit"), qsTr("Phone/CW"), qsTr("Audio"), qsTr("RX"), qsTr("Network"), qsTr("XVTR"), qsTr("Amp"), qsTr("USB")]
                delegate: AppButton {
                    required property int index
                    required property string modelData
                    objectName: "setupTab" + index
                    text: modelData
                    checked: setup.section === index
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onClicked: setup.section = index
                }
            }
        }
        PageScroll {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            StackLayout {
                width: scroll.availableWidth
                currentIndex: setup.section

                // ---- Radio ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    TextSetting {
                        label: qsTr("Callsign  (%1)").arg(setup.d.callsign || qsTr("not set"))
                        current: setup.d.callsign || ""
                        hint: qsTr("e.g. OH6OKSA")
                        onSaved: (t) => setup.set("callsign", t)
                    }
                    TextSetting {
                        label: qsTr("Radio name  (%1)").arg(setup.d.nickname || "–")
                        current: setup.d.nickname || ""
                        hint: qsTr("Shown in the radio list")
                        upper: false
                        onSaved: (t) => setup.set("nickname", t)
                    }
                    // How the radio lists this device to its other stations
                    // (the desktop's Station Name).
                    TextSetting {
                        objectName: "setupStationName"
                        label: qsTr("This station's name  (%1)").arg(setup.d.stationName || "–")
                        current: setup.d.stationName || ""
                        hint: qsTr("Shown to the radio's other stations")
                        upper: false
                        onSaved: (t) => setup.set("stationName", t)
                    }
                    Heading { text: qsTr("Remote") }
                    OnOff {
                        label: qsTr("Remote on (SmartLink may power the radio on)")
                        on: !!setup.d.remoteOn
                        onToggled: (v) => setup.set("remoteOn", v)
                    }
                    OnOff {
                        label: qsTr("Mute the radio's own audio while remote")
                        on: !!setup.d.muteLocalWhenRemote
                        onToggled: (v) => setup.set("muteLocalWhenRemote", v)
                    }
                    Heading { text: qsTr("Stations") }
                    // Several stations at once (multiFLEX), as the radio
                    // reports it: firmware 4.3 refuses "radio set mf_enable"
                    // (could not parse), so it is shown, not set.
                    Info {
                        objectName: "setupMultiFlex"
                        visible: !!setup.d.multiFlexKnown
                        label: qsTr("multiFLEX (several stations)")
                        value: setup.d.multiFlex ? qsTr("On") : qsTr("Off")
                    }
                    AppButton {
                        text: qsTr("Connected stations (%1)…").arg(Engine.stations.length)
                        leftPadding: 16
                        rightPadding: 16
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            setup.close()
                            Engine.requestStations()
                        }
                    }
                    Heading { text: qsTr("About the radio") }
                    Info { label: qsTr("Model"); value: setup.d.model || "" }
                    Info { label: qsTr("Serial"); value: setup.d.serial || "" }
                    Info { label: qsTr("Firmware"); value: setup.d.version || "" }
                    Info { label: qsTr("Region"); value: setup.d.region || "" }
                    Info { label: qsTr("Address"); value: setup.d.ip || "" }
                    Info { label: qsTr("Options"); value: setup.d.options || "" }
                    Heading { visible: !!setup.d.licenseKnown; text: qsTr("License") }
                    Info { visible: !!setup.d.licenseKnown; label: qsTr("Subscription"); value: setup.d.licenseSubscription || "" }
                    Info { visible: !!setup.d.licenseKnown; label: qsTr("Expires"); value: setup.d.licenseExpiration || "" }
                    Info { visible: !!setup.d.licenseKnown; label: qsTr("Highest licensed version"); value: setup.d.licenseMaxVersion || "" }
                    Info { visible: !!setup.d.licenseKnown; label: qsTr("Radio ID"); value: setup.d.licenseRadioId || "" }
                    // GPS, with one fitted: what the radio reports (nothing
                    // to set, as on the desktop).
                    Heading { visible: !!setup.d.gpsKnown; text: qsTr("GPS") }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("Status"); value: setup.d.gpsStatus || "" }
                    Info {
                        visible: !!setup.d.gpsKnown
                        label: qsTr("Satellites")
                        value: qsTr("%1 tracked, %2 visible").arg(setup.d.gpsTracked || 0).arg(setup.d.gpsVisible || 0)
                    }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("Grid"); value: setup.d.gpsGrid || "" }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("Latitude"); value: setup.d.gpsLat || "" }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("Longitude"); value: setup.d.gpsLon || "" }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("Altitude"); value: setup.d.gpsAltitude || "" }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("Frequency error"); value: setup.d.gpsFreqError || "" }
                    Info { visible: !!setup.d.gpsKnown; label: qsTr("UTC"); value: setup.d.gpsTime || "" }
                    // Restart the radio (asked first): every station loses
                    // it until it is back, a minute or so.
                    AppButton {
                        objectName: "setupReboot"
                        visible: !!setup.d.canReboot
                        text: qsTr("Restart the radio…")
                        leftPadding: 16
                        rightPadding: 16
                        Layout.topMargin: Theme.spacing
                        Layout.preferredHeight: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        onClicked: setup.setRisky(qsTr("Restart the radio?"),
                                                  qsTr("Every station connected to it loses the radio until it has started again, about a minute. Over SmartLink, connect again by hand."),
                                                  () => Engine.rebootRadio(), qsTr("Restart"))
                    }
                }

                // ---- Transmit ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Level {
                        label: qsTr("Maximum power")
                        unit: " %"
                        current: setup.d.maxPower || 0
                        onCommitted: (v) => setup.set("maxPower", v)
                    }
                    Level {
                        label: qsTr("TX delay")
                        unit: " ms"
                        to: 500
                        stepSize: 5
                        current: setup.d.txDelay || 0
                        onCommitted: (v) => setup.set("txDelay", v)
                    }
                    Level {
                        label: qsTr("Transmit timeout (0 = off)")
                        unit: " s"
                        to: 600
                        stepSize: 10
                        current: setup.d.interlockTimeoutS || 0
                        onCommitted: (v) => setup.set("interlockTimeoutS", v)
                    }
                    // The radio's own setting, as SmartSDR's Show TX in
                    // waterfall: off, the waterfall waits while transmitting.
                    OnOff {
                        label: qsTr("Show my transmission in the waterfall")
                        on: !!setup.d.showTxInWaterfall
                        onToggled: (v) => setup.set("showTxInWaterfall", v)
                    }
                    Heading { text: qsTr("TUNE signal") }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: qsTr("Single tone")
                            checked: setup.d.tuneMode === "single_tone"
                            focusPolicy: Qt.NoFocus
                            Layout.fillWidth: true
                            onClicked: setup.set("tuneMode", "single_tone")
                        }
                        AppButton {
                            text: qsTr("Two tone")
                            checked: setup.d.tuneMode === "two_tone"
                            focusPolicy: Qt.NoFocus
                            Layout.fillWidth: true
                            onClicked: setup.set("tuneMode", "two_tone")
                        }
                    }

                    // ---- Keying an amplifier: SmartSDR's Timings and
                    // Interlocks and its TX Band Settings, as the desktop's
                    // Transmit tab, so no other program is needed ----
                    Heading { text: qsTr("Keying outputs: delay after PTT") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Each output closes this long after PTT, and the radio's signal waits for TX delay, so an amplifier's relays are set before RF arrives (an amplifier needs about 20 ms: e.g. TX1 20 ms, TX delay 30 ms). Kept in the radio's TX profile (%1).").arg(setup.d.txProfile || "–")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Level {
                        label: qsTr("RCA TX1")
                        unit: " ms"
                        to: 500
                        stepSize: 5
                        current: setup.d.tx1Delay || 0
                        onCommitted: (v) => setup.set("tx1Delay", v)
                    }
                    Level {
                        label: qsTr("RCA TX2")
                        unit: " ms"
                        to: 500
                        stepSize: 5
                        current: setup.d.tx2Delay || 0
                        onCommitted: (v) => setup.set("tx2Delay", v)
                    }
                    Level {
                        label: qsTr("RCA TX3")
                        unit: " ms"
                        to: 500
                        stepSize: 5
                        current: setup.d.tx3Delay || 0
                        onCommitted: (v) => setup.set("tx3Delay", v)
                    }
                    Level {
                        label: qsTr("ACC TX")
                        unit: " ms"
                        to: 500
                        stepSize: 5
                        current: setup.d.accTxDelay || 0
                        onCommitted: (v) => setup.set("accTxDelay", v)
                    }

                    Heading { text: qsTr("TX REQ inputs") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("A footswitch or sequencer on a TX REQ input keys the radio, on the bands where that input is on (below). Active low: a contact to ground transmits, the usual way. Active high: a high level transmits, and so does an open input.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: [{ key: "rcaTxReqPolarity", band: "rcaTxReq", label: qsTr("RCA TX REQ"), name: "Rca" },
                                { key: "accTxReqPolarity", band: "accTxReq", label: qsTr("ACC TX REQ"), name: "Acc" }]
                        delegate: RowLayout {
                            id: polarity
                            required property var modelData
                            Layout.fillWidth: true
                            readonly property bool high: (setup.d[modelData.key] || 0) === 1
                            Label { text: polarity.modelData.label; color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            AppButton {
                                objectName: "txReq" + polarity.modelData.name + "Low"
                                text: qsTr("Active low")
                                checked: !polarity.high
                                Layout.preferredWidth: Math.max(Theme.buttonWidth * 1.5, implicitWidth)
                                onClicked: if (polarity.high) setup.set(polarity.modelData.key, 0)
                            }
                            AppButton {
                                objectName: "txReq" + polarity.modelData.name + "High"
                                text: qsTr("Active high")
                                checked: polarity.high
                                Layout.preferredWidth: Math.max(Theme.buttonWidth * 1.5, implicitWidth)
                                onClicked: {
                                    if (polarity.high) return
                                    const key = polarity.modelData.key
                                    setup.setRisky(qsTr("%1 active high?").arg(polarity.modelData.label),
                                                   qsTr("An open input then keys the radio: with nothing plugged in, the radio transmits on every band where %1 is on. Switch only with a sequencer that drives the input high to transmit.").arg(polarity.modelData.label),
                                                   () => setup.set(key, 1))
                                }
                            }
                        }
                    }

                    OnOff {
                        objectName: "pttMicOverride"
                        visible: !!setup.d.pttMicOverrideKnown
                        label: qsTr("PTT mic override (a hardware PTT sends the analog mic, whatever the TX source)")
                        on: !!setup.d.pttMicOverride
                        onToggled: (v) => setup.set("pttMicOverride", v)
                    }

                    Heading { text: qsTr("Inhibit during TUNE (this device)") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("The outputs held open while TUNE or the ATU transmits, so tuning at low power does not key the amplifier; they close again after. This device's setting, as the desktop's.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    OnOff { objectName: "tuneInhibitTx1"; label: qsTr("RCA TX1"); on: !!setup.d.TuneInhibitTx1; onToggled: (v) => setup.set("TuneInhibitTx1", v) }
                    OnOff { objectName: "tuneInhibitTx2"; label: qsTr("RCA TX2"); on: !!setup.d.TuneInhibitTx2; onToggled: (v) => setup.set("TuneInhibitTx2", v) }
                    OnOff { objectName: "tuneInhibitTx3"; label: qsTr("RCA TX3"); on: !!setup.d.TuneInhibitTx3; onToggled: (v) => setup.set("TuneInhibitTx3", v) }
                    OnOff { objectName: "tuneInhibitAccTx"; label: qsTr("ACC TX"); on: !!setup.d.TuneInhibitAccTx; onToggled: (v) => setup.set("TuneInhibitAccTx", v) }

                    Heading { text: qsTr("TX band settings") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Per band: the power, and which keying outputs and TX REQ inputs work there (TX profile %1). Pick a band:").arg(setup.d.txProfile || "–")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 7 * 64 ? 7 : 5
                        columnSpacing: 4
                        rowSpacing: 4
                        Repeater {
                            model: setup.txBandList
                            delegate: AppButton {
                                required property var modelData
                                objectName: "txBand" + modelData.name
                                text: setup.bandLabel(modelData.name)
                                checked: setup.txBandId === modelData.id
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                onClicked: setup.txBandId = modelData.id
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: setup.txBand !== null
                        spacing: Theme.spacing / 2
                        readonly property var b: setup.txBand || ({})
                        readonly property string band: setup.txBand ? setup.bandLabel(setup.txBand.name) : ""
                        Level {
                            label: qsTr("RF power on %1").arg(parent.band)
                            unit: " %"
                            current: parent.b.rfPower || 0
                            onCommitted: (v) => setup.setBand("rfPower", v)
                        }
                        Level {
                            label: qsTr("Tune power on %1").arg(parent.band)
                            unit: " %"
                            current: parent.b.tunePower || 0
                            onCommitted: (v) => setup.setBand("tunePower", v)
                        }
                        OnOff { objectName: "txBandTx1"; label: qsTr("RCA TX1 output"); on: !!parent.b.tx1; onToggled: (v) => setup.setBand("tx1", v) }
                        OnOff { objectName: "txBandTx2"; label: qsTr("RCA TX2 output"); on: !!parent.b.tx2; onToggled: (v) => setup.setBand("tx2", v) }
                        OnOff { objectName: "txBandTx3"; label: qsTr("RCA TX3 output"); on: !!parent.b.tx3; onToggled: (v) => setup.setBand("tx3", v) }
                        OnOff { objectName: "txBandAccTx"; label: qsTr("ACC TX output"); on: !!parent.b.accTx; onToggled: (v) => setup.setBand("accTx", v) }
                        OnOff {
                            objectName: "txBandRcaTxReq"
                            label: qsTr("RCA TX REQ input")
                            on: !!parent.b.rcaTxReq
                            onToggled: (v) => {
                                if (v && setup.d.rcaTxReqPolarity === 1)
                                    setup.setRisky(qsTr("RCA TX REQ on %1?").arg(parent.band),
                                                   qsTr("The input is set active high: with nothing plugged in, the radio transmits on %1 as soon as this is on.").arg(parent.band),
                                                   () => setup.setBand("rcaTxReq", true))
                                else
                                    setup.setBand("rcaTxReq", v)
                            }
                        }
                        OnOff {
                            objectName: "txBandAccTxReq"
                            label: qsTr("ACC TX REQ input")
                            on: !!parent.b.accTxReq
                            onToggled: (v) => {
                                if (v && setup.d.accTxReqPolarity === 1)
                                    setup.setRisky(qsTr("ACC TX REQ on %1?").arg(parent.band),
                                                   qsTr("The input is set active high: with nothing plugged in, the radio transmits on %1 as soon as this is on.").arg(parent.band),
                                                   () => setup.setBand("accTxReq", true))
                                else
                                    setup.setBand("accTxReq", v)
                            }
                        }
                        OnOff { objectName: "txBandInhibit"; label: qsTr("PTT inhibit (no transmitting on %1)").arg(parent.band); on: !!parent.b.inhibit; onToggled: (v) => setup.setBand("inhibit", v) }
                        OnOff { objectName: "txBandHwAlc"; label: qsTr("Hardware ALC (the amplifier's, at the ALC jack)"); on: !!parent.b.hwAlc; onToggled: (v) => setup.setBand("hwAlc", v) }
                    }
                }

                // ---- Phone/CW ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Heading { text: qsTr("Microphone") }
                    OnOff { label: qsTr("Mic bias (electret microphones)"); on: !!setup.d.micBias; onToggled: (v) => setup.set("micBias", v) }
                    OnOff { label: qsTr("Mic boost (+20 dB)"); on: !!setup.d.micBoost; onToggled: (v) => setup.set("micBoost", v) }
                    OnOff {
                        objectName: "setupMetInRx"
                        label: qsTr("Mic level meter in receive too")
                        on: !!setup.d.metInRx
                        onToggled: (v) => setup.set("metInRx", v)
                    }
                    // This device's microphone gain is the TX tab's Mic level.
                    Heading { text: qsTr("CW keyer") }
                    Level {
                        label: qsTr("Break-in delay")
                        unit: " ms"
                        to: 2000
                        stepSize: 10
                        current: setup.d.cwDelay || 0
                        onCommitted: (v) => setup.set("cwDelay", v)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Iambic"); color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true }
                        AppButton { text: qsTr("Off"); checked: !setup.d.iambic; focusPolicy: Qt.NoFocus; Layout.preferredWidth: 56; onClicked: setup.set("iambic", false) }
                        AppButton {
                            text: "A"
                            checked: !!setup.d.iambic && setup.d.iambicMode === 0
                            focusPolicy: Qt.NoFocus
                            Layout.preferredWidth: 56
                            onClicked: { setup.set("iambic", true); setup.set("iambicMode", 0) }
                        }
                        AppButton {
                            text: "B"
                            checked: !!setup.d.iambic && setup.d.iambicMode === 1
                            focusPolicy: Qt.NoFocus
                            Layout.preferredWidth: 56
                            onClicked: { setup.set("iambic", true); setup.set("iambicMode", 1) }
                        }
                    }
                    OnOff { label: qsTr("Swap paddles"); on: !!setup.d.swapPaddles; onToggled: (v) => setup.set("swapPaddles", v) }
                    // CW on the upper or the lower sideband (the radio's CW
                    // mode; CWL as the desktop's Sideband).
                    Choice {
                        objectName: "setupCwSideband"
                        label: qsTr("CW sideband")
                        options: [{ label: qsTr("CWU (upper)"), value: false }, { label: qsTr("CWL (lower)"), value: true }]
                        columns: 2
                        current: !!setup.d.cwlEnabled
                        onChosen: (v) => setup.set("cwlEnabled", v)
                    }
                    OnOff {
                        objectName: "setupSyncCwx"
                        label: qsTr("CWX text at the keyer's speed")
                        on: !!setup.d.syncCwx
                        onToggled: (v) => setup.set("syncCwx", v)
                    }
                    Heading { text: qsTr("RTTY") }
                    Level {
                        objectName: "setupRttyMark"
                        label: qsTr("The radio's default RTTY mark")
                        unit: " Hz"
                        from: 300
                        to: 3000
                        stepSize: 5
                        current: setup.d.rttyMarkDefault || 2125
                        onCommitted: (v) => setup.set("rttyMarkDefault", v)
                    }
                }

                // ---- Audio ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Heading { text: qsTr("The radio's own audio outputs") }
                    Level { label: qsTr("Line out"); current: setup.d.lineoutGain || 0; onCommitted: (v) => setup.set("lineoutGain", v) }
                    OnOff { label: qsTr("Line out muted"); on: !!setup.d.lineoutMute; onToggled: (v) => setup.set("lineoutMute", v) }
                    Level { label: qsTr("Headphones"); current: setup.d.headphoneGain || 0; onCommitted: (v) => setup.set("headphoneGain", v) }
                    OnOff { label: qsTr("Headphones muted"); on: !!setup.d.headphoneMute; onToggled: (v) => setup.set("headphoneMute", v) }
                    OnOff {
                        objectName: "setupFrontSpeaker"
                        visible: !!setup.d.frontSpeakerKnown
                        label: qsTr("Front speaker muted")
                        on: !!setup.d.frontSpeakerMute
                        onToggled: (v) => setup.set("frontSpeakerMute", v)
                    }
                }

                // ---- RX: calibration, the 10 MHz reference, binaural
                // receive and the radio's filters, as the desktop's Receive
                // and Filters pages ----
                ColumnLayout {
                    id: rx
                    spacing: Theme.spacing / 2
                    readonly property var calWords: ({ "starting": qsTr("Starting…"), "running": qsTr("Calibrating…"),
                                                       "error": qsTr("The radio refused (%1); the offset is back at %2 ppb.").arg(setup.d.calResult || "").arg(setup.d.calPrevPpb || 0),
                                                       "timeout": qsTr("No end reported in 20 s. The offset was %1 ppb before: enter it below to go back.").arg(setup.d.calPrevPpb || 0),
                                                       "done": qsTr("Done: %1 ppb").arg(setup.d.calResult || "–") })
                    function oscLabel(v) {
                        return v === "auto" ? qsTr("Auto") : v === "tcxo" ? qsTr("TCXO") : v === "gpsdo" ? qsTr("GPSDO")
                             : v === "external" ? qsTr("External 10 MHz") : (v || "").toUpperCase()
                    }
                    Heading { text: qsTr("Frequency calibration") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("The radio measures a steady carrier of known frequency (a GPS-locked source, or a time station such as WWV on 10 or 15 MHz, strong and in the clear) and sets its own frequency offset from it. Or enter the offset yourself.")
                        color: Theme.textDim
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    TextSetting {
                        objectName: "setupCalFreq"
                        label: qsTr("Calibration frequency  (%1 MHz)").arg(Number(setup.d.calFreq || 0).toFixed(6))
                        current: Number(setup.d.calFreq || 0).toFixed(6)
                        hint: qsTr("MHz, e.g. 15.000000")
                        upper: false
                        onSaved: (t) => { const v = parseFloat(t.replace(",", ".")); if (v > 0) setup.set("calFreq", v) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            objectName: "setupCalStart"
                            text: qsTr("Calibrate")
                            enabled: setup.d.calState !== "starting" && setup.d.calState !== "running"
                            leftPadding: 16
                            rightPadding: 16
                            focusPolicy: Qt.NoFocus
                            onClicked: setup.setRisky(qsTr("Calibrate on %1 MHz?").arg(Number(setup.d.calFreq || 0).toFixed(6)),
                                                      qsTr("The radio clears its frequency offset (now %1 ppb) and measures it again on that carrier. Without a strong, steady carrier there it measures noise: check the frequency first.").arg(setup.d.freqErrorPpb || 0),
                                                      () => Engine.startCalibration(), qsTr("Calibrate"))
                        }
                        Label {
                            Layout.fillWidth: true
                            text: rx.calWords[setup.d.calState || ""] || ""
                            color: setup.d.calState === "done" ? "#00cc66"
                                 : setup.d.calState === "error" || setup.d.calState === "timeout" ? Theme.transmit : Theme.textDim
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }
                    }
                    TextSetting {
                        objectName: "setupFreqError"
                        label: qsTr("Frequency offset  (%1 ppb)").arg(setup.d.freqErrorPpb || 0)
                        current: String(setup.d.freqErrorPpb || 0)
                        hint: qsTr("parts per billion")
                        upper: false
                        onSaved: (t) => { const v = parseInt(t); if (!isNaN(v)) setup.set("freqErrorPpb", v) }
                    }
                    Heading { text: qsTr("10 MHz reference") }
                    // The sources the radio has (as the desktop offers them);
                    // a change asks first: on an input with no signal the
                    // radio unlocks and drifts.
                    Choice {
                        objectName: "setupOsc"
                        options: {
                            const o = [{ label: qsTr("Auto"), value: "auto" }]
                            const st = setup.d.oscState || ""
                            const cur = setup.d.oscSetting || ""
                            if (st.length > 0 || setup.d.tcxoPresent || cur === "tcxo") o.push({ label: qsTr("TCXO"), value: "tcxo" })
                            if (setup.d.gpsdoPresent || cur === "gpsdo" || st === "gpsdo") o.push({ label: qsTr("GPSDO"), value: "gpsdo" })
                            if (st.length > 0 || setup.d.extPresent || cur === "external") o.push({ label: qsTr("External"), value: "external" })
                            return o
                        }
                        columns: 4
                        current: setup.d.oscSetting || ""
                        onChosen: (v) => setup.setRisky(qsTr("Use %1 as the reference?").arg(rx.oscLabel(v)),
                                                        qsTr("The radio's frequency reference changes. With no signal on the chosen input the radio unlocks and its frequency drifts."),
                                                        () => setup.set("oscillator", v), qsTr("Switch"))
                    }
                    Label {
                        Layout.fillWidth: true
                        readonly property string st: setup.d.oscState || ""
                        text: st.length === 0 ? qsTr("Waiting for the radio's oscillator status")
                            : (setup.d.oscSetting === "auto" && st !== "auto" ? qsTr("Auto → %1").arg(rx.oscLabel(st)) : rx.oscLabel(st))
                              + (setup.d.oscLocked ? qsTr(", locked") : qsTr(", not locked"))
                              + (st === "external" && !setup.d.extPresent ? qsTr(" (no signal found)") : "")
                        color: st.length === 0 ? Theme.textDim : setup.d.oscLocked ? "#00cc66" : Theme.transmit
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    // Binaural receive and the digital modes' low-latency
                    // filters as the radio reports them: firmware 4.3 refuses
                    // "radio set binaural_rx" and "low_latency_digital_modes"
                    // (could not parse), so they are shown, not set.
                    Heading { text: qsTr("Receive audio") }
                    Info {
                        objectName: "setupBinaural"
                        label: qsTr("Binaural receive")
                        value: setup.d.binauralRx ? qsTr("On") : qsTr("Off")
                    }
                    // The radio's filter sharpness per mode: 0 the least delay
                    // (low latency), 3 the sharpest skirts; Auto lets the
                    // radio choose. As the desktop's Filters page.
                    Heading { visible: !!setup.d.filtersKnown; text: qsTr("Filters: low latency 0 … 3 sharp") }
                    Repeater {
                        model: setup.d.filtersKnown
                               ? [{ mode: "voice", label: qsTr("Voice"), level: setup.d.filterVoice, auto: setup.d.filterVoiceAuto },
                                  { mode: "cw", label: qsTr("CW"), level: setup.d.filterCw, auto: setup.d.filterCwAuto },
                                  { mode: "digital", label: qsTr("Digital"), level: setup.d.filterDigital, auto: setup.d.filterDigitalAuto }]
                               : []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: modelData.label; color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true }
                            Repeater {
                                model: 4
                                delegate: AppButton {
                                    required property int index
                                    objectName: "setupFilter" + parent.modelData.mode + index
                                    text: index
                                    checked: !parent.modelData.auto && parent.modelData.level === index
                                    enabled: !parent.modelData.auto
                                    focusPolicy: Qt.NoFocus
                                    Layout.preferredWidth: 40
                                    onClicked: setup.set("filterLevel", { mode: parent.modelData.mode, value: index })
                                }
                            }
                            AppButton {
                                objectName: "setupFilterAuto" + modelData.mode
                                text: qsTr("Auto")
                                checked: !!modelData.auto
                                focusPolicy: Qt.NoFocus
                                onClicked: setup.set("filterAuto", { mode: modelData.mode, value: !checked })
                            }
                        }
                    }
                    Info {
                        objectName: "setupLowLatency"
                        visible: !!setup.d.filtersKnown
                        label: qsTr("Low-latency digital filters")
                        value: setup.d.lowLatencyDigital ? qsTr("On") : qsTr("Off")
                    }
                }

                // ---- Network ----
                ColumnLayout {
                    id: network
                    spacing: Theme.spacing / 2
                    // Held −/+ count from the value at the press (the radio
                    // read-back lags behind the repeats).
                    property int acc: 1450
                    function step(n, dir) {
                        if (n === 0) acc = setup.d.networkMtu || 1450
                        acc = Math.max(576, Math.min(9000, acc + dir * (n < 10 ? 10 : 50)))
                        setup.set("networkMtu", acc)
                    }
                    // The radio's address as it reports it, and how it gets
                    // one: DHCP, or a static address written to it (asked
                    // first: the radio may move to another address).
                    Heading { text: qsTr("The radio's address") }
                    Info { label: qsTr("IP address"); value: setup.d.radioIp || setup.d.ip || "" }
                    Info { label: qsTr("Netmask"); value: setup.d.netmask || "" }
                    Info { label: qsTr("Gateway"); value: setup.d.gateway || "" }
                    Info { label: qsTr("MAC"); value: setup.d.mac || "" }
                    Info { visible: (setup.d.networkName || "").length > 0; label: qsTr("Network"); value: setup.d.networkName || "" }
                    ColumnLayout {
                        id: ipConfig
                        visible: !!setup.d.netConfigKnown
                        Layout.fillWidth: true
                        spacing: Theme.spacing / 2
                        property bool editStatic: false
                        readonly property bool showStatic: editStatic || !!setup.d.hasStaticIp
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Address by"); color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true }
                            AppButton {
                                objectName: "setupDhcp"
                                text: qsTr("DHCP")
                                checked: !setup.d.hasStaticIp && !ipConfig.editStatic
                                focusPolicy: Qt.NoFocus
                                Layout.preferredWidth: Theme.buttonWidth * 1.4
                                onClicked: {
                                    ipConfig.editStatic = false
                                    if (setup.d.hasStaticIp)
                                        setup.setRisky(qsTr("Let the radio take its address by DHCP?"),
                                                       qsTr("The radio restarts with the new setting (every station loses it for about a minute) and may get a new address: connect again there."),
                                                       () => setup.set("staticIp", {}), qsTr("Use DHCP"))
                                }
                            }
                            AppButton {
                                objectName: "setupStatic"
                                text: qsTr("Static")
                                checked: ipConfig.showStatic
                                focusPolicy: Qt.NoFocus
                                Layout.preferredWidth: Theme.buttonWidth * 1.4
                                onClicked: ipConfig.editStatic = true
                            }
                        }
                        GridLayout {
                            visible: ipConfig.showStatic
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacing
                            Label { text: qsTr("IP address"); color: Theme.textDim; font.pixelSize: 15 }
                            TextField {
                                id: staticIpField
                                objectName: "setupStaticIp"
                                Accessible.name: qsTr("Static IP address")
                                Layout.fillWidth: true
                                font.pixelSize: 17
                                placeholderText: setup.d.staticIp || "192.168.1.60"
                                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                            }
                            Label { text: qsTr("Netmask"); color: Theme.textDim; font.pixelSize: 15 }
                            TextField {
                                id: staticMaskField
                                Accessible.name: qsTr("Static netmask")
                                Layout.fillWidth: true
                                font.pixelSize: 17
                                placeholderText: setup.d.staticNetmask || "255.255.255.0"
                                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                            }
                            Label { text: qsTr("Gateway"); color: Theme.textDim; font.pixelSize: 15 }
                            TextField {
                                id: staticGwField
                                Accessible.name: qsTr("Static gateway")
                                Layout.fillWidth: true
                                font.pixelSize: 17
                                placeholderText: setup.d.staticGateway || "192.168.1.1"
                                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                            }
                        }
                        AppButton {
                            objectName: "setupStaticApply"
                            visible: ipConfig.showStatic
                            readonly property string ip: staticIpField.text.trim() || setup.d.staticIp || ""
                            readonly property string mask: staticMaskField.text.trim() || setup.d.staticNetmask || ""
                            readonly property string gw: staticGwField.text.trim() || setup.d.staticGateway || ""
                            readonly property bool valid: /^\d{1,3}(\.\d{1,3}){3}$/.test(ip) && /^\d{1,3}(\.\d{1,3}){3}$/.test(mask)
                                                         && /^\d{1,3}(\.\d{1,3}){3}$/.test(gw)
                            // Something to write: DHCP now, or a field changed.
                            readonly property bool changed: !setup.d.hasStaticIp || ip !== (setup.d.staticIp || "")
                                                           || mask !== (setup.d.staticNetmask || "") || gw !== (setup.d.staticGateway || "")
                            text: qsTr("Apply the static address…")
                            enabled: valid && changed
                            leftPadding: 16
                            rightPadding: 16
                            focusPolicy: Qt.NoFocus
                            onClicked: setup.setRisky(qsTr("Give the radio %1?").arg(ip),
                                                      qsTr("Netmask %1, gateway %2. The radio restarts at that address (every station loses it for about a minute): connect again there. A wrong address can leave the radio out of reach until it is set back at the radio.").arg(mask).arg(gw),
                                                      () => setup.set("staticIp", { ip: ip, netmask: mask, gateway: gw }), qsTr("Apply"))
                        }
                    }
                    // Whether only stations on private addresses may connect
                    // (the radio's own guard), as the radio reports it.
                    Info {
                        objectName: "setupPrivateIp"
                        visible: !!setup.d.privateIpKnown
                        label: qsTr("Private addresses only")
                        value: setup.d.enforcePrivateIp ? qsTr("On") : qsTr("Off")
                    }
                    Heading { text: qsTr("Network MTU") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Largest packet the radio sends this app (VITA-49 over UDP). 1450 suits most networks and VPNs; lower it if the waterfall or audio break up over a tunnel. Over Tailscale at most 1200 is used.")
                        color: Theme.textDim
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        RepeatButton {
                            text: "−"
                            Layout.preferredWidth: 56
                            onStepped: (n) => network.step(n, -1)
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: (setup.d.networkMtu || 1450) + " " + qsTr("bytes")
                            color: Theme.text
                            font.pixelSize: 20
                            font.bold: true
                        }
                        RepeatButton {
                            text: "+"
                            Layout.preferredWidth: 56
                            onStepped: (n) => network.step(n, 1)
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: [1500, 1450, 1400, 1300, 1200, 1000]
                            delegate: AppButton {
                                required property int modelData
                                text: modelData + (modelData === 1450 ? " (" + qsTr("default") + ")" : "")
                                checked: setup.d.networkMtu === modelData
                                leftPadding: 12
                                rightPadding: 12
                                focusPolicy: Qt.NoFocus
                                onClicked: setup.set("networkMtu", modelData)
                            }
                        }
                    }
                }

                // ---- XVTR: the radio's transverters, as the desktop's tab ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Transverters: each adds a band with its name to the band buttons. RF is the band's frequency, IF the radio's (LO = RF − IF); the LO error corrects the converter's offset; max power is the drive to the transverter at its IF.")
                        color: Theme.textDim
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: setup.xvtrs
                        delegate: ColumnLayout {
                            id: xv
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            function put(key, value) { Engine.setTransverter(xv.modelData.index, key, value); refreshSoon.restart() }
                            RowLayout {
                                Layout.fillWidth: true
                                Heading {
                                    text: (xv.modelData.name || qsTr("XVTR %1").arg(xv.modelData.index))
                                          + "  ·  " + (xv.modelData.valid ? qsTr("valid") : qsTr("not valid yet"))
                                    Layout.fillWidth: true
                                }
                                AppButton {
                                    text: qsTr("Remove")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: { Engine.removeTransverter(xv.modelData.index); refreshSoon.restart() }
                                }
                            }
                            TextSetting {
                                label: qsTr("Name  (%1)").arg(xv.modelData.name || "–")
                                current: xv.modelData.name || ""
                                hint: qsTr("e.g. 2m")
                                upper: false
                                onSaved: (t) => xv.put("name", t)
                            }
                            TextSetting {
                                label: qsTr("RF frequency, MHz  (%1)").arg(Number(xv.modelData.rfMhz).toFixed(3))
                                current: Number(xv.modelData.rfMhz).toFixed(3)
                                hint: "144.000"
                                onSaved: (t) => xv.put("rf_freq", t)
                            }
                            TextSetting {
                                label: qsTr("IF frequency, MHz  (%1; LO %2)").arg(Number(xv.modelData.ifMhz).toFixed(3)).arg(Number(xv.modelData.loMhz).toFixed(3))
                                current: Number(xv.modelData.ifMhz).toFixed(3)
                                hint: "28.000"
                                onSaved: (t) => xv.put("if_freq", t)
                            }
                            TextSetting {
                                label: qsTr("LO error, MHz  (%1)").arg(Number(xv.modelData.loErrorMhz).toFixed(6))
                                current: Number(xv.modelData.loErrorMhz).toFixed(6)
                                hint: "0.000000"
                                onSaved: (t) => xv.put("lo_error", t)
                            }
                            TextSetting {
                                label: qsTr("RX gain, dB  (%1)").arg(Number(xv.modelData.rxGain).toFixed(1))
                                current: Number(xv.modelData.rxGain).toFixed(1)
                                hint: "0.0"
                                onSaved: (t) => xv.put("rx_gain", t)
                            }
                            TextSetting {
                                label: qsTr("Max power, dBm  (%1; %2 to %3)").arg(Number(xv.modelData.maxPowerDbm).toFixed(1))
                                       .arg(Number(xv.modelData.maxPowerMin).toFixed(0)).arg(Number(xv.modelData.maxPowerMax).toFixed(0))
                                current: Number(xv.modelData.maxPowerDbm).toFixed(1)
                                hint: "0.0"
                                onSaved: (t) => xv.put("max_power", t)
                            }
                            OnOff {
                                label: qsTr("Receive only")
                                on: xv.modelData.rxOnly === true
                                onToggled: (v) => xv.put("rx_only", v)
                            }
                        }
                    }
                    Label {
                        visible: setup.xvtrs.length === 0
                        Layout.fillWidth: true
                        text: qsTr("No transverters on this radio.")
                        color: Theme.textDim
                        font.pixelSize: 15
                    }
                    AppButton {
                        objectName: "addTransverter"
                        text: qsTr("Add transverter")
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        onClicked: { Engine.addTransverter(); refreshSoon.restart() }
                    }
                }

                // ---- Amp: an RF-KIT RF2K-S over its REST API, read only
                // unless Control is on (MobileEngineAmp.cpp); kept on this
                // device ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Heading { text: qsTr("RF-KIT RF2K-S") }
                    TextSetting {
                        objectName: "ampAddress"
                        label: qsTr("Address  (%1)").arg(Engine.amp.address || qsTr("not set"))
                        current: Engine.amp.address || ""
                        hint: qsTr("e.g. 192.168.1.60:8080")
                        upper: false
                        onSaved: (t) => Engine.setAmpAddress(t)
                    }
                    OnOff {
                        visible: !!Engine.amp.set
                        label: qsTr("Read the amplifier")
                        on: !!Engine.amp.on
                        onToggled: (v) => Engine.setAmpOn(v)
                    }
                    OnOff {
                        objectName: "ampControl"
                        visible: !!Engine.amp.set
                        label: qsTr("Control the amplifier")
                        on: !!Engine.amp.control
                        onToggled: (v) => Engine.setAmpControl(v)
                    }
                    Label {
                        objectName: "ampSetupState"
                        visible: !!Engine.amp.set
                        Layout.fillWidth: true
                        text: {
                            const a = Engine.amp
                            if (!a.on) return qsTr("Off.")
                            if (a.state === "connected")
                                return qsTr("Connected: %1%2.").arg(a.device || "RF-KIT").arg(a.name ? " " + a.name : "")
                            if (a.state === "failed") return qsTr("Not reached: %1.").arg(a.error)
                            if (a.state === "connecting") return qsTr("Connecting…")
                            return qsTr("Read while connected to a radio.")
                        }
                        color: Engine.amp.state === "connected" ? "#00cc66"
                             : Engine.amp.state === "failed" ? Theme.transmit : Theme.textDim
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    AppButton {
                        visible: !!Engine.amp.set
                        text: qsTr("Forget the amplifier")
                        focusPolicy: Qt.NoFocus
                        onClicked: Engine.setAmpAddress("")
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("The address of the amplifier's network port (8080 unless you set another after a colon). This device reaches it directly, on the same network or over your VPN when away: not through the radio, and not over SmartLink. Once set, Controls gets an Amp tab with what the amplifier reports: forward and reflected power, SWR, temperature, voltage, current, band, tuner and antenna. Every meter can show the amplifier's readings too.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Control off: read only, nothing is sent that changes the amplifier. Control on: the Amp tab also gets Operate / Standby, the internal antennas and the error reset. Each asks you first, none works while the radio transmits, and the tab shows only what the amplifier reads back.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    // 4O3A's amplifier and tuner: found by the radio, read
                    // through it and directly (MobileEngine4o3a.cpp).
                    Repeater {
                        model: [{ id: "pgxl", name: qsTr("4O3A Power Genius XL"), d: Engine.pgxl, port: 9008 },
                                { id: "tgxl", name: qsTr("4O3A Tuner Genius XL"), d: Engine.tgxl, port: 9010 }]
                        delegate: ColumnLayout {
                            required property var modelData
                            readonly property var d: modelData.d
                            Layout.fillWidth: true
                            spacing: Theme.spacing / 2
                            Heading { text: modelData.name }
                            Label {
                                objectName: modelData.id + "SetupState"
                                Layout.fillWidth: true
                                text: !d.present ? qsTr("Not found: the radio reports none, and no address is set.")
                                    : d.direct ? qsTr("Found by the radio, and read directly at %1.").arg(d.ip)
                                    : d.authBlocked ? qsTr("At %1 it asks for its remote code: enter it below.").arg(d.ip)
                                    : d.connecting ? qsTr("Connecting to %1…").arg(d.ip)
                                    : d.linkError ? qsTr("Through the radio only; %1 not reached directly (%2).").arg(d.ip || "–").arg(d.linkError)
                                    : qsTr("Through the radio.")
                                color: d.direct ? "#00cc66" : d.authBlocked ? Theme.transmit : Theme.textDim
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                            OnOff {
                                objectName: modelData.id + "Control"
                                label: modelData.id === "pgxl" ? qsTr("Control the amplifier") : qsTr("Control the tuner")
                                on: !!d.control
                                onToggled: (v) => Engine.setGeniusSetting(modelData.id, "control", v)
                            }
                            TextSetting {
                                objectName: modelData.id + "Address"
                                label: qsTr("Address  (%1)").arg(d.manualIp ? d.manualIp : qsTr("the radio's: %1").arg(d.ip || "–"))
                                current: d.manualIp || ""
                                hint: qsTr("Empty: the address the radio reports")
                                upper: false
                                onSaved: (t) => Engine.setGeniusSetting(modelData.id, "address", t)
                            }
                            AppButton {
                                visible: (d.manualIp || "").length > 0
                                text: qsTr("Use the radio's address")
                                focusPolicy: Qt.NoFocus
                                onClicked: Engine.setGeniusSetting(modelData.id, "address", "")
                            }
                            TextSetting {
                                objectName: modelData.id + "AuthCode"
                                visible: !!d.authBlocked
                                label: qsTr("Remote code")
                                current: ""
                                hint: qsTr("As set in the device's utility")
                                upper: false
                                onSaved: (t) => Engine.setGeniusSetting(modelData.id, "authCode", t)
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("A Power Genius XL or Tuner Genius XL connected to the radio shows on the Amp tab by itself: the radio relays its state, power and SWR and its Operate / Standby (over SmartLink too); this device also reads it directly at its address (ports 9008 and 9010; on the same network or your VPN) for its supply, temperatures, the tuner's relays and antenna switch. A remote code, when the device asks for one, is kept in this device's secure store once the device has accepted it. Control on: the Amp tab's buttons change the device, each asked first; the devices guard their own SWR, power and temperature, and the tuner's relays and antenna wait while the radio transmits.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }

                // ---- USB: serial cables in the radio's USB ports, as
                // SmartSDR's and the desktop's USB Cables (frequency or
                // band data for an amplifier, a tuner, an antenna switch) ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("USB-serial (FTDI) cables in the radio's USB ports, as SmartSDR's USB Cables: they give an amplifier, a tuner or an antenna switch the frequency (CAT), the band (BCD, Bit) or a serial port (Passthrough). A cable shows here once plugged into the radio; its settings stay in the radio.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "usbNone"
                        visible: setup.usbList.length === 0
                        Layout.fillWidth: true
                        text: qsTr("No USB cables: plug a USB-serial cable into one of the radio's USB ports.")
                        color: Theme.text
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        visible: setup.usbList.length > 1
                        columns: 2
                        columnSpacing: 4
                        rowSpacing: 4
                        Repeater {
                            model: setup.usbList
                            delegate: AppButton {
                                required property var modelData
                                required property int index
                                objectName: "usbCable" + index
                                text: modelData.label
                                checked: setup.usbSn === modelData.sn
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                onClicked: setup.usbSn = modelData.sn
                            }
                        }
                    }
                    ColumnLayout {
                        id: cableEditor
                        Layout.fillWidth: true
                        visible: setup.cable !== null
                        spacing: Theme.spacing / 2
                        readonly property var c: setup.cable || ({})
                        readonly property string family: c.type === "vbcd" || c.type === "bcd_vbcd" ? "bcd" : (c.type || "")
                        Info { label: qsTr("Serial number"); value: cableEditor.c.sn || "" }
                        Info { label: qsTr("State"); value: cableEditor.c.present ? qsTr("Plugged in") : qsTr("Not plugged in") }
                        TextSetting {
                            label: qsTr("Name  (%1)").arg(cableEditor.c.name || qsTr("none"))
                            current: cableEditor.c.name || ""
                            hint: qsTr("e.g. ACOM 2000A")
                            upper: false
                            onSaved: (t) => setup.setUsb("name", t)
                        }
                        OnOff { objectName: "usbEnable"; label: qsTr("Enabled"); on: !!cableEditor.c.enabled; onToggled: (v) => setup.setUsb("enable", v ? "1" : "0") }
                        Choice {
                            objectName: "usbType"
                            label: qsTr("Type")
                            columns: 5
                            options: [{ label: "CAT", value: "cat" }, { label: "BCD", value: "bcd" }, { label: "Bit", value: "bit" },
                                      { label: "LDPA", value: "ldpa" }, { label: qsTr("Pass"), value: "passthrough" }]
                            current: cableEditor.family
                            onChosen: (v) => setup.setUsb("type", v)
                        }

                        // CAT: the frequency as CAT text (Kenwood style), e.g. to an amplifier
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: cableEditor.family === "cat"
                            spacing: Theme.spacing / 2
                            SourceChoice { objectName: "usbCatSource"; item: cableEditor.c; onSetKey: (k, v) => setup.setUsb(k, v) }
                            OnOff { objectName: "usbAutoReport"; label: qsTr("Report the frequency by itself (auto report)"); on: !!cableEditor.c.autoReport; onToggled: (v) => setup.setUsb("auto_report", v ? "1" : "0") }
                            SerialSettings { item: cableEditor.c; onSetKey: (k, v) => setup.setUsb(k, v) }
                        }
                        // BCD: the band as BCD lines, for an amplifier's or antenna switch's band input
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: cableEditor.family === "bcd"
                            spacing: Theme.spacing / 2
                            Choice {
                                objectName: "usbBcdType"
                                label: qsTr("BCD bands")
                                columns: 3
                                options: [{ label: "HF", value: "bcd" }, { label: "VHF", value: "vbcd" }, { label: "HF + VHF", value: "bcd_vbcd" }]
                                current: cableEditor.c.type
                                onChosen: (v) => setup.setUsb("type", v)
                            }
                            Choice {
                                objectName: "usbBcdPolarity"
                                label: qsTr("Polarity")
                                columns: 2
                                options: [{ label: qsTr("Active high"), value: "active_high" }, { label: qsTr("Active low"), value: "active_low" }]
                                current: cableEditor.c.activeHigh === false ? "active_low" : "active_high"
                                onChosen: (v) => setup.setUsb("polarity", v)
                            }
                            SourceChoice { objectName: "usbBcdSource"; item: cableEditor.c; onSetKey: (k, v) => setup.setUsb(k, v) }
                        }
                        // Bit: eight lines, each on for a band or a frequency range
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: cableEditor.family === "bit"
                            spacing: Theme.spacing / 2
                            readonly property var b: (cableEditor.c.bits || [])[setup.usbBit] || ({})
                            Choice {
                                objectName: "usbBit"
                                label: qsTr("Bit")
                                columns: 8
                                options: [0, 1, 2, 3, 4, 5, 6, 7].map(i => ({ label: String(i), value: i }))
                                current: setup.usbBit
                                onChosen: (v) => setup.usbBit = v
                            }
                            OnOff { objectName: "usbBitEnable"; label: qsTr("Bit %1 enabled").arg(setup.usbBit); on: !!parent.b.enabled; onToggled: (v) => setup.setUsbBit("enable", v ? "1" : "0") }
                            SourceChoice { objectName: "usbBitSource"; item: parent.b; onSetKey: (k, v) => setup.setUsbBit(k, v) }
                            Choice {
                                objectName: "usbBitOutput"
                                label: qsTr("On for")
                                columns: 2
                                options: [{ label: qsTr("A band"), value: "band" }, { label: qsTr("A frequency range"), value: "freq_range" }]
                                current: parent.b.output || ""
                                onChosen: (v) => setup.setUsbBit("output", v)
                            }
                            Choice {
                                objectName: "usbBitBand"
                                visible: parent.b.output === "band"
                                label: qsTr("Band")
                                columns: 5
                                options: setup.txBandList.map(x => ({ label: setup.bandLabel(x.name), value: x.name }))
                                current: parent.b.band || ""
                                onChosen: (v) => setup.setUsbBit("band", v)
                            }
                            TextSetting {
                                visible: parent.b.output === "freq_range"
                                label: qsTr("From, MHz  (%1)").arg(Number(parent.b.lowMhz || 0).toFixed(3))
                                current: Number(parent.b.lowMhz || 0).toFixed(3)
                                hint: "14.000"
                                upper: false
                                onSaved: (t) => setup.setUsbBit("low_freq", Number(t.replace(",", ".")).toFixed(3))
                            }
                            TextSetting {
                                visible: parent.b.output === "freq_range"
                                label: qsTr("To, MHz  (%1)").arg(Number(parent.b.highMhz || 0).toFixed(3))
                                current: Number(parent.b.highMhz || 0).toFixed(3)
                                hint: "14.350"
                                upper: false
                                onSaved: (t) => setup.setUsbBit("high_freq", Number(t.replace(",", ".")).toFixed(3))
                            }
                            Choice {
                                objectName: "usbBitPolarity"
                                label: qsTr("Polarity")
                                columns: 2
                                options: [{ label: qsTr("Active high"), value: "active_high" }, { label: qsTr("Active low"), value: "active_low" }]
                                current: parent.b.activeHigh === false ? "active_low" : "active_high"
                                onChosen: (v) => setup.setUsbBit("polarity", v)
                            }
                            OnOff { objectName: "usbBitPtt"; label: qsTr("Only while transmitting (PTT dependent)"); on: !!parent.b.pttDependent; onToggled: (v) => setup.setUsbBit("ptt_dependent", v ? "1" : "0") }
                            Level {
                                visible: !!parent.b.pttDependent
                                label: qsTr("PTT delay")
                                unit: " ms"
                                to: 500
                                stepSize: 5
                                current: parent.b.pttDelay || 0
                                onCommitted: (v) => setup.setUsbBit("ptt_delay", v)
                            }
                            Level {
                                visible: !!parent.b.pttDependent
                                label: qsTr("TX delay")
                                unit: " ms"
                                to: 500
                                stepSize: 5
                                current: parent.b.txDelay || 0
                                onCommitted: (v) => setup.setUsbBit("tx_delay", v)
                            }
                        }
                        // LDPA: a FlexRadio LDPA (2 m / 4 m) amplifier's band and preamp
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: cableEditor.family === "ldpa"
                            spacing: Theme.spacing / 2
                            Choice {
                                objectName: "usbLdpaBand"
                                label: qsTr("Band")
                                columns: 2
                                options: [{ label: "2 m", value: "2" }, { label: "4 m", value: "4" }]
                                current: cableEditor.c.ldpaBand || ""
                                onChosen: (v) => setup.setUsb("band", v)
                            }
                            OnOff { objectName: "usbLdpaPreamp"; label: qsTr("Preamp"); on: !!cableEditor.c.preamp; onToggled: (v) => setup.setUsb("preamp", v ? "1" : "0") }
                            SourceChoice { objectName: "usbLdpaSource"; item: cableEditor.c; onSetKey: (k, v) => setup.setUsb(k, v) }
                        }
                        // Passthrough: a serial port through the radio
                        SerialSettings {
                            visible: cableEditor.family === "passthrough"
                            item: cableEditor.c
                            onSetKey: (k, v) => setup.setUsb(k, v)
                        }
                        AppButton {
                            objectName: "usbForget"
                            visible: !cableEditor.c.present
                            text: qsTr("Forget this cable")
                            onClicked: setup.setRisky(qsTr("Forget %1?").arg(cableEditor.c.name || cableEditor.c.sn),
                                                      qsTr("The radio drops this cable's settings; plugged in again, it starts unconfigured."),
                                                      () => Engine.removeUsbCable(setup.usbSn), qsTr("Forget"))
                        }
                    }
                }
            }
        }
    }

    // A setting that could key the radio by itself asks first (the app's
    // offer pattern, as the amplifier's confirmations).
    Dialog {
        id: risky
        objectName: "setupRisky"
        property string heading
        property string text
        property string yes
        property var apply: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.transmit; border.width: 2 }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: risky.heading
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: risky.text
                color: Theme.textDim
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    objectName: "setupRiskyNo"
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: risky.close()
                }
                AppButton {
                    objectName: "setupRiskyYes"
                    text: risky.yes
                    checked: true
                    Layout.fillWidth: true
                    onClicked: {
                        const f = risky.apply
                        risky.close()
                        if (f) f()
                    }
                }
            }
        }
    }
}
