import QtQuick
import QtQuick.Controls
import AetherMobile

// A hardware keyboard on the operating page, the tablet's and the phone's
// alike (the phone page took only the CW paddle keys): the CW paddles,
// arrows tune (up / down ten steps, Shift ten times as far), + / − zoom,
// F frequency entry, F1–F12 the CW memories or voice macros, Esc stops
// them, Space PTT. The page forwards its key events and focus changes; the
// keyboard focus returns to the page whenever a control took it.
Item {
    id: keys
    visible: false
    // The page, its frequency dialog and controls drawer (which stays open
    // while operating: it does not keep the keyboard).
    property Item page
    property var freqDialog
    property var drawer
    property bool spacePtt: false

    CwKeyboardPaddles { id: paddles }

    function press(event) {
        if (paddles.handlePress(event)) {
            return true
        }
        if (event.isAutoRepeat && event.key === Qt.Key_Space) {
            return true
        }
        const mult = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
        switch (event.key) {
        case Qt.Key_Left: Engine.tuneBySteps(-mult); break
        case Qt.Key_Right: Engine.tuneBySteps(mult); break
        case Qt.Key_Down: Engine.tuneBySteps(-10 * mult); break
        case Qt.Key_Up: Engine.tuneBySteps(10 * mult); break
        case Qt.Key_Plus:
        case Qt.Key_Equal: Engine.zoom(0.5); break
        case Qt.Key_Minus: Engine.zoom(2.0); break
        case Qt.Key_F: if (keys.freqDialog) keys.freqDialog.open(); break
        // CW memories M1-M12 and abort, as the desktop's CWX F-keys and Esc.
        case Qt.Key_F1: case Qt.Key_F2: case Qt.Key_F3: case Qt.Key_F4:
        case Qt.Key_F5: case Qt.Key_F6: case Qt.Key_F7: case Qt.Key_F8:
        case Qt.Key_F9: case Qt.Key_F10: case Qt.Key_F11: case Qt.Key_F12:
            // CW modes send the CW memories, voice modes the voice keyer.
            if (Engine.cwMode) Engine.sendCwMemory(event.key - Qt.Key_F1)
            else Engine.dvkPlay(event.key - Qt.Key_F1 + 1)
            break
        case Qt.Key_Escape: Engine.stopCwx(); Engine.dvkStop(); break
        case Qt.Key_Space:
            // iOS: GameController keys Space itself (keyboardOnPage).
            if (Engine.nativeKeyboardSides) break
            if (Engine.pttToggle && !Engine.cwMode) {
                Engine.togglePtt()
                break
            }
            keys.spacePtt = true
            Engine.setPtt(true)
            break
        default: return false
        }
        return true
    }

    function release(event) {
        if (paddles.handleRelease(event)) {
            return true
        }
        if (event.key === Qt.Key_Space && Engine.nativeKeyboardSides) {
            return true
        }
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            keys.spacePtt = false
            Engine.setPtt(false, "space released")
            return true
        }
        return false
    }

    function focusChanged(active) {
        if (!active) paddles.releaseAll()
        if (paddles.active) Engine.logKeyboardPaddles("page keyboard focus " + (active ? "in" : "out"))
        if (!active && keys.spacePtt) {
            keys.spacePtt = false
            Engine.setPtt(false, "page lost keyboard focus")
        }
    }

    // Keyboard focus returns to the page whenever a control (a slider, a
    // switch, a popup that closed) took it, so Space and the arrows keep
    // working; text entry and the digit dialogs keep it. An open popup (a
    // ComboBox list, a dialog) keeps it too: taking focus from a ComboBox's
    // list closed it before a choice could be made.
    function popupOpen() {
        const kids = keys.page && keys.page.Overlay.overlay ? keys.page.Overlay.overlay.children : []
        for (let i = 0; i < kids.length; ++i) {
            if (!kids[i].visible) continue
            let drawerItem = false
            for (let it = keys.drawer ? keys.drawer.contentItem : null; it; it = it.parent) {
                if (it === kids[i]) {
                    drawerItem = true
                    break
                }
            }
            if (!drawerItem) return true
        }
        return false
    }
    function keepsKeyboard(item) {
        return keys.popupOpen()
            || (item !== null && (item.cursorPosition !== undefined || item.keepsKeyboard === true))
    }
    Connections {
        target: keys.page ? keys.page.Window.window : null
        function onActiveFocusItemChanged() {
            const p = keys.page
            const item = p.Window.window ? p.Window.window.activeFocusItem : null
            if (!p.visible || p.activeFocus || keys.keepsKeyboard(item)) {
                return
            }
            Qt.callLater(() => {
                const now = p.Window.window ? p.Window.window.activeFocusItem : null
                if (p.visible && !p.activeFocus && !keys.keepsKeyboard(now)) {
                    p.forceActiveFocus()
                }
            })
        }
    }
}
