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
    readonly property real rightMeterWidth: rightMeters.width
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
    // Two analog faces side by side (the "analog2" style, on an iPad or a
    // Mac): each gets half the room the chip columns leave. Where that would
    // make the faces low, the same two readings are two bars, one over the
    // other.
    readonly property real pairMaxWidth: (meterRoom(chipColumns, 2 * 240 + 4) - 4) / 2
    readonly property bool pairFits: Math.min(dialHeight, (pairMaxWidth - 8) / 1.6) >= 60
    readonly property bool leftPair: Engine.meterStyleLeft === "analog2" && pairFits
    readonly property bool rightPair: Engine.meterStyleRight === "analog2" && pairFits
    readonly property bool leftStack: Engine.meterStyleLeft === "two" || Engine.meterStyleLeft === "analog2" && !pairFits
    readonly property bool rightStack: Engine.meterStyleRight === "two" || Engine.meterStyleRight === "analog2" && !pairFits

    // A meter in the two-bar style is two bars, one over the other, within
    // the dial's height (three or four readings at once: e.g. SWR and Mic
    // at the left, Power at the right, in transmit). One bar uses the
    // dial's height up to what its scale needs.
    readonly property real stackBarHeight: Math.max(22, (bar.dialHeight - 4) / 2 - 8)
    readonly property real singleBarHeight: Math.max(22, Math.min(bar.dialHeight - 8, 52))
    GridLayout {
        Layout.alignment: Qt.AlignTop
        columns: bar.leftPair ? 2 : 1
        columnSpacing: 4
        rowSpacing: 4
        MeterBadge {
            objectName: "meterLeft"
            side: "left"
            dialHeight: bar.dialHeight
            maxWidth: bar.leftPair ? bar.pairMaxWidth : bar.meterMaxWidth
            barMaxWidth: bar.leftPair ? bar.pairMaxWidth : bar.barMaxWidth
            analogRoom: Engine.meterStyleLeft !== "analog2" || bar.leftPair
            barHeight: bar.leftStack ? bar.stackBarHeight : bar.singleBarHeight
        }
        MeterBadge {
            objectName: "meterLeft2"
            side: "left2"
            visible: bar.leftStack || bar.leftPair
            dialHeight: bar.dialHeight
            maxWidth: bar.leftPair ? bar.pairMaxWidth : bar.meterMaxWidth
            barMaxWidth: bar.leftPair ? bar.pairMaxWidth : bar.barMaxWidth
            analogRoom: bar.leftPair
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
                    objectName: "sliceChip" + modelData.letter
                    Layout.preferredWidth: chips.cell
                    Layout.preferredHeight: bar.chipHeight
                    checked: modelData.active
                    leftPadding: 4
                    rightPadding: 4
                    font.pixelSize: bar.compact ? Math.round(Theme.buttonFont * 0.9) : Theme.buttonFont
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.selectSlice(modelData.id)
                    // The label is drawn below (no text of its own): named here.
                    Accessible.name: qsTr("Slice %1, %2 kHz, %3%4").arg(modelData.letter)
                                     .arg((modelData.frequency * 1000).toFixed(3)).arg(modelData.mode)
                                     .arg(modelData.tx ? qsTr(", the transmit slice") : "")
                    Accessible.description: qsTr("Hold for Make TX slice and Close slice")
                    // Under the pressed chip, not at the last pointer position.
                    onPressAndHold: sliceMenu.popup(chip, 0, chip.height)
                    // The set power in the TX tag (Evgeny R0ACS, roadmap 36): watts on
                    // a 100 W radio, a percentage on an Aurora (its 500 W PA), AMP
                    // while an amplifier is in Operate (then it is the drive). Where
                    // the chip is narrow (two slices on a phone) "100 W" alone, then
                    // "TX" over the power in small type (two bars at both meters
                    // leave a phone's chips about 90 px: the user's OnePlus), only
                    // "TX" last. A tap on the tag opens the power slider.
                    readonly property bool ampOperating: (!!Engine.amp.set && Engine.amp.operate === "OPERATE")
                                                         || (!!Engine.pgxl.present && !!Engine.pgxl.operate)
                    readonly property string powerText: Engine.rfPower + (Engine.rfPowerIsPercent ? " %" : " W")
                                                        + (ampOperating ? " AMP" : "")
                    readonly property real tagRoom: chip.availableWidth - chipLabel.implicitWidth - 5 - 8
                    TextMetrics { id: tagFull; font.pixelSize: chip.font.pixelSize; font.bold: true; text: "TX " + chip.powerText }
                    TextMetrics { id: tagPower; font.pixelSize: chip.font.pixelSize; font.bold: true; text: chip.powerText }
                    readonly property int smallPx: Math.max(9, Math.round(chip.font.pixelSize * 0.68))
                    readonly property string shortPower: Engine.rfPower + (Engine.rfPowerIsPercent ? " %" : " W")
                    TextMetrics { id: tagSmall; font.pixelSize: chip.smallPx; font.bold: true; text: chip.shortPower }
                    readonly property string tagStyle: !chip.modelData.tx ? ""
                        : tagFull.advanceWidth <= chip.tagRoom ? "full"
                        : tagPower.advanceWidth <= chip.tagRoom ? "power"
                        : tagSmall.advanceWidth <= chip.tagRoom ? "stacked" : "tx"
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 5
                            Text {
                                id: chipLabel
                                Accessible.ignored: true
                                anchors.verticalCenter: parent.verticalCenter
                                // Compact (phone): the mode is beside the frequency already.
                                text: chip.modelData.letter + " " + chip.modelData.frequency.toFixed(3)
                                      + (bar.compact ? "" : " " + chip.modelData.mode)
                                font: chip.font
                                color: chip.checked ? Theme.buttonTextOn : Theme.buttonText
                            }
                            // The transmit slice: a red tag, not just two letters.
                            Rectangle {
                                objectName: "sliceTxPower" + chip.modelData.letter
                                visible: chip.modelData.tx
                                anchors.verticalCenter: parent.verticalCenter
                                width: (chip.tagStyle === "stacked" ? stackedTag.implicitWidth : txText.implicitWidth) + 8
                                height: (chip.tagStyle === "stacked" ? stackedTag.implicitHeight : txText.implicitHeight) + 4
                                radius: 4
                                color: Theme.transmitTag
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Transmit power %1").arg(chip.powerText)
                                Accessible.description: qsTr("Opens the power slider")
                                Accessible.onPressAction: powerPopup.open()
                                Text {
                                    id: txText
                                    Accessible.ignored: true
                                    visible: chip.tagStyle !== "stacked"
                                    anchors.centerIn: parent
                                    text: chip.tagStyle === "full" ? "TX " + chip.powerText
                                        : chip.tagStyle === "power" ? chip.powerText : "TX"
                                    font.pixelSize: chip.font.pixelSize
                                    font.bold: true
                                    color: "#ffffff"
                                }
                                Column {
                                    id: stackedTag
                                    visible: chip.tagStyle === "stacked"
                                    anchors.centerIn: parent
                                    spacing: -1
                                    Text {
                                        Accessible.ignored: true
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "TX"
                                        font.pixelSize: chip.smallPx
                                        font.bold: true
                                        color: "#ffffff"
                                    }
                                    Text {
                                        Accessible.ignored: true
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: chip.shortPower
                                        font.pixelSize: chip.smallPx
                                        font.bold: true
                                        color: "#ffffff"
                                    }
                                }
                                // Its own tap, a little larger than the tag; the
                                // rest of the chip still selects the slice.
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -6
                                    onClicked: powerPopup.open()
                                }
                            }
                        }
                    }
                    // The power slider under the chip, as on the TX tab.
                    SlicePowerPopup {
                        id: powerPopup
                        objectName: "slicePowerPopup" + chip.modelData.letter
                        ampOperating: chip.ampOperating
                    }
                    SliceMenu {
                        id: sliceMenu
                        slice: chip.modelData
                    }
                }
            }
        }
    }

    // Mirrored: a pair's main face at the outer edge, as on the left.
    GridLayout {
        id: rightMeters
        Layout.alignment: Qt.AlignTop
        columns: bar.rightPair ? 2 : 1
        columnSpacing: 4
        rowSpacing: 4
        layoutDirection: bar.rightPair ? Qt.RightToLeft : Qt.LeftToRight
        MeterBadge {
            objectName: "meterRight"
            side: "right"
            dialHeight: bar.dialHeight
            maxWidth: bar.rightPair ? bar.pairMaxWidth : bar.meterMaxWidth
            barMaxWidth: bar.rightPair ? bar.pairMaxWidth : bar.barMaxWidth
            analogRoom: Engine.meterStyleRight !== "analog2" || bar.rightPair
            barHeight: bar.rightStack ? bar.stackBarHeight : bar.singleBarHeight
        }
        MeterBadge {
            objectName: "meterRight2"
            side: "right2"
            visible: bar.rightStack || bar.rightPair
            dialHeight: bar.dialHeight
            maxWidth: bar.rightPair ? bar.pairMaxWidth : bar.meterMaxWidth
            barMaxWidth: bar.rightPair ? bar.pairMaxWidth : bar.barMaxWidth
            analogRoom: bar.rightPair
            barHeight: bar.stackBarHeight
        }
    }
}
