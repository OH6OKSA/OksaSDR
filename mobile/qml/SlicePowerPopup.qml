import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The power slider a tap on a slice chip's TX tag opens, under the chip, as
// on the TX tab. One for the main screen's chips and Big buttons' (a view
// changes, the features stay).
Popup {
    id: powerPopup
    // An amplifier in Operate: the slider is its drive.
    property bool ampOperating: false
    modal: true
    dim: false
    y: parent ? parent.height + 4 : 0
    width: Math.min(380, Theme.windowWidth - 2 * Theme.spacing)
    margins: Theme.spacing
    bottomMargin: Theme.spacing + (parent && parent.Window.contentItem ? parent.Window.contentItem.SafeArea.margins.bottom : 0)
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
    contentItem: PanelContent {
        title: qsTr("TX power")
        panel: powerPopup
        rowSpacing: Theme.spacing / 2
        Label {
            text: Engine.rfPowerIsPercent ? qsTr("RF power  %1 %").arg(Engine.rfPower) : qsTr("RF power  %1 W").arg(Engine.rfPower)
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
        }
        FineSlider {
            objectName: "slicePowerSlider"
            accessibleName: qsTr("RF power")
            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            value: Engine.rfPower
            enabled: Engine.transmitAvailable || Engine.canUseDeviceMicrophone || Engine.tuneAvailable
            onMoved: (v) => Engine.rfPower = Math.round(v)
        }
        Label {
            Layout.fillWidth: true
            visible: powerPopup.ampOperating || Engine.rfPowerIsPercent
            text: powerPopup.ampOperating ? qsTr("An amplifier is in Operate: this is its drive; its output is on the meters.")
                                          : qsTr("A share of the radio's 500 W; the power out is on the Power meter.")
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.Wrap
        }
    }
}
