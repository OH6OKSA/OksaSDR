import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// One CW memory's editor (roadmap 139: the CW tab's and the Macros panel's
// press and hold open the same).
Dialog {
    id: memoryEditor
    // Named on its item: Accessible on a Popup itself is refused ("must be
    // attached to an object deriving from Item").
    Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => qsTr("Memory M%1   ({CALL} = your callsign, {HIS} = the call being worked)").arg(memoryEditor.index + 1))
    property int index: 0
    property alias text: memoryField.text
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(420, parent ? parent.width - 24 : 400)
    modal: true
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
    onOpened: memoryField.forceActiveFocus()
    contentItem: ColumnLayout {
        spacing: Theme.spacing
        Label {
            text: qsTr("Memory M%1   ({CALL} = your callsign, {HIS} = the call being worked)").arg(memoryEditor.index + 1)
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            // From the popup's fixed width: wrapped, not past its edge.
            Layout.preferredWidth: memoryEditor.width - 2 * memoryEditor.padding
            wrapMode: Text.WordWrap
        }
        TextField {
            id: memoryField
            Accessible.name: qsTr("Memory M%1 text").arg(memoryEditor.index + 1)
            Layout.fillWidth: true
            font.pixelSize: Theme.px(17)
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
            EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
            onAccepted: save.clicked()
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton { text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: memoryEditor.close() }
            AppButton {
                id: save
                text: qsTr("Save")
                Layout.fillWidth: true
                onClicked: {
                    Engine.setCwMemory(memoryEditor.index, memoryField.text)
                    memoryEditor.close()
                }
            }
        }
    }
}
