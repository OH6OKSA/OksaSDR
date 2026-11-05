import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// eSSB: AetherSDR's client-side TX chain (the desktop's Aetherial TX
// chain), this device's microphone processed on the phone or tablet before
// it is sent: gate, EQ (low cut, bass, presence), compressor, de-esser,
// tube, the EVO exciter and reverb, in an order of one's own. A tab of
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
        property int decimals: 0
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
            font.pixelSize: Theme.px(13)
            elide: Text.ElideRight
            Layout.preferredWidth: Theme.px(64)
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
            text: (csSlider.shown > 0 && cs.from < 0 ? "+" : "") + csSlider.shown.toFixed(cs.decimals) + cs.unit
            color: Theme.textDim
            font.pixelSize: Theme.px(13)
            Layout.preferredWidth: Theme.px(52)
            horizontalAlignment: Text.AlignRight
        }
    }
    // A row of choices for one setting: a title, then the values.
    component ChainChoice: RowLayout {
        id: cc
        property string key: ""
        property string title: ""
        property var values: []
        property var labels: []
        Layout.fillWidth: true
        Label { text: cc.title; color: Theme.textDim; font.pixelSize: Theme.px(13); Layout.preferredWidth: Theme.px(64) }
        Repeater {
            model: cc.values.length
            delegate: AppButton {
                required property int index
                objectName: "essb_" + cc.key + "_" + cc.values[index]
                text: cc.labels[index]
                checked: Math.round(Engine.txChain[cc.key] || 0) === cc.values[index]
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                leftPadding: 2
                rightPadding: 2
                onClicked: Engine.setTxChain(cc.key, cc.values[index])
            }
        }
    }
    readonly property var stageNames: ({
        gate: qsTr("Gate"), eq: qsTr("EQ"), comp: qsTr("Comp"), deess: qsTr("De-ess"),
        tube: qsTr("Tube"), exc: qsTr("EVO"), reverb: qsTr("Reverb")
    })

    // Where it does not act, why (an empty tab said nothing).
    Label {
        visible: !essb.acts
        Layout.fillWidth: true
        text: Engine.cwMode ? qsTr("eSSB processes speech from this device's microphone; CW does not use it.")
                            : qsTr("eSSB processes this device's microphone while it transmits.")
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
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
            font.pixelSize: Theme.px(15)
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
            Label { text: qsTr("Low cut"); color: Theme.textDim; font.pixelSize: Theme.px(13) }
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
            Label { text: qsTr("Ratio"); color: Theme.textDim; font.pixelSize: Theme.px(13); Layout.preferredWidth: Theme.px(64) }
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
                font.pixelSize: Theme.px(13)
                Layout.preferredWidth: Theme.px(52)
                horizontalAlignment: Text.AlignRight
            }
        }
        ChainSlider { title: qsTr("Makeup"); key: "compMakeup"; from: 0; to: 18 }
        // The desktop's other stages: off until turned on.
        ChainSlider { title: qsTr("De-ess"); toggleKey: "deessOn"; key: "deessThreshold"; from: -60; to: 0 }
        ChainChoice { title: qsTr("Band"); key: "deessFreq"; values: [4000, 5000, 6000, 7000]; labels: ["4k", "5k", "6k", "7k"] }
        ChainSlider { title: qsTr("Tube"); toggleKey: "tubeOn"; key: "tubeDrive"; from: 0; to: 24 }
        ChainChoice { title: qsTr("Model"); key: "tubeModel"; values: [0, 1, 2]; labels: ["A", "B", "C"] }
        ChainSlider { title: qsTr("Mix"); key: "tubeMix"; from: 0; to: 100; unit: " %" }
        ChainSlider { title: qsTr("EVO"); toggleKey: "excOn"; key: "excAir"; from: 0; to: 24 }
        ChainSlider { title: qsTr("Bass"); key: "excBass"; from: 0; to: 24 }
        ChainChoice { title: qsTr("Mode"); key: "excMode"; values: [0, 1]; labels: [qsTr("Even"), qsTr("Odd")] }
        ChainSlider { title: qsTr("Reverb"); toggleKey: "reverbOn"; key: "reverbMix"; from: 0; to: 100; unit: " %" }
        ChainSlider { title: qsTr("Decay"); key: "reverbDecay"; from: 0.3; to: 5; step: 0.1; unit: " s"; decimals: 1 }
        // The order the stages run in, and a way to change it.
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Order: %1").arg((Engine.txChain.order || []).map(k => essb.stageNames[k] || k).join(" › "))
                color: Theme.textDim
                font.pixelSize: Theme.px(13)
                wrapMode: Text.Wrap
            }
            AppButton {
                objectName: "essbOrder"
                text: qsTr("Change")
                focusPolicy: Qt.NoFocus
                onClicked: orderPanel.open()
            }
        }
    }

    // The chain's order: each stage with ▲ / ▼ (the desktop drags its boxes).
    Popup {
        id: orderPanel
        objectName: "essbOrderPanel"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: false
        padding: Theme.spacing
        width: Math.min(360, Theme.windowWidth - 2 * Theme.spacing)
        background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
        function move(index, by) {
            const order = (Engine.txChain.order || []).slice()
            const to = index + by
            if (to < 0 || to >= order.length) return
            const k = order[index]
            order[index] = order[to]
            order[to] = k
            Engine.setTxChain("order", order)
        }
        contentItem: PanelContent {
            title: qsTr("Order")
            panel: orderPanel
            Label {
                Layout.fillWidth: true
                text: qsTr("The microphone goes through the stages from the top down; a stage that is off is passed by.")
                color: Theme.textDim
                font.pixelSize: Theme.px(13)
                wrapMode: Text.Wrap
            }
            Repeater {
                model: Engine.txChain.order || []
                delegate: RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Label {
                        text: (index + 1) + ".  " + (essb.stageNames[modelData] || modelData)
                              + (Engine.txChain[modelData + "On"] ? "" : qsTr("  (off)"))
                        color: Engine.txChain[modelData + "On"] ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.px(16)
                        Layout.fillWidth: true
                    }
                    AppButton {
                        objectName: "essbUp_" + modelData
                        text: "▲"
                        enabled: index > 0
                        Layout.preferredWidth: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Move %1 up").arg(essb.stageNames[modelData] || modelData)
                        onClicked: orderPanel.move(index, -1)
                    }
                    AppButton {
                        objectName: "essbDown_" + modelData
                        text: "▼"
                        enabled: index + 1 < (Engine.txChain.order || []).length
                        Layout.preferredWidth: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Move %1 down").arg(essb.stageNames[modelData] || modelData)
                        onClicked: orderPanel.move(index, 1)
                    }
                }
            }
        }
    }

    // While this tab shows and the app is in front, the microphone stays
    // open so the Mic meter moves while the gate and compressor are set (as
    // on the TX tab).
    readonly property bool micCheckShown: visible && acts && Qt.application.state === Qt.ApplicationActive
    onMicCheckShownChanged: Engine.holdMicCheck(essb, micCheckShown)
    Component.onCompleted: Engine.holdMicCheck(essb, micCheckShown)
    Component.onDestruction: Engine.holdMicCheck(essb, false)
}
