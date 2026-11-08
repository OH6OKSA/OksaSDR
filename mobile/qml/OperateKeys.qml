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
    // The page, its frequency dialog, controls drawer and macros panel
    // (which stay open while operating: they do not keep the keyboard).
    property Item page
    property var freqDialog
    property var drawer
    property var macros
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
        // Any key ends a repeating macro (an F-key then starts its own).
        if (!event.isAutoRepeat) Engine.cancelMacroRepeat("a key")
        if (paddles.handlePress(event)) {
            return true
        }
        if (event.isAutoRepeat && event.key === Qt.Key_Space) {
            return true
        }
        // CW memories M1-M12 and abort, as the desktop's CWX F-keys and Esc:
        // CW modes send the CW memories, RTTY and PSK the Digi tab's macros
        // (F1-F8, as fldigi's F-keys), voice modes the voice keyer.
        const memory = keys.memoryKey(event)
        if (memory >= 0) {
            if (Engine.cwMode) Engine.sendCwMemory(memory)
            else if (Engine.textModeActive) { if (memory < 8) Engine.sendTextMacro(memory) }
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
            if (!keys.within(keys.drawer, kids[i]) && !keys.within(keys.macros, kids[i])) return true
        }
        return false
    }
    // True if this overlay child holds the popup's content.
    function within(popup, kid) {
        for (let it = popup ? popup.contentItem : null; it; it = it.parent) {
            if (it === kid) return true
        }
        return false
    }
    function keepsKeyboard(item) {
        return keys.popupOpen()
            || (item !== null && (item.cursorPosition !== undefined || item.keepsKeyboard === true))
    }
    function giveBack() {
        const p = keys.page
        const now = p.Window.window ? p.Window.window.activeFocusItem : null
        if (p.visible && now !== p && !keys.keepsKeyboard(now)) {
            // Inside the page every focus scope between the control and the
            // page is let go too (the pinned panel's scroll view, a section's
            // Loader): the page's forceActiveFocus otherwise hands the focus
            // down that chain again, to the scroll view, and only another
            // round gave it to the page.
            let inPage = false
            for (let it = now; it; it = it.parent) {
                if (it === p) {
                    inPage = true
                    break
                }
            }
            if (inPage) {
                for (let it = now; it !== p; it = it.parent) it.focus = false
            } else if (now) {
                now.focus = false
            }
            p.forceActiveFocus()
        }
    }
    Connections {
        target: keys.page ? keys.page.Window.window : null
        function onActiveFocusItemChanged() {
            const p = keys.page
            const item = p.Window.window ? p.Window.window.activeFocusItem : null
            // The page itself, not p.activeFocus: that is true for a control
            // inside the page too (the tablet's pinned panel), and a slider
            // there kept the keys (build 149 on an iPad: Space and the
            // arrows went to an EQ band).
            if (!p.visible || item === p || keys.keepsKeyboard(item)) {
                return
            }
            Qt.callLater(keys.giveBack)
        }
    }
    // A ComboBox keeps the focus when its list closes, which changes no
    // focus item, so nothing above runs: Space then opened the list again.
    // Looked at again while anything but the page has the keyboard.
    Timer {
        interval: 300
        repeat: true
        running: !!keys.page && keys.page.visible && !!keys.page.Window.window
                 && keys.page.Window.activeFocusItem !== keys.page
        onTriggered: keys.giveBack()
    }
}
