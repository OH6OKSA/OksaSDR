import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// CW: straight key (hold), CWX text via the radio's keyer, speed, pitch,
// break-in and the receive decoder. Shared by the tablet panel and drawer.
ColumnLayout {
    id: cw
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // The callsign {CALL} stands for: the radio's own, set in Radio Setup.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.connectionState === "connected"
        Label {
            text: qsTr("Callsign")
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
        }
        Label {
            Layout.fillWidth: true
            text: Engine.callsign.length > 0 ? Engine.callsign : qsTr("not set (Setup → Radio)")
            color: Engine.callsign.length > 0 ? Theme.text : Theme.warning
            font.pixelSize: Theme.px(17)
            font.bold: true
        }
    }

    // The key and the keyboard's paddles are set seldom: Setup › Phone/CW
    // (roadmap 131), its first part; the radio's Iambic and Swap there too.
    AppButton {
        objectName: "openCwKeying"
        Layout.fillWidth: true
        text: Engine.cwPaddles ? qsTr("Keying: paddles…") : qsTr("Keying: straight key…")
        focusPolicy: Qt.NoFocus
        onClicked: Engine.openSetup(2)
    }

    // The station being worked, for the memories' {HIS} and the Log tab's
    // Call field (the same call): the calls the decoder heard (after DE, or
    // twice) as buttons, one tap picks; the pick shows first, ✕ clears it.
    RowLayout {
        id: workedRow
        Layout.fillWidth: true
        visible: Engine.workedCall.length > 0 || (Engine.cwDecoderEnabled && heardRepeater.count > 0)
        spacing: 4
        Label {
            text: qsTr("Call")
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
        }
        AppButton {
            visible: Engine.workedCall.length > 0
            text: Engine.workedCall + "  ✕"
            Accessible.name: qsTr("%1, clear the worked call").arg(Engine.workedCall)
            font.family: "monospace"
            font.bold: true
            checked: true
            primary: true
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
            onClicked: Engine.workedCall = ""
        }
        Repeater {
            id: heardRepeater
            model: Engine.cwDecoderEnabled
                   ? Engine.cwCalls.filter(c => c !== Engine.workedCall).slice(0, workedRow.width >= 420 ? 3 : 2)
                   : []
            delegate: AppButton {
                required property string modelData
                objectName: "cwWorked_" + modelData
                text: modelData
                font.family: "monospace"
                focusPolicy: Qt.NoFocus
                Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
                onClicked: Engine.workedCall = modelData
            }
        }
        Item { Layout.fillWidth: true }
    }

    // Memories M1-M12: tap sends (CWX), press and hold edits; F1-F12 on a
    // keyboard send them too. {CALL} = the radio's callsign, {HIS} = the
    // call being worked (above). The straight key is the KEY button in PTT's
    // place.
    GridLayout {
        Layout.fillWidth: true
        visible: Engine.cwxAvailable
        columns: width > 520 ? 4 : 3
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2
        Repeater {
            model: Engine.cwMemories
            delegate: AppButton {
                required property int index
                required property string modelData
                objectName: "cwMemory_" + index
                text: "M" + (index + 1) + "  " + modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                onClicked: Engine.sendCwMemory(index)
                onPressAndHold: {
                    memoryEditor.index = index
                    memoryEditor.text = modelData
                    memoryEditor.open()
                }
            }
        }
    }

    // CWX: text sent by the radio's keyer at the set speed. Live sends each
    // character as it is typed (the desktop's live mode); otherwise Send (or
    // Return) sends the line. Stop, or Esc on a keyboard, aborts.
    // One row where the field's hint fits beside the buttons, else the field
    // over them (a narrow panel cut the hint to "CW text to s…").
    GridLayout {
        id: cwxRow
        Layout.fillWidth: true
        visible: Engine.cwxAvailable
        readonly property bool oneRow: width >= cwxHint.advanceWidth + 32 + liveButton.implicitWidth
                                                + send.implicitWidth + stopButton.implicitWidth + 3 * columnSpacing
        columns: oneRow ? 4 : 3
        TextMetrics {
            id: cwxHint
            font: cwxText.font
            text: qsTr("Type: each letter goes now")
        }
        TextField {
            id: cwxText
            Accessible.name: qsTr("CW text to send")
            property string sentText: ""
            Layout.fillWidth: true
            Layout.columnSpan: cwxRow.oneRow ? 1 : 3
            placeholderText: liveButton.checked ? qsTr("Type: each letter goes now") : qsTr("CW text to send")
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            font.pixelSize: Theme.px(16)
            // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
            EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
            onAccepted: {
                if (liveButton.checked) {
                    Engine.sendCwxChar(" ")
                    text = ""
                    sentText = ""
                } else {
                    send.clicked()
                }
            }
            // Live: only what was added at the end goes; edits before it
            // cannot be unsent.
            onTextEdited: {
                if (!liveButton.checked) {
                    return
                }
                if (text.startsWith(sentText) && text.length > sentText.length) {
                    const added = text.substring(sentText.length)
                    for (let i = 0; i < added.length; ++i) {
                        Engine.sendCwxChar(added[i])
                    }
                }
                sentText = text
            }
            // The keyboard's F-keys (or ⌘ / Ctrl with 1-9, 0) and Esc work
            // while typing too.
            Keys.onPressed: (event) => {
                if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F12) {
                    Engine.sendCwMemory(event.key - Qt.Key_F1)
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                    Engine.sendCwMemory(event.key === Qt.Key_0 ? 9 : event.key - Qt.Key_1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    Engine.stopCwx()
                    event.accepted = true
                }
            }
        }
        AppButton {
            id: liveButton
            text: qsTr("Live")
            checkable: true
            Layout.fillWidth: !cwxRow.oneRow
            onToggled: {
                cwxText.text = ""
                cwxText.sentText = ""
            }
        }
        AppButton {
            id: send
            visible: !liveButton.checked
            text: qsTr("Send")
            enabled: cwxText.text.length > 0
            Layout.fillWidth: !cwxRow.oneRow
            onClicked: {
                Engine.sendCwx(cwxText.text)
                cwxText.text = ""
            }
        }
        AppButton {
            id: stopButton
            text: qsTr("Stop")
            Layout.fillWidth: !cwxRow.oneRow
            onClicked: Engine.stopCwx()
        }
    }

    Label { text: qsTr("Speed  %1 WPM").arg(Engine.cwSpeed); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    FineSlider {
        accessibleName: qsTr("CW speed")
        valueText: qsTr("%1 WPM").arg(Engine.cwSpeed)
        Layout.fillWidth: true
        from: 5
        to: 60
        stepSize: 1
        value: Engine.cwSpeed
        onMoved: (v) => Engine.cwSpeed = Math.round(v)
    }
    Label { text: qsTr("Pitch  %1 Hz").arg(Engine.cwPitch); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    FineSlider {
        accessibleName: qsTr("CW pitch")
        valueText: qsTr("%1 Hz").arg(Engine.cwPitch)
        Layout.fillWidth: true
        from: 300
        to: 1000
        stepSize: 10
        value: Engine.cwPitch
        onMoved: (v) => Engine.cwPitch = Math.round(v)
    }
    // Sidetone: its own level, so a high receive volume does not make the
    // key deafening (Tyler K0OTC).
    Label { text: qsTr("Sidetone  %1").arg(Engine.cwSidetoneGain); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            objectName: "cwSidetone"
            text: qsTr("On")
            Accessible.name: qsTr("On, sidetone")
            checked: Engine.cwSidetone
            Layout.preferredWidth: 64
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwSidetone = !Engine.cwSidetone
        }
        FineSlider {
            accessibleName: qsTr("Sidetone level")
            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            value: Engine.cwSidetoneGain
            onMoved: (v) => Engine.cwSidetoneGain = Math.round(v)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("Break-in")
            checked: Engine.cwBreakIn
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwBreakIn = !Engine.cwBreakIn
        }
        AppButton {
            objectName: "cwDecoder"
            text: qsTr("Decoder")
            checked: Engine.cwDecoderEnabled
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwDecoderEnabled = !Engine.cwDecoderEnabled
        }
        // Now: the CW signal being heard to the pitch (the decoder's pitch,
        // else the display's peak in the passband), as the desktop's Zero
        // Beat.
        AppButton {
            objectName: "cwAutotune"
            text: qsTr("Autotune")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            enabled: Engine.cwMode
            onClicked: Engine.requestCwAutotune()
        }
    }

    // Decoder sensitivity (the desktop's Sens): higher shows only letters
    // decoded with confidence, so band noise stays out.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.cwDecoderEnabled
        Label { text: qsTr("Sens"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.preferredWidth: 64 }
        AppSlider {
            id: sensSlider
            Accessible.name: qsTr("CW decoder sensitivity")
            Accessible.onIncreaseAction: { increase(); moved() }
            Accessible.onDecreaseAction: { decrease(); moved() }
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: Engine.cwDecoderSensitivity = Math.round(value)
            Binding on value { when: !sensSlider.pressed; value: Engine.cwDecoderSensitivity }
        }
        Label {
            text: Math.round(sensSlider.value)
            color: Theme.textDim
            font.pixelSize: Theme.px(13)
            Layout.preferredWidth: 28
            horizontalAlignment: Text.AlignRight
        }
    }

    // Decoded text (CW modes only).
    Rectangle {
        Layout.fillWidth: true
        visible: Engine.cwDecoderEnabled
        implicitHeight: 130
        radius: Theme.radius
        color: Theme.background
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: Engine.cwMode ? (Engine.cwDecoderStats.length > 0 ? Engine.cwDecoderStats : qsTr("Listening…"))
                                        : qsTr("Decoder runs in CW mode")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(13)
                    Layout.fillWidth: true
                }
                AppButton {
                    text: qsTr("Clear")
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.clearCwDecoded()
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: Engine.cwDecodedText
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: Theme.px(14)
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideLeft
                verticalAlignment: Text.AlignBottom
                clip: true
            }
        }
    }

    // Edits one memory.
    CwMemoryEditor {
        id: memoryEditor
        objectName: "memoryEditor"
    }
}
