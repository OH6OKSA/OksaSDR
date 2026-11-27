import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Transmit settings shared by the tablet side panel and the controls drawer:
// tune carrier, ATU, speech processor, VOX, DEXP, AM carrier, monitor,
// power and microphone.
ColumnLayout {
    id: tx
    spacing: Theme.spacing / 2
    // Never ask for more width than the column gives; rows share what is there.
    implicitWidth: 200

    // ---- FM repeater (FM modes): offset and CTCSS on transmit ----
    // As the desktop's RX applet: the radio transmits on the frequency moved
    // by the offset (− below, + above) and sends the tone; receive stays on
    // the frequency shown. Presets: 0.6 MHz (2 m), 1.6 (70 cm, some
    // countries), 5 (US 70 cm), 7.6 (70 cm in Europe, e.g. Germany).
    ColumnLayout {
        id: fm
        visible: Engine.fmMode
        Layout.fillWidth: true
        spacing: Theme.spacing / 2
        Label { text: qsTr("FM repeater"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16) }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Offset"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
            Repeater {
                model: [{ key: "simplex", label: qsTr("Simplex") }, { key: "down", label: "−" }, { key: "up", label: "+" }]
                delegate: AppButton {
                    required property var modelData
                    text: modelData.label
                    // Said as words: a screen reader reads "−" as "minus" alone.
                    Accessible.name: modelData.key === "down" ? qsTr("Shift minus") : modelData.key === "up" ? qsTr("Shift plus") : modelData.label
                    checked: Engine.repeaterDir === modelData.key
                    Layout.fillWidth: true
                    Layout.preferredWidth: modelData.key === "simplex" ? 2 : 1
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.repeaterDir = modelData.key
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: Engine.repeaterDir !== "simplex"
            Label { text: qsTr("Shift"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
            Repeater {
                model: [0.6, 1.6, 5.0, 7.6]
                delegate: AppButton {
                    required property real modelData
                    Accessible.name: qsTr("%1: %2").arg(qsTr("Shift")).arg(qsTr("%1 MHz").arg(text))
                    text: modelData.toFixed(1)
                    checked: Math.abs(Engine.repeaterOffsetMhz - modelData) < 0.0005
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    leftPadding: 2
                    rightPadding: 2
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.repeaterOffsetMhz = modelData
                }
            }
            // Any other shift, in MHz.
            TextField {
                id: shiftField
                Accessible.name: qsTr("Repeater shift in MHz")
                Layout.preferredWidth: 76
                font.pixelSize: Theme.px(15)
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Binding on text { when: !shiftField.activeFocus; value: Engine.repeaterOffsetMhz.toFixed(3) }
                onEditingFinished: {
                    const v = parseFloat(text.replace(",", "."))
                    if (!isNaN(v) && v >= 0 && v <= 100) Engine.repeaterOffsetMhz = v
                }
            }
            Label { text: "MHz"; color: Theme.textDim; font.pixelSize: Theme.px(13) }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Tone"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
            AppButton {
                Accessible.name: qsTr("%1: %2").arg(qsTr("Tone")).arg(text)
                text: qsTr("Off")
                checked: !Engine.ctcssOn
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.ctcssOn = false
            }
            AppButton {
                Accessible.name: qsTr("%1: %2").arg(qsTr("Tone")).arg(text)
                text: "CTCSS"
                checked: Engine.ctcssOn
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.ctcssOn = true
            }
            AppComboBox {
                Accessible.name: qsTr("CTCSS tone")
                // Its text field would be read again, without a name.
                Component.onCompleted: contentItem.Accessible.ignored = true
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.preferredHeight: Theme.touchTarget
                enabled: Engine.ctcssOn
                model: Engine.ctcssTones
                currentIndex: Engine.ctcssTones.indexOf(Engine.ctcssTone)
                displayText: currentIndex >= 0 ? currentText + " Hz" : qsTr("tone")
                font.pixelSize: Theme.buttonFont
                onActivated: (index) => Engine.ctcssTone = Engine.ctcssTones[index]
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("The radio transmits on the frequency moved by the offset and sends the tone; receive stays on the frequency shown.")
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.WordWrap
        }
    }

    // ---- Tune and ATU (both transmit; routed like PTT) ----
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            id: tuneButton
            objectName: "txTune"
            text: Engine.tuning ? qsTr("TUNE ●") : Engine.txLocked ? qsTr("TUNE · SB") : qsTr("TUNE")
            // The dot and "SB" in words, as PttButton says them.
            Accessible.name: Engine.tuning ? qsTr("Tune, transmitting") : Engine.txLocked ? qsTr("Tune, transmit locked, standby") : qsTr("Tune")
            // Roadmap 229: arming the lock is on a hold only.
            Accessible.description: Engine.txLocked && !Engine.tuning ? qsTr("hold for a second to arm transmit") : ""
            enabled: Engine.tuneAvailable || Engine.tuning
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            // Transmit lock in standby: held for a second, it arms (a held
            // press skips clicked, so unlocked it acts as a tap).
            onPressAndHold: Engine.txLocked ? Engine.armTransmit() : Engine.toggleTune()
            background: Rectangle {
                implicitHeight: Theme.touchTarget
                radius: Theme.radius
                color: Engine.tuning ? Theme.transmit : tuneButton.down ? Theme.buttonFaceDown : Theme.buttonFace
                opacity: tuneButton.enabled ? 1.0 : 0.5
            }
            onClicked: Engine.toggleTune()
        }
        AppButton {
            text: Engine.atuTuned ? qsTr("ATU ✓") : qsTr("ATU")
            Accessible.name: Engine.atuTuned ? qsTr("ATU, tuned") : qsTr("ATU")
            visible: Engine.hasTuner
            enabled: Engine.tuneAvailable
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.atuPressed()
        }
        AppButton {
            text: qsTr("MEM")
            visible: Engine.hasTunerMemories
            checked: Engine.atuMemories
            Layout.preferredWidth: 56
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: Engine.atuMemories = !Engine.atuMemories
        }
    }
    Label {
        visible: Engine.hasTuner && Engine.atuStatusText.length > 0
        text: qsTr("ATU: %1").arg(Engine.atuStatusText)
        color: Theme.textDim
        font.pixelSize: Theme.px(13)
    }
    Label { text: qsTr("Tune power  %1").arg(Engine.tunePower); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    FineSlider {
        accessibleName: qsTr("Tune power")
        valueText: Engine.rfPowerIsPercent ? qsTr("%1 %").arg(Engine.tunePower) : qsTr("%1 W").arg(Engine.tunePower)
        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: Engine.tunePower
        enabled: Engine.tuneAvailable || Engine.tuning
        onMoved: (v) => Engine.tunePower = Math.round(v)
    }

    // ---- Power ----
    // Watts on a 100 W radio, a percentage on an Aurora (500 W PA).
    Label { text: Engine.rfPowerIsPercent ? qsTr("RF power  %1 %").arg(Engine.rfPower) : qsTr("RF power  %1 W").arg(Engine.rfPower); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    FineSlider {
        accessibleName: qsTr("RF power")
        valueText: Engine.rfPowerIsPercent ? qsTr("%1 %").arg(Engine.rfPower) : qsTr("%1 W").arg(Engine.rfPower)
        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: Engine.rfPower
        enabled: Engine.transmitAvailable || Engine.canUseDeviceMicrophone || Engine.tuneAvailable
        onMoved: (v) => Engine.rfPower = Math.round(v)
    }
    // Digital max (roadmap 37): the highest RF power in DIGU, DIGL and RTTY,
    // in 5s; Off at 0.
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Label { text: qsTr("Digital max"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        AppButton {
            objectName: "digitalMaxDown"
            text: "−"
            enabled: Engine.digitalMax > 0
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Lower the digital modes' power limit")
            Layout.preferredWidth: Theme.touchTarget
            onClicked: Engine.digitalMax = Math.max(0, Engine.digitalMax - 5)
        }
        Label {
            objectName: "digitalMaxValue"
            text: Engine.digitalMax > 0 ? (Engine.rfPowerIsPercent ? qsTr("%1 %").arg(Engine.digitalMax) : qsTr("%1 W").arg(Engine.digitalMax))
                                        : qsTr("Off")
            color: Theme.text
            font.pixelSize: Theme.px(15)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: Theme.touchTarget * 1.4
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Digital max %1").arg(text)
        }
        AppButton {
            objectName: "digitalMaxUp"
            text: "+"
            enabled: Engine.digitalMax < 100
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Raise the digital modes' power limit")
            Layout.preferredWidth: Theme.touchTarget
            onClicked: Engine.digitalMax = Math.min(100, Engine.digitalMax + 5)
        }
    }
    Label {
        objectName: "digitalMaxNote"
        Layout.fillWidth: true
        text: Engine.digitalMaxNote.length > 0 ? Engine.digitalMaxNote
              : Engine.digitalMax > 0 ? qsTr("The highest RF power while the TX slice is in DIGU, DIGL or RTTY (FT8 / FT4, PSK, RTTY, RADE); the power you had comes back when it leaves them.")
              : qsTr("Off: the digital modes transmit at the RF power above. Set a limit for them with +.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // ---- Speech processor (radio side) ----
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("PROC")
            checked: Engine.procEnabled
            Layout.preferredWidth: 64
            focusPolicy: Qt.NoFocus
            onClicked: Engine.procEnabled = !Engine.procEnabled
        }
        // Flex: three steps NOR / DX / DX+; other radios get a slider.
        Repeater {
            model: Engine.procLevelMax === 2 ? ["NOR", "DX", "DX+"] : []
            delegate: AppButton {
                required property int index
                required property string modelData
                // The PROC button beside it is the row's label: "Processor: NOR".
                Accessible.name: qsTr("%1: %2").arg(qsTr("Processor")).arg(text)
                text: modelData
                checked: Engine.procLevel === index
                enabled: Engine.procEnabled
                Layout.fillWidth: true
                leftPadding: 2
                rightPadding: 2
                focusPolicy: Qt.NoFocus
                onClicked: Engine.procLevel = index
            }
        }
        AppSlider {
            Accessible.name: qsTr("Speech processor level")
            Accessible.onIncreaseAction: { increase(); moved() }
            Accessible.onDecreaseAction: { decrease(); moved() }
            visible: Engine.procLevelMax !== 2
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, Engine.procLevelMax)
            stepSize: 1
            value: Engine.procLevel
            enabled: Engine.procEnabled
            onMoved: Engine.procLevel = Math.round(value)
        }
    }

    // ---- Phone: VOX, downward expander, AM carrier (the desktop's Phone
    // applet; levels sent when a slider is released) ----
    component PhoneLevel: RowLayout {
        id: pl
        property string key: ""
        property string toggleKey: ""
        property string title: ""
        // What a screen reader calls the level: the title, or more where the
        // title leans on the row above ("Delay" is VOX's).
        property string spokenTitle: title
        Layout.fillWidth: true
        AppButton {
            visible: pl.toggleKey.length > 0
            text: pl.title
            checked: !!Engine.phone[pl.toggleKey]
            Layout.preferredWidth: 64
            onClicked: Engine.setPhone(pl.toggleKey, checked ? 0 : 1)
        }
        Label {
            visible: pl.toggleKey.length === 0
            text: pl.title
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
            Layout.preferredWidth: 64
        }
        FineSlider {
            id: plSlider
            accessibleName: pl.spokenTitle
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 1
            liveDrag: false
            value: Engine.phone[pl.key] || 0
            onMoved: (v) => Engine.setPhone(pl.key, Math.round(v))
        }
        Label {
            text: Math.round(plSlider.shown)
            color: Theme.textDim
            font.pixelSize: Theme.px(13)
            Layout.preferredWidth: 28
            horizontalAlignment: Text.AlignRight
        }
    }
    // VOX is the radio's own: it keys on what its mic input brings. With the
    // radio's microphone (canUseDeviceMicrophone: a radio input chosen, PC
    // available) that is the radio's; with this device's (mic input PC) the
    // app keeps the microphone streaming while VOX is on (roadmap 19,
    // Christoph DM5HF). DEXP stays with the radio's own inputs.
    PhoneLevel { visible: !Engine.cwMode && (Engine.canUseDeviceMicrophone || Engine.usingDeviceMicrophone); key: "voxLevel"; toggleKey: "voxOn"; title: qsTr("VOX") }
    PhoneLevel { visible: !Engine.cwMode && (Engine.canUseDeviceMicrophone || Engine.usingDeviceMicrophone); key: "voxDelay"; title: qsTr("Delay"); spokenTitle: qsTr("VOX delay") }
    Label {
        objectName: "voxDeviceNote"
        visible: !Engine.cwMode && Engine.usingDeviceMicrophone && !!Engine.phone.voxOn
        Layout.fillWidth: true
        text: Engine.voxHoldsMicrophone
              ? qsTr("VOX keeps this device's microphone open and sends it to the radio, whose VOX keys on speech: use a headset, as sound from the speaker would key it too.")
              : qsTr("In standby (SB) VOX cannot key: the microphone opens once the transmit lock is armed (OA).")
        color: Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }
    PhoneLevel { visible: !Engine.cwMode && Engine.canUseDeviceMicrophone; key: "dexpLevel"; toggleKey: "dexpOn"; title: qsTr("DEXP") }
    PhoneLevel {
        visible: ["AM", "SAM", "DSB"].indexOf(Engine.mode) >= 0
        key: "amCarrier"
        title: qsTr("Carrier")
        spokenTitle: qsTr("AM carrier")
    }

    // ---- Monitor ----
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("MON")
            checked: Engine.monitor
            Layout.preferredWidth: 64
            focusPolicy: Qt.NoFocus
            onClicked: Engine.monitor = !Engine.monitor
        }
        FineSlider {
            accessibleName: qsTr("Monitor level")
            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            value: Engine.monitorGain
            onMoved: (v) => Engine.monitorGain = Math.round(v)
        }
    }

    // ---- Microphone: one level, the one that acts. With this device's
    // microphone that is the app's own gain (the radio has no gain stage
    // for its PC input, so its mic level does nothing there); with a
    // microphone on the radio, the radio's mic level. ----
    Label {
        text: Engine.transmitAvailable
              ? qsTr("Mic level  %1%2 dB").arg(Engine.micGainDb > 0 ? "+" : "").arg(Math.round(Engine.micGainDb))
              : qsTr("Mic level  %1").arg(Engine.radioMicLevel)
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    FineSlider {
        id: micSlider
        objectName: "micLevel"
        accessibleName: qsTr("Mic level")
        valueText: Engine.transmitAvailable ? qsTr("%1%2 dB").arg(Engine.micGainDb > 0 ? "+" : "").arg(Math.round(Engine.micGainDb)) : ""
        Layout.fillWidth: true
        from: Engine.transmitAvailable ? -20 : 0
        to: Engine.transmitAvailable ? 30 : 100
        stepSize: 1
        value: Engine.transmitAvailable ? Engine.micGainDb : Engine.radioMicLevel
        onMoved: (v) => Engine.transmitAvailable ? Engine.micGainDb = Math.round(v)
                                                 : Engine.radioMicLevel = Math.round(v)
    }
    Label {
        visible: Engine.transmitAvailable
        Layout.fillWidth: true
        text: Engine.micMeterLive
              ? qsTr("This device's microphone: with Mic chosen for a meter, speak as on air and set the level so speech peaks near -10 dB. While this tab is open the meter moves before PTT; nothing is sent.")
              : qsTr("This device's microphone: set it so speech peaks near -10 dB on the Mic meter while you transmit.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    AppButton {
        Layout.fillWidth: true
        visible: Engine.canUseDeviceMicrophone
        text: qsTr("Use this device's microphone")
        onClicked: Engine.useDeviceMicrophone()
    }
    // The way back: the radio's own microphone input (a headset on the radio).
    AppButton {
        objectName: "useRadioMic"
        Layout.fillWidth: true
        visible: Engine.canUseRadioMicrophone && Engine.audioOnRadio
        text: qsTr("Use the radio's microphone")
        onClicked: Engine.useRadioMicrophone()
    }

    // DAX: the radio transmits the audio a program on a computer sends
    // (FT8 and other digital modes) instead of the microphone. Left on, the
    // microphone goes nowhere, so it is shown, and can be turned off, here.
    RowLayout {
        Layout.fillWidth: true
        visible: !Engine.cwMode
        AppButton {
            objectName: "daxButton"
            text: qsTr("DAX")
            checked: Engine.dax
            enabled: Engine.connectionState === "connected"
            Layout.preferredWidth: 64
            focusPolicy: Qt.NoFocus
            onClicked: Engine.dax = !Engine.dax
        }
        Label {
            Layout.fillWidth: true
            text: Engine.dax
                  ? qsTr("On: the radio sends the audio from DAX (a digital-mode program on a computer), not the microphone.")
                  : qsTr("Off: the radio sends the microphone.")
            color: Engine.dax ? Theme.text : Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.WordWrap
        }
    }

    // PTT: hold to talk, or tap on / tap off (a latched transmission stops
    // by itself after 3 minutes).
    RowLayout {
        Layout.fillWidth: true
        visible: !Engine.cwMode
        Label { text: qsTr("PTT"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppButton {
            objectName: "pttHoldToTalk"
            Accessible.name: qsTr("%1: %2").arg(qsTr("PTT")).arg(text)
            text: qsTr("Hold to talk")
            checked: !Engine.pttToggle
            Layout.fillWidth: true
            onClicked: Engine.pttToggle = false
        }
        AppButton {
            objectName: "pttTapToggle"
            Accessible.name: qsTr("%1: %2").arg(qsTr("PTT")).arg(text)
            text: qsTr("Tap on / off")
            checked: Engine.pttToggle
            Layout.fillWidth: true
            onClicked: Engine.pttToggle = true
        }
    }

    // The learned PTT key is set seldom: Setup › Transmit (roadmap 131), its
    // first part.
    AppButton {
        objectName: "openPttKey"
        Layout.fillWidth: true
        visible: !Engine.cwMode
        text: Engine.pttKey !== 0 ? qsTr("PTT key: %1…").arg(Engine.pttKeyName) : qsTr("PTT key…")
        focusPolicy: Qt.NoFocus
        onClicked: Engine.openSetup(1)
    }

    // Split: a second slice (as SmartSDR) or this slice's XIT (as on a
    // Maestro): XIT needs no second slice, for a radio whose slices other
    // programs hold (Seb DG5CST, a FLEX-6400M shared with a station PC).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Split"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppButton {
            objectName: "splitBySlice"
            text: qsTr("Second slice")
            checked: Engine.splitMethod !== "xit"
            Layout.fillWidth: true
            onClicked: Engine.splitMethod = "slice"
        }
        AppButton {
            objectName: "splitByXit"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Split")).arg(text)
            text: qsTr("XIT")
            checked: Engine.splitMethod === "xit"
            Layout.fillWidth: true
            onClicked: Engine.splitMethod = "xit"
        }
    }
    Label {
        Layout.fillWidth: true
        text: Engine.splitMethod === "xit"
              ? qsTr("SPLIT turns this slice's XIT on: it transmits 1 kHz up in CW and the data modes, 5 kHz in voice (hold SPLIT for +1 / +2 / +5 kHz or Other… to type it) and receives where it is. No second slice; the XIT row on the DSP tab shows it, moves it and (tap the value) types it.")
              : qsTr("SPLIT opens a second slice for transmit, 1 kHz up in CW and the data modes, 5 kHz in voice. It needs a free slice on the radio.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // Transmit lock (Larry Averbeck): in standby (SB) nothing transmits;
    // PTT, KEY, TUNE or CQ held for a second arms it (OA) for five minutes
    // after the last transmission.
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Lock"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppButton {
            objectName: "txLockOff"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Lock")).arg(text)
            text: qsTr("Off")
            checked: !Engine.txLock
            Layout.fillWidth: true
            onClicked: Engine.txLock = false
        }
        AppButton {
            objectName: "txLockOn"
            text: qsTr("Transmit lock")
            checked: Engine.txLock
            Layout.fillWidth: true
            onClicked: Engine.txLock = true
        }
    }
    // With the lock off, standby once after each start of the app until a
    // second's hold (Larry Averbeck: aware of transmitting from the first
    // press); the lock itself starts in standby anyway.
    RowLayout {
        Layout.fillWidth: true
        visible: !Engine.txLock
        Label { text: qsTr("At start"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppButton {
            objectName: "txArmAtStartOff"
            Accessible.name: qsTr("%1: %2").arg(qsTr("At start")).arg(text)
            text: qsTr("Ready")
            checked: !Engine.txArmAtStart
            Layout.fillWidth: true
            onClicked: Engine.txArmAtStart = false
        }
        AppButton {
            objectName: "txArmAtStartOn"
            Accessible.name: qsTr("%1: %2").arg(qsTr("At start")).arg(text)
            text: qsTr("Hold to arm")
            checked: Engine.txArmAtStart
            Layout.fillWidth: true
            onClicked: Engine.txArmAtStart = true
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.txLock || Engine.txLocked
        Label {
            Layout.fillWidth: true
            text: !Engine.txLock
                  ? qsTr("SB since the app started: hold PTT, KEY, TUNE or CQ for a second to arm until it closes.")
                  : Engine.txLocked
                  ? qsTr("SB, standby: hold PTT, KEY, TUNE or CQ for a second to arm.")
                  : qsTr("OA, armed: %1 min left, five minutes after each transmission.")
                        .arg(Math.ceil(Engine.txArmedSeconds / 60))
            color: Engine.txLocked ? Theme.textDim : Theme.transmit
            font.pixelSize: Theme.px(12)
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "txStandby"
            visible: Engine.txLock && !Engine.txLocked
            text: qsTr("Standby")
            onClicked: Engine.standbyTransmit()
        }
    }

    // APD, adaptive pre-distortion (FLEX-8000, Aurora): the radio measures
    // its own transmission and pre-distorts it so the PA's IMD falls. As the
    // desktop's TX applet: on / off and what it is doing; the sampler ports
    // and the equalizer reset are in Radio Setup › Transmit.
    RowLayout {
        Layout.fillWidth: true
        visible: !!Engine.apd.configurable
        Label { text: qsTr("APD"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppButton {
            objectName: "apdOff"
            Accessible.name: qsTr("%1: %2").arg(qsTr("APD")).arg(text)
            text: qsTr("Off")
            checked: !Engine.apd.enabled
            Layout.fillWidth: true
            onClicked: Engine.setApdEnabled(false)
        }
        AppButton {
            objectName: "apdOn"
            Accessible.name: qsTr("%1: %2").arg(qsTr("APD")).arg(text)
            text: qsTr("On")
            checked: !!Engine.apd.enabled
            Layout.fillWidth: true
            onClicked: Engine.setApdEnabled(true)
        }
    }
    Label {
        objectName: "apdState"
        visible: !!Engine.apd.configurable && !!Engine.apd.enabled
        Layout.fillWidth: true
        // Active: correcting; Avail: ready, waiting for a transmission to
        // learn from (the desktop's Active / Avail indicators).
        text: Engine.apd.equalizerActive ? qsTr("APD active: correcting the transmission on this band and power.")
                                         : qsTr("APD ready: it learns from the next transmission.")
        color: Engine.apd.equalizerActive ? Theme.good : Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // TX audio passband (low and high cut of the transmitted audio).
    Label {
        text: qsTr("TX filter  %1 – %2 Hz").arg(Engine.txFilterLow).arg(Engine.txFilterHigh)
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
        Layout.topMargin: Theme.spacing / 2
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Low"); color: Theme.textDim; font.pixelSize: Theme.px(13) }
        FineSlider {
            accessibleName: qsTr("TX filter low cut")
            valueText: qsTr("%1 Hz").arg(Engine.txFilterLow)
            Layout.fillWidth: true
            enabled: Engine.connectionState === "connected"
            from: 0; to: 1000; stepSize: 10
            value: Engine.txFilterLow
            onMoved: (v) => Engine.txFilterLow = Math.round(v)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("High"); color: Theme.textDim; font.pixelSize: Theme.px(13) }
        FineSlider {
            accessibleName: qsTr("TX filter high cut")
            valueText: qsTr("%1 Hz").arg(Engine.txFilterHigh)
            Layout.fillWidth: true
            enabled: Engine.connectionState === "connected"
            from: 1000; to: 5000; stepSize: 50
            value: Engine.txFilterHigh
            onMoved: (v) => Engine.txFilterHigh = Math.round(v)
        }
    }

    // ---- Voice keyer: the radio's DVK where it is licensed (12 slots of up
    // to 10 s kept in the radio), otherwise the app's own voice macros (12
    // slots of up to 15 s on this device, played into the TX audio). Tap
    // plays (transmits), tap again stops; press and hold for record, listen
    // (radio only), rename and clear. F1-F12 on a keyboard play them in voice
    // modes, Esc stops. ----
    Label {
        visible: !Engine.cwMode && Engine.dvkState !== "unavailable"
        text: Engine.dvkState === "idle" ? qsTr("Voice keyer")
              : Engine.dvkState === "recording" ? qsTr("Voice keyer: recording F%1 (tap it to stop)").arg(Engine.dvkActiveId)
              : Engine.dvkState === "preview" ? qsTr("Voice keyer: listening to F%1").arg(Engine.dvkActiveId)
              : qsTr("Voice keyer: sending F%1").arg(Engine.dvkActiveId)
        Accessible.role: Accessible.Heading
        color: Engine.dvkState === "idle" ? Theme.textDim : Theme.warning
        font.pixelSize: Theme.px(15)
    }
    GridLayout {
        visible: !Engine.cwMode && Engine.dvkState !== "unavailable"
        Layout.fillWidth: true
        columns: width > 520 ? 4 : 3
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2
        Repeater {
            model: Engine.dvkSlots
            delegate: AppButton {
                id: slotButton
                required property var modelData
                objectName: "dvkSlot_" + modelData.id
                readonly property bool active: Engine.dvkActiveId === modelData.id && Engine.dvkState !== "idle"
                text: "F" + modelData.id + "  " + modelData.name
                      + (modelData.seconds > 0 ? "  " + modelData.seconds.toFixed(1) + " s" : "")
                checked: active
                opacity: modelData.seconds > 0 || active ? 1.0 : 0.6
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                onClicked: {
                    if (Engine.dvkState === "recording") Engine.dvkRecord(modelData.id)
                    else if (Engine.dvkState === "preview") Engine.dvkPreview(modelData.id)
                    else if (modelData.seconds > 0 || Engine.dvkState === "playback") Engine.dvkPlay(modelData.id)
                    else dvkMenu.openFor(modelData)
                }
                onPressAndHold: dvkMenu.openFor(modelData)
            }
        }
    }
    DvkSlotMenu {
        id: dvkMenu
        objectName: "dvkMenu"
    }

    // While this tab shows and the app is in front, the microphone stays
    // open so the level above moves before transmitting (as on the desktop).
    readonly property bool micCheckShown: visible && Qt.application.state === Qt.ApplicationActive
    onMicCheckShownChanged: Engine.holdMicCheck(tx, micCheckShown)
    Component.onCompleted: Engine.holdMicCheck(tx, micCheckShown)
    Component.onDestruction: Engine.holdMicCheck(tx, false)
}
