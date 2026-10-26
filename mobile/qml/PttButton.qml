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
    // and keeps it.
    readonly property bool digiHidden: Engine.digiRunning && !Engine.rade
    enabled: (keyMode ? Engine.cwKeyAvailable : Engine.transmitAvailable) || Engine.tuning || pressed
    text: Engine.tuning ? qsTr("TUNE") : keyMode ? (Engine.cwKeyDown ? qsTr("KEY ●") : qsTr("KEY"))
          : Engine.pttLatched ? qsTr("TX ■") : Engine.transmitting ? qsTr("TX") : qsTr("PTT")
    focusPolicy: Qt.NoFocus
    font.pixelSize: fontPx
    font.bold: true
    // Ready, unavailable and transmitting must look different at a glance:
    // ready has a bright border and label, unavailable is dim.
    contentItem: Text {
        visible: !ptt.paddleMode || !ptt.enabled
        text: ptt.text
        font: ptt.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: ptt.enabled ? "white" : Theme.textDim
        opacity: ptt.enabled ? 1.0 : 0.5
    }
    background: Rectangle {
        radius: Theme.radius
        // The paddles draw their own faces.
        opacity: ptt.paddleMode && ptt.enabled ? 0 : 1
        color: Engine.transmitting || Engine.tuning || Engine.cwKeyDown ? Theme.transmit
             : ptt.enabled ? Theme.pttReady : Theme.surface
        border.width: Engine.transmitting ? 3 : ptt.enabled ? 2 : 1
        border.color: Engine.transmitting ? "white"
                    : ptt.enabled ? Theme.transmit : Theme.surfaceRaised
    }
    // Hold to transmit; release (or losing the touch) always unkeys. While
    // the tune carrier is on, PTT is its off switch.
    // In tap on/off mode a press switches and the release does nothing.
    onPressed: {
        if (paddleMode) return
        if (Engine.tuning) Engine.stopTransmit("PTT pressed during tune")
        else if (keyMode) Engine.setCwKey(true)
        else if (toggleMode) Engine.togglePtt()
        else Engine.setPtt(true)
    }
    onReleased: {
        if (paddleMode) return
        if (keyMode) Engine.setCwKey(false, "key released")
        else if (!toggleMode) Engine.setPtt(false, "button released")
    }
    onCanceled: {
        if (paddleMode) return
        if (keyMode) Engine.setCwKey(false, "touch cancelled by the system")
        else if (!toggleMode) Engine.setPtt(false, "touch cancelled by the system")
    }

    Item {
        id: paddles
        visible: ptt.paddleMode && ptt.enabled
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
            color: padTap.pressed ? Theme.transmit : Theme.pttReady
            border.width: 2
            border.color: padTap.pressed ? "white" : Theme.transmit
            Column {
                anchors.centerIn: parent
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: pad.symbol
                    color: "white"
                    font.pixelSize: ptt.fontPx * 1.3
                    font.bold: true
                }
                Text {
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
