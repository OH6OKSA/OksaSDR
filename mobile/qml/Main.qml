import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

ApplicationWindow {
    id: window
    width: 1366
    height: 1024
    visible: true
    title: qsTr("OksaSDR")
    color: Theme.background
    // The Basic style takes sliders' fill and track, switches, menus, text
    // fields and default label text from the palette, which differs by
    // platform (Android's drew a slider's filled part dark and the rest
    // bright; a light system palette made the ⋯ menu and the text fields
    // white, while an iPad in dark mode has them black): the same
    // everywhere, the app's blue left of a slider's handle on a dark track.
    palette.dark: Theme.accent
    palette.midlight: Theme.surfaceRaised
    palette.light: Theme.light ? "#c3ccd6" : "#2d3947"
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.light ? "#ffffff" : "#05080b"
    palette.text: Theme.text
    palette.placeholderText: Theme.textDim
    // The render tool's stand-in for a device's bars (Engine.testSafeTop):
    // margins added to the window's safe area, which Qt then treats as the
    // device's own (the page inside them, a drawer's content padded). 0 on
    // every device.
    SafeArea.additionalMargins.top: Engine.testSafeTop
    SafeArea.additionalMargins.bottom: Engine.testSafeBottom
    SafeArea.additionalMargins.left: Engine.testSafeLeft
    SafeArea.additionalMargins.right: Engine.testSafeRight

    // Phone-sized windows (iPhone, folded iPhone Duo, narrow Split View)
    // get the phone layout; it switches live on rotation or fold/unfold.
    // Adaptive metrics follow the room the pages have (Theme.qml): the
    // window less its safe area (status and navigation bars, cutouts), which
    // matters on a phone's outer screen in landscape.
    Binding {
        target: Theme; property: "windowWidth"
        value: pages.width - pages.SafeArea.margins.left - pages.SafeArea.margins.right
    }
    Binding {
        target: Theme; property: "windowHeight"
        value: pages.height - pages.SafeArea.margins.top - pages.SafeArea.margins.bottom
    }
    // The same without the on-screen keyboard's room, for the phone / tablet
    // class: with the keyboard counted an opened OnePlus Open (751 high, a
    // 316 keyboard) fell under the phone layout's 500, the page was built
    // anew, the field let go and the keyboard went: no keyboard at all
    // (beta 117, the user: "sisäkannella ei missään asennossa").
    Binding {
        target: Theme; property: "classHeight"
        value: pages.height + window.keyboardLift - pages.SafeArea.margins.top - pages.SafeArea.margins.bottom
    }
    readonly property bool phoneSized: Theme.phoneLayout
    // The page's layout, settled: the two bindings above update one after
    // the other, so a phone turning passed through a square window that
    // read as a tablet, and the page, its 3D view and waterfall were built
    // twice (the display went dark for seconds on rotation, WA2N).
    // The start value only: the assignment at completion ends the binding.
    property bool pagePhone: phoneSized
    Component.onCompleted: pagePhone = phoneSized
    onPhoneSizedChanged: Qt.callLater(() => { window.pagePhone = window.phoneSized })

    // The on-screen keyboard over the window (roadmap 142): the render
    // tool's stand-in, or the system's. On an iPad with a hardware keyboard
    // iOS shows only its shortcut bar, no keys: not counted then.
    readonly property bool touchOs: Qt.platform.os === "ios" || Qt.platform.os === "android"
    readonly property rect keyboardRect: Engine.testKeyboard
                                         ? Qt.rect(0, height - Engine.testKeyboardHeight, width, Engine.testKeyboardHeight)
                                         : systemKeyboardRect
    // Qt on Android gives the keyboard's rectangle in device pixels, not the
    // window's (the OnePlus Open, beta 116: "keyboard 1413+1071 of 764" on
    // the outer screen, 1532+908 of 751 on the inner, a ratio of 3.25): it
    // lay below the window, so nothing made room for it and the keyboard
    // covered PTT and Done (the user: in Big buttons' Log the opened phone
    // showed no PTT; Done unseen in a lower Controls drawer). Scaled back
    // when its bottom lies that far past the window.
    readonly property rect systemKeyboardRect: {
        const r = Qt.platform.os === "android" ? androidKeyboardRaw : Qt.inputMethod.keyboardRectangle
        const dpr = Screen.devicePixelRatio
        const s = Qt.platform.os === "android" && dpr > 1.05 && r.y + r.height > height * 1.2 ? 1 / dpr : 1
        return Qt.rect(r.x * s, r.y * s, r.width * s, r.height * s)
    }
    // Android: Qt's keyboard rectangle changes without its signal and
    // inputMethod.visible lags it (the OnePlus, beta 117: typing with the
    // keyboard up read "keyboard 0+0, the system's 1413+1027", and the room
    // came only after Enter had put the keyboard away). Read here every
    // 150 ms while a field has the focus or the keyboard is still counted,
    // and the keyboard is up while its rectangle has a height.
    property rect androidKeyboardRaw: Qt.rect(0, 0, 0, 0)
    Timer {
        interval: 150
        repeat: true
        running: Qt.platform.os === "android"
                 && (window.androidKeyboardRaw.height > 0
                     || (!!window.activeFocusItem && window.activeFocusItem.cursorRectangle !== undefined))
        onTriggered: {
            const r = Qt.inputMethod.keyboardRectangle
            const o = window.androidKeyboardRaw
            if (r.x !== o.x || r.y !== o.y || r.width !== o.width || r.height !== o.height)
                window.androidKeyboardRaw = r
        }
    }
    readonly property bool keyboardKeysShown: Qt.platform.os !== "ios" || keyboardRect.height >= 150
    // On Android a rectangle under 120 is not the keyboard but a strip left
    // of it (the OnePlus Open, beta 117: turned upright with no keyboard up,
    // the Controls drawer took the whole page as it does above a keyboard).
    readonly property bool onScreenKeyboard: (touchOs && (Qt.platform.os === "android"
                                                          ? systemKeyboardRect.height >= 120
                                                          : Qt.inputMethod.visible && keyboardKeysShown))
                                             || Engine.testKeyboard
    // How much of the window's bottom it covers. The page and the controls
    // drawers end above it (the user, build 203's preview on the iPad:
    // typing in the Log tab the keyboard covered PTT, "virtuaalinäppäimistöllä
    // ei PTT-painamme onnistu silloin, kun logikirjoitus päällä", and "melkein
    // koko vesiputouksen"). 0 when the system made the window shorter itself.
    readonly property real keyboardCover: onScreenKeyboard && keyboardRect.height > 0
                                          ? Math.max(0, Math.min(height * 0.7, height - keyboardRect.y)) : 0
    // The device's bars: the status bar's and the navigation bar's (or the
    // home indicator's) share of the window. Qt lays the pages inside them
    // (ApplicationWindow pads its content by the safe area since Qt 6.9) and
    // pads a popup's content by them, so a page is shorter than the window
    // by both with no keyboard anywhere. The pages' sums took that
    // difference for the keyboard (roadmap 169, measured on two Android
    // phones: a drawer's Close and PTT 85 pt above the window's bottom, the
    // status bar's 41 of them for nothing).
    readonly property real barTop: SafeArea.margins.top
    readonly property real barBottom: SafeArea.margins.bottom
    readonly property real barRight: SafeArea.margins.right
    // What the keyboard takes of the pages' room: its cover less the
    // navigation bar under it, which the pages end above already. With the
    // whole cover the page ended the bar's height above the keyboard (the
    // OnePlus Open, beta 117: "musta raita ... ihan turhaa").
    readonly property real keyboardLift: Math.max(0, keyboardCover - barBottom)
    // Each change in the log, with the system's numbers: the room a device
    // makes (or not) can then be read from a tester's log.
    onKeyboardCoverChanged: Qt.callLater(() => Engine.logUi("keyboard: cover " + Math.round(window.keyboardCover) + " of "
        + window.width + "x" + window.height + " (the system's " + Math.round(Qt.inputMethod.keyboardRectangle.y) + "+"
        + Math.round(Qt.inputMethod.keyboardRectangle.height) + ", ratio " + Screen.devicePixelRatio + "; bars top "
        + Math.round(window.barTop) + " bottom " + Math.round(window.barBottom) + "; the window at " + window.x + "," + window.y
        + " of " + Screen.width + "x" + Screen.height + ")"))
    // The window and its bars in the log once they settle (a turn, a fold,
    // a resized window): a layout question from a tester's log starts here.
    readonly property string geometryNote: width + "x" + height + " at " + x + "," + y + " of " + Screen.width + "x" + Screen.height
                                           + ", bars top " + Math.round(barTop) + " bottom "
                                           + Math.round(barBottom) + ", the page " + Math.round(pages.width) + "x" + Math.round(pages.height)
    onGeometryNoteChanged: geometryLog.restart()
    Timer {
        id: geometryLog
        interval: 800
        onTriggered: Engine.logUi("window: " + window.geometryNote)
    }

    // The page follows the connection: radio list until connected.
    Loader {
        id: pages
        anchors.fill: parent
        anchors.bottomMargin: window.keyboardLift
        sourceComponent: Engine.connectionState !== "connected" ? connectPage
                       : window.pagePhone ? phoneOperatePage : operatePage
    }

    // A spot alert (roadmap 132) over whichever operating page is shown, in
    // the middle of the screen at every size, where it catches the eye
    // (the user, build 201's preview; at the top it covered the top row's
    // buttons and was easy to miss).
    // Above the controls drawer, the Macros panel, Setup and the dialogs
    // (roadmap 222, the user: "pitäisi aina olla päällimmäisenä"): as an
    // item of the page it lay under an open drawer, on a phone wholly. A
    // popup that takes no focus and never closes by a press elsewhere, as
    // the notices and the keyboard's Done: the page keeps the keyboard
    // (OperateKeys leaves this popup out of the ones that hold it), and a
    // press on Tune or × is not a press outside the drawer, which stays
    // open. In the same place as before: the middle of the page's own room.
    property alias alertPopup: spotAlertPopup
    Popup {
        id: spotAlertPopup
        objectName: "spotAlertPopup"
        parent: Overlay.overlay
        z: 900
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        margins: 0
        background: null
        width: Math.min(window.width - window.SafeArea.margins.left - window.barRight - 2 * Theme.spacing, 640)
        x: Math.round(window.SafeArea.margins.left
                      + (window.width - window.SafeArea.margins.left - window.barRight - width) / 2)
        y: Math.round(window.barTop + (window.height - window.barTop - window.barBottom - height) / 2)
        contentItem: SpotAlertBanner { id: spotAlertBanner }
        readonly property bool wanted: spotAlertBanner.wanted
        onWantedChanged: wanted ? open() : close()
        Component.onCompleted: if (wanted) open()
        // Whatever closes it while an alert waits: shown again.
        onClosed: if (wanted) Qt.callLater(open)
    }

    // What a screen reader is told without being on it: a refusal or an
    // error, why a connection ended, an amplifier's or tuner's fault, a
    // spot alert. They are shown (the notice over the display, the connect
    // page's status, the Amp tab's red line, the banner) but a VoiceOver or
    // TalkBack user hears a text only when the reader is on it, and nothing
    // moves it there: "TX refused" or "Amplifier fault: SWR" went unheard.
    // Said once here, from the engine, when the text changes to a new one:
    // the pages and the controls drawer have several copies, and a closed
    // drawer's labels are not visible, so the labels themselves would say it
    // twice or not at all. Polite (after what is being read); only these,
    // not the mode or the frequency, which the reader is asked for.
    Item {
        id: spoken
        objectName: "spokenNotices"
        function say(text) {
            if (text.length > 0)
                Accessible.announce(text)
        }
        function fault(prefix, s) {
            return s.fault ? qsTr("%1 fault: %2").arg(prefix).arg(s.fault)
                 : s.ifaceError ? qsTr("%1 error: %2").arg(prefix).arg(s.ifaceError) : ""
        }
        readonly property string lastError: Engine.lastError
        readonly property string ended: Engine.disconnectCause.length > 0
                                        ? qsTr("The connection ended: %1.").arg(Engine.disconnectCause) : ""
        readonly property string amp: Engine.amp.set ? fault(qsTr("Amplifier"), Engine.amp) : ""
        readonly property string kpa: Engine.kpa.set ? fault(qsTr("Amplifier"), Engine.kpa) : ""
        readonly property string kpa500: Engine.kpa500.set ? fault(qsTr("Amplifier"), Engine.kpa500) : ""
        readonly property string kat500: Engine.kat500.set && Engine.kat500.fault
                                         ? qsTr("Tuner fault: %1").arg(Engine.kat500.fault) : ""
        readonly property string tgxl: Engine.tgxl.present ? (Engine.tgxl.alert || "") : ""
        readonly property string pgxl: Engine.pgxl.present ? (Engine.pgxl.alert || "") : ""
        readonly property string spot: Engine.connectionState === "connected" && !!Engine.spotAlert.call
            ? qsTr("Spot alert: %1 on %2 MHz, %3").arg(Engine.spotAlert.call)
                  .arg(Number(Engine.spotAlert.mhz || 0).toFixed(3)).arg(Engine.spotAlert.why || "") : ""
        onLastErrorChanged: say(lastError)
        onEndedChanged: say(ended)
        onAmpChanged: say(amp)
        onKpaChanged: say(kpa)
        onKpa500Changed: say(kpa500)
        onKat500Changed: say(kat500)
        onTgxlChanged: say(tgxl)
        onPgxlChanged: say(pgxl)
        onSpotChanged: say(spot)
    }

    // The on-screen keyboard's Done (roadmap 142, Robin Bishop G5OUT): an
    // iPhone's keyboard has no key to put it away, and Return in some fields
    // acts (the Log tab's call logs the contact). It lets go of the field,
    // and the page takes the keys again (Space PTT on a tablet's keyboard).
    // A tap outside the field does the same (TextFocusRelease).
    // A popup of its own, above the controls drawer and Setup: a press on
    // it is not a press outside them (which closed the drawer).
    Popup {
        id: keyboardDonePopup
        readonly property rect keyboard: window.keyboardRect
        // Not with a hardware keyboard on an iPad (the user, build 203's
        // preview: "Eikä tuo iPadissäkään kovin järkevältä näytä", Done at
        // the top with no on-screen keyboard).
        readonly property bool wanted: window.onScreenKeyboard
        parent: Overlay.overlay
        z: 1000
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        background: null

        // Where: by the field being edited, which the system keeps in sight
        // above the keyboard (sliding the page up for a field low on the
        // screen). At the window's top Done went out of sight then (the
        // user, build 203's preview on the OnePlus Open's outer screen:
        // "Etukannen tilassa sitä ei näy ollenkaan"; an iPhone slides the
        // page the same way), and it covered the top row. Below the field
        // at its right end where the keyboard leaves that in sight, else
        // above it; the first of those spots with nothing to press or type
        // in under it, as the empty end of the Log tab's Sent / Rcvd row.
        property string spot: ""
        function topmostAt(root, sx, sy) {
            // The deepest visible item under a window point (the Done
            // popup itself left out), from the topmost child down.
            let found = null
            let item = root
            for (let depth = 0; depth < 48 && item; ++depth) {
                const kids = item.children
                let hit = null
                for (let i = kids.length - 1; i >= 0; --i) {
                    const c = kids[i]
                    if (!c.visible || c.opacity <= 0 || c === keyboardDonePopup.contentItem.parent)
                        continue
                    const p = c.mapFromItem(null, sx, sy)
                    if (p.x >= 0 && p.y >= 0 && p.x < c.width && p.y < c.height && (!hit || c.z > hit.z))
                        hit = c
                }
                if (!hit)
                    break
                found = hit
                item = hit
            }
            return found
        }
        function pressable(item) {
            // A button, a switch or a text field, or a part of one; also an
            // item pressed through a TapHandler (the mode button: Done sat
            // on Big buttons' mode in its Log panel, the user on Android).
            for (let i = 0; i < 4 && item; ++i, item = item.parent) {
                if (typeof item.clicked === "function" || item.cursorRectangle !== undefined
                        || typeof item.moved === "function" || item.keepsTextFocus === true)
                    return true
                const handlers = item.data
                for (let j = 0; handlers && j < handlers.length; ++j) {
                    if (handlers[j] && typeof handlers[j].tapped === "function" && handlers[j].enabled !== false)
                        return true
                }
            }
            return false
        }
        function covers(sx, sy) {
            const points = [[sx + 4, sy + 4], [sx + width - 4, sy + 4], [sx + 4, sy + height - 4],
                            [sx + width - 4, sy + height - 4], [sx + width / 2, sy + height / 2]]
            for (const pt of points) {
                const item = topmostAt(Overlay.overlay, pt[0], pt[1]) || topmostAt(window.contentItem, pt[0], pt[1])
                if (pressable(item))
                    return true
            }
            return false
        }
        function place() {
            const field = window.activeFocusItem
            const top = pages.SafeArea.margins.top + 4
            let where = "top"
            if (!field || field.cursorRectangle === undefined) {
                x = Math.round((window.width - width) / 2)
                y = top
            } else {
                const p = field.mapToItem(null, 0, 0)
                const right = Math.round(Math.max(8, Math.min(p.x + field.width - width, window.width - width - 8)))
                const left = Math.round(Math.max(8, Math.min(p.x, window.width - width - 8)))
                const below = Math.round(p.y + field.height + 6)
                const above = Math.round(p.y - height - 6)
                const keyboardTop = keyboard.height > 0 ? keyboard.y : window.height
                const belowSeen = below + height <= Math.min(keyboardTop, window.height) - 4
                const aboveSeen = above >= top
                // First beside the field on its own row, where a field sized
                // to its content (the call, roadmap 158) leaves the room: the
                // user, "miksi se done nappi ei voi olla kutsu laatikossa".
                const besideX = Math.round(p.x + field.width + 6)
                const besideY = Math.round(p.y + (field.height - height) / 2)
                const spots = []
                if (besideX + width <= window.width - 8 && besideY + height <= Math.min(keyboardTop, window.height) - 4)
                    spots.push(["beside", besideX, besideY])
                if (belowSeen) spots.push(["below right", right, below])
                if (aboveSeen) spots.push(["above right", right, above])
                if (belowSeen) spots.push(["below left", left, below])
                if (aboveSeen) spots.push(["above left", left, above])
                let chosen = null
                for (const s of spots) {
                    if (!covers(s[1], s[2])) {
                        chosen = s
                        break
                    }
                }
                // Every spot over another control (a field at the bottom of
                // Setup's room, its port field above): over the field's own
                // right end instead, not over a neighbour.
                if (!chosen)
                    chosen = ["in the field's right end", right - 4, Math.round(p.y + (field.height - height) / 2)]
                where = chosen[0]
                x = chosen[1]
                y = chosen[2]
            }
            if (where !== spot) {
                spot = where
                // In the engine's log, so a device's developer link and
                // Send to the developer carry it (a QML console line did not).
                Engine.logUi("keyboard: Done " + where + " at " + x + "," + y + "; keyboard "
                             + Math.round(keyboard.y) + "+" + Math.round(keyboard.height) + " of " + window.height
                             + " (the system's " + Math.round(Qt.inputMethod.keyboardRectangle.y) + "+"
                             + Math.round(Qt.inputMethod.keyboardRectangle.height) + ", ratio " + Screen.devicePixelRatio
                             + "; bars top " + Math.round(window.barTop) + " bottom " + Math.round(window.barBottom) + ")")
            }
        }
        // The field scrolled into its page's sight once the page has made
        // room for the keyboard (a phone in landscape keeps little of the
        // drawer: the Log tab's call went below its fold). Once per field,
        // so a scroll of one's own while typing stays.
        // The scroll reveal() made, given back when the keyboard goes: it
        // stayed, and with the room back the call was scrolled out of sight
        // under the drawer's tab buttons (the OnePlus, beta 117).
        property var revealedFlick: null
        property real revealedFromY: 0
        function unreveal() {
            const flick = revealedFlick
            revealedFlick = null
            if (flick && flick.contentY !== undefined)
                flick.contentY = Math.max(0, Math.min(revealedFromY, flick.contentHeight - flick.height))
        }
        function reveal() {
            const field = window.activeFocusItem
            if (!field || field.cursorRectangle === undefined)
                return
            for (let flick = field.parent; flick; flick = flick.parent) {
                if (flick.contentY === undefined || flick.contentHeight === undefined || flick.contentItem === undefined)
                    continue
                if (revealedFlick !== flick) {
                    revealedFlick = flick
                    revealedFromY = flick.contentY
                }
                const p = field.mapToItem(flick.contentItem, 0, 0)
                const margin = 8
                if (p.y + field.height + margin > flick.contentY + flick.height)
                    flick.contentY = Math.min(p.y + field.height + margin - flick.height,
                                              Math.max(0, flick.contentHeight - flick.height))
                if (p.y - margin < flick.contentY)
                    flick.contentY = Math.max(0, p.y - margin)
                return
            }
        }
        Timer {
            id: revealTimer
            interval: 350
            onTriggered: {
                keyboardDonePopup.reveal()
                keyboardDonePopup.place()
            }
        }
        onWantedChanged: {
            if (wanted) {
                spot = ""
                place()
                open()
                revealTimer.restart()
            } else {
                close()
                revealTimer.stop()
                unreveal()
            }
        }
        // Follows the field while the drawer slides or its page scrolls.
        Timer {
            interval: 200
            repeat: true
            running: keyboardDonePopup.opened
            onTriggered: keyboardDonePopup.place()
        }
        Connections {
            target: window
            function onActiveFocusItemChanged() {
                if (keyboardDonePopup.opened) {
                    keyboardDonePopup.place()
                    revealTimer.restart()
                }
            }
        }
        contentItem: AppButton {
            objectName: "keyboardDone"
            // The press-outside release (TextFocusRelease) leaves the field
            // to Done, which finishes it first.
            readonly property bool keepsTextFocus: true
            leftPadding: 16
            rightPadding: 16
            // A plain word: Android's font has no "⌄" (it showed a box).
            text: qsTr("Done")
            Accessible.name: qsTr("Done: as Enter, then hide the keyboard")
            focusPolicy: Qt.NoFocus
            onClicked: {
                // Done finishes the field as its Enter does, then puts the
                // keyboard away (the user, build 210 on the iPad and the
                // OnePlus: Done only hid the keyboard, so the call was not
                // logged and the CW text not sent, "toimii epäloogisesti").
                // An empty field, and one whose Enter does nothing, only
                // lose the keyboard.
                const field = window.activeFocusItem
                if (field && field.cursorRectangle !== undefined && typeof field.accepted === "function"
                        && String(field.text || "").trim().length > 0)
                    field.accepted()
                if (window.activeFocusItem)
                    window.activeFocusItem.focus = false
                Qt.inputMethod.hide()
                if (Engine.testKeyboard)
                    Engine.testKeyboardOff()
            }
        }
    }
    // The render tool's stand-in for a phone's status and navigation bars
    // (plan verb safearea): dim strips where they would be; at the sides
    // too, as a phone's outer screen on its side has them (a camera cutout
    // and the navigation bar). One item in the window's overlay, made only
    // while a test inset is set: as a Repeater's rectangles moved into the
    // overlay they wrote "Cannot stack ..." into every log at each start,
    // on every device (roadmap 207).
    Loader {
        active: Engine.testSafeTop + Engine.testSafeBottom + Engine.testSafeLeft + Engine.testSafeRight > 0
        sourceComponent: Item {
            parent: Overlay.overlay
            z: 998
            width: window.width
            height: window.height
            // Drawn only: a press goes to what is under it (TextFocusRelease).
            property bool passesPresses: true
            Rectangle { width: parent.width; height: Engine.testSafeTop; color: "#000000"; opacity: 0.55 }
            Rectangle { y: parent.height - height; width: parent.width; height: Engine.testSafeBottom; color: "#000000"; opacity: 0.55 }
            Rectangle { width: Engine.testSafeLeft; height: parent.height; color: "#000000"; opacity: 0.55 }
            Rectangle { x: parent.width - width; width: Engine.testSafeRight; height: parent.height; color: "#000000"; opacity: 0.55 }
        }
    }
    // The render tool's stand-in keyboard, drawn where a phone's would be.
    Rectangle {
        parent: Overlay.overlay
        z: 999
        // Drawn only: a press goes to what is under it (TextFocusRelease).
        property bool passesPresses: true
        visible: Engine.testKeyboard && Engine.testKeyboardHeight > 0
        x: 0
        y: window.height - Engine.testKeyboardHeight
        width: window.width
        height: Engine.testKeyboardHeight
        color: "#6b6f76"
    }
    // The developer link's press marks (a plan's "pressmarks on"): a ring
    // and a cross where the app took the last press, for two seconds, so a
    // tester sees it against where the finger was. Drawn only.
    Item {
        parent: Overlay.overlay
        z: 1000
        property bool passesPresses: true
        visible: Engine.pressMarks && pressMarkTimer.running
        x: Engine.pressMark.x
        y: Engine.pressMark.y
        Rectangle { x: -24; y: -24; width: 48; height: 48; radius: 24; color: "transparent"; border.width: 3; border.color: "#ffd400" }
        Rectangle { x: -34; y: -1; width: 68; height: 2; color: "#ffd400" }
        Rectangle { x: -1; y: -34; width: 2; height: 68; color: "#ffd400" }
        Timer { id: pressMarkTimer; interval: 2000 }
        Connections {
            target: Engine
            function onPressMarked() { pressMarkTimer.restart() }
        }
    }

    // A SmartLink radio presented a different TLS certificate than the one
    // pinned for it on first use. The connect waits for the operator.
    Dialog {
        id: certDialog
        property string host
        property string expected
        property string presented
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 24)
        // Fixed size: a wrapped label inside an auto-sized dialog loops.
        height: Math.min(340, parent.height - 24)
        title: qsTr("Radio certificate changed")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            // From the dialog's fixed width, not availableWidth, which would
            // feed the wrapped height back into the dialog's own size.
            width: certDialog.width - certDialog.leftPadding - certDialog.rightPadding
            wrapMode: Text.Wrap
            text: qsTr("The radio at %1 now presents a different security certificate than before. This is normal after a firmware update or reset, but it can also mean someone is intercepting the connection.\n\nTrust the new certificate and connect?").arg(certDialog.host)
        }
        onAccepted: Engine.answerCertificate(true)
        onRejected: Engine.answerCertificate(false)
    }
    Connections {
        target: Engine
        function onCertificateChanged(host, expected, presented) {
            certDialog.host = host
            certDialog.expected = expected
            certDialog.presented = presented
            certDialog.open()
        }
    }

    // The radio takes one station at a time (multiFLEX off, or a one-client
    // licence over SmartLink) and another is on it: the connect waits, as
    // the desktop's Connected stations dialog asks before going on.
    Popup {
        id: conflictDialog
        objectName: "stationConflict"
        // Named on its item: Accessible on a Popup itself is refused ("must be
        // attached to an object deriving from Item").
        Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => conflictDialog.c.full ? qsTr("All station slots are in use") : qsTr("Another station is on the radio"))
        readonly property var c: Engine.stationConflict
        visible: !!c.open
        modal: true
        closePolicy: Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, window.width - 2 * Theme.spacing)
        margins: Theme.spacing
        // Above the window's unsafe bottom (Android's navigation bar or a
        // OnePlus taskbar; the home indicator), as the pages keep: a tall
        // panel scrolls instead of going under it.
        bottomMargin: Theme.spacing + (parent && parent.Window.contentItem ? parent.Window.contentItem.SafeArea.margins.bottom : 0)
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
        // Esc, or the popup going away any other way while asked: cancel.
        onClosed: if (Engine.stationConflict.open) Engine.resolveStationConflict(0)
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: conflictDialog.c.full ? qsTr("All station slots are in use") : qsTr("Another station is on the radio")
                Accessible.role: Accessible.Heading
                color: Theme.text
                font.bold: true
                font.pixelSize: Math.round(Theme.buttonFont * 1.6)
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: (conflictDialog.c.full
                       ? qsTr("%1 has no station slot left. These stations are on it:")
                       : conflictDialog.c.wan
                       ? qsTr("%1 takes one station at a time over SmartLink, and this one is on it:")
                       : qsTr("%1 takes one station at a time (multiFLEX is off), and this one is on it:"))
                      .arg(conflictDialog.c.radio || qsTr("The radio"))
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Repeater {
                model: conflictDialog.c.clients || []
                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: Theme.spacing / 2
                    Label {
                        Layout.fillWidth: true
                        text: ([modelData.program, modelData.station].filter(t => !!t).join(" — ") || qsTr("A station"))
                              + (modelData.txMhz > 0 ? qsTr(", TX %1 MHz").arg(Number(modelData.txMhz).toFixed(3)) : "")
                        color: Theme.text
                        font.bold: true
                        wrapMode: Text.Wrap
                    }
                    AppButton {
                        objectName: "stationConflictDisconnect" + index
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        text: qsTr("Disconnect it and connect")
                        // Every row's button reads the same: the name says which.
                        readonly property string who: [modelData.program, modelData.station].filter(t => !!t).join(" — ")
                        Accessible.name: who ? qsTr("Disconnect %1 and connect").arg(who) : text
                        focusPolicy: Qt.NoFocus
                        onClicked: Engine.resolveStationConflict(modelData.handle)
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: conflictDialog.c.full
                      ? qsTr("Disconnecting ends that station's session on the radio. Several devices of yours tell apart by their names: Radio Setup › Radio › This station's name.")
                      : qsTr("Disconnecting ends that station's session on the radio. To share the radio instead, turn multiFLEX on in SmartSDR's Radio Setup.")
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.8)
                wrapMode: Text.Wrap
            }
            AppButton {
                objectName: "stationConflictCancel"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                text: qsTr("Cancel")
                focusPolicy: Qt.NoFocus
                onClicked: Engine.resolveStationConflict(0)
            }
        }
    }

    // A newer Android beta (roadmap 111), over whichever page is shown; the
    // page gets the keyboard back after it (Space PTT on a tablet).
    UpdateOffer {
        onClosed: if (pages.item) pages.item.forceActiveFocus()
    }

    Component { id: connectPage; ConnectPage {} }
    Component { id: operatePage; OperatePage {} }
    Component { id: phoneOperatePage; PhoneOperatePage {} }
}
