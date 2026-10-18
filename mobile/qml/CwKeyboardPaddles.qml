import QtQuick
import AetherMobile

// CW paddles on a hardware keyboard (CW tab > Key: Paddles, and Paddles on
// a keyboard): left and right Ctrl by default, which USB paddle adapters
// (Vail, VBand) send, left and right Alt / Option, for keyboards with no
// right Ctrl (Apple's Magic Keyboard), or [ and ]. A page forwards its key
// events; handle*() return true when they took the key. Left = dit, right =
// dah (the keyer applies the radio's swapped-paddles setting itself).
QtObject {
    id: kp
    property bool dit: false
    property bool dah: false
    readonly property bool active: Engine.cwMode && Engine.cwPaddles && Engine.cwKeyboardPaddles !== "off"
    // The first keys while active go to the log with their codes (any key,
    // taken or not), so a keyboard or adapter can be checked from the
    // developer link.
    property int keysLogged: 0
    onActiveChanged: if (active) Engine.logKeyboardPaddles("on (" + Engine.cwKeyboardPaddles + ")")
    function logKey(event, s) {
        if (keysLogged >= 24) return
        ++keysLogged
        Engine.logKeyboardPaddles("key " + event.key + " scan " + event.nativeScanCode
                                  + " modifiers 0x" + event.modifiers.toString(16)
                                  + (event.text.length > 0 ? " text \"" + event.text + "\"" : "")
                                  + " -> " + (s === "" ? "not a paddle" : s + (s === "left" ? " (dit)" : " (dah)"))
                                  + " (" + Engine.cwKeyboardPaddles + ")")
    }

    // Left or right for a key, "" when it is not a paddle key now.
    function side(event) {
        if (Engine.cwKeyboardPaddles === "brackets") {
            if (event.key === Qt.Key_BracketLeft) return "left"
            if (event.key === Qt.Key_BracketRight) return "right"
            return ""
        }
        // Qt reports both Ctrl (or Alt) keys as one key: the hardware code
        // tells them apart (evdev/Android, X11, iOS HID, macOS).
        const sc = event.nativeScanCode
        if (Engine.cwKeyboardPaddles === "alt") {
            // Right Alt is AltGr on many PC layouts.
            if (event.key === Qt.Key_AltGr) return "right"
            if (event.key !== Qt.Key_Alt) return ""
            if ([56, 64, 226, 58].indexOf(sc) >= 0) return "left"
            if ([100, 108, 230, 61].indexOf(sc) >= 0) return "right"
                return "left"
        }
        if (event.key !== Qt.Key_Control && event.key !== Qt.Key_Meta) return ""
        // Qt on Apple platforms reports Command as Ctrl: Command (and the
        // Windows / Super keys) is never a paddle, so a Command shortcut does
        // not key a dit.
        if ([227, 231, 55, 54, 125, 126, 133, 134].indexOf(sc) >= 0) return ""
        if ([29, 37, 224, 59].indexOf(sc) >= 0) return "left"
        if ([97, 105, 228, 62].indexOf(sc) >= 0) return "right"
        return "left"
    }
    function apply() {
        Engine.setCwPaddleState(dit, dah)
    }
    function handlePress(event) {
        if (!active) return false
        const s = side(event)
        if (!event.isAutoRepeat) logKey(event, s)
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
