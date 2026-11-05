import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// First-run hints: the gestures nothing on screen shows (the notch hold,
// the dBm scale and divider drags, the slice and SPLIT holds, the
// frequency tap and memory holds), once, a few seconds after the first
// connect, after the setup offer if that shows. View › Hints shows them
// again.
Dialog {
    id: hints
    objectName: "hintsDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 24 : 400)
    modal: true
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }

    property int page: 0
    readonly property var pages: [
        { title: qsTr("A notch"),
          text: qsTr("Press and hold on the display where a notch should go: the notch panel opens, and Add puts it on the radio.") },
        { title: qsTr("The display"),
          text: qsTr("Drag the dBm scale at the left up or down to move the trace. Drag the short bar on the line between the spectrum and the waterfall to share the room; double-tap it for the automatic split.") },
        { title: qsTr("Slices and split"),
          text: qsTr("Press and hold a slice for Make TX slice and Close slice. Press and hold SPLIT to choose the offset.") },
        { title: qsTr("Quick entries"),
          text: qsTr("Tap the frequency to type one. Press and hold a memory, a voice keyer slot or a CW memory to edit it. The Manual has the rest (on a phone, under ⋯).") }
    ]

    function start() {
        page = 0
        open()
    }

    // After the setup offer (5 s) has had its turn; waits while it, or any
    // other panel, is open.
    Timer {
        id: check
        interval: 9000
        running: Engine.connectionState === "connected" && !Engine.hintsShown
        repeat: true
        onTriggered: {
            if (Engine.hintsShown) {
                stop()
            } else if (!hints.visible && hints.parent && !hints.parent.children.some(c => c.visible)) {
                stop()
                hints.start()
            }
        }
    }
    Connections {
        target: Engine
        function onHintsRequested() { hints.start() }
    }
    onClosed: Engine.hintsShown = true

    contentItem: ColumnLayout {
        spacing: Theme.spacing
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: hints.pages[hints.page].title
                color: Theme.text
                font.pixelSize: Theme.px(20)
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                text: qsTr("%1 / %2").arg(hints.page + 1).arg(hints.pages.length)
                color: Theme.textDim
                font.pixelSize: Theme.px(15)
            }
        }
        Label {
            Layout.fillWidth: true
            text: hints.pages[hints.page].text
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                objectName: "hintsSkip"
                text: hints.page + 1 < hints.pages.length ? qsTr("Skip") : qsTr("Back")
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (hints.page + 1 < hints.pages.length) {
                        hints.close()
                    } else {
                        hints.page -= 1
                    }
                }
            }
            AppButton {
                objectName: "hintsNext"
                text: hints.page + 1 < hints.pages.length ? qsTr("Next") : qsTr("Got it")
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (hints.page + 1 < hints.pages.length) {
                        hints.page += 1
                    } else {
                        hints.close()
                    }
                }
            }
        }
    }
}
