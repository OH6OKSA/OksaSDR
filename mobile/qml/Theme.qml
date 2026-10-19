pragma Singleton
import QtQuick
import AetherMobile

// Colours, plus the app's adaptive metrics: everything sized through Theme
// follows the window (rotation, Split View, Stage Manager, folding devices)
// without per-device rules. Main.qml feeds in the window size.
QtObject {
    readonly property color background: "#0f1419"
    readonly property color surface: "#1a222b"
    readonly property color surfaceRaised: "#243040"
    readonly property color text: "#e6edf3"
    readonly property color textDim: "#8b98a5"
    readonly property color accent: "#3fb6ff"
    readonly property color transmit: "#e5484d"
    readonly property color pttReady: "#7a1f22"
    readonly property color warning: "#f5a524"
    readonly property color passband: "#ffd75e"

    // ---- Buttons (Engine.buttonStyle, remembered) ----
    // classic: light buttons, dark when on; light: dark buttons, light when
    // on; lit: dark buttons, blue when on.
    readonly property string buttonStyle: Engine.buttonStyle
    readonly property bool darkButtons: buttonStyle !== "classic"
    readonly property color buttonFace: darkButtons ? "#2d3947" : "#e0e0e0"
    readonly property color buttonFaceDown: darkButtons ? "#3d4b5c" : "#bdbdbd"
    readonly property color buttonText: darkButtons ? "#e6edf3" : "#1a1a1a"
    readonly property color buttonFaceOn: buttonStyle === "light" ? "#f2f4f7" : buttonStyle === "lit" ? "#1f78d1" : "#353637"
    readonly property color buttonFaceOnDown: buttonStyle === "light" ? "#cfd5dc" : buttonStyle === "lit" ? "#1a66b3" : "#4a4b4c"
    readonly property color buttonTextOn: buttonStyle === "light" ? "#10161c" : "#ffffff"
    // Secondary text on a button face (e.g. a memory's mode).
    readonly property color buttonTextDim: darkButtons ? "#7fc3ff" : "#2a5c8a"

    // ---- Window (set by Main.qml) ----
    property real windowWidth: 1366
    property real windowHeight: 1024

    // ---- Classes, as Apple's size classes ----
    readonly property real shortSide: Math.min(windowWidth, windowHeight)
    readonly property bool landscape: windowWidth > windowHeight
    // Compact width: an iPhone, a folded phone, a narrow Split View pane.
    readonly property bool compactWidth: windowWidth < 700
    // Compact height: an iPhone (or small pane) in landscape.
    readonly property bool compactHeight: windowHeight < 500
    // Very low: a phone's outer screen in landscape, small iPhones in
    // landscape. Meters fall back to bars to leave room for the display.
    readonly property bool tightHeight: windowHeight < 400
    // The phone layout (drawer controls, strip PTT) for either.
    readonly property bool phoneLayout: compactWidth || compactHeight

    // ---- Continuous metrics from the short side ----
    // 1.0 on an 11" iPad (834 pt); about 0.9 on an iPhone, 1.1 on a 13" iPad.
    readonly property real scale: Math.max(0.85, Math.min(1.15, 0.7 + shortSide / 2800))
    // Never under Apple's 44 pt minimum touch target.
    readonly property int touchTarget: Math.max(44, Math.round(48 * scale))
    // Standard button (the band buttons' size); PTT and KEY are larger.
    readonly property int buttonWidth: Math.round(56 * scale)
    readonly property int buttonFont: Math.max(12, Math.round(14 * scale))
    readonly property int labelFont: Math.max(12, Math.round(15 * scale))
    readonly property int smallFont: Math.max(10, Math.round(12 * scale))
    readonly property int spacing: Math.round(12 * scale)
    readonly property int radius: 8

    // ---- Step buttons under the display ----
    // Hz for each button, down then up. HF tunes by 1 Hz to 1 kHz; with an
    // FM channel step (5 kHz or more, e.g. 12.5 kHz on 70 cm) the buttons
    // take the radio's STEP and jump 100 kHz, 1 MHz and 10 MHz, all on the
    // STEP's grid.
    function stepButtons(stepHz) {
        if (stepHz >= 5000) {
            return [-10000000, -1000000, -100000, -stepHz, stepHz, 100000, 1000000, 10000000]
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
