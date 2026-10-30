import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Display: band plan, the band row and step buttons, spots and the DX cluster, 2D waterfall or 3D,
// waterfall black level and colour gain (on
// top of the auto-tracked noise floor; Auto off holds it), and the spectrum
// trace's range (Auto, or reference level and range). Remembered on this
// device.
// Meter styles are chosen on each meter (tap it).
ColumnLayout {
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Band plan strip along the bottom of the spectrum.
    Label { text: qsTr("Band plan"); color: Theme.textDim; font.pixelSize: 16 }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: [""].concat(Engine.bandPlans)
            delegate: AppButton {
                required property string modelData
                text: modelData.length > 0 ? modelData : qsTr("Off")
                checked: Engine.bandPlan === modelData
                leftPadding: 10
                rightPadding: 10
                focusPolicy: Qt.NoFocus
                onClicked: Engine.bandPlan = modelData
            }
        }
    }
    // The band row and the step buttons (also B/S on the display), and what
    // the step buttons step by (Larry Averbeck).
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        Label { text: qsTr("Bands and steps"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
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
    // Spots on the display (the radio's, from every client) and this app's
    // DX cluster connection, whose spots go to the radio.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        Label { text: qsTr("Spots"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
        AppButton {
            text: Engine.showSpots ? qsTr("Shown") : qsTr("Hidden")
            checked: Engine.showSpots
            leftPadding: 10
            rightPadding: 10
            onClicked: Engine.showSpots = !Engine.showSpots
        }
    }
    // Which spots show: by mode and age.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.showSpots
        Repeater {
            model: [{ key: "all", label: qsTr("All") }, { key: "cw", label: qsTr("CW") },
                    { key: "phone", label: qsTr("Phone") }, { key: "digital", label: qsTr("Digital") }]
            delegate: AppButton {
                required property var modelData
                text: modelData.label
                checked: Engine.spotModes === modelData.key
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: Engine.spotModes = modelData.key
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.showSpots
        Repeater {
            model: [0, 5, 15, 30]
            delegate: AppButton {
                required property int modelData
                text: modelData === 0 ? qsTr("Any age") : qsTr("%1 min").arg(modelData)
                checked: Engine.spotMaxAgeMin === modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: Engine.spotMaxAgeMin = modelData
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            readonly property string state: Engine.dxCluster.state || "off"
            text: state === "off" ? qsTr("DX cluster") : state === "connected" ? qsTr("DX cluster ✓") : qsTr("Connecting…")
            checked: state !== "off"
            enabled: Engine.connectionState === "connected"
            Layout.preferredWidth: 140
            onClicked: state === "off" ? Engine.connectDxCluster() : Engine.disconnectDxCluster()
        }
        Label {
            Layout.fillWidth: true
            text: (Engine.dxCluster.host || "") + ":" + (Engine.dxCluster.port || "")
            color: Theme.textDim
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }
    // Whether the cluster is on, as the logbook's and PSK Reporter's lines.
    Label {
        objectName: "dxClusterStatus"
        Layout.fillWidth: true
        visible: Engine.connectionState === "connected" || Engine.dxClusterStatusKind === "failed"
        text: Engine.dxClusterStatus
        color: Engine.dxClusterStatusKind === "ok" ? "#00cc66"
             : Engine.dxClusterStatusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: dxHost
            Accessible.name: qsTr("DX cluster server")
            Layout.fillWidth: true
            placeholderText: qsTr("Cluster server")
            text: Engine.dxCluster.host || ""
            font.pixelSize: 14
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
            onEditingFinished: if (text !== Engine.dxCluster.host) Engine.setDxCluster("host", text)
        }
        TextField {
            Accessible.name: qsTr("DX cluster port")
            Layout.preferredWidth: 72
            placeholderText: qsTr("Port")
            text: Engine.dxCluster.port || ""
            font.pixelSize: 14
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 65535 }
            onEditingFinished: Engine.setDxCluster("port", Number(text))
        }
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            Accessible.name: qsTr("DX cluster callsign")
            Layout.fillWidth: true
            placeholderText: qsTr("Callsign (the radio's if empty)")
            text: Engine.dxCluster.callsignFromRadio ? "" : (Engine.dxCluster.callsign || "")
            font.pixelSize: 14
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            onEditingFinished: Engine.setDxCluster("callsign", text)
        }
        AppButton {
            text: qsTr("Auto")
            checked: !!Engine.dxCluster.autoConnect
            leftPadding: 10
            rightPadding: 10
            onClicked: Engine.setDxCluster("autoConnect", !checked)
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Logs in as %1. Spots go to the radio, so every program on it shows them; tap one on the display to tune to it. Auto connects with the radio.")
              .arg(Engine.dxCluster.callsign || qsTr("(no callsign)"))
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    // 2D waterfall or the 3D stacked trace.
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
    // The desktop's 3D Floor: how far under the noise the 3D zero sits (also
    // set by dragging the dBm scale on the display).
    Label {
        visible: Engine.spectrum3D
        text: qsTr("3D floor  %1 dB under the noise").arg(Engine.spectrum3DFloorDepth)
        color: Theme.textDim
        font.pixelSize: 15
    }
    Slider {
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
        font.pixelSize: 15
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
        font.pixelSize: 15
    }
    Slider {
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
            font.pixelSize: 15
        }
        Slider {
            id: psSlider
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
        Label { text: qsTr("Waterfall"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
        AppButton {
            text: qsTr("Auto")
            checked: Engine.wfAutoLevel
            focusPolicy: Qt.NoFocus
            onClicked: Engine.wfAutoLevel = !Engine.wfAutoLevel
        }
    }
    Label { text: qsTr("Black level  %1").arg(Engine.wfBlackLevel); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Accessible.name: qsTr("Black level")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        Layout.fillWidth: true
        from: 0; to: 100; stepSize: 1
        value: Engine.wfBlackLevel
        onMoved: Engine.wfBlackLevel = Math.round(value)
    }
    Label { text: qsTr("Colour gain  %1").arg(Engine.wfColorGain); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
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
        Label { text: qsTr("Spectrum"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
        AppButton {
            text: qsTr("Auto")
            checked: Engine.traceAutoRange
            focusPolicy: Qt.NoFocus
            onClicked: Engine.traceAutoRange = !Engine.traceAutoRange
        }
    }
    Label {
        text: qsTr("Reference  %1 dBm").arg(Engine.traceRefDbm)
        color: Theme.textDim
        opacity: Engine.traceAutoRange ? 0.5 : 1
        font.pixelSize: 15
    }
    Slider {
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
        font.pixelSize: 15
    }
    Slider {
        Accessible.name: qsTr("Range")
        Accessible.onIncreaseAction: { increase(); moved() }
        Accessible.onDecreaseAction: { decrease(); moved() }
        Layout.fillWidth: true
        from: 20; to: 150; stepSize: 5
        value: Engine.traceRangeDb
        onMoved: Engine.traceRangeDb = Math.round(value)
    }
}
