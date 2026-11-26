import QtQuick
import QtQuick.Controls
import AetherMobile

// Hold-to-talk PTT; the straight key (KEY) in CW modes; the tune carrier's
// off switch while tuning. One component for the page and the controls
// drawer, which carries its own copy at the bottom so the button is never
// covered.
AppButton {
    id: ptt
    property int fontPx: 32
    // A press here keeps the text field typed in and its on-screen keyboard
    // (TextFocusRelease; roadmap 142): call while logging, then go on typing.
    readonly property bool keepsTextFocus: true
    // Stays enabled while held: disabling a pressed button cancels the
    // press, which would unkey mid-transmission.
    readonly property bool keyMode: Engine.cwMode && !Engine.tuning
    // Tap on, tap off (TX tab), voice only; the straight key always holds.
    readonly property bool toggleMode: Engine.pttToggle && !keyMode && !Engine.tuning
    // CW paddles (CW tab > Key): dit and dah side by side over the button,
    // each its own touch so both can be held (iambic squeeze).
    readonly property bool paddleMode: keyMode && Engine.cwPaddles
    // "left" or "right": this button is one paddle only, and the page puts
    // the other where the other thumb is (a phone: one at each edge, the
    // Controls button between); both halves share paddleState (dit, dah).
    property string paddleSide: "both"
    property QtObject paddleState: null
    // FT8 / FT4 decoding keys the radio itself: the pages hide PTT then, so
    // the display gets the room (Halt TX is on the Digi tab). RADE is voice
    // and keeps it, and so does a TX slice in a voice mode while decoding is
    // on (Engine.digiOwnsTx: it had no PTT at all).
    readonly property bool digiHidden: Engine.digiOwnsTx && !Engine.rade
    // A finger is on it: the button itself or one of its paddles (the side
    // drawer's copy stays while held, OperatePage).
    readonly property bool held: pressed || paddles.dit || paddles.dah || skirt.pressed
    // Room to press under the button, not drawn (pt). For a button right
    // above the on-screen keyboard: on the user's iPad in landscape (build
    // 213) 14 of 30 presses meant for PTT came in 0-24 pt under its edge
    // (y 483-507 for a button at 417-483), which keyed nothing, let the Log
    // tab's field go and put the keyboard away. A press there keys as one
    // on the button; nothing else is under it but the keyboard.
    property real touchBelow: 0
    // Transmit lock (TX tab, Larry Averbeck): in standby the button says SB
    // and a second's hold arms it (nothing is sent); armed, the time left
    // shows under the label as OA m:ss.
    readonly property bool txStandby: Engine.txLocked && !Engine.tuning
    property bool arming: false
    function armedLeft() {
        const s = Engine.txArmedSeconds
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    enabled: (keyMode ? Engine.cwKeyAvailable : Engine.transmitAvailable) || Engine.tuning || pressed || skirt.pressed
    text: Engine.tuning ? qsTr("TUNE") : txStandby ? qsTr("SB")
          : keyMode ? (Engine.cwKeyDown ? qsTr("KEY ●") : qsTr("KEY"))
          : Engine.pttLatched ? qsTr("TX ■") : Engine.transmitting ? qsTr("TX") : qsTr("PTT")
    focusPolicy: Qt.NoFocus
    font.pixelSize: fontPx
    font.bold: true
    // In words for a screen reader (its double tap and hold keys, as a
    // finger's hold does).
    Accessible.name: Engine.tuning ? qsTr("Tune, transmitting") : txStandby ? qsTr("Transmit locked, standby")
                     : keyMode ? qsTr("CW key")
                     : Engine.pttLatched ? qsTr("Transmitting, tap to stop") : Engine.transmitting ? qsTr("Transmitting")
                     : qsTr("Push to talk")
    Accessible.description: txStandby ? qsTr("Hold for a second to arm transmitting")
                            : toggleMode ? qsTr("Tap to transmit, tap again to stop")
                            : qsTr("Double tap and hold to transmit")
    // A screen reader's double tap is Qt's click(): press and release at
    // once. Through onPressed / onReleased that keyed the radio for no time
    // at all (PTT down and up in one go, a dit on the CW key) and could never
    // arm the transmit lock, whose hold it cut to nothing. So the double tap
    // does what a tap means where a tap is meant (stop the tune carrier,
    // switch in Tap on / off) and nothing where a hold is (Hold to talk, the
    // key, standby): those are keyed by the screen reader's double tap and
    // hold, which passes a real touch through and keys as a finger's hold.
    Accessible.onPressAction: {
        if (!ptt.enabled || ptt.paddleMode)
            return
        if (Engine.tuning)
            Engine.stopTransmit("PTT (screen reader) during tune")
        else if (ptt.toggleMode && !ptt.txStandby)
            Engine.togglePtt()
    }
    // Ready, unavailable and transmitting must look different at a glance:
    // ready has a bright border and label, unavailable is dim.
    contentItem: Item {
        Accessible.ignored: true
        visible: !ptt.paddleMode || !ptt.enabled || ptt.txStandby
        implicitHeight: label.implicitHeight + (lockLine.visible ? lockLine.implicitHeight : 0)
        Column {
            anchors.centerIn: parent
            Text {
                id: label
                anchors.horizontalCenter: parent.horizontalCenter
                text: ptt.text
                font: ptt.font
                color: ptt.txStandby ? Theme.textDim : ptt.enabled ? "white" : Theme.textDim
                opacity: ptt.enabled ? 1.0 : 0.5
            }
            Text {
                id: lockLine
                anchors.horizontalCenter: parent.horizontalCenter
                visible: (Engine.txLock || ptt.txStandby) && !Engine.tuning && ptt.enabled
                text: ptt.txStandby ? qsTr("hold to arm") : qsTr("OA %1").arg(ptt.armedLeft())
                font.pixelSize: Math.max(11, ptt.fontPx * 0.4)
                color: ptt.txStandby ? Theme.textDim : "white"
            }
        }
    }
    background: Rectangle {
        radius: Theme.radius
        // The paddles draw their own faces.
        opacity: ptt.paddleMode && ptt.enabled ? 0 : 1
        color: Engine.transmitting || Engine.tuning || Engine.cwKeyDown ? Theme.transmit
             : ptt.enabled && !ptt.txStandby ? Theme.pttReady : Theme.surface
        border.width: Engine.transmitting ? 3 : ptt.enabled && !ptt.txStandby ? 2 : 1
        border.color: Engine.transmitting ? "white"
                    : ptt.enabled && !ptt.txStandby ? Theme.transmit : Theme.surfaceRaised
    }
    // Hold to transmit; release (or losing the touch) always unkeys. While
    // the tune carrier is on, PTT is its off switch.
    // In tap on/off mode a press switches and the release does nothing.
    // A tap felt on press and release, voice only: the straight key and the
    // paddles key too fast for it.
    // y: the press down from the button's top edge, for the log (not the
    // straight key's, which keys every dit).
    function pressAction(y) {
        if (!keyMode)
            Engine.notePttPress(y, height, touchBelow)
        if (txStandby) {
            arming = true
            Engine.armHold(true)
            return
        }
        if (paddleMode) return
        if (!keyMode) Engine.haptic("press")
        if (Engine.tuning) Engine.stopTransmit("PTT pressed during tune")
        else if (keyMode) Engine.setCwKey(true)
        else if (toggleMode) Engine.togglePtt()
        else Engine.setPtt(true)
    }
    function releaseAction() {
        if (arming) {
            arming = false
            Engine.armHold(false)
            return
        }
        if (paddleMode) return
        if (keyMode) Engine.setCwKey(false, "key released")
        else if (!toggleMode) {
            Engine.haptic("release")
            Engine.setPtt(false, "button released")
        }
    }
    function cancelAction() {
        if (arming) {
            arming = false
            Engine.armHold(false)
            return
        }
        if (paddleMode) return
        if (keyMode) Engine.setCwKey(false, "touch cancelled by the system")
        else if (!toggleMode) Engine.setPtt(false, "touch cancelled by the system")
    }
    onPressed: pressAction(pressY)
    onReleased: releaseAction()
    onCanceled: cancelAction()
    // The room under the button (touchBelow): the same press, release and
    // cancel as the button's own, and held as long as the finger is down
    // wherever it rolls. Not for the paddles, which have their own touches.
    // While the button cannot key it takes nothing, as the button itself.
    MouseArea {
        id: skirt
        objectName: "pttTouchBelow"
        y: ptt.height
        width: ptt.width
        height: ptt.touchBelow
        visible: ptt.touchBelow > 0
        enabled: !ptt.paddleMode
        onPressed: (mouse) => ptt.pressAction(ptt.height + mouse.y)
        onReleased: ptt.releaseAction()
        onCanceled: ptt.cancelAction()
    }

    Item {
        id: paddles
        visible: ptt.paddleMode && ptt.enabled && !ptt.txStandby
        anchors.fill: parent
        z: 2
        // The keyer swaps the paddles itself (the radio's setting): the left
        // pad is always the "dit" input; only its label follows the swap.
        readonly property bool swapped: Engine.cwSwapPaddles
        readonly property bool dit: leftPad.visible && leftPad.tap.pressed
        readonly property bool dah: rightPad.visible && rightPad.tap.pressed
        function report() {
            if (ptt.paddleState) {
                if (ptt.paddleSide !== "right") ptt.paddleState.dit = dit
                if (ptt.paddleSide !== "left") ptt.paddleState.dah = dah
            } else {
                Engine.setCwPaddleState(dit, dah)
            }
        }
        onDitChanged: report()
        onDahChanged: report()
        // Hidden or disabled with a paddle down: let go.
        onVisibleChanged: if (!visible) report()
        component Pad: Rectangle {
            id: pad
            property alias tap: padTap
            property string symbol
            property string name
            radius: Theme.radius
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Paddle, %1").arg(pad.name)
            color: padTap.pressed ? Theme.transmit : Theme.pttReady
            border.width: 2
            border.color: padTap.pressed ? "white" : Theme.transmit
            Column {
                anchors.centerIn: parent
                Text {
                    Accessible.ignored: true
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: pad.symbol
                    color: "white"
                    font.pixelSize: ptt.fontPx * 1.3
                    font.bold: true
                }
                Text {
                    Accessible.ignored: true
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: pad.name
                    color: "white"
                    opacity: 0.8
                    font.pixelSize: Math.max(11, ptt.fontPx * 0.4)
                }
            }
            // Pressed while the finger stays on it; sliding off lets go.
            TapHandler { id: padTap; gesturePolicy: TapHandler.WithinBounds }
        }
        Row {
            anchors.fill: parent
            spacing: 4
            Pad {
                id: leftPad
                visible: ptt.paddleSide !== "right"
                width: ptt.paddleSide === "both" ? (parent.width - 4) / 2 : parent.width
                height: parent.height
                symbol: paddles.swapped ? "–" : "•"
                name: paddles.swapped ? qsTr("dah") : qsTr("dit")
            }
            Pad {
                id: rightPad
                visible: ptt.paddleSide !== "left"
                width: ptt.paddleSide === "both" ? (parent.width - 4) / 2 : parent.width
                height: parent.height
                symbol: paddles.swapped ? "•" : "–"
                name: paddles.swapped ? qsTr("dit") : qsTr("dah")
            }
        }
    }
}
