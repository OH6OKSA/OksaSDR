import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive filter and DSP, as on SmartSDR/AetherSDR: the mode's eight
// widths (the active one checked; picking one closes), the radio's DSP
// toggles plus the app's beat cancel (stay open: several are often set
// together), and a level slider for each DSP that is on.
Popup {
    id: chooser
    objectName: "filterChooser"
    modal: true
    dim: false
    readonly property real cellWidth: Math.max(Theme.buttonWidth * 0.8, 60)
    function label(hz) {
        if (hz >= 1000) {
            return (hz / 1000).toFixed(1).replace(/\.0$/, "") + "k"
        }
        return String(hz)
    }
    margins: Theme.spacing
    padding: Theme.spacing
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.surfaceRaised
        radius: Theme.radius
    }
    // Close for the DSP toggles, which keep it open (Larry Averbeck).
    contentItem: PanelContent {
        panel: chooser
        title: Engine.filterPresets.length === 0 ? qsTr("DSP")
               : Engine.rade ? qsTr("%1 filter: RADE uses 750–2250 Hz; widths centre on it").arg(Engine.mode)
                             : qsTr("%1 filter").arg(Engine.mode)
        // The four columns' width less Close's: the RADE line wraps rather
        // than widening the panel.
        titleWidth: chooser.cellWidth * 4 + 12 - Theme.buttonWidth * 1.5 - Theme.spacing / 2
        rowSpacing: Theme.spacing / 2
        GridLayout {
            columns: Math.min(4, Math.max(1, Engine.filterPresets.length))
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: Engine.filterPresets
                delegate: AppButton {
                    required property int modelData
                    text: chooser.label(modelData)
                    checked: Engine.activeFilterPreset === modelData
                    focusPolicy: Qt.NoFocus
                    Layout.preferredWidth: chooser.cellWidth
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        Engine.setFilterWidth(modelData)
                        chooser.close()
                    }
                }
            }
        }
        Label {
            text: qsTr("DSP")
            color: Theme.textDim
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
        }
        // Toggles stay open: several are often set together.
        GridLayout {
            columns: 4
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: Engine.dspKeys
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    // Re-read on every DSP change (dspOn is a method).
                    checked: Engine.dspKeys.length >= 0 && Engine.dspOn(modelData)
                    // BC only works on voice; in CW and data it shows idle.
                    enabled: modelData !== "BC" || Engine.beatCancelAvailable
                    focusPolicy: Qt.NoFocus
                    Layout.preferredWidth: chooser.cellWidth
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: Engine.setDsp(modelData, !checked)
                }
            }
        }
        // Levels of the DSP that is on (the desktop's DSP popups): a slider
        // each, sent when released.
        Repeater {
            model: Engine.dspKeys.filter(k => Engine.dspOn(k) && Engine.dspHasLevel(k))
            delegate: RowLayout {
                id: levelRow
                required property string modelData
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                Label {
                    text: levelRow.modelData
                    color: Theme.text
                    font.pixelSize: Math.round(Theme.buttonFont * 0.85)
                    Layout.preferredWidth: chooser.cellWidth * 0.7
                }
                // Sent once the finger lifts (the radio's DSP level), each
                // − / + step on its own.
                FineSlider {
                    id: levelSlider
                    Layout.fillWidth: true
                    Layout.minimumWidth: chooser.cellWidth * 3
                    from: 0; to: 100; stepSize: 1
                    liveDrag: false
                    value: Engine.dspKeys.length >= 0 ? Engine.dspLevel(levelRow.modelData) : 0
                    onMoved: (v) => Engine.setDspLevel(levelRow.modelData, Math.round(v))
                }
                Label {
                    text: Math.round(levelSlider.shown)
                    color: Theme.textDim
                    font.pixelSize: Math.round(Theme.buttonFont * 0.85)
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: chooser.cellWidth * 0.5
                }
            }
        }
    }
}
