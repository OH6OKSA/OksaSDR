import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Recent app log for testers: Send to the developer mails it, Share gives it
// to any other app.
Popup {
    id: popup
    objectName: "logPage"
    modal: true
    anchors.centerIn: parent
    width: parent.width * 0.95
    height: parent.height * 0.85
    background: Rectangle { color: Theme.surface; radius: Theme.radius }
    // Send to the developer's outcome: -1 not tried, 0 nothing shown, 1 a
    // mail app, 2 the share sheet (no mail account, the address copied).
    property int sent: -1
    // Clear and the developer link, for the developer's own testing: a press
    // and hold on the title shows them (the link's row stays while it is on).
    property bool devTools: false
    onOpened: sent = -1
    // One top row where it fits; on a phone upright the actions go under it.
    readonly property bool wide: width >= 560
    // A phone on its side: the description only for a notice, so the log
    // keeps its lines.
    readonly property bool low: height < 500
    readonly property string sendNote: sent === 2 ? qsTr("No Mail account: the address %1 is copied, paste it as the recipient.").arg(LogBuffer.developerAddress)
        : sent === 0 ? qsTr("No mail could be opened. The address %1 is copied.").arg(LogBuffer.developerAddress)
        : LogBuffer.previousRun.length > 0 ? qsTr("A mail to %1 with this log and the previous run's attached.").arg(LogBuffer.developerAddress)
        : qsTr("A mail to %1 with this log attached.").arg(LogBuffer.developerAddress)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "logTitle"
                text: qsTr("Log (%1 lines)").arg(popup.opened ? LogBuffer.lines.length : 0)
                color: Theme.text
                font.pixelSize: Theme.px(22)
                elide: Text.ElideRight
                Layout.fillWidth: true
                TapHandler { onLongPressed: popup.devTools = !popup.devTools }
            }
            AppButton {
                objectName: "sendToDeveloperTop"
                visible: popup.wide
                text: qsTr("Send to the developer")
                onClicked: popup.sent = LogBuffer.sendToDeveloper()
            }
            AppButton { visible: popup.wide; text: qsTr("Share"); onClicked: LogBuffer.share() }
            AppButton { text: qsTr("Close"); onClicked: popup.close() }
        }

        // A mail to the developer with the address filled in and this run's
        // and the previous run's log attached (Dennis Daniel KM6DF); Share
        // for anything else (Copy, AirDrop, Messages are in its sheet).
        RowLayout {
            Layout.fillWidth: true
            visible: !popup.wide
            AppButton {
                objectName: "sendToDeveloper"
                text: qsTr("Send to the developer")
                onClicked: popup.sent = LogBuffer.sendToDeveloper()
            }
            AppButton { text: qsTr("Share"); onClicked: LogBuffer.share() }
        }
        Label {
            Layout.fillWidth: true
            visible: !popup.wide || !popup.low || popup.sent === 0 || popup.sent === 2
            text: popup.sendNote
            color: popup.sent === 0 ? Theme.warning : Theme.textDim
            font.pixelSize: Theme.px(15)
            wrapMode: Text.Wrap
        }

        // After a crash: the previous run's log (kept in a file as it ran,
        // ending with the crash and the call stack) goes along with the mail.
        Label {
            Layout.fillWidth: true
            visible: LogBuffer.previousRunCrashed
            text: qsTr("The previous run ended in a crash: Send to the developer sends its log too.")
            color: Theme.transmit
            font.pixelSize: Theme.px(15)
            wrapMode: Text.Wrap
        }

        // Developer link (off by default): streams this log to a developer's
        // PC and lets it take screenshots. It cannot transmit. On a phone
        // upright the address has a row of its own: beside the rest it was
        // squeezed too narrow to read.
        component HostField: TextField {
            Accessible.name: qsTr("Developer link PC address")
            Layout.fillWidth: true
            text: DevLink.host
            placeholderText: qsTr("PC address")
            inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
            font.pixelSize: Theme.px(15)
            onEditingFinished: DevLink.host = text
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: popup.devTools || DevLink.enabled
            spacing: Theme.spacing
            RowLayout {
                Layout.fillWidth: true
                Switch {
                    id: devSwitch
                    checked: DevLink.enabled
                    onToggled: DevLink.enabled = checked
                }
                Label {
                    text: qsTr("Developer link")
                    color: Theme.text
                    font.pixelSize: Theme.px(15)
                    elide: Text.ElideRight
                    Layout.fillWidth: !popup.wide
                }
                HostField { visible: popup.wide }
                Label {
                    text: DevLink.status
                    color: DevLink.status === "connected" ? Theme.accent : Theme.textDim
                    font.pixelSize: Theme.px(13)
                    elide: Text.ElideRight
                    Layout.maximumWidth: 160
                }
                // Empties the log, so the next report starts from a known point.
                AppButton { text: qsTr("Clear"); onClicked: LogBuffer.clear() }
            }
            HostField { visible: !popup.wide }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // A bar at the right while the log is longer than the view.
            ScrollBar.vertical: AppScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            // Only while the page is open: every new line replaces the model,
            // and rebuilding wrapped text rows is main-thread work.
            model: popup.opened ? LogBuffer.lines : []
            onCountChanged: positionViewAtEnd()
            delegate: Text {
                required property string modelData
                width: ListView.view.width
                text: modelData
                color: modelData.indexOf("PTT") >= 0 || modelData.indexOf("zoom") >= 0
                       ? Theme.warning : Theme.textDim
                font.family: "monospace"
                font.pixelSize: Theme.px(12)
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
