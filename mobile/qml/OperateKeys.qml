import QtQuick
import QtQuick.Controls
import AetherMobile

// A hardware keyboard on the operating page, the tablet's and the phone's
// alike (the phone page took only the CW paddle keys): the CW paddles,
// arrows tune (up / down ten steps, Shift ten times as far), + / − zoom,
// F frequency entry, F1–F12 the CW memories, Digi macros or voice macros
// (⌘1–⌘9 and ⌘0 too, Ctrl on Android and a PC keyboard: an iPad's Magic
// Keyboard has no F-keys), Esc stops them, Space PTT. The page forwards its key events and focus changes; the
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

    // F1-F12, or ⌘ / Ctrl with 1-9 and 0 as F1-F10: 0-11, else -1.
    function memoryKey(event) {
        if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F12) {
            return event.key - Qt.Key_F1
        }
        if ((event.modifiers & Qt.ControlModifier) && event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            return event.key === Qt.Key_0 ? 9 : event.key - Qt.Key_1
        }
        return -1
    }

    function press(event) {
        if (paddles.handlePress(event)) {
            return true
        }
        if (event.isAutoRepeat && event.key === Qt.Key_Space) {
            return true
        }
        // CW memories M1-M12 and abort, as the desktop's CWX F-keys and Esc:
        // CW modes send the CW memories, RTTY the Digi tab's macros (F1-F8,
        // as fldigi's F-keys), voice modes the voice keyer.
        const memory = keys.memoryKey(event)
        if (memory >= 0) {
            if (Engine.cwMode) Engine.sendCwMemory(memory)
            else if (Engine.rttyActive) { if (memory < 8) Engine.sendTextMacro(memory) }
            else Engine.dvkPlay(memory + 1)
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
        case Qt.Key_Escape: Engine.stopCwx(); Engine.dvkStop(); Engine.stopTextTx(); break
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

    function pageFocusChanged(active) {
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
