import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// One selectable meter face (SmartSDR style). Tap to choose what it shows.
// TX meters read only while the radio is on the air; otherwise they show "–".
Item {
    id: meter
    // Which remembered selection this face uses: "left" or "right", or
    // "left2" / "right2", the lower bar under it in the two-bar style.
    property string side: "left"
    readonly property string baseSide: side.startsWith("right") ? "right" : "left"
    readonly property bool lower: side === "left2" || side === "right2"
    // "analog", "bar" or "two" (two bars), the meter's own style.
    readonly property string style: baseSide === "left" ? Engine.meterStyleLeft : Engine.meterStyleRight
    property bool compact: false
    // Receive and transmit each have their own choice (e.g. S / SWR).
    readonly property bool txView: Engine.radioTransmitting || Engine.tuning || Engine.previewTx
    readonly property string rxKind: side === "left" ? Engine.meterLeft : side === "right" ? Engine.meterRight
                                   : side === "left2" ? Engine.meterLeft2 : Engine.meterRight2
    readonly property string txKind: side === "left" ? Engine.meterLeftTx : side === "right" ? Engine.meterRightTx
                                   : side === "left2" ? Engine.meterLeft2Tx : Engine.meterRight2Tx
    readonly property string kind: txView ? txKind : rxKind

    implicitWidth: compact ? 120 : 180
    // Where space is short (iPhone landscape) the host can ask for bars.
    property bool analogAllowed: true
    // Analog stays analog while transmitting too.
    readonly property bool analog: style === "analog" && !lower && analogAllowed
    // The bar's height from the host (0: the normal).
    property real slimHeight: 0
    // Height of an analog face; the host sizes it to the space available.
    property real faceHeight: compact ? 66 : 92
    implicitHeight: analog ? faceHeight : slimHeight > 0 ? slimHeight : (compact ? 40 : 50)
    // Width the analog face needs at its height (no empty sides).
    readonly property real analogFitWidth: gauge.fitWidth

    // value, min, max, text, alarm level and whether the reading is live
    readonly property var reading: {
        const tx = Engine.radioTransmitting
        switch (kind) {
        case "S":
            return { v: Engine.sMeterDbm, min: -127, max: -13, live: true, text: Engine.sMeterText, red: 1e9 }
        case "Power":
            return { v: Engine.forwardPowerW, min: 0, max: 120, live: tx,
                     text: Engine.forwardPowerW.toFixed(0) + " W", red: 100 }
        case "SWR":
            return { v: Engine.swrValid ? Engine.swr : 1, min: 1, max: 3, live: tx && Engine.swrValid,
                     text: "SWR " + (Engine.swrValid ? Engine.swr.toFixed(1) : "–"), red: 2.5 }
        case "ALC":
            return { v: Engine.alcDbfs, min: -20, max: 0, live: tx && Engine.alcAvailable,
                     text: "ALC " + meter.dbText(Engine.alcDbfs, -20), red: -3 }
        case "Mic":
            return { v: Engine.micLevelDbfs, min: -40, max: 0, live: Engine.micMeterLive,
                     text: "Mic " + meter.dbText(Engine.micLevelDbfs, -40), red: -3 }
        case "Comp":
            return { v: Engine.compressionDb, min: 0, max: 25, live: tx && Engine.procEnabled,
                     text: Math.round(Engine.compressionDb) > 0 ? "Comp −" + Engine.compressionDb.toFixed(0) + " dB" : "Comp 0 dB", red: 1e9 }
        case "PA temp":
            return { v: Engine.paTempC, min: 0, max: 120, live: Engine.hasPaTemp,
                     text: "PA " + Engine.paTempC.toFixed(0) + " °C", red: 70 }
        case "Volts":
            return { v: Engine.supplyVolts, min: 10, max: 16, live: Engine.hasSupplyVolts,
                     text: Engine.supplyVolts.toFixed(1) + " V", red: 15 }
        }
        return { v: 0, min: 0, max: 1, live: false, text: kind, red: 1e9 }
    }

    // A dB reading below the scale reads "< min" rather than a raw floor
    // value such as -150 dB (no signal at all, e.g. ALC during TUNE).
    function dbText(v, min) {
        return v < min ? "< " + min + " dB" : v.toFixed(0) + " dB"
    }

    // Peak hold, as the desktop S meter's decay choice (Engine.meterPeakHold,
    // shared by both meters): the highest reading stays, then falls back
    // toward the current one. Fast 0.2 s then 20 dB/s, Medium 0.5 s then
    // 10 dB/s, Slow 1 s then 5 dB/s; other scales fall in the same time for
    // their span as the S meter's 114 dB. It starts over whenever the meter
    // shows another quantity (receive/transmit switch, a new choice) or stops
    // reading, so one scale's peak never lands on another's.
    readonly property bool peakOn: Engine.meterPeakHold > 0
    readonly property int peakHoldMs: [0, 200, 500, 1000][Engine.meterPeakHold]
    readonly property real peakFallDbPerSec: [0, 20, 10, 5][Engine.meterPeakHold]
    property real peak: reading.min
    property double peakHoldUntil: 0
    readonly property real currentValue: reading.live ? reading.v : reading.min
    readonly property string scaleKey: reading.min + "/" + reading.max + "/" + reading.live
    function resetPeak() {
        peak = currentValue
        peakHoldUntil = 0
    }
    onScaleKeyChanged: Qt.callLater(resetPeak)
    onPeakOnChanged: Qt.callLater(resetPeak)
    // Deferred: written while currentValue's binding evaluates (e.g. for
    // the peak tick's visibility), peak would loop back into that binding.
    function updatePeak() {
        if (currentValue >= peak) {
            peak = currentValue
            peakHoldUntil = Date.now() + peakHoldMs
        }
    }
    onCurrentValueChanged: Qt.callLater(updatePeak)
    Timer {
        interval: 100
        repeat: true
        running: meter.peakOn && meter.reading.live && meter.peak > meter.currentValue
        onTriggered: {
            if (Date.now() < meter.peakHoldUntil) return
            const fall = (meter.reading.max - meter.reading.min) * meter.peakFallDbPerSec / 114 * 0.1
            meter.peak = Math.max(meter.currentValue, meter.peak - fall)
        }
    }

    AnalogGauge {
        id: gauge
        anchors.fill: parent
        visible: meter.analog
        peak: meter.peakOn ? meter.peak : meter.reading.min
        compact: meter.compact
        kind: meter.kind
        value: meter.reading.v
        min: meter.reading.min
        max: meter.reading.max
        red: meter.reading.red
        live: meter.reading.live
        text: meter.reading.text
    }

    BarGauge {
        id: bars
        anchors.fill: parent
        visible: !meter.analog
        kind: meter.kind
        value: meter.reading.v
        min: meter.reading.min
        max: meter.reading.max
        red: meter.reading.red
        live: meter.reading.live
        text: meter.reading.text
        peak: meter.peak
        showPeak: meter.peakOn
        headerRightInset: barCaret.width + 4
    }
    Caret {
        id: barCaret
        visible: !meter.analog
        anchors.right: parent.right
        y: Math.max(0, (bars.headerHeight - height) / 2)
        color: Theme.textDim
        size: meter.compact ? 9 : 11
    }

    // Open the chooser right under this meter; the popup's margins keep it
    // inside the window (the right meter's opens leftwards).
    TapHandler { onTapped: chooser.open() }

    // Style and what the face shows in receive and in transmit, as the
    // app's buttons: the current choice of each is the dark (checked) one.
    Popup {
        id: chooser
        objectName: "meterChooser"
        parent: meter
        y: meter.height + 4
        margins: Theme.spacing
        padding: Theme.spacing
        modal: false
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
        readonly property real cellWidth: Math.max(Theme.buttonWidth * 0.9, 64)
        contentItem: ColumnLayout {
            spacing: Theme.spacing / 2
            Label { text: qsTr("Style"); color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.85) }
            // Two bars: one over the other, each with its own receive and
            // transmit choice (the lower bar's from a tap on it).
            RowLayout {
                spacing: 4
                Repeater {
                    model: [{ label: qsTr("Analog"), style: "analog" }, { label: qsTr("Bar"), style: "bar" },
                            { label: qsTr("Two bars"), style: "two" }]
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        checked: meter.style === modelData.style
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: (chooser.cellWidth * 4 + 4) / 3
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            // From the lower bar, which goes away with it.
                            if (meter.lower && modelData.style !== "two")
                                chooser.close()
                            Engine.setMeterStyle(meter.baseSide, modelData.style)
                        }
                    }
                }
            }
            Label { text: qsTr("Peak hold"); color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.85) }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [qsTr("Off"), qsTr("Fast"), qsTr("Medium"), qsTr("Slow")]
                    delegate: AppButton {
                        required property string modelData
                        required property int index
                        text: modelData
                        checked: Engine.meterPeakHold === index
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: chooser.cellWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.meterPeakHold = index
                    }
                }
            }
            Label {
                visible: meter.style === "analog" && !meter.lower && !meter.analogAllowed
                text: qsTr("Bars here: an analog face needs more room. It returns where there is space.")
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.8)
                wrapMode: Text.Wrap
                Layout.preferredWidth: chooser.cellWidth * 4 + 8
            }
            Repeater {
                // With two bars each has its own choices.
                model: meter.lower ? [{ title: qsTr("Lower bar: receive"), tx: false }, { title: qsTr("Lower bar: transmit"), tx: true }]
                     : meter.style === "two" ? [{ title: qsTr("Upper bar: receive"), tx: false }, { title: qsTr("Upper bar: transmit"), tx: true }]
                     : [{ title: qsTr("Receive"), tx: false }, { title: qsTr("Transmit"), tx: true }]
                delegate: ColumnLayout {
                    id: section
                    required property var modelData
                    spacing: Theme.spacing / 2
                    Label { text: section.modelData.title; color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.85) }
                    GridLayout {
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Repeater {
                            model: Engine.meterKindsFor(meter.side, section.modelData.tx)
                            delegate: AppButton {
                                required property string modelData
                                text: modelData
                                checked: (section.modelData.tx ? meter.txKind : meter.rxKind) === modelData
                                focusPolicy: Qt.NoFocus
                                Layout.preferredWidth: chooser.cellWidth
                                Layout.preferredHeight: Theme.touchTarget
                                onClicked: Engine.setMeter(meter.side, section.modelData.tx, modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
