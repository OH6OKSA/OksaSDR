import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A newer Android beta is on GitHub (roadmap 111): offered once per beta, a
// few seconds after it is found, never while transmitting. Download opens the
// APK in the browser; Android's installer installs it over this one and the
// settings stay. Later: not offered again until the next beta (Setup › App › New
// versions still has Download).
Dialog {
    id: offer
    objectName: "updateOffer"
    // Named on its item: Accessible on a Popup itself is refused ("must be
    // attached to an object deriving from Item").
    Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => Engine.updateSameBeta ? qsTr("A newer build of Android beta %1").arg(Engine.updateBeta) : qsTr("Android beta %1 is out").arg(Engine.updateBeta))
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 24 : 400)
    modal: true
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }

    Timer {
        interval: 4000
        running: Engine.updateOffered && !Engine.radioTransmitting && !offer.opened
        onTriggered: offer.open()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing
        Label {
            Layout.fillWidth: true
            text: Engine.updateSameBeta ? qsTr("A newer build of Android beta %1").arg(Engine.updateBeta)
                                        : qsTr("Android beta %1 is out").arg(Engine.updateBeta)
            Accessible.role: Accessible.Heading
            color: Theme.text
            font.pixelSize: Theme.px(20)
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            // Roadmap 133: a preview's APK has the coming release's number.
            text: Engine.updateSameBeta
                  ? qsTr("This is an earlier build of beta %1 (a preview). Download the released APK and open it: Android installs it over this one, and your settings stay.").arg(Engine.ownBeta)
                  : qsTr("This is beta %1. Download the new APK and open it: Android installs it over this one, and your settings stay.").arg(Engine.ownBeta)
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                objectName: "updateLater"
                text: qsTr("Later")
                Layout.fillWidth: true
                onClicked: { Engine.dismissUpdate(); offer.close() }
            }
            AppButton {
                objectName: "updateNotes"
                text: qsTr("What's new")
                Layout.fillWidth: true
                onClicked: Qt.openUrlExternally(Engine.updateNotesUrl)
            }
            AppButton {
                objectName: "updateDownloadNow"
                text: qsTr("Download")
                Layout.fillWidth: true
                onClicked: {
                    Qt.openUrlExternally(Engine.updateApkUrl)
                    offer.close()
                }
            }
        }
    }
}
