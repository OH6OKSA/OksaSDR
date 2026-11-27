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
    implicitHeight: content.implicitHeight + 2 * 8
    // The list of the other alerts (roadmap 172, the user: a small list of
    // what has been spotted lately, to pick the one that interests most):
    // opened with ▼, closed when the last one goes.
    property bool listOpen: false
    readonly property var queue: Engine.spotAlertQueue
    onQueueChanged: if (queue.length === 0) listOpen = false
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.color: Theme.warning
    border.width: 3
    Accessible.role: Accessible.AlertMessage
    Accessible.name: visible ? qsTr("Spot alert: %1 on %2 MHz, %3").arg(a.call).arg(Number(a.mhz || 0).toFixed(3)).arg(a.why) : ""

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 8
        spacing: Theme.spacing / 2
    RowLayout {
        id: row
        Layout.fillWidth: true
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
        // The other alerts in a list under this one (roadmap 172).
        AppButton {
            objectName: "spotAlertMore"
            visible: banner.queue.length > 0
            text: (banner.listOpen ? "▲ " : "▼ ") + banner.queue.length
            checked: banner.listOpen
            Accessible.name: banner.listOpen ? qsTr("Hide the other alerts") : qsTr("Show the other %1 alerts").arg(banner.queue.length)
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: implicitContentWidth + 20
            onClicked: banner.listOpen = !banner.listOpen
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
    // The other alerts, newest first: a tap tunes to that one and shows it
    // above; Clear all ends them all.
    ListView {
        id: list
        objectName: "spotAlertList"
        visible: banner.listOpen
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 5 * 40)
        clip: true
        interactive: contentHeight > height
        model: banner.listOpen ? banner.queue : []
        delegate: ItemDelegate {
            id: alertRow
            required property var modelData
            required property int index
            objectName: "spotAlertRow" + index
            width: list.width
            height: 40
            padding: 4
            contentItem: RowLayout {
                spacing: Theme.spacing / 2
                Label {
                    text: alertRow.modelData.call || ""
                    color: Theme.text
                    font.pixelSize: Theme.px(16)
                    font.bold: true
                    Layout.preferredWidth: Theme.px(90)
                    elide: Text.ElideRight
                }
                Label {
                    text: Number(alertRow.modelData.mhz || 0).toFixed(3) + " " + (alertRow.modelData.mode || "")
                    color: Theme.text
                    font.pixelSize: Theme.px(15)
                }
                Label {
                    text: (alertRow.modelData.country || "") + "  " + (alertRow.modelData.why || "")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(13)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: alertRow.modelData.time || ""
                    color: Theme.textDim
                    font.pixelSize: Theme.px(13)
                }
            }
            Accessible.name: qsTr("%1 on %2 MHz, %3").arg(alertRow.modelData.call).arg(Number(alertRow.modelData.mhz || 0).toFixed(3)).arg(alertRow.modelData.why || "")
            Accessible.description: qsTr("Tunes to it")
            focusPolicy: Qt.NoFocus
            onClicked: {
                banner.listOpen = false
                Engine.tuneQueuedSpotAlert(alertRow.index)
            }
        }
    }
    AppButton {
        objectName: "spotAlertClearAll"
        visible: banner.listOpen
        text: qsTr("Clear all")
        focusPolicy: Qt.NoFocus
        Layout.alignment: Qt.AlignRight
        onClicked: {
            banner.listOpen = false
            Engine.dismissAllSpotAlerts()
        }
    }
    }
}
