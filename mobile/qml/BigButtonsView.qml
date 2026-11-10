import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Big buttons (roadmap 55, Michael Walker): the operating page covered by a
// few large controls for the car (mounted, used as the law allows), gloves
// or a glance: the frequency and mode large, the S meter as a bar, − and +
// filling the middle (one tuning step a tap, repeating while held), the step
// (a tap takes the next one), and PTT across the bottom. It lies over the
// page rather than in a popup, so the page keeps the keyboard: Space and a
// Bluetooth PTT button key, the arrows tune. Out by holding Exit, so a stray
// tap does not leave it.
Rectangle {
    id: big
    color: Theme.background
    readonly property bool landscape: width > height * 1.2
    // The page's formatter (14.074.000).
    property var formatMhz: (mhz) => mhz.toFixed(6)
    // The page's safe area, given by the page.
    property real marginLeft: 0
    property real marginRight: 0
    property real marginTop: 0
    property real marginBottom: 0
    // S units from dBm (S9 = -73 dBm, 6 dB a unit), as the S meter.
    readonly property real dbm: Engine.sMeterDbm
    readonly property string sText: dbm >= -73 ? "S9+" + Math.round(dbm + 73) : "S" + Math.max(0, Math.floor((dbm + 127) / 6))
    function stepText(hz) {
        return hz >= 1000 ? (hz / 1000) + " kHz" : hz + " Hz"
    }
    function nextStep() {
        const list = Engine.stepList.slice().sort((a, b) => a - b)
        if (list.length === 0) return
        const i = list.indexOf(Engine.stepHz)
        Engine.setStepHz(list[(i + 1) % list.length])
    }

    // Touches never reach the page under it.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    // One step down or up a tap, repeating while held.
    component TuneButton: AppButton {
        id: tune
        property int direction: 1
        objectName: direction > 0 ? "bigTuneUp" : "bigTuneDown"
        text: direction > 0 ? "+" : "−"
        Accessible.name: direction > 0 ? qsTr("Tune up one step") : qsTr("Tune down one step")
        font.pixelSize: Math.min(width, height) * 0.45
        font.bold: true
        autoRepeat: true
        autoRepeatDelay: 400
        autoRepeatInterval: 150
        focusPolicy: Qt.NoFocus
        Layout.fillWidth: true
        Layout.fillHeight: true
        onClicked: {
            Engine.tuneBySteps(direction)
            Engine.haptic("step")
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing + big.marginLeft
        anchors.rightMargin: Theme.spacing + big.marginRight
        anchors.topMargin: Theme.spacing + big.marginTop
        anchors.bottomMargin: Theme.spacing + big.marginBottom
        columns: big.landscape ? 3 : 1
        columnSpacing: Theme.spacing
        rowSpacing: Theme.spacing

        // Landscape: − at the left, the rest in the middle, + at the right.
        TuneButton {
            visible: big.landscape
            direction: -1
            Layout.horizontalStretchFactor: 2
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.horizontalStretchFactor: 3
            spacing: Theme.spacing
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    id: exitButton
                    objectName: "bigExit"
                    text: qsTr("Exit")
                    Accessible.name: qsTr("Exit big buttons")
                    Accessible.description: qsTr("Hold to exit")
                    focusPolicy: Qt.NoFocus
                    onPressAndHold: Engine.bigButtons = false
                    onClicked: holdHint.visible = true
                }
                Label {
                    id: holdHint
                    visible: false
                    text: qsTr("hold to exit")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(14)
                    Timer { running: holdHint.visible; interval: 2500; onTriggered: holdHint.visible = false }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: Engine.mode
                    color: Theme.accent
                    font.pixelSize: Theme.px(28)
                    font.bold: true
                }
            }
            Label {
                objectName: "bigFrequency"
                Layout.fillWidth: true
                text: big.formatMhz(Engine.frequencyMhz)
                color: Engine.transmitting ? Theme.transmit : Theme.text
                font.pixelSize: big.landscape ? Math.min(big.height * 0.16, 96) : Math.min(big.width * 0.17, 96)
                font.bold: true
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 24
                horizontalAlignment: Text.AlignHCenter
                Accessible.name: qsTr("Frequency %1 kHz").arg((Engine.frequencyMhz * 1000).toFixed(3))
            }
            // The S meter as a bar, its value at the right.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.touchTarget * 0.5
                    radius: 4
                    color: Theme.surface
                    Rectangle {
                        height: parent.height
                        width: parent.width * Math.max(0, Math.min(1, (big.dbm + 127) / 94))
                        radius: 4
                        color: big.dbm >= -73 ? Theme.transmit : Theme.accent
                    }
                }
                Label {
                    text: big.sText
                    color: Theme.text
                    font.pixelSize: Theme.px(24)
                    font.bold: true
                    Layout.preferredWidth: implicitWidth
                }
            }
            // Portrait: − and + side by side in the middle.
            RowLayout {
                visible: !big.landscape
                Layout.fillWidth: true
                Layout.fillHeight: true
                // The rest of the height: about 55 % to − and +, 45 % to PTT.
                Layout.verticalStretchFactor: 11
                spacing: Theme.spacing
                TuneButton { direction: -1 }
                TuneButton { direction: 1 }
            }
            AppButton {
                objectName: "bigStep"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget * 1.3
                text: qsTr("Step %1").arg(big.stepText(Engine.stepHz))
                Accessible.description: qsTr("Tap for the next tuning step")
                font.pixelSize: Math.max(Theme.px(22), big.height * 0.03)
                focusPolicy: Qt.NoFocus
                onClicked: big.nextStep()
            }
            PttButton {
                objectName: "bigPtt"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.verticalStretchFactor: 9
                Layout.preferredHeight: Theme.touchTarget * 2
                // Large enough to read on a tablet's large button too.
                fontPx: Math.round(Math.max(40, Math.min(80, big.height * 0.06)))
            }
        }
        TuneButton {
            visible: big.landscape
            direction: 1
            Layout.horizontalStretchFactor: 2
        }
    }
}
