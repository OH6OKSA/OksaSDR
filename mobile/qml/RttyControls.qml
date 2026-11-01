import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// RTTY's settings and text: the Controls RTTY tab, and the panel a tap on
// the RTTY line along the top of the waterfall opens. Decode on / off, the
// shift and mark (Auto: the radio's RTTY settings), the speed, reverse,
// squelch, Autotune, whether it has locked on, and the text so far.
ColumnLayout {
    id: rtty
    spacing: Theme.spacing / 2
    // In the panel its title row holds Decode on / off, and RTTY is being
    // received already (the line that opens it shows only then).
    property bool inPanel: false
    readonly property var r: Engine.rtty

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

    // How RTTY is received: the radio's RTTY mode (lower sideband: the
    // standard, the shown frequency the mark), or AFSK in DIGL (lower
    // sideband) or DIGU (upper sideband, mark and space the other way round,
    // which the decoder follows). DIGL / DIGU stop FT8 / FT4 decoding.
    Label {
        visible: !rtty.inPanel
        text: qsTr("Receive in")
        color: Theme.textDim
        font.pixelSize: Theme.smallFont
    }
    RowLayout {
        visible: !rtty.inPanel
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
        text: rtty.r.via === "DIGU"
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
            text: !Engine.rttyDecoderEnabled ? qsTr("Decoding is off.")
                  : !Engine.rttyActive ? qsTr("Decoding starts when RTTY is chosen above.")
                  : rtty.r.locked ? qsTr("Locked on, SNR %1 dB").arg(rtty.r.snr)
                  : qsTr("Listening in %1 (mark %2 Hz, space %3 Hz): tap the signal on the display, or Autotune.")
                        .arg(rtty.r.via).arg(rtty.r.markTone).arg(rtty.r.spaceTone)
            color: rtty.r.locked && Engine.rttyActive ? Theme.accent : Theme.textDim
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
    // As the notch panel: each row's name over it, the buttons sharing the
    // width alike.
    Label { text: qsTr("Shift"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Choice { key: "shift"; value: 0; text: qsTr("Auto") }
        Choice { key: "shift"; value: 170; text: "170" }
        Choice { key: "shift"; value: 425; text: "425" }
        Choice { key: "shift"; value: 850; text: "850" }
    }
    Label { text: qsTr("Speed and mark"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
    RowLayout {
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
        Layout.fillWidth: true
        spacing: 4
        Choice { key: "mark"; value: 0; text: qsTr("Mark auto") }
        Choice { key: "mark"; value: 2125; text: "2125" }
        Choice { key: "mark"; value: 1275; text: "1275" }
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
        ScrollBar.vertical: ScrollBar { policy: rttyText.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
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
            objectName: "rttyAutotune"
            text: qsTr("Autotune")
            enabled: Engine.rttyActive
            Layout.preferredHeight: Theme.touchTarget
            onClicked: Engine.rttyAutotune()
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
    Label {
        visible: !rtty.inPanel
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        text: qsTr("The text also runs along the top of the waterfall; a tap on it opens these settings over the display. Receive only: the app does not transmit RTTY.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
