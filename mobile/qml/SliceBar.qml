import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The slice row: the two selectable meters at its ends and the slice chips
// between them, two side by side (four slices make 2 x 2, also on an
// iPhone). A chip ("A 14.100 USB" with a red TX tag on the transmit slice)
// selects on tap; press and hold opens make-TX / close. "+ Slice" and
// "+ Pan" are at the bottom right of the waterfall.
RowLayout {
    id: bar
    spacing: Theme.spacing / 2
    property bool compact: false
    // The right meter's width: the RF GAIN label under it matches it.
    readonly property real rightMeterWidth: rightMeter.width
    // The page gives the row its width (fillWidth); its contents must not
    // ask for more, as the meters size themselves from that width.
    Layout.preferredWidth: 0
    Layout.minimumWidth: 0

    // Chip height: two rows fit beside a meter where there is height.
    readonly property real chipHeight: Theme.touchTarget
    // Meter dial height: two chip rows on a phone, more on a tablet; low
    // (iPhone landscape) keeps it small.
    readonly property real dialHeight: Theme.tightHeight ? 40
                                     : Theme.compactHeight ? Theme.touchTarget * 1.3
                                     : Theme.phoneLayout ? 2 * chipHeight + 4 - 8
                                     : Theme.touchTarget * 2.3
    // The chips' column keeps room for two chips side by side.
    readonly property real chipMinWidth: Theme.phoneLayout ? 96 : 150
    readonly property real chipMaxWidth: Theme.phoneLayout ? 140 : 210
    // Room for each meter: what two chip columns leave (an analog face, so
    // one slice does not make it taller), and for a bar face what the chip
    // columns in use leave, so bars reach into the free width. From the
    // column the row sits in: the row's own width is the layout's output.
    readonly property int chipColumns: Engine.slices.length > 1 ? 2 : 1
    function meterRoom(columns, cap) {
        return Math.max(80, Math.min(cap,
            ((bar.parent ? bar.parent.width : 0) - columns * chipMinWidth - (columns - 1) * 4 - 4 * bar.spacing) / 2))
    }
    readonly property real meterMaxWidth: meterRoom(2, 240)
    readonly property real barMaxWidth: meterRoom(chipColumns, 320)

    // A meter in the two-bar style is two bars, one over the other, within
    // the dial's height (three or four readings at once: e.g. SWR and Mic
    // at the left, Power at the right, in transmit). One bar uses the
    // dial's height up to what its scale needs.
    readonly property real stackBarHeight: Math.max(22, (bar.dialHeight - 4) / 2 - 8)
    readonly property real singleBarHeight: Math.max(22, Math.min(bar.dialHeight - 8, 52))
    ColumnLayout {
        Layout.alignment: Qt.AlignTop
        spacing: 4
        MeterBadge {
            side: "left"
            dialHeight: bar.dialHeight
            maxWidth: bar.meterMaxWidth
            barMaxWidth: bar.barMaxWidth
            barHeight: Engine.meterStyleLeft === "two" ? bar.stackBarHeight : bar.singleBarHeight
        }
        MeterBadge {
            objectName: "meterLeft2"
            side: "left2"
            visible: Engine.meterStyleLeft === "two"
            dialHeight: bar.dialHeight
            maxWidth: bar.meterMaxWidth
            barMaxWidth: bar.barMaxWidth
            barHeight: bar.stackBarHeight
        }
    }

    Item {
        id: chipArea
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: chips.implicitHeight
        GridLayout {
            id: chips
            anchors.horizontalCenter: parent.horizontalCenter
            // Two columns of equal chips (one for a single slice), centred
            // between the meters.
            readonly property real cell: Math.max(0, Math.min(bar.chipMaxWidth,
                (chipArea.width - (bar.chipColumns - 1) * columnSpacing) / bar.chipColumns))
            width: bar.chipColumns * cell + (bar.chipColumns - 1) * columnSpacing
            columns: 2
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: Engine.slices
                delegate: AppButton {
                    id: chip
                    required property var modelData
                    Layout.preferredWidth: chips.cell
                    Layout.preferredHeight: bar.chipHeight
                    checked: modelData.active
                    leftPadding: 4
                    rightPadding: 4
                    font.pixelSize: bar.compact ? Math.round(Theme.buttonFont * 0.9) : Theme.buttonFont
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.selectSlice(modelData.id)
                    // Under the pressed chip, not at the last pointer position.
                    onPressAndHold: sliceMenu.popup(chip, 0, chip.height)
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 5
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                // Compact (phone): the mode is beside the frequency already.
                                text: chip.modelData.letter + " " + chip.modelData.frequency.toFixed(3)
                                      + (bar.compact ? "" : " " + chip.modelData.mode)
                                font: chip.font
                                color: chip.checked ? Theme.buttonTextOn : Theme.buttonText
                            }
                            // The transmit slice: a red tag, not just two letters.
                            Rectangle {
                                visible: chip.modelData.tx
                                anchors.verticalCenter: parent.verticalCenter
                                width: txText.implicitWidth + 8
                                height: txText.implicitHeight + 4
                                radius: 4
                                color: Theme.transmit
                                Text {
                                    id: txText
                                    anchors.centerIn: parent
                                    text: "TX"
                                    font.pixelSize: chip.font.pixelSize
                                    font.bold: true
                                    color: "#ffffff"
                                }
                            }
                        }
                    }
                    Menu {
                        id: sliceMenu
                        modal: true
                        dim: false
                        MenuItem {
                            text: qsTr("Make TX slice")
                            enabled: !chip.modelData.tx
                            onTriggered: Engine.makeTxSlice(chip.modelData.id)
                        }
                        MenuItem {
                            text: qsTr("Close slice")
                            enabled: Engine.slices.length > 1
                            onTriggered: Engine.removeSlice(chip.modelData.id)
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        Layout.alignment: Qt.AlignTop
        spacing: 4
        MeterBadge {
            id: rightMeter
            side: "right"
            dialHeight: bar.dialHeight
            maxWidth: bar.meterMaxWidth
            barMaxWidth: bar.barMaxWidth
            barHeight: Engine.meterStyleRight === "two" ? bar.stackBarHeight : bar.singleBarHeight
        }
        MeterBadge {
            objectName: "meterRight2"
            side: "right2"
            visible: Engine.meterStyleRight === "two"
            dialHeight: bar.dialHeight
            maxWidth: bar.meterMaxWidth
            barMaxWidth: bar.barMaxWidth
            barHeight: bar.stackBarHeight
        }
    }
}
