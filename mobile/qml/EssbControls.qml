import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// eSSB: AetherSDR's client-side TX chain (the desktop's Aetherial TX
// chain), this device's microphone processed on the phone or tablet before
// it is sent: gate, EQ (low cut, bass, presence) and compressor. A tab of
// its own, as on the desktop; the radio's own audio settings (its
// equalizer, PROC, the TX filter) stay in Audio and TX.
// Shared by the tablet side panel and the controls drawer.
ColumnLayout {
    id: essb
    spacing: Theme.spacing / 2
    // Never ask for more width than the column gives; rows share what is there.
    implicitWidth: 200
    readonly property bool acts: !Engine.cwMode && Engine.transmitAvailable

    component ChainSlider: RowLayout {
        id: cs
        property string key: ""
        property string toggleKey: ""
        property string title: ""
        property real from: 0
        property real to: 100
        property real step: 1
        property string unit: " dB"
        Layout.fillWidth: true
        AppButton {
            objectName: "essb_" + cs.toggleKey
            visible: cs.toggleKey.length > 0
            text: cs.title
            checked: !!Engine.txChain[cs.toggleKey]
            Layout.preferredWidth: 64
            onClicked: Engine.setTxChain(cs.toggleKey, !checked)
        }
        Label {
            visible: cs.toggleKey.length === 0
            text: cs.title
            color: Theme.textDim
            font.pixelSize: 13
            elide: Text.ElideRight
            Layout.preferredWidth: 64
        }
        FineSlider {
            id: csSlider
            accessibleName: cs.title
            Layout.fillWidth: true
            from: cs.from; to: cs.to; stepSize: cs.step
            value: Engine.txChain[cs.key] || 0
            onMoved: (v) => Engine.setTxChain(cs.key, v)
        }
        Label {
            text: (csSlider.shown > 0 && cs.from < 0 ? "+" : "") + csSlider.shown.toFixed(0) + cs.unit
            color: Theme.textDim
            font.pixelSize: 13
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
        }
    }
    // Where it does not act, why (an empty tab said nothing).
    Label {
        visible: !essb.acts
        Layout.fillWidth: true
        text: Engine.cwMode ? qsTr("eSSB processes speech from this device's microphone; CW does not use it.")
                            : qsTr("eSSB processes this device's microphone while it transmits.")
        color: Theme.textDim
        font.pixelSize: 15
        wrapMode: Text.WordWrap
    }
    ColumnLayout {
        visible: essb.acts
        Layout.fillWidth: true
        spacing: Theme.spacing / 2
        Label {
            Layout.fillWidth: true
            text: qsTr("This device's microphone: processing before it is sent")
            color: Theme.textDim
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }
        ChainSlider { title: qsTr("Gate"); toggleKey: "gateOn"; key: "gateThreshold"; from: -80; to: 0 }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                objectName: "essb_eqOn"
                text: qsTr("EQ")
                checked: !!Engine.txChain.eqOn
                Layout.preferredWidth: 64
                onClicked: Engine.setTxChain("eqOn", !checked)
            }
            Label { text: qsTr("Low cut"); color: Theme.textDim; font.pixelSize: 13 }
            Repeater {
                model: [0, 100, 150, 200, 300]
                delegate: AppButton {
                    required property int modelData
                    text: modelData === 0 ? qsTr("Off") : modelData
                    checked: (Engine.txChain.lowCut || 0) === modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    leftPadding: 2
                    rightPadding: 2
                    onClicked: Engine.setTxChain("lowCut", modelData)
                }
            }
        }
        ChainSlider { title: qsTr("Bass"); key: "bass"; from: -12; to: 12 }
        ChainSlider { title: qsTr("Presence"); key: "presence"; from: -12; to: 12 }
        ChainSlider { title: qsTr("Comp"); toggleKey: "compOn"; key: "compThreshold"; from: -60; to: 0 }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Ratio"); color: Theme.textDim; font.pixelSize: 13; Layout.preferredWidth: 64 }
            Repeater {
                model: [2, 3, 4, 8]
                delegate: AppButton {
                    required property int modelData
                    text: modelData + ":1"
                    checked: Math.round(Engine.txChain.compRatio || 0) === modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onClicked: Engine.setTxChain("compRatio", modelData)
                }
            }
            // The compressor's work while on the air.
            Label {
                text: Engine.radioTransmitting && Engine.txChain.compOn
                      ? Engine.compReductionDb.toFixed(0) + " dB" : ""
                color: Theme.warning
                font.pixelSize: 13
                Layout.preferredWidth: 52
                horizontalAlignment: Text.AlignRight
            }
        }
        ChainSlider { title: qsTr("Makeup"); key: "compMakeup"; from: 0; to: 18 }
    }

    // While this tab shows and the app is in front, the microphone stays
    // open so the Mic meter moves while the gate and compressor are set (as
    // on the TX tab).
    readonly property bool micCheckShown: visible && acts && Qt.application.state === Qt.ApplicationActive
    onMicCheckShownChanged: Engine.holdMicCheck(essb, micCheckShown)
    Component.onCompleted: Engine.holdMicCheck(essb, micCheckShown)
    Component.onDestruction: Engine.holdMicCheck(essb, false)
}
