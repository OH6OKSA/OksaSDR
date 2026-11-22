pragma Singleton
import QtQuick
import AetherMobile

// Colours, plus the app's adaptive metrics: everything sized through Theme
// follows the window (rotation, Split View, Stage Manager, folding devices)
// without per-device rules. Main.qml feeds in the window size.
QtObject {
    // ---- Dark or light (Setup › App › Theme, roadmap 118, 131) ----
    // Dark is the default and Classic as it has always been: every dark value
    // below is the one the app had before the light theme, so a dark screen
    // stays pixel for pixel the same (classic-check). Light is the operator's
    // choice for daylight and tired eyes (Larry Averbeck W9FLA).
    readonly property bool light: Engine.lightTheme
    // The display (spectrum, waterfall, 3D and what is drawn on them) stays
    // dark in both, as a radio's screen: the waterfall's palette is made on
    // black, and its overlays keep one set of colours. Items on the display
    // take these; the page around it takes the tokens below.
    readonly property QtObject dark: QtObject {
        readonly property color background: "#0f1419"
        readonly property color surface: "#1a222b"
        readonly property color surfaceRaised: "#243040"
        readonly property color text: "#e6edf3"
        readonly property color textDim: "#8b98a5"
        readonly property color accent: "#3fb6ff"
        readonly property color transmit: "#e5484d"
        readonly property color warning: "#f5a524"
    }

    // Light: text 4.5:1 or more (WCAG AA) on the page, the panels, the raised
    // rows and the buttons, the accent, red, amber and green as text too
    // (computed: the dim text 5.4-7.2, the accent 5.3-7.1, the others 5.0 or
    // more).
    readonly property color background: light ? "#eef1f4" : dark.background
    readonly property color surface: light ? "#ffffff" : dark.surface
    readonly property color surfaceRaised: light ? "#d9e0e8" : dark.surfaceRaised
    readonly property color text: light ? "#10161c" : dark.text
    readonly property color textDim: light ? "#4c5966" : dark.textDim
    readonly property color accent: light ? "#0958a8" : dark.accent
    readonly property color transmit: light ? "#b3202a" : dark.transmit
    // "OK" in the status lines (a connection, a sent QSO, a locked GPS).
    readonly property color good: light ? "#086b34" : "#00cc66"
    // Small white-text tags (TX, the slice letter): a shade darker than the
    // transmit red and the slice blue, for 4.5:1 against white (the
    // usability review: 3.9 and 4.1).
    readonly property color transmitTag: "#cf3a3f"
    readonly property color sliceTag: "#2a72c2"
    readonly property color pttReady: "#7a1f22"
    readonly property color warning: light ? "#8a4d00" : dark.warning
    readonly property color passband: "#ffd75e"

    // ---- Buttons (Engine.buttonStyle, remembered) ----
    // classic: light buttons, dark when on; light: dark buttons, light when
    // on; lit: dark buttons, blue when on.
    readonly property string buttonStyle: Engine.buttonStyle
    // The light theme has its own: light grey buttons, blue when on.
    readonly property bool darkButtons: !light && buttonStyle !== "classic"
    readonly property color buttonFace: light ? "#dde3ea" : darkButtons ? "#2d3947" : "#e0e0e0"
    readonly property color buttonFaceDown: light ? "#c3ccd6" : darkButtons ? "#3d4b5c" : "#bdbdbd"
    readonly property color buttonText: light ? "#10161c" : darkButtons ? "#e6edf3" : "#1a1a1a"
    readonly property color buttonFaceOn: light ? "#1c70c8" : buttonStyle === "light" ? "#f2f4f7" : buttonStyle === "lit" ? "#1c70c8" : "#353637"
    readonly property color buttonFaceOnDown: light ? "#1a66b3" : buttonStyle === "light" ? "#cfd5dc" : buttonStyle === "lit" ? "#1a66b3" : "#4a4b4c"
    readonly property color buttonTextOn: light ? "#ffffff" : buttonStyle === "light" ? "#10161c" : "#ffffff"
    // Secondary text on a button face (e.g. a memory's mode).
    readonly property color buttonTextDim: light ? "#1f4f80" : darkButtons ? "#7fc3ff" : "#2a5c8a"

    // ---- Window (set by Main.qml) ----
    property real windowWidth: 1366
    property real windowHeight: 1024

    // ---- Classes, as Apple's size classes ----
    readonly property real shortSide: Math.min(windowWidth, windowHeight)
    readonly property bool landscape: windowWidth > windowHeight
    // Compact width: an iPhone, a folded phone, a narrow Split View pane.
    // A foldable's inner screen keeps the tablet layout in both orientations
    // (upright, the iPhone Duo's is 626 pt wide and the OnePlus Open's 698;
    // both got the phone's).
    readonly property bool compactWidth: windowWidth < 600
    // Compact height: an iPhone (or small pane) in landscape.
    readonly property bool compactHeight: windowHeight < 500
    // Very low: a phone's outer screen in landscape, small iPhones in
    // landscape. Meters fall back to bars to leave room for the display.
    readonly property bool tightHeight: windowHeight < 400
    // The phone layout (drawer controls, strip PTT) for either.
    readonly property bool phoneLayout: compactWidth || compactHeight

    // ---- Continuous metrics from the short side ----
    // 1.0 on an 11" iPad (834 pt), about 0.9 on an iPhone. Above it grows
    // faster: a 13" iPad 1.14, a MacBook's full window about 1.15, an
    // external display up to 1.3 (it stopped at 1.07 on a 13" iPad and 1.15
    // anywhere: phone-sized text on a big screen, the usability review).
    readonly property real scale: shortSide <= 840 ? Math.max(0.85, 0.7 + shortSide / 2800)
                                                   : Math.min(1.3, 1.0 + (shortSide - 840) / 1300)
    // The big screen's share of it, for text read in the panels: phones and
    // the 11" iPad 1.0, so their panels stay as they are.
    readonly property real bigScale: Math.max(1.0, scale)
    // Never under Apple's 44 pt minimum touch target.
    readonly property int touchTarget: Math.max(44, Math.round(48 * scale))
    // Standard button (the band buttons' size); PTT and KEY are larger.
    readonly property int buttonWidth: Math.round(56 * scale)
    // The system's text size (Engine.textScale: iOS Dynamic Type, Android's
    // font scale, or View › Text size) for the text that is read: the panels'
    // and dialogs' labels, explanations and lists, through px(). The
    // operating screen's dense controls (frequency, info row, meters, the
    // step and band rows, PTT) and the buttons' own text keep their size: at
    // 115 % a phone's step row and PTT row no longer fit its width. A big
    // screen (bigScale) grows them all through scale, the panels' text too.
    readonly property real textScale: Engine.textScale
    function px(n) { return Math.round(n * textScale * bigScale) }
    readonly property int buttonFont: Math.max(12, Math.round(14 * scale))
    readonly property int labelFont: Math.max(12, Math.round(15 * scale))
    readonly property int smallFont: Math.max(10, Math.round(12 * scale))
    readonly property int spacing: Math.round(12 * scale)
    readonly property int radius: 8

    // ---- Step buttons under the display ----
    // Hz for each button, down then up. HF tunes by 1 Hz to 1 kHz, or with
    // radioStep (Display › Step buttons) by the radio's STEP and ten and a
    // hundred of it (Larry Averbeck: 500 Hz). With an FM channel step (5 kHz
    // or more, e.g. 12.5 kHz on 70 cm) the buttons take the radio's STEP
    // either way and jump 100 kHz, 1 MHz and 10 MHz, all on the STEP's grid.
    function stepButtons(stepHz, radioStep) {
        if (stepHz >= 5000) {
            return [-10000000, -1000000, -100000, -stepHz, stepHz, 100000, 1000000, 10000000]
        }
        if (radioStep && stepHz > 0) {
            return [-100 * stepHz, -10 * stepHz, -stepHz, stepHz, 10 * stepHz, 100 * stepHz]
        }
        return [-1000, -100, -10, -1, 1, 10, 100, 1000]
    }
    // "1", "100", "12.5k", "100k", "1M".
    function stepLabel(hz) {
        if (hz >= 1000000) return (hz / 1000000) + "M"
        if (hz >= 1000) return (hz / 1000) + "k"
        return String(hz)
    }
}
