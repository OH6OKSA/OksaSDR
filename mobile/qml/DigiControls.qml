import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// FT8 and FT4 on this device: decode the receiver slot by slot, call CQ or
// tap a decode to answer it; the app runs the QSO (reports, RR73 / 73) and
// logs it. Transmit audio goes to the radio in DIGU, past the mic chain.
ColumnLayout {
    id: digi
    spacing: Theme.spacing / 2

    readonly property var qso: Engine.digi
    readonly property real slotSeconds: qso.slotSeconds || 15
    property real slotPos: 0

    // The drawer's view height (ControlsPanel gives it to a tab shown on its
    // own; 0 in the tablet's pinned panel). With List only (Michael Whaley's
    // option B) the tab fills it: the main buttons stay at the top, the
    // settings fold under More, and only the decode list scrolls, with a
    // bar that shows it does.
    property real viewHeight: 0
    // Scroll as chosen; until it is, a phone shows List only (roadmap 193,
    // the user's choice (a): Michael Walker VA3MW looked for a full screen
    // of decodes and the way was hard to find), a tablet the whole tab.
    readonly property bool listScroll: Engine.digiListScrollChosen ? Engine.digiListScroll : Theme.phoneLayout
    readonly property bool wantList: listScroll && viewHeight > 0
    property bool settingsOpen: false
    readonly property bool settingsShown: !wantList || settingsOpen
    // List only means the list (roadmap 169, the user: "eihän list only
    // tilassa näy normaalisti kaikki nuo painikkeet"): while decoding runs
    // only the slot, the CQ row with More and the QSO's state stay above
    // it. Decode, FT8 / FT4 and the bands are needed to start, so they show
    // until decoding runs; the Show choices go under More with the
    // settings. Before, the fixed part took 373 pt of a phone's 529 and
    // left the list four rows.
    // Nor while decoding is on for a TX slice outside DIGU / DIGL (Decode
    // pressed in LSB): Decode and the bands then say so and mend it.
    readonly property bool setupShown: settingsShown || !Engine.digiOwnsTx
    // Where the view is too low for the main buttons and four rows (a
    // phone on its side, the drawer at half height), the whole tab scrolls
    // as before. Decided from the view's height alone: judged from where
    // the list starts, the page's scroll bar coming and going moved that
    // (rows wrap), which flipped the choice, which moved the bar again, and
    // the app hung.
    readonly property real listRows: 4 * 34
    // The CQ row, the QSO's state and four rows fit from about 260 (it was
    // 520 for the whole fixed part: a 764 pt phone's 489 never reached it,
    // so List only scrolled the whole tab there and did nothing).
    readonly property bool useList: wantList && viewHeight >= 260
    // The height the tab is to have while the list fills the view (0: its
    // own height). ControlsPanel gives it, and the list takes what is over.
    readonly property real viewFill: useList ? viewHeight : 0

    Timer {
        interval: 200
        repeat: true
        running: digi.visible && Engine.digiRunning
        onTriggered: digi.slotPos = (Date.now() / 1000) % digi.slotSeconds
    }
    // Answer CQ or Keep CQ running: the presence limit counts down in the
    // status box (a touch anywhere starts it again).
    readonly property bool automatic: Engine.digiAutoAnswer || (Engine.digiAutoCq && qso.state === "cq")
    property int presenceLeft: 0
    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: digi.visible && digi.automatic
        onTriggered: digi.presenceLeft = Engine.digiPresenceLeft()
    }

    RowLayout {
        visible: digi.setupShown
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            objectName: "digiDecode"
            text: Engine.digiRunning ? qsTr("Decoding") : qsTr("Decode")
            checked: Engine.digiRunning
            enabled: Engine.connectionState === "connected"
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiRunning = !Engine.digiRunning
        }
        Repeater {
            model: ["FT8", "FT4"]
            delegate: AppButton {
                required property string modelData
                text: modelData
                checked: Engine.digiProtocol === modelData
                Layout.preferredWidth: 64
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiProtocol = modelData
            }
        }
    }

    // A decoding slice on another band than its panadapter (roadmap 92):
    // the radio gives it noise or another slice's audio there. The app gives
    // a slice moved to another band a panadapter of its own; this shows what
    // was left that way (no panadapter free, tuned there by a drag, another
    // program).
    readonly property string offBandSlices: {
        if (!Engine.digiRunning) return ""
        const letters = []
        for (const s of Engine.slices) {
            if (s.offBand && (s.mode === "DIGU" || s.mode === "DIGL" || s.tx)) letters.push(s.letter)
        }
        return letters.join(", ")
    }
    Rectangle {
        objectName: "digiOffBand"
        Layout.fillWidth: true
        visible: digi.offBandSlices !== ""
        implicitHeight: offBandText.implicitHeight + 12
        radius: 6
        color: "#7a1a1d"
        border.color: "#ff5a5f"
        Label {
            id: offBandText
            anchors.fill: parent
            anchors.margins: 6
            text: qsTr("Slice %1 is on another band than its panadapter: the radio does not receive it there. Tune it back, or give that band a panadapter of its own (+ Pan).")
                  .arg(digi.offBandSlices)
            color: "#ffffff"
            font.pixelSize: Theme.px(13)
            wrapMode: Text.WordWrap
        }
    }
    // The protocol's dial frequency on each band (DIGU).
    Flow {
        visible: digi.setupShown
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: Engine.digiFrequencies
            delegate: AppButton {
                required property var modelData
                text: modelData.band
                checked: Math.abs(Engine.frequencyMhz - modelData.mhz) < 0.0005 && Engine.mode === "DIGU"
                width: Math.max(56, Theme.buttonWidth * 0.8)
                height: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiTune(modelData.mhz)
            }
        }
    }

    // The slot: where in it we are, and whether the next one is ours.
    ProgressBar {
        Layout.fillWidth: true
        visible: Engine.digiRunning
        from: 0
        to: digi.slotSeconds
        value: digi.slotPos
    }

    // The row's labels in the buttons' type, and how much of that fits: with
    // More the row has five buttons, which a 320 pt pinned panel cut to
    // "Answer…" and "Keep…". The type gives way a little instead.
    TextMetrics {
        id: cqRowText
        font.pixelSize: Theme.buttonFont
        text: cqButton.text + qsTr("Keep CQ") + qsTr("Answer CQ") + qsTr("Halt TX") + (digi.wantList ? qsTr("More") : "")
    }
    // Only with More in the row (List only): without it the row is as it
    // always was.
    readonly property int cqRowFont: !wantList ? Theme.buttonFont : Math.round(Theme.buttonFont
        * Math.max(0.7, Math.min(1, (width - 4 * 4 - 5 * 12) / Math.max(1, cqRowText.advanceWidth))))
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            id: cqButton
            objectName: "digiCq"
            // With More in the row, as wide as its own label asks: a short
            // one then does not take a long one's room.
            Layout.preferredWidth: digi.wantList ? implicitContentWidth + 12 : -1
            font.pixelSize: digi.cqRowFont
            // The direction in the button itself: CQ DX (Kurt DL5EAG).
            readonly property string cqText: Engine.digiCqDirectable && Engine.digiCqTarget.length > 0
                                             ? qsTr("CQ %1").arg(Engine.digiCqTarget) : qsTr("CQ")
            text: Engine.txLocked ? qsTr("%1 · SB").arg(cqText) : cqText
            Accessible.name: Engine.txLocked ? qsTr("%1, transmit locked, standby").arg(cqText) : cqText
            checked: digi.qso.state === "cq"
            // A Hound never calls CQ: it calls the Fox.
            enabled: Engine.transmitAvailable && !Engine.digiHound
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiCallCq()
            // Transmit lock in standby: held for a second, it arms (a held
            // press skips clicked, so unlocked it acts as a tap).
            onPressAndHold: Engine.txLocked ? Engine.armTransmit() : Engine.digiCallCq()
        }
        // Keep CQ: back to CQ after each QSO our CQ brought. Beside CQ: in
        // the slot row a narrow panel cut it to "Keep …".
        AppButton {
            objectName: "digiAutoCq"
            // With More in the row, as wide as its own label asks: a short
            // one then does not take a long one's room.
            Layout.preferredWidth: digi.wantList ? implicitContentWidth + 12 : -1
            text: qsTr("Keep CQ")
            font.pixelSize: digi.cqRowFont
            checked: Engine.digiAutoCq
            enabled: !Engine.digiHound
            Accessible.name: qsTr("Call CQ again after each QSO")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiAutoCq = !Engine.digiAutoCq
        }
        // Answer CQ (Kurt DL5EAG): the app answers CQs by itself, its calls
        // per station set under More (2-20), then another; Halt TX ends it.
        AppButton {
            objectName: "digiAutoAnswer"
            // With More in the row, as wide as its own label asks: a short
            // one then does not take a long one's room.
            Layout.preferredWidth: digi.wantList ? implicitContentWidth + 12 : -1
            text: qsTr("Answer CQ")
            font.pixelSize: digi.cqRowFont
            checked: Engine.digiAutoAnswer
            enabled: (Engine.transmitAvailable || Engine.digiAutoAnswer) && !Engine.digiHound
            Accessible.name: Engine.digiAutoAnswer ? qsTr("Stop answering CQs by itself") : qsTr("Answer CQs by itself")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiAutoAnswer = !Engine.digiAutoAnswer
        }
        AppButton {
            objectName: "digiHalt"
            // With More in the row, as wide as its own label asks: a short
            // one then does not take a long one's room.
            Layout.preferredWidth: digi.wantList ? implicitContentWidth + 12 : -1
            text: qsTr("Halt TX")
            font.pixelSize: digi.cqRowFont
            // Also while a spotted station is waited for (roadmap 224).
            enabled: digi.qso.state !== "idle" || digi.qso.transmitting || Engine.digiAutoAnswer || !!digi.qso.waitFor
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiHalt()
        }
        // List only: the settings below fold away; More shows them.
        AppButton {
            objectName: "digiMore"
            // With More in the row, as wide as its own label asks: a short
            // one then does not take a long one's room.
            Layout.preferredWidth: digi.wantList ? implicitContentWidth + 12 : -1
            visible: digi.wantList
            text: qsTr("More")
            font.pixelSize: digi.cqRowFont
            checked: digi.settingsOpen
            Accessible.name: digi.settingsOpen ? qsTr("Hide the FTx settings") : qsTr("Show the FTx settings")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: digi.settingsOpen = !digi.settingsOpen
        }
    }
    // How the tab scrolls in the drawer: all of it, or the decode list only
    // under the fixed main buttons.
    RowLayout {
        visible: digi.viewHeight > 0 && digi.settingsShown
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("Scroll")
            color: Theme.textDim
        }
        AppButton {
            objectName: "digiScrollTab"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Scroll")).arg(text)
            text: qsTr("Whole tab")
            checked: !digi.listScroll
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiListScroll = false
        }
        AppButton {
            objectName: "digiScrollList"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Scroll")).arg(text)
            text: qsTr("List only")
            checked: digi.listScroll
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.digiListScroll = true
                digi.settingsOpen = false
            }
        }
    }
    // Hound (roadmap 10 a): working a DXpedition's Fox in FT8 DXpedition
    // mode by WSJT-X's rules (Michael Whaley, Gianluca IK6QOO). The list
    // shows the Fox's part below 1000 Hz; All Hz the Hounds' pileup too.
    RowLayout {
        visible: digi.settingsShown && Engine.digiProtocol === "FT8"
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("DXpedition")
            color: Theme.textDim
        }
        AppButton {
            objectName: "digiHoundOff"
            Accessible.name: qsTr("%1: %2").arg(qsTr("DXpedition")).arg(text)
            text: qsTr("Off")
            checked: !Engine.digiHound
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiHound = false
        }
        AppButton {
            objectName: "digiHound"
            text: qsTr("Hound")
            checked: Engine.digiHound
            enabled: Engine.transmitAvailable || Engine.digiHound
            Accessible.name: qsTr("Hound: call a DXpedition's Fox")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiHound = true
        }
        AppButton {
            objectName: "digiHoundAllHz"
            visible: Engine.digiHound
            text: qsTr("All Hz")
            checked: Engine.digiHoundAllHz
            Accessible.name: Engine.digiHoundAllHz ? qsTr("Show only the Fox's part below 1000 Hz")
                                                   : qsTr("Show the stations above 1000 Hz too")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiHoundAllHz = !Engine.digiHoundAllHz
        }
    }
    // CQ's slot (even = 0 / 30 s, odd = 15 / 45 s in FT8; Auto = the next).
    RowLayout {
        visible: digi.settingsShown
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("CQ slot")
            color: Theme.textDim
        }
        Repeater {
            model: [{ key: "auto", text: qsTr("Auto") }, { key: "even", text: qsTr("Even") },
                    { key: "odd", text: qsTr("Odd") }]
            delegate: AppButton {
                required property var modelData
                objectName: "digiCqParity_" + modelData.key
                Accessible.name: qsTr("%1: %2").arg(qsTr("CQ slot")).arg(text)
                text: modelData.text
                checked: Engine.digiCqParity === modelData.key
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiCqParity = modelData.key
            }
        }
    }
    // CQ to (Kurt DL5EAG): All, or a direction from the user's own list
    // (CQ DX, CQ NA, ...); + adds one, a press and hold removes one. FT8
    // carries 1-4 letters or 3 digits, and only with a standard call.
    Flow {
        objectName: "digiCqTargetRow"
        visible: digi.settingsShown
        Layout.fillWidth: true
        spacing: 4
        Label {
            text: qsTr("CQ to")
            color: Theme.textDim
            height: Theme.touchTarget
            verticalAlignment: Text.AlignVCenter
            rightPadding: 4
        }
        AppButton {
            objectName: "digiCqTarget_all"
            Accessible.name: qsTr("%1: %2").arg(qsTr("CQ to")).arg(text)
            text: qsTr("All")
            checked: Engine.digiCqTarget.length === 0
            enabled: Engine.digiCqDirectable
            width: Math.max(56, implicitWidth)
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiCqTarget = ""
        }
        Repeater {
            model: Engine.digiCqTargets
            delegate: AppButton {
                required property string modelData
                objectName: "digiCqTarget_" + modelData
                text: modelData
                checked: Engine.digiCqTarget === modelData
                enabled: Engine.digiCqDirectable
                Accessible.name: qsTr("CQ %1").arg(modelData)
                Accessible.description: qsTr("Press and hold to remove it")
                width: Math.max(56, implicitWidth)
                height: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiCqTarget = modelData
                onPressAndHold: Engine.removeDigiCqTarget(modelData)
            }
        }
        AppButton {
            objectName: "digiCqTargetAdd"
            text: "+"
            checked: cqTargetEntry.visible
            enabled: Engine.digiCqDirectable
            Accessible.name: qsTr("Add a CQ direction")
            width: Theme.touchTarget
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            onClicked: {
                cqTargetEntry.visible = !cqTargetEntry.visible
                cqTargetField.text = ""
            }
        }
    }
    RowLayout {
        id: cqTargetEntry
        visible: false
        Layout.fillWidth: true
        spacing: 4
        TextField {
            id: cqTargetField
            objectName: "digiCqTargetField"
            Layout.fillWidth: true
            // Sized to what it holds, about 14 characters (roadmap 158)
            Layout.maximumWidth: Math.ceil(14 * font.pixelSize * 0.62) + leftPadding + rightPadding
            placeholderText: qsTr("e.g. SA, JA, POTA or 001")
            maximumLength: 4
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            Accessible.name: qsTr("CQ direction")
            // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
            EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
            onAccepted: cqTargetSave.clicked()
        }
        AppButton {
            id: cqTargetSave
            objectName: "digiCqTargetSave"
            text: qsTr("Add")
            enabled: /^([A-Za-z]{1,4}|[0-9]{3})$/.test(cqTargetField.text.trim())
            focusPolicy: Qt.NoFocus
            onClicked: {
                if (Engine.addDigiCqTarget(cqTargetField.text)) {
                    cqTargetField.text = ""
                    cqTargetEntry.visible = false
                }
            }
        }
    }
    Label {
        visible: digi.settingsShown && (cqTargetEntry.visible || !Engine.digiCqDirectable)
        Layout.fillWidth: true
        text: Engine.digiCqDirectable
              ? qsTr("1 to 4 letters (a continent, a country, POTA) or 3 digits: what FT8 carries after CQ.")
              : qsTr("%1 is not a standard callsign: FT8 sends its CQ without a direction (as WSJT-X does).")
                .arg(digi.qso.myCall || qsTr("The radio's callsign"))
        color: Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }
    // Auto stops, Answer CQ's calls and PSK Reporter are set seldom: Setup ›
    // FTx (roadmap 131).
    AppButton {
        objectName: "openFtxSetup"
        visible: digi.settingsShown
        Layout.fillWidth: true
        text: qsTr("Auto stops %1 min · %2 calls · PSK Reporter %3…").arg(Engine.digiPresenceMin).arg(Engine.digiAnswerCalls)
              .arg(Engine.pskReporter ? qsTr("on") : qsTr("off"))
        focusPolicy: Qt.NoFocus
        onClicked: Engine.openSetup(13)
    }
    // The device clock, judged from the decodes' DT: FT8 fails beyond about
    // a second.
    Rectangle {
        Layout.fillWidth: true
        visible: digi.qso.clockWarn === true
        implicitHeight: clockText.implicitHeight + 12
        radius: 6
        color: "#7a1a1d"
        border.color: "#ff5a5f"
        Label {
            id: clockText
            anchors.fill: parent
            anchors.margins: 6
            text: qsTr("This device's clock looks %1 s off (the decodes' DT). FT8 needs it within a second: set Date & Time to automatic.")
                  .arg((digi.qso.clockOffset > 0 ? "+" : "") + Number(digi.qso.clockOffset || 0).toFixed(1))
            color: "#ffffff"
            font.pixelSize: Theme.px(13)
            wrapMode: Text.WordWrap
        }
    }
    // FT8 / FT4 run on a time server's time: which server and how far off
    // the device's own clock is, always (the user: shown only from 0.3 s,
    // an iPhone's few ms never showed that the sync ran at all). Dim while a
    // server is in use, however far off the device's clock: the app runs on
    // the server's time, so nothing is wrong (the user: an Android phone's
    // +496 ms in amber looked like a fault). Amber only without a server,
    // when the decodes' DT says the device's clock is off.
    Label {
        objectName: "digiClock"
        Layout.fillWidth: true
        readonly property string server: digi.qso.clockServer || ""
        readonly property real correction: Number(digi.qso.clockCorrection || 0)
        visible: digi.settingsShown && (Engine.digiRunning || server !== "")
        text: server !== ""
              ? qsTr("FT8 / FT4 run on %1's time; this device's clock is %2 ms off.")
                    .arg(server).arg((correction < 0 ? "+" : correction > 0 ? "−" : "")
                                     + Math.round(Math.abs(correction) * 1000))
              : qsTr("FT8 / FT4 run on this device's clock: no time server reached yet.")
        color: server === "" && Math.abs(Number(digi.qso.clockOffset || 0)) >= 0.3 ? Theme.warning : Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    RowLayout {
        visible: digi.settingsShown
        Layout.fillWidth: true
        Label {
            text: qsTr("TX %1 Hz").arg(Engine.digiTxHz)
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "digiZoom"
            text: qsTr("Zoom")
            Accessible.name: qsTr("Fit the display to the audio band")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiZoom()
        }
        AppButton {
            objectName: "digiFreeTx"
            text: qsTr("Free")
            Accessible.name: qsTr("Move TX to the clearest frequency heard")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.digiFindFreeTx()
        }
        AppButton {
            text: "−"
            Accessible.name: qsTr("Lower TX audio frequency by 1 Hz")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            autoRepeat: true
            onClicked: Engine.digiTxHz = Engine.digiTxHz - 1
        }
        AppButton {
            text: "+"
            Accessible.name: qsTr("Raise TX audio frequency by 1 Hz")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            autoRepeat: true
            onClicked: Engine.digiTxHz = Engine.digiTxHz + 1
        }
    }
    // The transmit audio level (WSJT-X's Pwr): 0 dB drives the radio fully,
    // so the power out is the TX tab's power; lower it to back off.
    RowLayout {
        visible: digi.settingsShown
        Layout.fillWidth: true
        Label {
            text: qsTr("TX level %1 dB").arg(Engine.digiTxLevelDb)
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "digiTxLevelDown"
            text: "−"
            Accessible.name: qsTr("Lower the TX level by 1 dB")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiTxLevelDb > -20
            onClicked: Engine.digiTxLevelDb = Engine.digiTxLevelDb - 1
        }
        AppButton {
            objectName: "digiTxLevelUp"
            text: "+"
            Accessible.name: qsTr("Raise the TX level by 1 dB")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiTxLevelDb < 0
            onClicked: Engine.digiTxLevelDb = Engine.digiTxLevelDb + 1
        }
    }
    // What the last transmission gave: the answer to "is the TX level
    // right?" (below the TX tab's power: drive or the band's RF power).
    Label {
        objectName: "digiLastTx"
        Layout.fillWidth: true
        visible: digi.settingsShown && (digi.qso.lastTxW || 0) > 0
        text: (Engine.rfPowerIsPercent ? qsTr("Last TX: %1 W%2 (RF power %3 %)") : qsTr("Last TX: %1 W%2 (RF power %3 W)"))
              .arg(Number(digi.qso.lastTxW || 0).toFixed(1))
              .arg((digi.qso.lastTxSwr || 0) > 0 ? qsTr(", SWR %1").arg(Number(digi.qso.lastTxSwr).toFixed(1)) : "")
              .arg(Engine.rfPower)
        color: Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }

    // The QSO's state. While a QSO or CQ runs it is a box that stands out:
    // amber while waiting, red while transmitting.
    Rectangle {
        Layout.fillWidth: true
        readonly property bool active: digi.qso.state !== "idle" || Engine.digiAutoAnswer || !!digi.qso.waitFor
        visible: statusText.text.length > 0
        implicitHeight: statusText.implicitHeight + (active ? 14 : 0)
        radius: 6
        color: !active ? "transparent" : digi.qso.transmitting ? "#d7262d" : "#ffbf00"
        Label {
            id: statusText
            objectName: "digiStatus"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: parent.active ? 8 : 0
            anchors.rightMargin: parent.active ? 8 : 0
            text: {
                const s = digi.qso
                let line = s.status || ""
                if (s.state !== "idle" && s.txMsg)
                    line += (line ? "\n" : "") + (s.transmitting ? qsTr("TX: ") : qsTr("Next (%1): ").arg(s.parity)) + s.txMsg
                if (digi.automatic) {
                    const left = digi.presenceLeft
                    line += (line ? "\n" : "") + qsTr("Auto stops in %1 without a touch")
                            .arg(Math.floor(left / 60) + ":" + String(left % 60).padStart(2, "0"))
                }
                return line
            }
            color: !parent.active ? Theme.text : digi.qso.transmitting ? "#ffffff" : "#101010"
            font.pixelSize: parent.active ? 15 : 13
            font.bold: parent.active
            wrapMode: Text.WordWrap
        }
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.digiRunning && Engine.digiDecodes.length === 0
        text: Engine.digiHound
              ? qsTr("Hound: decodes appear at the end of each %1 s slot. Tap the Fox's CQ or a message from it (below 1000 Hz) to call it.").arg(digi.slotSeconds)
              : qsTr("Decodes appear at the end of each %1 s slot. Tap a CQ to answer it; tap the waterfall inside the passband to set the TX audio frequency.").arg(digi.slotSeconds)
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // WSJT-X's highlighting colours (its defaults, in its priority order):
    // a light ground with dark text, so a row reads at a glance.
    function hlColor(hl) {
        switch (hl) {
        case "mycall": return "#ff6666"
        case "continent": return "#ff0063"
        case "continentBand": return "#ff99c2"
        case "cqzone": return "#ffbf00"
        case "cqzoneBand": return "#ffe499"
        case "dxcc": return "#ff00ff"
        case "dxccBand": return "#ffaaff"
        case "newcall": return "#00ffff"
        case "newcallBand": return "#99ffff"
        case "cq": return "#66ff66"
        case "tx": return "#ffff00"
        default: return "transparent"
        }
    }
    // What a highlight colour means, in words for a screen reader (a CQ's
    // own green is left out: the message says CQ).
    function hlWords(hl) {
        switch (hl) {
        case "mycall": return qsTr("your call")
        case "continent": return qsTr("new continent")
        case "continentBand": return qsTr("new continent on this band")
        case "cqzone": return qsTr("new zone")
        case "cqzoneBand": return qsTr("new zone on this band")
        case "dxcc": return qsTr("new DXCC")
        case "dxccBand": return qsTr("new DXCC on this band")
        case "newcall": return qsTr("new call")
        case "newcallBand": return qsTr("new call on this band")
        case "b4": return qsTr("worked before")
        case "tx": return qsTr("your transmission")
        default: return ""
        }
    }
    // What the list shows; your call, your TX and the station in the QSO
    // always show.
    function passes(row) {
        if (row.hl === "mycall" || row.hl === "tx" || (qso.dx && row.call === qso.dx)) {
            return true
        }
        // Hound: the Fox transmits below 1000 Hz, the Hounds above (WSJT-X
        // decodes only below 1000 Hz unless Rx All Freqs).
        if (Engine.digiHound && !Engine.digiHoundAllHz && row.freq >= 1000) {
            return false
        }
        switch (Engine.digiFilter) {
        case "cq": return row.cq === true
        case "pota": return row.cq === true && /^CQ POTA /i.test(row.text)
        case "new": return ["continent", "continentBand", "cqzone", "cqzoneBand", "dxcc", "dxccBand"].indexOf(row.hl) >= 0
        case "dxcc": return row.newDxcc > 0
        case "newcall": return row.newCall > 0
        default: return true
        }
    }
    // The row tapped last flashes for a second. Kept here, not in the row:
    // the answer changes the QSO, the list is rebuilt and the rows made anew.
    property string flashKey: ""
    property real flashAt: 0
    Timer {
        interval: 1400
        running: digi.flashKey !== ""
        onTriggered: digi.flashKey = ""
    }
    // The shown rows with their index in Engine.digiDecodes (a tap answers
    // by that index).
    readonly property var shown: {
        const rows = Engine.digiDecodes
        const out = []
        for (let i = 0; i < rows.length && out.length < 40; ++i) {
            if (passes(rows[i])) {
                out.push({ row: rows[i], index: i })
            }
        }
        return out
    }

    // Six choices: one row where they fit, else two of three (the iPad's
    // pinned panel cut "New DXCC" and "New call" short).
    GridLayout {
        id: showRow
        visible: digi.settingsShown
        Layout.fillWidth: true
        readonly property bool oneRow: width >= 560
        columns: oneRow ? 7 : 4
        columnSpacing: 4
        rowSpacing: 4
        Label {
            text: qsTr("Show")
            color: Theme.textDim
            Layout.rowSpan: showRow.oneRow ? 1 : 2
        }
        Repeater {
            model: [{ key: "all", text: qsTr("All") }, { key: "cq", text: qsTr("CQ") },
                    { key: "pota", text: qsTr("CQ POTA") }, { key: "new", text: qsTr("New") }, { key: "dxcc", text: qsTr("New DXCC") },
                    { key: "newcall", text: qsTr("New call") }]
            delegate: AppButton {
                required property var modelData
                required property int index
                objectName: "digiFilter_" + modelData.key
                Accessible.name: qsTr("%1: %2").arg(qsTr("Show")).arg(text)
                text: modelData.text
                checked: Engine.digiFilter === modelData.key
                Layout.fillWidth: true
                // Two rows: All, CQ, CQ POTA over New, New DXCC, New call.
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.digiFilter = modelData.key
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.digiFilter !== "all" && Engine.digiDecodes.length > 0
        text: digi.shown.length > 0
              ? qsTr("%1 of the last %2 decodes").arg(digi.shown.length).arg(Engine.digiDecodes.length)
              : qsTr("None of the last %1 decodes match").arg(Engine.digiDecodes.length)
        color: Theme.textDim
        font.pixelSize: Theme.px(11)
    }
    Label {
        Layout.fillWidth: true
        visible: digi.settingsShown && Engine.digiDecodes.length > 0
        text: qsTr("Green CQ · pink / magenta new DXCC · amber new CQ zone · red new continent or your call · cyan a new call (light: new on this band) · yellow your TX · grey worked before on this band and mode")
        color: Theme.textDim
        font.pixelSize: Theme.px(11)
        wrapMode: Text.WordWrap
    }

    // Decodes from every DIGU slice, newest first: slice, UTC, SNR, DT,
    // audio Hz, message. A tap answers (TX and the display move to that
    // slice).
    component DecodeRow: ItemDelegate {
        required property var modelData
        readonly property var row: modelData.row
        readonly property string key: row.utc + "|" + row.freq + "|" + row.text
        Component.onCompleted: {
            const left = 1350 - (Date.now() - digi.flashAt)
            if (digi.flashKey === key && left > 0) {
                flashAnim.loops = Math.max(1, Math.ceil(left / 450))
                flashAnim.restart()
            }
        }
        readonly property color ground: digi.hlColor(row.hl)
        readonly property bool lit: row.hl && row.hl !== "b4"
        Layout.fillWidth: true
        implicitHeight: Math.max(32, decodeText.implicitHeight + 8)
        padding: 2
        // The message, its SNR and audio Hz, and in words what the row's
        // colour says; a double tap answers, as a tap does.
        Accessible.name: [row.text,
                          row.snr === "" || row.snr === undefined ? "" : qsTr("%1 dB").arg((row.snr >= 0 ? "+" : "") + row.snr),
                          qsTr("%1 Hz").arg(row.freq),
                          digi.hlWords(row.hl)].filter(t => t && t.length > 0).join(", ")
        Accessible.description: qsTr("Answers this station: transmits in the next slot")
        background: Rectangle {
            color: parent.ground
            radius: 4
            // A tapped row flashes, so the answer shows where the
            // finger is even with the status line scrolled away.
            Rectangle {
                id: flash
                anchors.fill: parent
                radius: 4
                color: "#ffff00"
                border.color: "#ffffff"
                border.width: 2
                opacity: 0
            }
        }
        SequentialAnimation {
            id: flashAnim
            loops: 3
            NumberAnimation { target: flash; property: "opacity"; to: 0.9; duration: 80 }
            PauseAnimation { duration: 170 }
            NumberAnimation { target: flash; property: "opacity"; to: 0; duration: 200 }
        }
        contentItem: Label {
            id: decodeText
            // FT4's slots are 7.5 s: its time with the seconds, as WSJT-X's.
            text: "%1 %2 %3 %4 %5  %6".arg(row.slice || " ")
                                      .arg(row.protocol === "FT4" ? row.utc : row.utc.substring(0, 4))
                                      .arg(row.snr === "" ? "   " : ((row.snr >= 0 ? "+" : "") + row.snr))
                                      .arg(row.dt === "" ? "   " : row.dt)
                                      .arg(row.freq)
                                      .arg(row.text)
            color: parent.lit ? "#101010" : row.hl === "b4" ? Theme.textDim : Theme.text
            font.family: "monospace"
            font.pixelSize: Theme.px(13)
            elide: Text.ElideRight
        }
        onClicked: {
            digi.flashKey = key
            digi.flashAt = Date.now()
            flashAnim.loops = 3
            flashAnim.restart()
            Engine.digiAnswer(modelData.index)
        }
    }
    Repeater {
        model: digi.useList ? [] : digi.shown
        delegate: DecodeRow {}
    }
    // (One more gap above the list, where the mark of its start was: the
    // list stays where it was drawn.)
    Item {
        visible: digi.useList
        Layout.fillWidth: true
    }
    // List only: the list in the rest of the view. The tab is as high as
    // the view (viewFill) and the list is the one item that stretches; four
    // rows at least, so the page scrolls a little if the settings open on a
    // phone. Its height used to be worked out from where the list starts
    // (the view's height less the list's y), which put the layout's own
    // result back into what it is laid out from: with decodes in the list
    // and the tab loaded but not shown (another tab open in the drawer: no
    // width), the tab's height went down a point a pass and the app stood
    // still (the preview of build 213: a OnePlus Open froze after unfolding
    // with FT8 running; the same with the Log tab opened on a tablet).
    ListView {
        id: decodeList
        objectName: "digiDecodeList"
        visible: digi.useList
        Layout.fillWidth: true
        Layout.fillHeight: digi.useList
        Layout.minimumHeight: digi.useList ? digi.listRows : 0
        Layout.preferredHeight: digi.useList ? digi.listRows : 0
        clip: true
        spacing: digi.spacing
        boundsBehavior: Flickable.StopAtBounds
        model: digi.useList ? digi.shown : []
        readonly property bool overflows: contentHeight > height + 1
        ScrollBar.vertical: AppScrollBar { policy: decodeList.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        delegate: DecodeRow {
            width: decodeList.width - (decodeList.overflows ? decodeList.ScrollBar.vertical.width + 2 : 0)
        }
    }
}
