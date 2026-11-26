import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// An offset typed (roadmap 80, Sebastian Schumann DG5CST): the RIT or XIT,
// or a split's distance. kHz with a decimal ("1.34", "-0.5", a comma too),
// a whole number of 10 or more as Hz ("1340"); quick values; Reset; Done.
// Return applies, Esc closes.
Popup {
    id: editor
    // Named on its item: Accessible on a Popup itself is refused ("must be
    // attached to an object deriving from Item").
    Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => editor.title)
    objectName: "offsetEditor"
    property string title: ""
    property int limitHz: 9999
    property bool showReset: true
    property var quick: [-1000, 1000, 2000, 3000, 5000]
    signal applied(int hz)
    function ask(heading, hz) {
        title = heading
        field.text = hz === 0 ? "" : (hz / 1000).toFixed(3).replace(/\.?0+$/, "")
        error.text = ""
        open()
        field.forceActiveFocus()
        field.selectAll()
    }
    function parseHz(text) {
        const t = text.trim().replace(",", ".").replace(/\s*k?hz$/i, "")
        if (!/^[+-]?\d+(\.\d+)?$/.test(t))
            return NaN
        const v = parseFloat(t)
        return t.indexOf(".") >= 0 || Math.abs(v) < 10 ? Math.round(v * 1000) : Math.round(v)
    }
    function apply(hz) {
        if (isNaN(hz)) {
            error.text = qsTr("Type the offset in kHz, e.g. 1.34 or -0.5.")
            return
        }
        if (Math.abs(hz) > limitHz) {
            error.text = qsTr("Up to ±%1 kHz.").arg((limitHz / 1000).toFixed(3))
            return
        }
        applied(hz)
        close()
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(400, parent ? parent.width - 24 : 360)
    modal: true
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing
        Label {
            Layout.fillWidth: true
            text: editor.title
            Accessible.role: Accessible.Heading
            color: Theme.text
            font.pixelSize: Theme.px(18)
            font.bold: true
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: field
                objectName: "offsetField"
                Accessible.name: qsTr("Offset in kHz")
                Layout.fillWidth: true
                // Sized to what it holds, about 8 characters (roadmap 158)
                Layout.maximumWidth: Math.ceil(8 * font.pixelSize * 0.62) + leftPadding + rightPadding
                placeholderText: qsTr("kHz, e.g. 1.34")
                font.pixelSize: Theme.px(20)
                inputMethodHints: Qt.ImhFormattedNumbersOnly | Qt.ImhNoPredictiveText
                // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
                EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
                onAccepted: editor.apply(editor.parseHz(text))
            }
            Label { text: qsTr("kHz"); color: Theme.textDim; font.pixelSize: Theme.px(16) }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: editor.quick
                delegate: AppButton {
                    required property int modelData
                    objectName: "offsetQuick" + (modelData < 0 ? "Minus" : "Plus") + Math.abs(modelData / 1000)
                    text: (modelData > 0 ? "+" : "−") + Math.abs(modelData / 1000)
                    focusPolicy: Qt.NoFocus
                    width: Math.max(Theme.buttonWidth * 0.8, 56)
                    height: Theme.touchTarget
                    onClicked: editor.apply(modelData)
                }
            }
        }
        Label {
            id: error
            objectName: "offsetError"
            visible: text.length > 0
            Layout.fillWidth: true
            color: Theme.warning
            font.pixelSize: Theme.px(13)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                objectName: "offsetReset"
                visible: editor.showReset
                text: qsTr("Reset")
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                onClicked: editor.apply(0)
            }
            AppButton { objectName: "offsetCancel"; text: qsTr("Cancel"); focusPolicy: Qt.NoFocus; Layout.fillWidth: true; onClicked: editor.close() }
            AppButton {
                objectName: "offsetDone"
                text: qsTr("Done")
                checked: true
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                onClicked: editor.apply(editor.parseHz(field.text))
            }
        }
    }
}
