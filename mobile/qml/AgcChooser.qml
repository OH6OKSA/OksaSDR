import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// AGC from the info panel: the mode (off/slow/med/fast; picking one keeps
// the popup open), the AGC threshold (AGC-T) and the panadapter's RF gain,
// each slider with − / + for the last step.
Popup {
    id: chooser
    objectName: "agcChooser"
    modal: true
    dim: false
    margins: Theme.spacing
    padding: Theme.spacing
    // From the window, not the parent: in the wrapped phone header the info
    // panel is in pieces a few labels wide (beta 45 squeezed the AGC modes
    // and sliders into one). The panadapter's RF gain sets its own.
    width: Math.min(420, Theme.windowWidth - 2 * Theme.spacing)
    // One label width for both slider rows, wide enough for a negative RF
    // gain ("RF gain  -10 dB" ran into the − button at 90).
    readonly property real labelWidth: Math.max(90, Math.ceil(widestLabel.width) + 4)
    TextMetrics {
        id: widestLabel
        text: qsTr("RF gain  %1 dB").arg(Math.min(Engine.rfGainLow, -10))
        font.pixelSize: 14
    }
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.surfaceRaised
        radius: Theme.radius
    }
    contentItem: PanelContent {
        title: qsTr("AGC")
        panel: chooser
        rowSpacing: Theme.spacing / 2
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: Engine.agcModes
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    checked: Engine.agcMode === modelData
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: Engine.agcMode = modelData
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("AGC-T  %1").arg(Math.round(agcT.shown))
                color: Theme.textDim
                font.pixelSize: 14
                Layout.preferredWidth: chooser.labelWidth
            }
            FineSlider {
                id: agcT
                Layout.fillWidth: true
                enabled: Engine.connectionState === "connected" && Engine.agcMode !== "off"
                from: 0; to: 100; stepSize: 1
                value: Engine.agcThreshold
                onMoved: (v) => Engine.agcThreshold = Math.round(v)
            }
        }
        // RF gain (preamp / attenuator) of the panadapter, in the radio's steps.
        RowLayout {
            Layout.fillWidth: true
            visible: Engine.connectionState === "connected"
            Label {
                text: qsTr("RF gain  %1 dB").arg(Math.round(rfGain.shown))
                color: Theme.textDim
                font.pixelSize: 14
                Layout.preferredWidth: chooser.labelWidth
            }
            FineSlider {
                id: rfGain
                Layout.fillWidth: true
                from: Engine.rfGainLow
                to: Engine.rfGainHigh
                stepSize: Engine.rfGainStep
                value: Engine.rfGain
                onMoved: (v) => Engine.rfGain = Math.round(v)
            }
        }
    }
}
