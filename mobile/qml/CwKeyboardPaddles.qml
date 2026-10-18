import QtQuick
import AetherMobile

// CW paddles on a hardware keyboard (CW tab > Key: Paddles, and Keyboard
// paddles): [ and ] by default, or left and right Ctrl, which USB paddle
// adapters (Vail, VBand) send. A page forwards its key events; handle*()
// return true when they took the key. Left = dit, right = dah (the keyer
// applies the radio's swapped-paddles setting itself).
QtObject {
    id: kp
    property bool dit: false
    property bool dah: false
    readonly property bool active: Engine.cwMode && Engine.cwPaddles && Engine.cwKeyboardPaddles !== "off"

    // Left or right for a key, "" when it is not a paddle key now.
    function side(event) {
        if (Engine.cwKeyboardPaddles === "brackets") {
            if (event.key === Qt.Key_BracketLeft) return "left"
            if (event.key === Qt.Key_BracketRight) return "right"
            return ""
        }
        if (event.key !== Qt.Key_Control && event.key !== Qt.Key_Meta) return ""
        // Qt reports both Ctrl keys as one key: the hardware code tells them
        // apart (evdev/Android, X11, iOS HID, macOS).
        const sc = event.nativeScanCode
        if ([29, 37, 224, 59].indexOf(sc) >= 0) return "left"
        if ([97, 105, 228, 62].indexOf(sc) >= 0) return "right"
        console.info("CW keyboard paddles: unknown Ctrl scan code", sc, "taken as the left paddle")
        return "left"
    }
    function apply() {
        Engine.setCwPaddleState(dit, dah)
    }
    function handlePress(event) {
        if (!active) return false
        const s = side(event)
        if (s === "") return false
        if (!event.isAutoRepeat) {
            if (s === "left") dit = true
            else dah = true
            apply()
        }
        return true
    }
    function handleRelease(event) {
        if (!active && !dit && !dah) return false
        const s = side(event)
        if (s === "") return false
        if (!event.isAutoRepeat) {
            if (s === "left") dit = false
            else dah = false
            apply()
        }
        return true
    }
    // Focus lost (a dialog, the app left the screen): nothing stays down.
    function releaseAll() {
        if (dit || dah) {
            dit = false
            dah = false
            apply()
        }
    }
}
