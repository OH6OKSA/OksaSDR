import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A spot alert in the middle of the screen (roadmap 132): a call on the watch
// list or a needed DXCC, spotted from where Spotted from allows. Tune goes
// to the spot (the slice's mode by the spot, as a tap on it does), × shows
// the next one (+N more). The main window shows it in a popup of its own
// above the controls drawer, Setup and the dialogs (roadmap 222: as an item
// of the page it lay under an open drawer, on a phone wholly); that popup
// takes no focus, so the page keeps the keyboard (Space PTT on a tablet).
Rectangle {
    id: banner
    objectName: "spotAlertBanner"
    readonly property var a: Engine.spotAlert
    // An alert to show; the host opens and closes by it.
    readonly property bool wanted: Engine.connectionState === "connected" && !!a.call
    implicitWidth: 640
    implicitHeight: row.implicitHeight + 2 * 8
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.color: Theme.warning
    border.width: 3
    Accessible.role: Accessible.AlertMessage
    Accessible.name: visible ? qsTr("Spot alert: %1 on %2 MHz, %3").arg(a.call).arg(Number(a.mhz || 0).toFixed(3)).arg(a.why) : ""

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 8
        spacing: Theme.spacing / 2
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                objectName: "spotAlertText"
                Layout.fillWidth: true
                text: (banner.a.call || "") + "  " + Number(banner.a.mhz || 0).toFixed(3) + " " + (banner.a.mode || "")
                      + (banner.a.country ? "  ·  " + banner.a.country : "")
                color: Theme.text
                font.pixelSize: Theme.px(19)
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: (banner.a.why || "") + (banner.a.spotter ? qsTr(", spotted by %1").arg(banner.a.spotter) : "")
                      + (banner.a.time ? "  " + banner.a.time + " UTC" : "")
                      + (banner.a.more > 0 ? "  ·  " + qsTr("%1 more").arg(banner.a.more) : "")
                color: Theme.warning
                font.pixelSize: Theme.px(14)
                elide: Text.ElideRight
            }
        }
        AppButton {
            objectName: "spotAlertTune"
            text: qsTr("Tune")
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: Theme.buttonWidth
            onClicked: Engine.tuneSpotAlert()
        }
        AppButton {
            objectName: "spotAlertDismiss"
            text: "×"
            Accessible.name: banner.a.more > 0 ? qsTr("Next alert") : qsTr("Dismiss the alert")
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: Theme.touchTarget
            onClicked: Engine.dismissSpotAlert()
            onPressAndHold: Engine.dismissAllSpotAlerts()
        }
    }
}
