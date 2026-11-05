import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The slice's mode next to the frequency: tap to choose another from the
// app's buttons (the current one is the dark, checked button). With the
// band plan lock on, PLAN beside it (a tap turns the lock off), the modes
// the plan does not use here dimmed (a tap says why), and the lock's switch
// under the modes.
Item {
    id: modeButton
    property int fontSize: 32
    // On a phone (the small mode) PLAN goes under the mode, in the touch
    // target's spare height, so the row keeps its width.
    readonly property bool compact: fontSize < 24
    implicitWidth: row.implicitWidth + (planBadge.visible && !compact ? planBadge.width + 6 : 0)
    implicitHeight: Math.max(row.implicitHeight, Theme.touchTarget)
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Mode %1").arg(Engine.mode)
    Accessible.onPressAction: chooser.open()

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Label {
            Accessible.ignored: true
            text: Engine.mode
            color: Theme.accent
            font.pixelSize: modeButton.fontSize
        }
        Caret {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
            size: Math.max(12, Math.round(modeButton.fontSize * 0.45))
        }
    }
    TapHandler { onTapped: chooser.open() }

    Rectangle {
        id: planBadge
        objectName: "bandPlanBadge"
        visible: Engine.bandPlanLock
        x: modeButton.compact ? Math.round((row.implicitWidth - width) / 2) : row.implicitWidth + 6
        y: modeButton.compact ? row.y + row.implicitHeight - 1 : row.y + Math.round((row.implicitHeight - height) / 2)
        width: planText.implicitWidth + (modeButton.compact ? 6 : 8)
        height: planText.implicitHeight + (modeButton.compact ? 0 : 4)
        radius: 3
        color: "transparent"
        border.color: Theme.warning
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Band plan lock on")
        Accessible.description: qsTr("Turns the band plan lock off")
        Accessible.onPressAction: Engine.bandPlanLock = false
        Label {
            id: planText
            anchors.centerIn: parent
            text: qsTr("PLAN")
            color: Theme.warning
            font.pixelSize: modeButton.compact ? 9 : Math.max(10, Math.round(modeButton.fontSize * 0.42))
            font.bold: true
        }
        TapHandler {
            margin: 8
            onTapped: Engine.bandPlanLock = false
        }
    }

    Popup {
        id: chooser
        objectName: "modeChooser"
        modal: true
        dim: false
        y: modeButton.height + 4
        margins: Theme.spacing
        padding: Theme.spacing
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacing / 2
            GridLayout {
                id: modeGrid
                columns: Math.min(4, Math.max(1, Engine.modes.length))
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: Engine.modes
                    delegate: AppButton {
                        required property string modelData
                        readonly property bool planAllows: Engine.bandPlanModes.length === 0
                                                           || Engine.bandPlanModes.indexOf(modelData) >= 0
                        text: modelData
                        checked: Engine.mode === modelData
                        opacity: planAllows ? 1.0 : 0.4
                        focusPolicy: Qt.NoFocus
                        Accessible.description: planAllows ? "" : qsTr("Not used here by the band plan")
                        Layout.preferredWidth: Math.max(Theme.buttonWidth * 0.9, 64)
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            Engine.chooseMode(modelData)
                            chooser.close()
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: modeGrid.implicitWidth
                Label {
                    text: qsTr("Band plan lock")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(15)
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                AppButton {
                    objectName: "bandPlanLockChooser"
                    text: Engine.bandPlanLock ? qsTr("On") : qsTr("Off")
                    checked: Engine.bandPlanLock
                    leftPadding: 10
                    rightPadding: 10
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.bandPlanLock = !Engine.bandPlanLock
                }
            }
        }
    }
}
