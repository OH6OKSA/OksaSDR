import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// One Digi macro's editor (roadmap 139: the Digi tab's and the Macros
// panel's press and hold open the same).
Dialog {
    id: macroEditor
    // Named on its item: Accessible on a Popup itself is refused ("must be
    // attached to an object deriving from Item").
    Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => qsTr("Macro F%1: {CALL} = your call, {HIS} = the call being worked, {NAME}, {RST}, {GRID}").arg(macroEditor.index + 1))
    property int index: 0
    property alias label: macroLabel.text
    property alias text: macroText.text
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 24 : 420)
    modal: true
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
    onOpened: macroText.forceActiveFocus()
    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2
        Label {
            text: qsTr("Macro F%1: {CALL} = your call, {HIS} = the call being worked, {NAME}, {RST}, {GRID}").arg(macroEditor.index + 1)
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            Layout.preferredWidth: macroEditor.width - 2 * macroEditor.padding
            wrapMode: Text.WordWrap
        }
        TextField {
            id: macroLabel
            Accessible.name: qsTr("Macro F%1 name").arg(macroEditor.index + 1)
            Layout.fillWidth: true
            placeholderText: qsTr("Name on the button")
            font.pixelSize: Theme.px(16)
        }
        TextArea {
            id: macroText
            Accessible.name: qsTr("Macro F%1 text").arg(macroEditor.index + 1)
            Layout.fillWidth: true
            Layout.preferredHeight: 5 * font.pixelSize * 1.4
            wrapMode: TextEdit.Wrap
            font.pixelSize: Theme.px(16)
            font.family: "monospace"
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            background: Rectangle { color: Theme.surfaceRaised; radius: 4 }
        }
        // What it sends now, the placeholders filled (or what is missing).
        Label {
            objectName: "macroPreview"
            Layout.preferredWidth: macroEditor.width - 2 * macroEditor.padding
            text: qsTr("Sends: %1").arg(Engine.previewText(macroText.text))
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.WordWrap
            maximumLineCount: 4
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            // The default into the fields, to see and Save (or Cancel).
            AppButton {
                objectName: "macroDefault"
                text: qsTr("Default")
                Layout.fillWidth: true
                onClicked: {
                    const d = Engine.defaultTextMacro(macroEditor.index)
                    macroEditor.label = d.label
                    macroEditor.text = d.text
                }
            }
            AppButton { objectName: "macroCancel"; text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: macroEditor.close() }
            AppButton {
                objectName: "macroSave"
                text: qsTr("Save")
                Layout.fillWidth: true
                onClicked: {
                    Engine.setTextMacro(macroEditor.index, macroLabel.text, macroText.text)
                    macroEditor.close()
                }
            }
        }
    }
}
