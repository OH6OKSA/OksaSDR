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
// as two halves (the left a smaller step, the right a larger, roadmap 69),
// and PTT across the bottom. It lies over the page rather than in a popup,
// so the page keeps the keyboard: Space and a Bluetooth PTT button key, the
// arrows tune. Out by holding Exit, so a stray tap does not leave it.
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
                                         || (!!Engine.kpa500.set && Engine.kpa500.operate === "OPERATE")
                                         || (!!Engine.pgxl.present && !!Engine.pgxl.operate)
    // The meters' reading size (the first version's S reading), larger on a
    // tablet, and the chips' text from it.
    readonly property real readingPx: Math.round(Math.max(Theme.px(24), Math.min(44, height * 0.03)))
    function stepText(hz) {
        return hz >= 1000 ? (hz / 1000) + " kHz" : hz + " Hz"
    }
    readonly property var steps: Engine.stepList.slice().sort((a, b) => a - b)
    readonly property int stepIndex: steps.indexOf(Engine.stepHz)
    // The next smaller (-1) or larger (+1) step, stopping at the ends.
    function stepBy(direction) {
        if (steps.length === 0) return
        const i = stepIndex < 0 ? (direction > 0 ? -1 : steps.length) : stepIndex
        const next = Math.max(0, Math.min(steps.length - 1, i + direction))
        if (next !== stepIndex) Engine.setStepHz(steps[next])
    }

    // Touches never reach the page under it (the page also turns its own
    // controls off meanwhile: tap handlers under an item that takes a press
    // still get it, and a tap on a meter here opened the page's mode list).
    // Big buttons' log (roadmap 75, Michael Walker): the call and the two
    // reports in big type in place of the buttons, PTT kept at the bottom.
    // The call is the Log tab's too (Engine.workedCall), and so is My park.
    property bool logging: false
    property string logTimeOn: ""
    property string logRstSent: "59"
    property string logRstRcvd: "59"
    property string logResult: ""
    function openLog() {
        const d = Engine.qsoDefaults()
        logRstSent = d.rstSent || d.rst || "59"
        logRstRcvd = d.rstRcvd || d.rst || "59"
        logTimeOn = ""
        logCall.text = Engine.workedCall
        logging = true
        logCall.forceActiveFocus()
    }
    function closeLog() {
        logCall.focus = false
        logging = false
    }
    // − / + step a report: an RST's strength digit (59, 599) from 1 to 9,
    // an FTx report in dB.
    function stepReport(rst, direction) {
        if (/^[1-5][1-9][1-9]?$/.test(rst)) {
            const strength = Math.max(1, Math.min(9, parseInt(rst[1]) + direction))
            return rst[0] + strength + rst.slice(2)
        }
        if (/^[+-]?[0-9]{1,2}$/.test(rst)) {
            const db = Math.max(-30, Math.min(30, parseInt(rst) + direction))
            return (db < 0 ? "-" : "+") + String(Math.abs(db)).padStart(2, "0")
        }
        return rst
    }
    function logIt() {
        const call = logCall.text.trim().toUpperCase()
        if (call.length < 3) return
        const ok = Engine.logQso({
            call: call, rstSent: logRstSent, rstRcvd: logRstRcvd, comment: "", timeOn: logTimeOn,
            sig: "", sigInfo: "", mySig: Engine.mySig, mySigInfo: Engine.mySig.length > 0 ? Engine.myRef : ""
        })
        if (ok) {
            logResult = qsTr("Logged %1").arg(call)
            Engine.workedCall = ""
            closeLog()
        }
    }
    Timer { running: big.logResult.length > 0; interval: 4000; onTriggered: big.logResult = "" }

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    // The tuning step as one bar of two halves (Michael Walker: "split the
    // Bar and have an UP and DOWN"): the left half a smaller step (▼ at its
    // edge), the right a larger (▲), the step itself across the middle. An
    // end dims its half; neither wraps round.
    component StepBar: Item {
        id: bar
        property int fontPx: Theme.buttonFont
        // The step between the two glyphs, clear of both.
        implicitWidth: Math.max(stepLabel.implicitWidth + 2 * (glyph.advanceWidth + 20) + 2,
                                Theme.touchTarget * 2)
        implicitHeight: Theme.touchTarget
        TextMetrics { id: glyph; font.pixelSize: bar.fontPx; text: "▼" }
        RowLayout {
            anchors.fill: parent
            spacing: 2
            Repeater {
                model: [-1, 1]
                delegate: AppButton {
                    id: half
                    required property int modelData
                    objectName: modelData < 0 ? "bigStepDown" : "bigStepUp"
                    text: modelData < 0 ? "▼" : "▲"
                    font.pixelSize: bar.fontPx
                    leftPadding: 10
                    rightPadding: 10
                    enabled: modelData < 0 ? big.stepIndex !== 0
                                           : big.stepIndex < big.steps.length - 1
                    Accessible.name: modelData < 0 ? qsTr("Smaller tuning step") : qsTr("Larger tuning step")
                    Accessible.description: qsTr("Step %1").arg(big.stepText(Engine.stepHz))
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentItem: Text {
                        text: half.text
                        font: half.font
                        horizontalAlignment: half.modelData < 0 ? Text.AlignLeft : Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.buttonText
                        opacity: half.enabled ? 1.0 : 0.4
                    }
                    onClicked: big.stepBy(modelData)
                }
            }
        }
        // Over both halves; a tap on it falls to the half under it.
        Label {
            id: stepLabel
            objectName: "bigStep"
            anchors.centerIn: parent
            Accessible.ignored: true
            text: qsTr("Step %1").arg(big.stepText(Engine.stepHz))
            font.pixelSize: bar.fontPx
            color: Theme.buttonText
        }
    }

    // A slice as the main screen's chip, working as it does there: its
    // letter and, on the transmit slice, the red tag with the set power
    // ("TX 100 W", a % on an Aurora, AMP with an amplifier in Operate). A tap
    // makes it the active slice, a tap on the tag opens the power slider, a
    // hold the menu (Make TX slice, Close slice).
    // Log (roadmap 75): in the Step row, or the top row on a low landscape
    // phone where Step sits there (the top row has no room for it upright).
    component LogButton: AppButton {
        objectName: "bigLog"
        text: qsTr("Log")
        Accessible.name: qsTr("Log a contact")
        focusPolicy: Qt.NoFocus
        font.pixelSize: Math.round(Math.max(Theme.buttonFont, big.readingPx * 0.7))
        leftPadding: 16
        rightPadding: 16
        onClicked: big.openLog()
    }

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
        visible: !big.logging
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
            // As large as the slice chips beside it, and out after half the
            // usual press and hold (Michael Walker: "a big bigger and less of
            // a HOLD time by 1/2").
            AppButton {
                id: exitButton
                objectName: "bigExit"
                text: qsTr("Exit")
                Accessible.name: qsTr("Exit big buttons")
                Accessible.description: qsTr("Hold to exit")
                focusPolicy: Qt.NoFocus
                font.pixelSize: Math.round(Math.max(Theme.buttonFont, big.readingPx * 0.7))
                leftPadding: 16
                rightPadding: 16
                property bool exiting: false
                onPressedChanged: if (pressed) exiting = false
                Timer {
                    running: exitButton.pressed
                    interval: Qt.styleHints.mousePressAndHoldInterval / 2
                    onTriggered: {
                        exitButton.exiting = true
                        Engine.bigButtons = false
                    }
                }
                onClicked: if (!exiting) holdHint.visible = true
            }
            Label {
                id: holdHint
                visible: false
                text: qsTr("hold to exit")
                // Amber: in the dim text colour it went unseen (roadmap 161).
                // Not bold, which is wider and pushed the mode off a phone's row.
                color: Theme.warning
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
            StepBar {
                visible: big.lowLandscape
                fontPx: Theme.px(20)
            }
            LogButton {
                visible: big.lowLandscape
            }
            // The mode with its chooser, as on the main screen (the user,
            // build 172 on Android: "Yläkulmasta pitää pystyä mode valitsemaan").
            ModeButton {
                objectName: "bigMode"
                fontSize: Theme.px(28)
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
            RowLayout {
                visible: !big.lowLandscape
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.verticalStretchFactor: 2
                Layout.preferredHeight: Theme.touchTarget * 1.3
                Layout.minimumHeight: Theme.touchTarget
                spacing: Theme.spacing
                LogButton {
                    visible: !big.lowLandscape
                    Layout.fillHeight: true
                }
                StepBar {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    fontPx: Math.round(Math.max(Theme.px(22), big.readingPx))
                }
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
    // "Logged CALL" for a moment over the frequency, outside the layout so
    // it never widens the top row.
    Rectangle {
        objectName: "bigLogResult"
        visible: big.logResult.length > 0 && !big.logging
        anchors.horizontalCenter: parent.horizontalCenter
        y: Theme.spacing + big.marginTop + Theme.touchTarget + Theme.spacing
        z: 3
        width: resultText.implicitWidth + 2 * Theme.spacing
        height: resultText.implicitHeight + Theme.spacing
        radius: height / 2
        color: Theme.surface
        border.color: Theme.good
        Label {
            id: resultText
            anchors.centerIn: parent
            text: big.logResult
            color: Theme.good
            font.pixelSize: Theme.px(18)
            font.bold: true
        }
    }

    ColumnLayout {
        id: logPanel
        objectName: "bigLogPanel"
        visible: big.logging
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing + big.marginLeft
        anchors.rightMargin: Theme.spacing + big.marginRight
        anchors.topMargin: Theme.spacing + big.marginTop
        anchors.bottomMargin: Theme.spacing + big.marginBottom
        spacing: Theme.spacing
        readonly property int bigPx: Math.round(big.readingPx * 1.2)

        RowLayout {
            Layout.fillWidth: true
            AppButton {
                objectName: "bigLogCancel"
                text: qsTr("Cancel")
                focusPolicy: Qt.NoFocus
                font.pixelSize: Math.round(Math.max(Theme.buttonFont, big.readingPx * 0.7))
                leftPadding: 16
                rightPadding: 16
                onClicked: big.closeLog()
            }
            Label {
                Layout.fillWidth: true
                text: Engine.mySig.length > 0 && Engine.myRef.length > 0 ? Engine.mySig + " " + Engine.myRef.toUpperCase() : ""
                color: Theme.textDim
                font.pixelSize: Theme.px(16)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            // The mode with its chooser, as on the main screen (the user,
            // build 172 on Android: "Yläkulmasta pitää pystyä mode valitsemaan").
            ModeButton {
                objectName: "bigMode"
                fontSize: Theme.px(28)
            }
        }
        // Upright: the call, its lookup, then Log; on its side one row, as
        // the on-screen keyboard takes the lower part.
        GridLayout {
            Layout.fillWidth: true
            columns: big.landscape ? 2 : 1
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacing
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing
                TextField {
                    id: logCall
                    objectName: "bigLogCall"
                    Accessible.name: qsTr("Callsign")
                    Layout.fillWidth: true
                    // Sized to what it holds, about 16 characters (roadmap 158)
                    Layout.maximumWidth: Math.ceil(16 * font.pixelSize * 0.62) + leftPadding + rightPadding
                    placeholderText: qsTr("Callsign")
                    font.pixelSize: logPanel.bigPx
                    font.family: "monospace"
                    font.capitalization: Font.AllUppercase
                    inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    onTextChanged: {
                        if (!big.logging) return
                        Engine.workedCall = text
                        if (text.length > 0 && big.logTimeOn.length === 0)
                            big.logTimeOn = Qt.formatDateTime(new Date(new Date().getTime() + new Date().getTimezoneOffset() * 60000), "HHmm")
                        if (text.length === 0) big.logTimeOn = ""
                        lookupDelay.restart()
                    }
                    // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
                    EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
                    onAccepted: big.logIt()
                    Keys.onEscapePressed: big.closeLog()
                }
                VoiceButton {
                    objectName: "bigLogVoice"
                    Layout.preferredHeight: logCall.height
                    Layout.preferredWidth: logCall.height
                    font.pixelSize: Math.round(logPanel.bigPx * 0.8)
                }
            }
            AppButton {
                objectName: "bigLogSave"
                text: qsTr("Log %1").arg(logCall.text.trim().toUpperCase())
                enabled: logCall.text.trim().length >= 3
                checked: enabled
                primary: true
                focusPolicy: Qt.NoFocus
                font.pixelSize: Math.round(Math.max(Theme.buttonFont, big.readingPx * 0.8))
                Layout.fillWidth: true   // the rest of the row beside a call field sized to its content (roadmap 158)
                Layout.preferredHeight: Math.max(Theme.touchTarget, logPanel.bigPx * 1.6)
                onClicked: big.logIt()
            }
        }
        // A call the voice button heard (roadmap 79), with its report.
        Connections {
            target: Engine
            enabled: big.logging
            function onVoiceHeard(call, report) {
                logCall.text = call
                if (report.length > 0) big.logRstSent = report
            }
        }
        Label {
            objectName: "bigLogVoiceStatus"
            visible: Engine.voiceInputOffered && Engine.voiceInputStatus.length > 0
            Layout.fillWidth: true
            text: Engine.voiceInputStatus
            color: Theme.textDim
            font.pixelSize: Theme.px(16)
            wrapMode: Text.Wrap
        }
        Timer {
            id: lookupDelay
            interval: 600
            onTriggered: if (logCall.text.trim().length >= 3) Engine.lookupCallsign(logCall.text)
        }
        Label {
            objectName: "bigLogLookup"
            Layout.fillWidth: true
            readonly property var info: Engine.callLookup
            visible: logCall.text.trim().length >= 3 && text.length > 0
            text: (info.call || "").toUpperCase() !== logCall.text.trim().toUpperCase() ? ""
                  : [info.name, info.qth || info.country].filter(s => s && s.length > 0).join(", ")
            color: Theme.text
            font.pixelSize: Theme.px(18)
            elide: Text.ElideRight
        }
        GridLayout {
            Layout.fillWidth: true
            columns: big.landscape ? 2 : 1
            columnSpacing: Theme.spacing * 3
            rowSpacing: Theme.spacing
            Repeater {
                model: [{ key: "sent", name: qsTr("Sent") }, { key: "rcvd", name: qsTr("Rcvd") }]
                delegate: RowLayout {
                    id: reportRow
                    required property var modelData
                    readonly property string value: modelData.key === "sent" ? big.logRstSent : big.logRstRcvd
                    function step(direction) {
                        const next = big.stepReport(value, direction)
                        if (modelData.key === "sent") big.logRstSent = next
                        else big.logRstRcvd = next
                    }
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    Label {
                        text: reportRow.modelData.name
                        color: Theme.textDim
                        font.pixelSize: Theme.px(18)
                        Layout.preferredWidth: Theme.px(56)
                    }
                    AppButton {
                        objectName: "bigLogDown_" + reportRow.modelData.key
                        text: "−"
                        Accessible.name: qsTr("%1 report down").arg(reportRow.modelData.name)
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: logPanel.bigPx
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(Theme.touchTarget, logPanel.bigPx * 1.6)
                        onClicked: reportRow.step(-1)
                    }
                    Label {
                        objectName: "bigLogReport_" + reportRow.modelData.key
                        text: reportRow.value
                        color: Theme.text
                        font.pixelSize: logPanel.bigPx
                        font.bold: true
                        font.family: "monospace"
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: logPanel.bigPx * 2.4
                    }
                    AppButton {
                        objectName: "bigLogUp_" + reportRow.modelData.key
                        text: "+"
                        Accessible.name: qsTr("%1 report up").arg(reportRow.modelData.name)
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: logPanel.bigPx
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(Theme.touchTarget, logPanel.bigPx * 1.6)
                        onClicked: reportRow.step(1)
                    }
                }
            }
        }
        // PTT takes the rest, as on Big buttons' own page (the on-screen
        // keyboard covers it while the call is typed).
        PttButton {
            objectName: "bigLogPtt"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacing
            Layout.preferredHeight: Theme.touchTarget * 2
            Layout.minimumHeight: Theme.touchTarget
            fontPx: Math.round(Math.max(Math.min(40, big.height * 0.1), Math.min(80, big.height * 0.06)))
        }
    }
}
