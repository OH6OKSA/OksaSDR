import QtQuick
import QtQuick.Controls
import AetherMobile

// The voice button for logging by voice (roadmap 79): hold it, spell the
// call, let go; a quick tap listens until a second tap (or 15 s). Shown where
// the device has a speech recogniser.
AppButton {
    id: voice
    visible: Engine.voiceInputOffered
    readonly property bool listening: Engine.voiceInputState === "listening"
    property double pressedAt: 0
    text: Engine.voiceInputState === "recognizing" ? "…" : "🎤"
    checked: listening
    focusPolicy: Qt.NoFocus
    Accessible.name: listening ? qsTr("Stop listening") : qsTr("Spell the callsign by voice")
    implicitWidth: Math.max(Theme.touchTarget, implicitHeight)
    onPressed: {
        if (listening) {
            Engine.voiceInputStop()
            pressedAt = 0
            return
        }
        pressedAt = Date.now()
        Engine.voiceInputStart()
    }
    // Held: let go to stop. A quick tap keeps listening until tapped again.
    // A press the system cancels (the on-screen keyboard closing moves the
    // layout under the finger) ends as a release.
    function letGo() {
        if (pressedAt > 0 && Date.now() - pressedAt > 400 && listening)
            Engine.voiceInputStop()
        pressedAt = 0
    }
    onReleased: letGo()
    onCanceled: letGo()
}
