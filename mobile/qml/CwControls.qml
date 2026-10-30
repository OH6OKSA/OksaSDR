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
            font.pixelSize: 15
        }
        Label {
            Layout.fillWidth: true
            text: Engine.callsign.length > 0 ? Engine.callsign : qsTr("not set (Setup → Radio)")
            color: Engine.callsign.length > 0 ? Theme.text : Theme.warning
            font.pixelSize: 17
            font.bold: true
        }
    }

    // Keying in PTT's place: a straight key, or paddles (dit, dah) timed by
    // the app's iambic keyer, which follows the radio's iambic mode and
    // swapped paddles (set here or in Setup > Phone/CW).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Key"); color: Theme.textDim; font.pixelSize: 15; Layout.preferredWidth: 64 }
        AppButton {
            text: qsTr("Straight key")
            checked: !Engine.cwPaddles
            Layout.fillWidth: true
            onClicked: Engine.cwPaddles = false
        }
        AppButton {
            text: qsTr("Paddles")
            checked: Engine.cwPaddles
            Layout.fillWidth: true
            onClicked: Engine.cwPaddles = true
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.cwPaddles
        Label { text: qsTr("Iambic"); color: Theme.textDim; font.pixelSize: 15; Layout.preferredWidth: 64 }
        AppButton {
            text: "A"
            checked: Engine.cwIambicMode === 0
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwIambicMode = 0
        }
        AppButton {
            text: "B"
            checked: Engine.cwIambicMode === 1
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwIambicMode = 1
        }
        AppButton {
            text: qsTr("Swap ⇄")
            checked: Engine.cwSwapPaddles
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwSwapPaddles = !Engine.cwSwapPaddles
        }
    }
    Label {
        visible: Engine.cwPaddles
        Layout.fillWidth: true
        text: qsTr("Dit and dah take PTT's place; hold both for a squeeze. B adds one more element after a squeeze, A does not. Swap puts dah on the left.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // Paddles on a hardware keyboard: which two keys are dit and dah.
    Label {
        visible: Engine.cwPaddles
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        text: qsTr("Paddles on a keyboard   (left key dit, right key dah)")
        color: Theme.textDim
        font.pixelSize: 15
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.cwPaddles
        Repeater {
            model: [{ key: "off", label: qsTr("Off") }, { key: "ctrl", label: "Ctrl" },
                    { key: "alt", label: "Alt ⌥" }, { key: "brackets", label: "[  ]" }]
            delegate: AppButton {
                required property var modelData
                text: modelData.label
                checked: Engine.cwKeyboardPaddles === modelData.key
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.cwKeyboardPaddles = modelData.key
            }
        }
    }
    Label {
        visible: Engine.cwPaddles
        Layout.fillWidth: true
        text: {
            switch (Engine.cwKeyboardPaddles) {
            case "off": return qsTr("The keyboard does not key CW; use the dit and dah pads on the screen.")
            case "ctrl": return qsTr("Left Ctrl = dit, right Ctrl = dah. USB paddle adapters (Vail, VBand) send these.")
            case "alt": return qsTr("Left Alt / Option = dit, right Alt / Option = dah. For keyboards with no right Ctrl, such as Apple's Magic Keyboard.")
            default: return qsTr("[ = dit, ] = dah. Where [ and ] need AltGr or Option (Finnish, German…), pick Ctrl or Alt.")
            }
        }
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
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
            font.pixelSize: 12
        }
        AppButton {
            visible: Engine.workedCall.length > 0
            text: Engine.workedCall + "  ✕"
            font.family: "monospace"
            font.bold: true
            checked: true
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
            property string sentText: ""
            Layout.fillWidth: true
            Layout.columnSpan: cwxRow.oneRow ? 1 : 3
            placeholderText: liveButton.checked ? qsTr("Type: each letter goes now") : qsTr("CW text to send")
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            font.pixelSize: 16
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
            // The keyboard's F-keys and Esc work while typing too.
            Keys.onPressed: (event) => {
                if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F12) {
                    Engine.sendCwMemory(event.key - Qt.Key_F1)
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

    Label { text: qsTr("Speed  %1 WPM").arg(Engine.cwSpeed); color: Theme.textDim; font.pixelSize: 15 }
    FineSlider {
        Layout.fillWidth: true
        from: 5
        to: 60
        stepSize: 1
        value: Engine.cwSpeed
        onMoved: (v) => Engine.cwSpeed = Math.round(v)
    }
    Label { text: qsTr("Pitch  %1 Hz").arg(Engine.cwPitch); color: Theme.textDim; font.pixelSize: 15 }
    FineSlider {
        Layout.fillWidth: true
        from: 300
        to: 1000
        stepSize: 10
        value: Engine.cwPitch
        onMoved: (v) => Engine.cwPitch = Math.round(v)
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
        Label { text: qsTr("Sens"); color: Theme.textDim; font.pixelSize: 15; Layout.preferredWidth: 64 }
        Slider {
            id: sensSlider
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: Engine.cwDecoderSensitivity = Math.round(value)
            Binding on value { when: !sensSlider.pressed; value: Engine.cwDecoderSensitivity }
        }
        Label {
            text: Math.round(sensSlider.value)
            color: Theme.textDim
            font.pixelSize: 13
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
                    font.pixelSize: 13
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
                font.pixelSize: 14
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideLeft
                verticalAlignment: Text.AlignBottom
                clip: true
            }
        }
    }

    // Edits one memory.
    Dialog {
        id: memoryEditor
        objectName: "memoryEditor"
        property int index: 0
        property alias text: memoryField.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
        onOpened: memoryField.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                text: qsTr("Memory M%1   ({CALL} = your callsign, {HIS} = the call being worked)").arg(memoryEditor.index + 1)
                color: Theme.textDim
                font.pixelSize: 14
                // From the popup's fixed width: wrapped, not past its edge.
                Layout.preferredWidth: memoryEditor.width - 2 * memoryEditor.padding
                wrapMode: Text.WordWrap
            }
            TextField {
                id: memoryField
                Layout.fillWidth: true
                font.pixelSize: 17
                inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
                onAccepted: save.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: memoryEditor.close() }
                AppButton {
                    id: save
                    text: qsTr("Save")
                    Layout.fillWidth: true
                    onClicked: {
                        Engine.setCwMemory(memoryEditor.index, memoryField.text)
                        memoryEditor.close()
                    }
                }
            }
        }
    }
}
