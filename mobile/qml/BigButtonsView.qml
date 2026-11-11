import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile
import "MeterMarks.js" as MeterMarks

// Big buttons (roadmap 55, Michael Walker): the operating page covered by a
// few large controls for the car (mounted, used as the law allows), gloves
// or a glance: the frequency and mode large, the slice as the main screen's
// chip with the set power and two bar meters of their own choosing (roadmap
// 58, the user), − and +
// filling the middle (one tuning step a tap, repeating while held), the step
// (a tap takes the next one), and PTT across the bottom. It lies over the
// page rather than in a popup, so the page keeps the keyboard: Space and a
// Bluetooth PTT button key, the arrows tune. Out by holding Exit, so a stray
// tap does not leave it.
Rectangle {
    id: big
    color: Theme.background
    readonly property bool landscape: width > height * 1.2
    // A low screen on its side (a phone): the step beside Exit, so PTT keeps
    // a height to press and read.
    readonly property bool lowLandscape: landscape && height < 480
    // The page's formatter (14.074.000).
    property var formatMhz: (mhz) => mhz.toFixed(6)
    // The page's safe area, given by the page.
    property real marginLeft: 0
    property real marginRight: 0
    property real marginTop: 0
    property real marginBottom: 0
    // The frequency tapped: the page opens its frequency entry.
    signal frequencyTapped()
    // Every slice as a chip, as on the main screen (by letter): up to two in
    // the top row, more on a row of their own. The frequency, mode, − / +
    // and the meters follow the active one.
    readonly property var allSlices: {
        const list = []
        for (const s of Engine.slices) list.push(s)
        return list.sort((x, y) => x.letter.localeCompare(y.letter))
    }
    readonly property bool chipRow: allSlices.length > 2
    readonly property bool ampOperating: (!!Engine.amp.set && Engine.amp.operate === "OPERATE")
                                         || (!!Engine.kpa.set && Engine.kpa.operate === "OPERATE")
                                         || (!!Engine.pgxl.present && !!Engine.pgxl.operate)
    // The meters' reading size (the first version's S reading), larger on a
    // tablet, and the chips' text from it.
    readonly property real readingPx: Math.round(Math.max(Theme.px(24), Math.min(44, height * 0.03)))
    function stepText(hz) {
        return hz >= 1000 ? (hz / 1000) + " kHz" : hz + " Hz"
    }
    function nextStep() {
        const list = Engine.stepList.slice().sort((a, b) => a - b)
        if (list.length === 0) return
        const i = list.indexOf(Engine.stepHz)
        Engine.setStepHz(list[(i + 1) % list.length])
    }

    // Touches never reach the page under it (the page also turns its own
    // controls off meanwhile: tap handlers under an item that takes a press
    // still get it, and a tap on a meter here opened the page's mode list).
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    // The next tuning step a tap.
    component StepButton: AppButton {
        objectName: "bigStep"
        text: qsTr("Step %1").arg(big.stepText(Engine.stepHz))
        Accessible.description: qsTr("Tap for the next tuning step")
        focusPolicy: Qt.NoFocus
        onClicked: big.nextStep()
    }

    // A slice as the main screen's chip, working as it does there: its
    // letter and, on the transmit slice, the red tag with the set power
    // ("TX 100 W", a % on an Aurora, AMP with an amplifier in Operate). A tap
    // makes it the active slice, a tap on the tag opens the power slider, a
    // hold the menu (Make TX slice, Close slice).
    component SliceChip: AppButton {
        id: chip
        property var slice
        objectName: "bigSlice" + slice.letter
        checked: slice.active
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: Math.round(Math.max(Theme.buttonFont, big.readingPx * 0.7))
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTr("Slice %1%2").arg(slice.letter)
                         .arg(slice.tx ? qsTr(", the transmit slice, %1").arg(chip.powerText) : "")
        Accessible.description: qsTr("Hold for Make TX slice and Close slice")
        readonly property string powerText: Engine.rfPower + (Engine.rfPowerIsPercent ? " %" : " W")
                                            + (big.ampOperating ? " AMP" : "")
        onClicked: Engine.selectSlice(slice.id)
        onPressAndHold: sliceMenu.popup(chip, 0, chip.height)
        // The letter at the left, the tag at the right and room between
        // them, so a tap in the middle selects the slice (a large hand in the
        // car hit the tag).
        contentItem: Item {
            implicitWidth: letter.implicitWidth + (tag.visible ? tag.width + Theme.touchTarget * 0.6 : Theme.touchTarget * 0.4)
            implicitHeight: Math.max(letter.implicitHeight, tag.height)
            Text {
                id: letter
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: chip.slice.letter
                font.pixelSize: chip.font.pixelSize
                font.bold: true
                color: chip.checked ? Theme.buttonTextOn : Theme.buttonText
            }
            Rectangle {
                id: tag
                visible: !!chip.slice.tx
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: tagText.implicitWidth + 10
                height: tagText.implicitHeight + 4
                radius: 4
                color: Theme.transmitTag
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Transmit power %1").arg(chip.powerText)
                Accessible.description: qsTr("Opens the power slider")
                Accessible.onPressAction: powerPopup.open()
                Text {
                    id: tagText
                    Accessible.ignored: true
                    anchors.centerIn: parent
                    text: "TX " + chip.powerText
                    font.pixelSize: chip.font.pixelSize
                    font.bold: true
                    color: "#ffffff"
                }
                // Its own tap, the tag alone; the rest of the chip selects
                // the slice.
                MouseArea {
                    objectName: "bigSliceTxPower"
                    anchors.fill: parent
                    onClicked: powerPopup.open()
                }
            }
        }
        SlicePowerPopup {
            id: powerPopup
            objectName: "bigSlicePowerPopup"
            ampOperating: big.ampOperating
        }
        SliceMenu {
            id: sliceMenu
            slice: chip.slice
        }
    }

    // A meter as the first version's S bar, readable in the car: its name,
    // a thick bar and the reading large beside it. A tap chooses what it
    // shows (the main meters' chooser, receive and transmit each, kept apart
    // from them), through the meter face hidden under it.
    component BigMeter: Item {
        id: bm
        property alias side: face.side
        implicitHeight: Math.max(big.readingPx * 1.25, Theme.touchTarget * 0.6)
        MeterView {
            id: face
            anchors.fill: parent
            opacity: 0
            compact: true
            analogAllowed: false
            styleChoice: false
        }
        readonly property var r: face.reading
        // S: the S units (S9+10); the others: the number and unit alone.
        readonly property string value: !r.live ? "–"
            : face.kind === "S" ? r.text.split(" ")[0] : r.text.replace(/^[A-Za-z]+ (?=[-−0-9<])/, "")
        // S9 and over red, as the first version; the others at their alarm.
        readonly property real redAt: face.kind === "S" ? -73 : r.red
        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacing
            Label {
                text: MeterMarks.meterName(face.kind, true)
                color: Theme.textDim
                font.pixelSize: Math.round(big.readingPx * 0.6)
                Layout.preferredWidth: nameMetrics.advanceWidth
                TextMetrics { id: nameMetrics; font.pixelSize: Math.round(big.readingPx * 0.6); text: "Power" }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Math.round(big.readingPx * 0.9)
                radius: 4
                color: Theme.surface
                Rectangle {
                    height: parent.height
                    width: bm.r.live ? parent.width * Math.max(0, Math.min(1, (bm.r.v - bm.r.min) / (bm.r.max - bm.r.min))) : 0
                    radius: 4
                    color: bm.r.v >= bm.redAt ? Theme.transmit : Theme.accent
                }
            }
            Label {
                text: bm.value
                color: Theme.text
                font.pixelSize: big.readingPx
                font.bold: true
                horizontalAlignment: Text.AlignRight
                // A fixed width, so the bar does not jump with the reading.
                Layout.preferredWidth: valueMetrics.advanceWidth
                TextMetrics { id: valueMetrics; font.pixelSize: big.readingPx; font.bold: true; text: "S9+40" }
            }
        }
    }

    component BigMeters: ColumnLayout {
        spacing: Theme.spacing / 2
        BigMeter {
            objectName: "bigMeter1"
            side: "big1"
            Layout.fillWidth: true
        }
        BigMeter {
            objectName: "bigMeter2"
            side: "big2"
            Layout.fillWidth: true
        }
    }

    // One step down or up a tap, repeating while held.
    component TuneButton: AppButton {
        id: tune
        property int direction: 1
        objectName: direction > 0 ? "bigTuneUp" : "bigTuneDown"
        text: direction > 0 ? "+" : "−"
        Accessible.name: direction > 0 ? qsTr("Tune up one step") : qsTr("Tune down one step")
        // The size the layout starts from is fixed: taken from the font, which
        // follows the size the layout gives, it made a layout loop that hung
        // the app on a turn or a resize (Kurt Hanisch DL5EAG, beta 77).
        implicitWidth: Theme.touchTarget * 2
        implicitHeight: Theme.touchTarget * 2
        font.pixelSize: Math.min(width, height) * 0.45
        font.bold: true
        autoRepeat: true
        autoRepeatDelay: 400
        autoRepeatInterval: 150
        focusPolicy: Qt.NoFocus
        Layout.fillWidth: true
        Layout.fillHeight: true
        onClicked: {
            Engine.tuneBySteps(direction)
            Engine.haptic("step")
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing + big.marginLeft
        anchors.rightMargin: Theme.spacing + big.marginRight
        anchors.topMargin: Theme.spacing + big.marginTop
        anchors.bottomMargin: Theme.spacing + big.marginBottom
        columns: big.landscape ? 3 : 1
        columnSpacing: Theme.spacing
        rowSpacing: Theme.spacing

        // The top row and the slices across the whole width, upright and on
        // its side.
        RowLayout {
            Layout.fillWidth: true
            Layout.columnSpan: big.landscape ? 3 : 1
            AppButton {
                id: exitButton
                objectName: "bigExit"
                text: qsTr("Exit")
                Accessible.name: qsTr("Exit big buttons")
                Accessible.description: qsTr("Hold to exit")
                focusPolicy: Qt.NoFocus
                onPressAndHold: Engine.bigButtons = false
                onClicked: holdHint.visible = true
            }
            Label {
                id: holdHint
                visible: false
                text: qsTr("hold to exit")
                color: Theme.textDim
                font.pixelSize: Theme.px(14)
                Timer { running: holdHint.visible; interval: 2500; onTriggered: holdHint.visible = false }
            }
            Repeater {
                model: big.chipRow ? [] : big.allSlices
                delegate: SliceChip {
                    required property var modelData
                    slice: modelData
                }
            }
            Item { Layout.fillWidth: true }
            StepButton {
                visible: big.lowLandscape
                leftPadding: 14
                rightPadding: 14
                font.pixelSize: Theme.px(20)
            }
            Label {
                text: Engine.mode
                color: Theme.accent
                font.pixelSize: Theme.px(28)
                font.bold: true
            }
        }
        RowLayout {
            visible: big.chipRow
            Layout.fillWidth: true
            Layout.columnSpan: big.landscape ? 3 : 1
            spacing: Theme.spacing
            Repeater {
                model: big.chipRow ? big.allSlices : []
                delegate: SliceChip {
                    required property var modelData
                    slice: modelData
                    Layout.fillWidth: true
                }
            }
        }
        // On its side the meters across the whole width too, − and + lower
        // beside the frequency, Step and PTT (the user).
        BigMeters {
            visible: big.landscape
            Layout.fillWidth: true
            Layout.columnSpan: 3
        }
        // Landscape: − at the left, the rest in the middle, + at the right.
        TuneButton {
            visible: big.landscape
            direction: -1
            Layout.horizontalStretchFactor: 1
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.horizontalStretchFactor: 2
            spacing: Theme.spacing
            Label {
                objectName: "bigFrequency"
                Layout.fillWidth: true
                text: big.formatMhz(Engine.frequencyMhz)
                color: Engine.transmitting ? Theme.transmit : Theme.text
                font.pixelSize: big.landscape ? Math.min(big.height * 0.16, 96) : Math.min(big.width * 0.17, 96)
                font.bold: true
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 24
                horizontalAlignment: Text.AlignHCenter
                // Tap for direct entry, as on the page.
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Frequency %1 kHz").arg((Engine.frequencyMhz * 1000).toFixed(3))
                Accessible.description: qsTr("Opens frequency entry")
                Accessible.onPressAction: big.frequencyTapped()
                TapHandler { onTapped: big.frequencyTapped() }
            }
            // Upright the two bars under the frequency.
            BigMeters {
                visible: !big.landscape
                Layout.fillWidth: true
            }
            // Portrait: − and + side by side in the middle.
            RowLayout {
                visible: !big.landscape
                Layout.fillWidth: true
                Layout.fillHeight: true
                // The rest of the height: − and + as PTT, Step two fifths of
                // them, large enough for a big hand in the car (the user).
                Layout.verticalStretchFactor: 5
                spacing: Theme.spacing
                TuneButton { direction: -1 }
                TuneButton { direction: 1 }
            }
            StepButton {
                visible: !big.lowLandscape
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.verticalStretchFactor: 2
                Layout.preferredHeight: Theme.touchTarget * 1.3
                Layout.minimumHeight: Theme.touchTarget
                font.pixelSize: Math.round(Math.max(Theme.px(22), big.readingPx))
            }
            PttButton {
                objectName: "bigPtt"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.verticalStretchFactor: 5
                Layout.preferredHeight: Theme.touchTarget * 2
                Layout.minimumHeight: Theme.touchTarget
                // Large enough to read on a tablet's large button too, and
                // inside a low one on a phone on its side.
                fontPx: Math.round(Math.max(Math.min(40, big.height * 0.1), Math.min(80, big.height * 0.06)))
            }
        }
        TuneButton {
            visible: big.landscape
            direction: 1
            Layout.horizontalStretchFactor: 1
        }
    }
}
