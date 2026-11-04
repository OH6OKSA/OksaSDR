import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The macros of the mode the radio is in, a tap away from PTT (Tyler
// Akerman K0OTC: the voice keyer at the bottom of the TX tab and the CW
// memories halfway down the CW tab took scrolling past sliders on a phone,
// where a thumb could move one). The keyboard's F-keys' mapping
// (OperateKeys): CW modes send the CW memories M1-M12 (the call being
// worked beside them, {HIS}), RTTY and PSK the Digi tab's macros F1-F8,
// voice modes the voice keyer F1-F12. Buttons only, a tap sends and Stop
// stops; editing stays on the tabs. Not modal and not dimmed, so PTT and
// the display stay in use; open until closed (✕ or the Macros button).
Popup {
    id: macros
    objectName: "macrosPanel"

    // The row the panel sits above (the page's PTT row), and how wide it
    // may grow (the tablet keeps it to the display's side).
    property Item anchorItem
    property real maxWidth: 1e9

    // Which macros: "cw", "text" or "voice"; "" while the mode has none
    // (FT8 / FT4 running, no CWX, no voice keyer).
    readonly property string kind: Engine.digiRunning && !Engine.rade ? ""
        : Engine.cwMode ? (Engine.cwxAvailable ? "cw" : "")
        : Engine.textModeActive ? "text"
        : Engine.dvkState !== "unavailable" ? "voice" : ""
    readonly property bool available: kind !== ""
    onAvailableChanged: if (!available) close()

    modal: false
    dim: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: Theme.spacing / 2
    x: Theme.spacing
    width: Math.min(maxWidth, parent ? parent.width - 2 * Theme.spacing : 400)
    background: Rectangle {
        color: Theme.surface
        opacity: 0.97
        radius: Theme.radius
        border.color: Theme.accent
        border.width: 1
    }
    readonly property int columns: width >= 700 ? 6 : width > 520 ? 4 : 3

    // Above the PTT row, placed again as the page or the panel resizes.
    function place() {
        if (anchorItem && parent) {
            y = anchorItem.mapToItem(parent, 0, 0).y - height - Theme.spacing / 2
        }
    }
    onAboutToShow: place()
    onHeightChanged: if (visible) place()
    Connections {
        target: macros.parent
        function onWidthChanged() { macros.place() }
        function onHeightChanged() { macros.place() }
    }

    // A word on a tap that sent nothing (an empty voice keyer slot).
    property string notice: ""
    Timer {
        id: noticeTimer
        interval: 4000
        onTriggered: macros.notice = ""
    }
    function stop() {
        if (kind === "cw") Engine.stopCwx()
        else if (kind === "text") Engine.stopTextTx()
        else Engine.dvkStop()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: macros.kind === "cw" ? qsTr("CW memories")
                    : macros.kind === "text" ? qsTr("Digi macros")
                    : Engine.dvkState === "playback" ? qsTr("Voice keyer: sending F%1").arg(Engine.dvkActiveId)
                    : Engine.dvkState === "recording" ? qsTr("Voice keyer: recording F%1").arg(Engine.dvkActiveId)
                    : qsTr("Voice keyer")
                color: Engine.dvkState === "playback" || Engine.dvkState === "recording" ? Theme.warning : Theme.text
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
            }
            AppButton {
                objectName: "macrosStop"
                text: qsTr("Stop")
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Stop sending")
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                Layout.preferredHeight: Theme.touchTarget
                onClicked: macros.stop()
            }
            AppButton {
                objectName: "macrosClose"
                text: "✕"
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Close the macros")
                Layout.preferredWidth: Theme.touchTarget
                Layout.preferredHeight: Theme.touchTarget
                onClicked: macros.close()
            }
        }

        // The decoder's latest text, two lines, in the panel itself: the
        // panel covers the strip at the top of the waterfall (on a phone
        // always), and sending memories is when it is read (Tyler, build 152).
        Rectangle {
            id: decodedBox
            readonly property bool text: macros.kind === "text"
            readonly property string stream: text ? Engine.rttyStrip : Engine.cwStrip
            visible: (macros.kind === "cw" && Engine.cwDecoderEnabled && Engine.cwMode)
                     || (text && Engine.textModeActive && Engine.rttyDecoderEnabled)
            Layout.fillWidth: true
            implicitHeight: 2 * decodedFont.height + 8
            color: "#cc000000"
            radius: 4
            clip: true
            objectName: "macrosDecoded"
            TextMetrics { id: decodedChar; font: decodedLine.font; text: "M" }
            FontMetrics { id: decodedFont; font: decodedLine.font }
            Label {
                id: decodedLine
                anchors.fill: parent
                anchors.margins: 4
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                readonly property int columns: Math.max(8, Math.floor(width / Math.max(1, decodedChar.advanceWidth)))
                // The last two lines' worth, newest at the bottom right.
                text: {
                    const flat = decodedBox.stream.replace(/\s+/g, " ")
                    return flat.length > 0 ? flat.slice(-2 * columns)
                        : (decodedBox.text ? qsTr("%1: nothing decoded yet").arg(Engine.textMode) : qsTr("CW: nothing decoded yet"))
                }
                color: decodedBox.stream.length > 0 ? "#ffffff" : Theme.textDim
                font.family: "monospace"
                font.pixelSize: 15
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                verticalAlignment: Text.AlignBottom
            }
        }

        // CW: the call being worked, as on the CW tab ({HIS} in the
        // memories): the pick, then the calls the decoder heard.
        RowLayout {
            id: workedRow
            Layout.fillWidth: true
            visible: macros.kind === "cw"
                     && (Engine.workedCall.length > 0 || (Engine.cwDecoderEnabled && heardRepeater.count > 0))
            spacing: 4
            Label { text: qsTr("Call"); color: Theme.textDim; font.pixelSize: 12 }
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
                model: macros.kind === "cw" && Engine.cwDecoderEnabled
                       ? Engine.cwCalls.filter(c => c !== Engine.workedCall).slice(0, workedRow.width >= 420 ? 3 : 2)
                       : []
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    font.family: "monospace"
                    focusPolicy: Qt.NoFocus
                    Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
                    onClicked: Engine.workedCall = modelData
                }
            }
            Item { Layout.fillWidth: true }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: macros.columns
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: macros.kind === "cw" ? Engine.cwMemories
                     : macros.kind === "text" ? Engine.textMacros
                     : macros.kind === "voice" ? Engine.dvkSlots : []
                delegate: AppButton {
                    required property int index
                    required property var modelData
                    readonly property bool voiceActive: macros.kind === "voice"
                        && Engine.dvkActiveId === modelData.id && Engine.dvkState !== "idle"
                    objectName: "macro" + index
                    text: macros.kind === "cw" ? "M" + (index + 1) + "  " + modelData
                        : macros.kind === "text" ? (modelData.label.length > 0 ? modelData.label : "F" + (index + 1))
                        : "F" + modelData.id + "  " + modelData.name
                          + (modelData.seconds > 0 ? "  " + modelData.seconds.toFixed(1) + " s" : "")
                    checked: voiceActive
                    opacity: macros.kind !== "voice" || modelData.seconds > 0 || voiceActive ? 1.0 : 0.6
                    focusPolicy: Qt.NoFocus
                    leftPadding: 2
                    rightPadding: 2
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        if (macros.kind === "cw") {
                            Engine.sendCwMemory(index)
                        } else if (macros.kind === "text") {
                            Engine.sendTextMacro(index)
                        } else if (Engine.dvkState === "recording") {
                            Engine.dvkRecord(modelData.id)
                        } else if (Engine.dvkState === "preview") {
                            Engine.dvkPreview(modelData.id)
                        } else if (modelData.seconds > 0 || Engine.dvkState === "playback") {
                            Engine.dvkPlay(modelData.id)
                        } else {
                            macros.notice = qsTr("F%1 is empty: record it on the TX tab (press and hold it there).").arg(modelData.id)
                            noticeTimer.restart()
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: macros.notice.length > 0 ? macros.notice
                : macros.kind === "cw" ? qsTr("Edit a memory on the CW tab: press and hold it.")
                : macros.kind === "text" ? qsTr("Edit a macro on the Digi tab: press and hold it.")
                : qsTr("Record or rename a slot on the TX tab: press and hold it.")
            color: macros.notice.length > 0 ? Theme.warning : Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }
}
