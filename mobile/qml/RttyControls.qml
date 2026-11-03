import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The Controls Digi tab (RTTY, PSK31, PSK63), and the panel a tap on the
// text line along the top of the waterfall opens. The mode (the tab only);
// receiving: how (RTTY: the RTTY mode, DIGL, DIGU; PSK: DIGU and a tap on the
// signal), Decode on / off, the text so far, Autotune (RTTY) or AFC (PSK),
// Squelch; operating (the tab only): the call being worked from the calls
// heard, macros as fldigi's (tap sends, press and hold edits), a field to
// type and send, Stop, Log; then RTTY's shift, speed, mark and Reverse, or
// PSK's carrier.
ColumnLayout {
    id: rtty
    spacing: Theme.spacing / 2
    // In the panel its title row holds Decode on / off, and the mode is
    // being received already (the line that opens it shows only then).
    property bool inPanel: false
    readonly property var r: Engine.rtty
    readonly property var k: Engine.psk
    readonly property bool pskMode: Engine.textMode !== "RTTY"

    component Choice: AppButton {
        property string key
        property var value
        objectName: "rtty_" + key + "_" + String(value).replace(".", "_")
        checked: rtty.r[key] === value
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: Theme.touchTarget
        leftPadding: 2
        rightPadding: 2
        onClicked: Engine.setRtty(key, value)
    }

    // The keyboard mode: RTTY, or PSK31 / PSK63 (in DIGU, FT8 / FT4 off).
    Label {
        visible: !rtty.inPanel
        text: qsTr("Mode")
        color: Theme.textDim
        font.pixelSize: Theme.smallFont
    }
    RowLayout {
        visible: !rtty.inPanel
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: ["RTTY", "PSK31", "PSK63"]
            AppButton {
                required property string modelData
                objectName: "textMode" + modelData
                text: modelData
                checked: Engine.textMode === modelData
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                leftPadding: 2
                rightPadding: 2
                onClicked: Engine.setTextMode(modelData)
            }
        }
    }
    // How RTTY is received: the radio's RTTY mode (lower sideband: the
    // standard, the shown frequency the mark), or AFSK in DIGL (lower
    // sideband) or DIGU (upper sideband, mark and space the other way round,
    // which the decoder follows). DIGL / DIGU stop FT8 / FT4 decoding.
    Label {
        visible: !rtty.inPanel && !rtty.pskMode
        text: qsTr("Receive in")
        color: Theme.textDim
        font.pixelSize: Theme.smallFont
    }
    RowLayout {
        visible: !rtty.inPanel && !rtty.pskMode
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: [{ via: "RTTY", label: qsTr("RTTY (LSB)") }, { via: "DIGL", label: qsTr("DIGL") },
                    { via: "DIGU", label: qsTr("DIGU (USB)") }]
            AppButton {
                required property var modelData
                objectName: "rttyVia" + modelData.via
                text: modelData.label
                checked: rtty.r.via === modelData.via
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                leftPadding: 2
                rightPadding: 2
                onClicked: Engine.setRttyVia(modelData.via)
            }
        }
    }
    Label {
        visible: !rtty.inPanel
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        text: rtty.pskMode
              ? (Engine.pskActive
                 ? qsTr("%1 in %2, as the standard (upper sideband): tap a signal on the display to receive it; AFC follows it, and text goes out on the same frequency.").arg(rtty.k.mode).arg(rtty.k.via)
                 : qsTr("%1 runs in DIGU (FT8 / FT4 off): choose it again above if the slice left DIGU.").arg(rtty.k.mode))
              : rtty.r.via === "DIGU"
              ? qsTr("DIGU: AFSK on the upper sideband, where the mark is the upper tone (mark %1 Hz, space %2 Hz); the decoder follows.").arg(rtty.r.markTone).arg(rtty.r.spaceTone)
              : rtty.r.via === "DIGL"
              ? qsTr("DIGL: AFSK on the lower sideband (mark %1 Hz, space %2 Hz), as the standard.").arg(rtty.r.markTone).arg(rtty.r.spaceTone)
              : rtty.r.via === "RTTY"
              ? qsTr("The radio's RTTY mode: lower sideband, the shown frequency is the mark (the standard).")
              : qsTr("Not receiving RTTY now. RTTY (LSB) is the standard; DIGL and DIGU are for AFSK, as many logging programs send it.")
        color: Theme.textDim
        font.pixelSize: Theme.smallFont
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            // Its long text must not widen the panel (rows of buttons past
            // its edge, and a layout that never settled).
            Layout.preferredWidth: 1
            text: Engine.textTransmitting ? qsTr("Transmitting %1…").arg(Engine.textMode)
                  : !Engine.rttyDecoderEnabled ? qsTr("Decoding is off.")
                  : rtty.pskMode
                  ? (!Engine.pskActive ? qsTr("Decoding starts when %1 is chosen above.").arg(rtty.k.mode)
                     : rtty.k.locked ? qsTr("Receiving at %1 Hz (%2 MHz), quality %3 %").arg(Math.round(rtty.k.trackedHz))
                                           .arg(rtty.k.carrierMhz.toFixed(6)).arg(rtty.k.quality)
                     : qsTr("Listening at %1 Hz (%2 MHz): tap a PSK signal on the display.").arg(Math.round(rtty.k.trackedHz))
                           .arg(rtty.k.carrierMhz.toFixed(6)))
                  : !Engine.rttyActive ? qsTr("Decoding starts when RTTY is chosen above.")
                  : rtty.r.locked ? qsTr("Locked on, SNR %1 dB").arg(rtty.r.snr)
                  : qsTr("Listening in %1 (mark %2 Hz, space %3 Hz): tap the signal on the display, or Autotune.")
                        .arg(rtty.r.via).arg(rtty.r.markTone).arg(rtty.r.spaceTone)
            color: Engine.textTransmitting ? Theme.transmit
                   : (rtty.pskMode ? rtty.k.locked && Engine.pskActive : rtty.r.locked && Engine.rttyActive) ? Theme.accent
                   : Theme.textDim
            font.pixelSize: Theme.smallFont
            wrapMode: Text.WordWrap
        }
        AppButton {
            visible: !rtty.inPanel
            objectName: "rttyDecode"
            text: Engine.rttyDecoderEnabled ? qsTr("Decode on") : qsTr("Decode off")
            checked: Engine.rttyDecoderEnabled
            focusPolicy: Qt.NoFocus
            Layout.preferredHeight: Theme.touchTarget
            leftPadding: 10
            rightPadding: 10
            onClicked: Engine.rttyDecoderEnabled = !Engine.rttyDecoderEnabled
        }
    }
    // The text so far, the newest kept in sight at the bottom: a fixed
    // height, its width not from the text.
    FontMetrics {
        id: rttyLine
        font.family: "monospace"
        font.pixelSize: 14
    }
    Flickable {
        id: rttyText
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 6 * rttyLine.height + 8
        clip: true
        contentWidth: width
        contentHeight: rttyTextLabel.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        readonly property bool overflows: contentHeight > height + 1
        ScrollBar.vertical: AppScrollBar { policy: rttyText.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        Label {
            id: rttyTextLabel
            width: rttyText.width - 12
            text: Engine.rttyDecodedText.length > 0 ? Engine.rttyDecodedText : qsTr("Nothing decoded yet.")
            color: Engine.rttyDecodedText.length > 0 ? Theme.text : Theme.textDim
            font: rttyLine.font
            wrapMode: Text.WrapAnywhere
            onTextChanged: Qt.callLater(() => rttyText.contentY = Math.max(0, rttyText.contentHeight - rttyText.height))
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            visible: !rtty.pskMode
            objectName: "rttyAutotune"
            text: qsTr("Autotune")
            enabled: Engine.rttyActive
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.rttyAutotune()
        }
        // PSK: AFC follows a drifting signal (within 25 Hz of the tap).
        AppButton {
            visible: rtty.pskMode
            objectName: "pskAfc"
            text: qsTr("AFC")
            checked: rtty.k.afc
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.setPsk("afc", !checked)
        }
        // Noise's letters held back between transmissions.
        AppButton {
            objectName: "rttySquelch"
            text: qsTr("Squelch")
            checked: rtty.r.squelch
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.setRtty("squelch", !checked)
        }
        Item { Layout.fillWidth: true }
        AppButton {
            objectName: "rttyClear"
            text: qsTr("Clear")
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.clearRttyText()
        }
    }
    // ---- Operating (the tab) ----
    // The station being worked: the Log tab's Call (the same call, as on the
    // CW tab); the calls the text holds (after DE, or twice) as buttons, one
    // tap picks; the pick shows first, ✕ clears it.
    RowLayout {
        id: workedRow
        visible: !rtty.inPanel
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("Call")
            color: Theme.textDim
            font.pixelSize: 12
        }
        AppButton {
            visible: Engine.workedCall.length > 0
            objectName: "textWorkedCall"
            text: Engine.workedCall + "  ✕"
            font.family: "monospace"
            font.bold: true
            checked: true
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
            onClicked: Engine.workedCall = ""
        }
        Repeater {
            id: heardCalls
            model: Engine.textCalls.filter(c => c !== Engine.workedCall).slice(0, workedRow.width >= 420 ? 3 : 2)
            delegate: AppButton {
                required property string modelData
                objectName: "textHeard_" + modelData
                text: modelData
                font.family: "monospace"
                focusPolicy: Qt.NoFocus
                Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
                onClicked: Engine.workedCall = modelData
            }
        }
        Label {
            visible: Engine.workedCall.length === 0 && heardCalls.count === 0
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("Calls heard show here; or type one on the Log tab.")
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillWidth: true; visible: Engine.workedCall.length > 0 || heardCalls.count > 0 }
    }
    // Macros (F1-F8 on a keyboard): tap sends, press and hold edits.
    GridLayout {
        visible: !rtty.inPanel
        Layout.fillWidth: true
        columns: 4
        columnSpacing: 4
        rowSpacing: 4
        Repeater {
            model: Engine.textMacros
            delegate: AppButton {
                required property int index
                required property var modelData
                objectName: "textMacro" + index
                text: modelData.label.length > 0 ? modelData.label : "F" + (index + 1)
                enabled: Engine.textModeActive
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                leftPadding: 2
                rightPadding: 2
                onClicked: Engine.sendTextMacro(index)
                onPressAndHold: {
                    macroEditor.index = index
                    macroEditor.label = modelData.label
                    macroEditor.text = modelData.text
                    macroEditor.open()
                }
            }
        }
    }
    // Typed text: Send (or Return) sends the line; sent while text is
    // going out, it follows without unkeying. Stop (or Esc) stops at once.
    RowLayout {
        visible: !rtty.inPanel
        Layout.fillWidth: true
        spacing: 4
        TextField {
            id: textToSend
            objectName: "textToSend"
            Accessible.name: qsTr("%1 text to send").arg(Engine.textMode)
            Layout.fillWidth: true
            placeholderText: qsTr("Text to send")
            // RTTY has capitals only; PSK has both cases.
            inputMethodHints: rtty.pskMode ? Qt.ImhNoPredictiveText : Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            font.pixelSize: 16
            onAccepted: sendButton.clicked()
            // F1-F8 (or ⌘ / Ctrl with 1-8) and Esc work while typing too.
            Keys.onPressed: (event) => {
                if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F8) {
                    Engine.sendTextMacro(event.key - Qt.Key_F1)
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && event.key >= Qt.Key_1 && event.key <= Qt.Key_8) {
                    Engine.sendTextMacro(event.key - Qt.Key_1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    Engine.stopTextTx()
                    event.accepted = true
                }
            }
        }
        AppButton {
            id: sendButton
            objectName: "textSend"
            text: qsTr("Send")
            enabled: Engine.textModeActive && textToSend.text.trim().length > 0
            focusPolicy: Qt.NoFocus
            Layout.preferredHeight: Theme.touchTarget
            onClicked: {
                if (Engine.sendText(textToSend.text + "\n"))
                    textToSend.text = ""
            }
        }
        AppButton {
            objectName: "textStop"
            text: qsTr("Stop")
            enabled: Engine.textTransmitting
            focusPolicy: Qt.NoFocus
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.stopTextTx()
        }
    }
    // Into the log as RTTY on the signal's mark, or PSK31 / PSK63 on its
    // carrier (599 both ways; the Log tab for anything more).
    AppButton {
        visible: !rtty.inPanel
        objectName: "textLog"
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchTarget
        text: Engine.workedCall.length > 0 ? qsTr("Log %1").arg(Engine.workedCall) : qsTr("Log QSO")
        enabled: Engine.workedCall.length >= 3
        focusPolicy: Qt.NoFocus
        onClicked: {
            if (Engine.logQso({ call: Engine.workedCall, rstSent: "599", rstRcvd: "599", timeOn: Engine.workedCallTimeOn() }))
                Engine.workedCall = ""
        }
    }

    // PSK's carrier by hand (a tap is a finger wide; AFC does the last
    // hertz): 10 Hz a step.
    Label { visible: rtty.pskMode; text: qsTr("Carrier"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
    RowLayout {
        visible: rtty.pskMode
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            objectName: "pskCarrierDown"
            text: "−10"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.setPsk("carrier", Math.round(rtty.k.trackedHz) - 10)
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("%1 Hz").arg(Math.round(rtty.k.trackedHz))
            color: Theme.text
            font.pixelSize: 16
        }
        AppButton {
            objectName: "pskCarrierUp"
            text: "+10"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.setPsk("carrier", Math.round(rtty.k.trackedHz) + 10)
        }
    }
    // As the notch panel: each row's name over it, the buttons sharing the
    // width alike.
    Label { visible: !rtty.pskMode; text: qsTr("Shift"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
    RowLayout {
        visible: !rtty.pskMode
        Layout.fillWidth: true
        spacing: 4
        Choice { key: "shift"; value: 0; text: qsTr("Auto") }
        Choice { key: "shift"; value: 170; text: "170" }
        Choice { key: "shift"; value: 425; text: "425" }
        Choice { key: "shift"; value: 850; text: "850" }
    }
    Label { visible: !rtty.pskMode; text: qsTr("Speed and mark"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
    RowLayout {
        visible: !rtty.pskMode
        Layout.fillWidth: true
        spacing: 4
        Choice { key: "baud"; value: 45.45; text: "45.45" }
        Choice { key: "baud"; value: 50; text: "50" }
        Choice { key: "baud"; value: 75; text: "75" }
        AppButton {
            objectName: "rttyReverse"
            text: qsTr("Reverse")
            checked: rtty.r.reverse
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.preferredHeight: Theme.touchTarget
            leftPadding: 2
            rightPadding: 2
            onClicked: Engine.setRtty("reverse", !checked)
        }
    }
    RowLayout {
        visible: !rtty.pskMode
        Layout.fillWidth: true
        spacing: 4
        Choice { key: "mark"; value: 0; text: qsTr("Mark auto") }
        Choice { key: "mark"; value: 2125; text: "2125" }
        Choice { key: "mark"; value: 1275; text: "1275" }
    }
    Label {
        visible: !rtty.inPanel
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        text: (rtty.pskMode
               ? qsTr("Macros and the text field transmit %1 from this slice, which must be the TX slice, at the FTx tab's TX level, on the frequency received (keep the ALC at zero).").arg(rtty.k.mode)
               : qsTr("Macros and the text field transmit RTTY from this slice, which must be the TX slice, at the FTx tab's TX level; the mark always the higher radio frequency."))
              + " " + qsTr("In a macro {CALL} is your call, {HIS} the call being worked, {NAME} its name from the lookup, {RST} 599, {GRID} your locator. The text also runs along the top of the waterfall; a tap on it opens the receive settings over the display.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // Edits one macro.
    Dialog {
        id: macroEditor
        objectName: "macroEditor"
        property int index: 0
        property alias label: macroLabel.text
        property alias text: macroText.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent ? parent.width - 24 : 420)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
        onOpened: macroText.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: Theme.spacing / 2
            Label {
                text: qsTr("Macro F%1: {CALL} = your call, {HIS} = the call being worked, {NAME}, {RST}, {GRID}").arg(macroEditor.index + 1)
                color: Theme.textDim
                font.pixelSize: 14
                Layout.preferredWidth: macroEditor.width - 2 * macroEditor.padding
                wrapMode: Text.WordWrap
            }
            TextField {
                id: macroLabel
                Accessible.name: qsTr("Macro F%1 name").arg(macroEditor.index + 1)
                Layout.fillWidth: true
                placeholderText: qsTr("Name on the button")
                font.pixelSize: 16
            }
            TextArea {
                id: macroText
                Accessible.name: qsTr("Macro F%1 text").arg(macroEditor.index + 1)
                Layout.fillWidth: true
                Layout.preferredHeight: 5 * font.pixelSize * 1.4
                wrapMode: TextEdit.Wrap
                font.pixelSize: 16
                font.family: "monospace"
                inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
                background: Rectangle { color: Theme.surfaceRaised; radius: 4 }
            }
            // What it sends now, the placeholders filled (or what is missing).
            Label {
                objectName: "macroPreview"
                Layout.preferredWidth: macroEditor.width - 2 * macroEditor.padding
                text: qsTr("Sends: %1").arg(Engine.previewText(macroText.text))
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                // The default into the fields, to see and Save (or Cancel).
                AppButton {
                    objectName: "macroDefault"
                    text: qsTr("Default")
                    Layout.fillWidth: true
                    onClicked: {
                        const d = Engine.defaultTextMacro(macroEditor.index)
                        macroEditor.label = d.label
                        macroEditor.text = d.text
                    }
                }
                AppButton { objectName: "macroCancel"; text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: macroEditor.close() }
                AppButton {
                    objectName: "macroSave"
                    text: qsTr("Save")
                    Layout.fillWidth: true
                    onClicked: {
                        Engine.setTextMacro(macroEditor.index, macroLabel.text, macroText.text)
                        macroEditor.close()
                    }
                }
            }
        }
    }
}
