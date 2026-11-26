import QtQuick
import QtQuick.Controls
import AetherMobile

// One slice's chip ("A 14.100 USB", the red TX tag with the set power on the
// transmit slice): a tap selects it, press and hold opens make-TX / close,
// the tag opens the power slider. Shared by Classic's slice row (SliceBar)
// and an own layout's Slices block (roadmap 116), so both stay the same.
AppButton {
    id: chip
    // The slice (Engine.slices' entry).
    required property var slice
    // A phone's row: the mode is beside the frequency already.
    property bool compact: false

    objectName: "sliceChip" + chip.slice.letter
    checked: chip.slice.active
    leftPadding: 4
    rightPadding: 4
    font.pixelSize: chip.compact ? Math.round(Theme.buttonFont * 0.9) : Theme.buttonFont
    focusPolicy: Qt.NoFocus
    onClicked: Engine.selectSlice(chip.slice.id)
    // The label is drawn below (no text of its own): named here.
    Accessible.name: qsTr("Slice %1, %2 kHz, %3%4%5").arg(chip.slice.letter)
                     .arg((chip.slice.frequency * 1000).toFixed(3)).arg(chip.slice.mode)
                     .arg(chip.slice.tx ? qsTr(", the transmit slice") : "")
                     .arg(chip.slice.offBand ? qsTr(", on another band than its panadapter: not received") : "")
    // On another band than its panadapter (roadmap 92): an
    // outline in the warning colour, no room taken.
    Rectangle {
        objectName: "sliceOffBand" + chip.slice.letter
        anchors.fill: parent
        visible: !!chip.slice.offBand
        color: "transparent"
        border.color: Theme.warning
        border.width: 2
        radius: Theme.radius
        z: 3
    }
    // Make TX slice and Close slice are on the hold only, which TalkBack
    // and a keyboard cannot give: a screen reader's double tap on the
    // active chip (where a tap selects nothing new) opens them instead.
    Accessible.description: chip.slice.active ? qsTr("Opens Make TX slice and Close slice")
                                              : qsTr("Makes it the active slice")
    Accessible.onPressAction: chip.slice.active ? sliceMenu.popup(chip, 0, chip.height)
                                                : Engine.selectSlice(chip.slice.id)
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
                                         || (!!Engine.kpa.set && Engine.kpa.operate === "OPERATE")
                                         || (!!Engine.pgxl.present && !!Engine.pgxl.operate)
    readonly property string powerText: Engine.rfPower + (Engine.rfPowerIsPercent ? " %" : " W")
                                        + (ampOperating ? " AMP" : "")
    readonly property real tagRoom: chip.availableWidth - chipLabel.implicitWidth - 5 - 8
    TextMetrics { id: tagFull; font.pixelSize: chip.font.pixelSize; font.bold: true; text: "TX " + chip.powerText }
    TextMetrics { id: tagPower; font.pixelSize: chip.font.pixelSize; font.bold: true; text: chip.powerText }
    readonly property int smallPx: Math.max(9, Math.round(chip.font.pixelSize * 0.68))
    readonly property string shortPower: Engine.rfPower + (Engine.rfPowerIsPercent ? " %" : " W")
    TextMetrics { id: tagSmall; font.pixelSize: chip.smallPx; font.bold: true; text: chip.shortPower }
    readonly property string tagStyle: !chip.slice.tx ? ""
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
                text: chip.slice.letter + " " + chip.slice.frequency.toFixed(3)
                      + (chip.compact ? "" : " " + chip.slice.mode)
                font: chip.font
                color: chip.checked ? Theme.buttonTextOn : Theme.buttonText
            }
            // The transmit slice: a red tag, not just two letters.
            Rectangle {
                objectName: "sliceTxPower" + chip.slice.letter
                visible: chip.slice.tx
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
        objectName: "slicePowerPopup" + chip.slice.letter
        ampOperating: chip.ampOperating
    }
    SliceMenu {
        id: sliceMenu
        slice: chip.slice
    }
}
