import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A voice keyer slot's record, listen, rename and clear (roadmap 139: the
// TX tab's and the Macros panel's press and hold open the same).
Popup {
    id: dvkMenu
    property int slotId: 1
    property string slotName: ""
    function openFor(slot) {
        slotId = slot.id
        slotName = slot.name
        nameField.text = slot.name
        open()
    }
    anchors.centerIn: Overlay.overlay
    modal: true
    margins: Theme.spacing
    // Above the window's unsafe bottom (Android's navigation bar or a
    // OnePlus taskbar; the home indicator), as the pages keep: a tall
    // panel scrolls instead of going under it.
    bottomMargin: Theme.spacing + (parent && parent.Window.contentItem ? parent.Window.contentItem.SafeArea.margins.bottom : 0)
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
    contentItem: PanelContent {
        title: qsTr("F%1  %2").arg(dvkMenu.slotId).arg(dvkMenu.slotName)
        panel: dvkMenu
        rowSpacing: Theme.spacing / 2
        Label {
            Layout.maximumWidth: 320
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Theme.px(13)
            text: Engine.dvkOnRadio
                  ? qsTr("Record speaks into this device's microphone; the radio keeps up to 10 s. "
                         + "Recording and listening do not transmit.")
                  : qsTr("Record speaks into this device's microphone; this device keeps up to 15 s "
                         + "(the radio's own voice keyer needs SmartSDR+). Recording and listening do not transmit.")
        }
        RowLayout {
            AppButton {
                text: Engine.dvkState === "recording" ? qsTr("Stop recording") : qsTr("Record")
                Layout.fillWidth: true
                onClicked: Engine.dvkRecord(dvkMenu.slotId)
            }
            AppButton {
                // The radio's slot, or the app's own clip once recorded.
                readonly property var slot: Engine.dvkSlots[dvkMenu.slotId - 1]
                visible: Engine.dvkOnRadio || (!!slot && slot.seconds > 0)
                enabled: Engine.dvkState === "idle" || Engine.dvkState === "preview"
                text: Engine.dvkState === "preview" ? qsTr("Stop") : qsTr("Listen")
                Layout.fillWidth: true
                onClicked: Engine.dvkPreview(dvkMenu.slotId)
            }
        }
        RowLayout {
            TextField {
                id: nameField
                Accessible.name: qsTr("Voice keyer slot name")
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
            }
            AppButton {
                text: qsTr("Rename")
                onClicked: Engine.dvkRename(dvkMenu.slotId, nameField.text)
            }
        }
        AppButton {
            text: qsTr("Clear")
            leftPadding: 16
            rightPadding: 16
            onClicked: Engine.dvkClear(dvkMenu.slotId)
        }
    }
}
