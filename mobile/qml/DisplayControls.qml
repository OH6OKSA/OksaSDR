import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Display: the band row and step buttons, 2D waterfall or 3D (spots have a
// tab of their own since build 203, SpotsControls.qml),
// waterfall black level and colour gain (on
// top of the auto-tracked noise floor; Auto off holds it), and the spectrum
// trace's range (Auto, or reference level and range). Remembered on this
// device.
// Meter styles are chosen on each meter (tap it).
ColumnLayout {
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Big buttons (roadmap 55, Michael Walker): the page covered by a few
    // large controls.
    AppButton {
        objectName: "bigButtonsOpen"
        Layout.fillWidth: true
        // Already there, it leaves: the button did nothing then, and a tester
        // who had not found Exit's hold took himself for stuck (Kent
        // Winrich, FB 2026-11-24, Android beta 117; roadmap 161).
        text: Engine.bigButtons ? qsTr("Exit Big buttons") : qsTr("Big buttons")
        focusPolicy: Qt.NoFocus
        onClicked: Engine.bigButtons = !Engine.bigButtons
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Large − and + by the tuning step, the step and PTT on one screen: in the car (mounted, used as the law allows), with gloves or at a glance. Hold Exit to leave.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // Own layouts (roadmap 113, Larry Averbeck W9FLA): the screen's parts
    // where the operator puts them, the desktop's Workspace Canvas. On the
    // tablet page (iPad, Mac, Android tablets); a phone keeps its own.
    readonly property QtObject ws: Engine.workspace
    Label {
        visible: !Theme.phoneLayout
        text: qsTr("Layout")
        Accessible.role: Accessible.Heading
        color: Theme.textDim
        font.pixelSize: Theme.px(16)
    }
    Flow {
        visible: !Theme.phoneLayout
        Layout.fillWidth: true
        spacing: Theme.spacing / 2
        AppButton {
            objectName: "layoutClassic"
            text: qsTr("Classic")
            checked: !ws.canvas
            focusPolicy: Qt.NoFocus
            onClicked: ws.canvas = false
        }
        Repeater {
            model: ws.layouts
            delegate: AppButton {
                required property var modelData
                objectName: "layout:" + modelData.id
                text: modelData.label
                checked: ws.canvas && ws.active === modelData.id
                focusPolicy: Qt.NoFocus
                onClicked: {
                    ws.active = modelData.id
                    ws.canvas = true
                }
            }
        }
    }
    RowLayout {
        visible: !Theme.phoneLayout
        Layout.fillWidth: true
        AppButton {
            objectName: "layoutEdit"
            text: ws.canvas ? qsTr("Edit") : qsTr("Edit a copy")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: ws.requestEdit(false)
        }
        AppButton {
            objectName: "layoutNew"
            text: qsTr("New")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: ws.requestEdit(true)
        }
    }
    Label {
        visible: !Theme.phoneLayout
        Layout.fillWidth: true
        text: qsTr("Arrange the screen yourself: in Edit drag a part to move it, tap it and drag its corners or edges to resize it, × hides it and + Add brings it back. Each layout keeps landscape and portrait apart; Classic stays as it is.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // The band row and the step buttons (also B/S on the display), and what
    // the step buttons step by (Larry Averbeck).
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        Label { text: qsTr("Bands and steps"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16); Layout.fillWidth: true }
        AppButton {
            text: Engine.tuneRowsShown ? qsTr("Shown") : qsTr("Hidden")
            checked: Engine.tuneRowsShown
            leftPadding: 10
            rightPadding: 10
            focusPolicy: Qt.NoFocus
            onClicked: Engine.tuneRowsShown = !Engine.tuneRowsShown
        }
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("1 Hz – 1 kHz")
            checked: !Engine.radioStepButtons
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            focusPolicy: Qt.NoFocus
            onClicked: Engine.radioStepButtons = false
        }
        AppButton {
            text: qsTr("STEP ×1 ×10 ×100")
            checked: Engine.radioStepButtons
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            focusPolicy: Qt.NoFocus
            onClicked: Engine.radioStepButtons = true
        }
    }
    // This app's look and behaviour (Theme, Text size, Zoom buttons, Band
    // plan, Felt taps, Hints, New versions) are set seldom: Setup › App
    // (roadmap 131, the user: the Controls tabs keep what is used while
    // operating, one button to where the rest is set).
    AppButton {
        objectName: "openAppSetup"
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        text: qsTr("Theme, text size, band plan…")
        focusPolicy: Qt.NoFocus
        onClicked: Engine.openSetup(11)
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            objectName: "view2D"
            text: qsTr("2D waterfall")
            checked: !Engine.spectrum3D
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.spectrum3D = false
        }
        AppButton {
            objectName: "view3D"
            text: qsTr("3D")
            checked: Engine.spectrum3D
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.spectrum3D = true
        }
    }
    // The noise floor the display reads (its auto levels, Auto AGC-T's
    // following) as a dashed line with its dBm, 2D and 3D (roadmap 48).
    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: qsTr("Noise floor line")
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "noiseFloorLine"
            text: Engine.noiseFloorLine ? qsTr("On") : qsTr("Off")
            checked: Engine.noiseFloorLine
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Noise floor line, %1").arg(Engine.noiseFloorLine ? qsTr("on") : qsTr("off"))
            leftPadding: 10
            rightPadding: 10
            onClicked: Engine.noiseFloorLine = !Engine.noiseFloorLine
        }
    }
    // The desktop's 3D Floor: how far under the noise the 3D zero sits (also
    // set by dragging the dBm scale on the display).
    Label {
        visible: Engine.spectrum3D
        text: qsTr("3D floor  %1 dB under the noise").arg(Engine.spectrum3DFloorDepth)
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    AppSlider {
        objectName: "floor3dSlider"
        Accessible.name: qsTr("3D floor")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        visible: Engine.spectrum3D
        Layout.fillWidth: true
        from: 0; to: 24; stepSize: 1
        value: Engine.spectrum3DFloorDepth
        onMoved: Engine.spectrum3DFloorDepth = Math.round(value)
    }
    // How much the 3D rows are smoothed: Off is as live as the 2D trace,
    // Light keeps narrow dips, Desktop is the desktop's calm surface.
    Label {
        visible: Engine.spectrum3D
        text: qsTr("3D smoothing")
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    RowLayout {
        visible: Engine.spectrum3D
        Layout.fillWidth: true
        Repeater {
            model: [qsTr("Off"), qsTr("Light"), qsTr("Desktop")]
            delegate: AppButton {
                required property string modelData
                required property int index
                text: modelData
                checked: Engine.spectrum3DSmoothing === index
                Layout.fillWidth: true
                onClicked: Engine.spectrum3DSmoothing = index
            }
        }
    }
    // The desktop's 3D Gain: the 3D colours' gamma (higher lifts colour toward
    // the noise floor, lower keeps it for the strongest signals).
    Label {
        visible: Engine.spectrum3D
        text: qsTr("3D gain  %1").arg(Engine.spectrum3DGain)
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    AppSlider {
        objectName: "gain3dSlider"
        Accessible.name: qsTr("3D gain")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        visible: Engine.spectrum3D
        Layout.fillWidth: true
        from: 0; to: 100; stepSize: 1
        value: Engine.spectrum3DGain
        onMoved: Engine.spectrum3DGain = Math.round(value)
    }
    // The radio's FFT and waterfall pacing (the desktop's overlay menu):
    // averaging smooths the trace, FPS sets its frame rate, the waterfall
    // speed how fast lines scroll. Sent when a slider is released.
    component PanSetting: ColumnLayout {
        id: ps
        property string key: ""
        property string title: ""
        property int from: 0
        property int to: 100
        Layout.fillWidth: true
        spacing: 0
        Label {
            text: ps.title + "  " + Math.round(psSlider.value)
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
        }
        AppSlider {
            id: psSlider
            objectName: ps.key + "Slider"
            Accessible.name: ps.title
            // Sent on release; a screen reader's swipe is one step, sent at once.
            Accessible.onIncreaseAction: { increase(); Engine.setPanDisplay(ps.key, Math.round(value)) }
            Accessible.onDecreaseAction: { decrease(); Engine.setPanDisplay(ps.key, Math.round(value)) }
            Layout.fillWidth: true
            from: ps.from; to: ps.to; stepSize: 1
            onPressedChanged: if (!pressed) Engine.setPanDisplay(ps.key, Math.round(value))
            Binding on value {
                when: !psSlider.pressed
                value: Engine.panDisplay[ps.key] !== undefined ? Engine.panDisplay[ps.key] : ps.from
            }
        }
    }
    PanSetting { key: "average"; title: qsTr("FFT average"); from: 0; to: 100 }
    PanSetting { key: "fps"; title: qsTr("FFT frames/s"); from: 5; to: 60 }
    PanSetting { key: "wfRate"; title: qsTr("Waterfall speed"); from: 1; to: 100 }

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Waterfall"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16); Layout.fillWidth: true }
        AppButton {
            text: qsTr("Auto")
            // Two buttons say "Auto": what each sets, after the word.
            Accessible.name: qsTr("Auto, waterfall level")
            checked: Engine.wfAutoLevel
            focusPolicy: Qt.NoFocus
            onClicked: Engine.wfAutoLevel = !Engine.wfAutoLevel
        }
    }
    Label { text: qsTr("Black level  %1").arg(Engine.wfBlackLevel); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    AppSlider {
        objectName: "blackLevelSlider"
        Accessible.name: qsTr("Black level")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        Layout.fillWidth: true
        from: 0; to: 100; stepSize: 1
        value: Engine.wfBlackLevel
        onMoved: Engine.wfBlackLevel = Math.round(value)
    }
    Label { text: qsTr("Colour gain  %1").arg(Engine.wfColorGain); color: Theme.textDim; font.pixelSize: Theme.px(15) }
    AppSlider {
        objectName: "colourGainSlider"
        Accessible.name: qsTr("Colour gain")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        Layout.fillWidth: true
        from: 0; to: 100; stepSize: 1
        value: Engine.wfColorGain
        onMoved: Engine.wfColorGain = Math.round(value)
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        Label { text: qsTr("Spectrum"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16); Layout.fillWidth: true }
        AppButton {
            text: qsTr("Auto")
            Accessible.name: qsTr("Auto, spectrum range")
            checked: Engine.traceAutoRange
            focusPolicy: Qt.NoFocus
            onClicked: Engine.traceAutoRange = !Engine.traceAutoRange
        }
    }
    Label {
        text: qsTr("Reference  %1 dBm").arg(Engine.traceRefDbm)
        color: Theme.textDim
        opacity: Engine.traceAutoRange ? 0.5 : 1
        font.pixelSize: Theme.px(15)
    }
    AppSlider {
        objectName: "refLevelSlider"
        Accessible.name: qsTr("Reference level")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        Layout.fillWidth: true
        enabled: !Engine.traceAutoRange
        from: -140; to: 0; stepSize: 1
        value: Engine.traceRefDbm
        onMoved: Engine.traceRefDbm = Math.round(value)
    }
    // Range is the span in Auto too (Auto slides the level to the noise),
    // and the 3D view's height span.
    Label {
        text: qsTr("Range  %1 dB").arg(Engine.traceRangeDb)
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    AppSlider {
        objectName: "rangeSlider"
        Accessible.name: qsTr("Range")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        Layout.fillWidth: true
        from: 20; to: 150; stepSize: 5
        value: Engine.traceRangeDb
        onMoved: Engine.traceRangeDb = Math.round(value)
    }
    // The desktop's FFT Smooth and De-comb (roadmap 22): the 2D trace only,
    // so hidden with 3D on. Smooth evens the noise across frequency (linear
    // power, the floor not pulled down); De-comb draws a zoomed-in SSB
    // voice's comb of pitch harmonics as one outline. 0 is Off.
    Label {
        visible: !Engine.spectrum3D
        text: Engine.fftSmoothing > 0 ? qsTr("FFT smooth  %1 px").arg(Engine.fftSmoothing) : qsTr("FFT smooth  Off")
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    AppSlider {
        objectName: "fftSmoothSlider"
        Accessible.name: qsTr("FFT smooth")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        visible: !Engine.spectrum3D
        Layout.fillWidth: true
        from: 0; to: 16; stepSize: 1
        value: Engine.fftSmoothing
        onMoved: Engine.fftSmoothing = Math.round(value)
    }
    Label {
        visible: !Engine.spectrum3D
        text: Engine.fftDecombHz > 0 ? qsTr("De-comb  %1 Hz").arg(Engine.fftDecombHz) : qsTr("De-comb  Off")
        color: Theme.textDim
        font.pixelSize: Theme.px(15)
    }
    AppSlider {
        objectName: "fftDecombSlider"
        Accessible.name: qsTr("De-comb")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        visible: !Engine.spectrum3D
        Layout.fillWidth: true
        from: 0; to: 600; stepSize: 10
        value: Engine.fftDecombHz
        onMoved: Engine.fftDecombHz = Math.round(value)
    }
}
