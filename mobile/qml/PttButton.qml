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
    enabled: (keyMode ? Engine.cwKeyAvailable : Engine.transmitAvailable) || Engine.tuning || pressed
    text: Engine.tuning ? qsTr("TUNE") : keyMode ? (Engine.cwKeyDown ? qsTr("KEY ●") : qsTr("KEY"))
          : Engine.pttLatched ? qsTr("TX ■") : Engine.transmitting ? qsTr("TX") : qsTr("PTT")
    focusPolicy: Qt.NoFocus
    font.pixelSize: fontPx
    font.bold: true
    // Ready, unavailable and transmitting must look different at a glance:
    // ready has a bright border and label, unavailable is dim.
    contentItem: Text {
        text: ptt.text
        font: ptt.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: ptt.enabled ? "white" : Theme.textDim
        opacity: ptt.enabled ? 1.0 : 0.5
    }
    background: Rectangle {
        radius: Theme.radius
        color: Engine.transmitting || Engine.tuning || Engine.cwKeyDown ? Theme.transmit
             : ptt.enabled ? Theme.pttReady : Theme.surface
        border.width: Engine.transmitting ? 3 : ptt.enabled ? 2 : 1
        border.color: Engine.transmitting ? "white"
                    : ptt.enabled ? Theme.transmit : Theme.surfaceRaised
    }
    // Hold to transmit; release (or losing the touch) always unkeys. While
    // the tune carrier is on, PTT is its off switch.
    // In tap on/off mode a press switches and the release does nothing.
    onPressed: Engine.tuning ? Engine.stopTransmit("PTT pressed during tune")
             : keyMode ? Engine.setCwKey(true) : toggleMode ? Engine.togglePtt() : Engine.setPtt(true)
    onReleased: {
        if (keyMode) Engine.setCwKey(false, "key released")
        else if (!toggleMode) Engine.setPtt(false, "button released")
    }
    onCanceled: {
        if (keyMode) Engine.setCwKey(false, "touch cancelled by the system")
        else if (!toggleMode) Engine.setPtt(false, "touch cancelled by the system")
    }
}
