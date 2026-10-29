import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The active slice at a glance and in reach, after the slice flag of
// SmartSDR-style clients: slice letter, TX tag, SPLIT, RX antenna (blue), TX
// antenna (red), filter width, the speaker and the tuning lock; then the
// DSP flags (lit when on) and AGC, which can run long. Each is a
// finger-sized target with its choices behind it: antennas open their
// chooser, the width and the flags the filter/DSP chooser, AGC its modes, AGC-T and RF gain;
// the speaker the volume and mute, the lock locks, the dot records the slice
// and the triangle plays it back (the radio's quick record).
// Two rows next to the frequency on a tablet; one row elsewhere.
Item {
    id: info
    property bool singleLine: false
    // The tablet shows the step under the mode instead (StepButton).
    property bool showStep: true
    // Groups, always-on first: the slice (letter, TX), the core settings
    // (antennas, filter width, AGC), the controls (speaker, lock); then the
    // optional SPLIT and DSP flags. Phones in portrait split them between the
    // frequency row and the row under it.
    property bool showIdentity: true
    property bool showCore: true
    property bool showControls: true
    property bool showOptions: true
    // Row height as a share of the default (a compact beside-the-frequency line).
    property real rowScale: 1.0
    // Type size as a share of the default (the narrowest phones).
    property real fontScale: 1.0
    // Only measured (the tablet page's probes, shown at opacity 0): out of
    // the screen reader's way, which would read every probe as a second
    // panel.
    property bool measureOnly: false

    readonly property var activeSlice: {
        for (const s of Engine.slices) {
            if (s.active) return s
        }
        return null
    }
    readonly property real fontPx: Math.round((singleLine ? Theme.buttonFont * 0.85 : Theme.buttonFont * 1.15)
                                              * fontScale)
    // Height of a row, and so of every target in it.
    readonly property real rowH: Math.round((singleLine ? Theme.touchTarget : Theme.touchTarget * 0.8) * rowScale)
    readonly property real gap: Math.round(fontPx * 0.45)
    // The icons' least width on one line (lock, record, play: 21 px wide in
    // the phone's rows were hard to hit), 36 on a phone. Not in the tablet's
    // two lines beside the frequency: their width must shrink with the type,
    // or the page's fit (two probes, OperatePage) drops the panel under the
    // frequency and leaves the room beside it empty (the iPhone Duo's inner
    // screen did).
    property real iconW: Math.round(Theme.touchTarget * 0.82)
    // Off flags and idle icons: 5:1 on the header (#6b7785 was 3.5:1).
    readonly property color dim: "#84919e"
    readonly property color rxColor: "#3fb6ff"

    function stepLabel(hz) {
        return hz >= 1000 ? (hz / 1000).toFixed(hz % 1000 ? 1 : 0) + " kHz" : hz + " Hz"
    }
    function widthLabel(hz) {
        if (hz <= 0) return ""
        return hz >= 1000 ? (hz / 1000).toFixed(1).replace(/\.0$/, "") + "K" : hz + ""
    }
    readonly property var flags: {
        Engine.dspKeys.length   // re-evaluate on DSP changes
        const keys = Engine.dspKeys
        const out = []
        // Every DSP the radio and mode offer, lit when on. On one line
        // (phones) the rarer ones only while on: there is no room for all.
        const base = ["NB", "NR", "ANF", "APF", "BC"]
        for (const k of ["NB", "NR", "ANF", "NRL", "ANFL", "ANFT", "NRS", "RNN", "NRF", "APF", "BC"]) {
            if (keys.indexOf(k) < 0) continue
            const on = Engine.dspOn(k)
            if (info.singleLine && !on && base.indexOf(k) < 0) continue
            out.push({ text: k, on: on, idle: k === "BC" && !Engine.beatCancelAvailable })
        }
        if (Engine.cwMode) out.push({ text: "QSK", on: Engine.cwBreakIn })
        return out
    }
    readonly property string agcText: {
        const m = Engine.agcMode.toLowerCase()
        return m === "off" ? "AGC-OFF" : "AGC-" + m.charAt(0).toUpperCase()
    }

    implicitWidth: lines.implicitWidth
    implicitHeight: lines.implicitHeight

    // A tappable word: the row's full height, a little wider than the text,
    // pressed feedback.
    component Hit: Item {
        id: hit
        property alias text: label.text
        property alias color: label.color
        property real fontPx: 16
        property real rowH: 40
        property real pad: 12
        signal tapped()
        signal held()
        implicitWidth: label.implicitWidth + pad
        implicitHeight: rowH
        Rectangle {
            anchors.fill: parent
            radius: 6
            color: "#ffffff"
            opacity: area.pressed ? 0.15 : 0
        }
        Text {
            id: label
            Accessible.ignored: info.measureOnly
            anchors.centerIn: parent
            font.pixelSize: hit.fontPx
            font.bold: true
        }
        MouseArea {
            id: area
            anchors.fill: parent
            onClicked: hit.tapped()
            onPressAndHold: hit.held()
        }
    }

    Grid {
        id: lines
        anchors.verticalCenter: parent.verticalCenter
        columns: info.singleLine ? 2 : 1
        columnSpacing: info.gap
        rowSpacing: 0
        verticalItemAlignment: Grid.AlignVCenter

        // Always on: the step (where shown), slice letter and TX, antennas,
        // filter width, AGC, speaker, lock.
        Row {
            visible: info.showStep || info.showIdentity || info.showCore || info.showControls
            spacing: 0
            // Tuning step (the desktop's STEP): tap to choose from the
            // radio's list. Tap-tune, the arrows and Pencil use it.
            Hit {
                objectName: "stepHit"
                visible: info.showStep
                text: info.stepLabel(Engine.stepHz)
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                pad: Math.round((info.singleLine ? 6 : 12) * info.fontScale)
                onTapped: stepChooser.open()
            }
            Row {
                visible: info.showIdentity
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                rightPadding: info.gap
                Rectangle {
                    width: letterText.implicitWidth + 10
                    height: letterText.implicitHeight + 4
                    radius: 4
                    color: Theme.sliceTag
                    Text {
                        id: letterText
                        Accessible.ignored: info.measureOnly
                        anchors.centerIn: parent
                        text: info.activeSlice ? info.activeSlice.letter : "–"
                        font.pixelSize: info.fontPx
                        font.bold: true
                        color: "#ffffff"
                    }
                }
                Rectangle {
                    visible: !!info.activeSlice && info.activeSlice.tx
                    width: txTagText.implicitWidth + 10
                    height: txTagText.implicitHeight + 4
                    radius: 4
                    color: Theme.transmitTag
                    Text {
                        id: txTagText
                        Accessible.ignored: info.measureOnly
                        anchors.centerIn: parent
                        text: "TX"
                        font.pixelSize: info.fontPx
                        font.bold: true
                        color: "#ffffff"
                    }
                }
            }
            Hit {
                visible: (info.showCore) && (Engine.rxAntenna.length > 0)
                text: Engine.rxAntenna
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                pad: Math.round((info.singleLine ? 6 : 12) * info.fontScale)
                onTapped: rxChooser.open()
            }
            Hit {
                visible: (info.showCore) && (Engine.txAntenna.length > 0 && Engine.txAntennas.length > 0)
                text: Engine.txAntenna
                color: Theme.transmit
                fontPx: info.fontPx
                rowH: info.rowH
                pad: Math.round((info.singleLine ? 6 : 12) * info.fontScale)
                onTapped: txChooser.open()
            }
            Hit {
                visible: (info.showCore) && (Engine.filterPresets.length > 0)
                text: info.widthLabel(Engine.filterWidth)
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                pad: Math.round((info.singleLine ? 6 : 12) * info.fontScale)
                onTapped: filterChooser.open()
            }
            Hit {
                visible: info.showCore
                text: info.agcText
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                pad: Math.round((info.singleLine ? 6 : 12) * info.fontScale)
                onTapped: agcChooser.open()
            }
            // Speaker: tap for the volume (AF gain) and mute. The slice's
            // volume beside it, as a phone shows its battery charge (testers
            // missed that a new slice plays at 50); written on the icon, the
            // number was cramped and 100 did not fit.
            Item {
                visible: info.showControls
                implicitWidth: (info.singleLine ? 10 : 12) + speaker.width + 3 + volumeText.implicitWidth
                implicitHeight: info.rowH
                Canvas {
                    id: speaker
                    x: (parent.width - width - 3 - volumeText.implicitWidth) / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(info.fontPx * 1.4)
                    height: Math.round(info.fontPx * 1.2)
                    property bool muted: Engine.muted
                    onMutedChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = width, h = height
                        ctx.fillStyle = muted ? Theme.transmit : info.rxColor
                        ctx.strokeStyle = ctx.fillStyle
                        ctx.beginPath()
                        ctx.moveTo(0, h * 0.35); ctx.lineTo(w * 0.22, h * 0.35); ctx.lineTo(w * 0.5, h * 0.08)
                        ctx.lineTo(w * 0.5, h * 0.92); ctx.lineTo(w * 0.22, h * 0.65); ctx.lineTo(0, h * 0.65)
                        ctx.closePath(); ctx.fill()
                        ctx.lineWidth = Math.max(1.5, h * 0.1)
                        if (muted) {
                            ctx.beginPath(); ctx.moveTo(w * 0.62, h * 0.3); ctx.lineTo(w * 0.95, h * 0.7); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(w * 0.95, h * 0.3); ctx.lineTo(w * 0.62, h * 0.7); ctx.stroke()
                        } else {
                            ctx.beginPath(); ctx.arc(w * 0.5, h / 2, w * 0.25, -0.8, 0.8); ctx.stroke()
                            ctx.beginPath(); ctx.arc(w * 0.5, h / 2, w * 0.45, -0.8, 0.8); ctx.stroke()
                        }
                    }
                }
                Label {
                    id: volumeText
                    Accessible.ignored: info.measureOnly
                    anchors.left: speaker.right
                    anchors.leftMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(Math.max(0, Math.min(100, Engine.afGain)))
                    color: speaker.muted ? Theme.transmit : info.rxColor
                    font.pixelSize: Math.round(info.fontPx * 0.8)
                    font.bold: true
                }
                MouseArea { anchors.fill: parent; onClicked: volumePopup.open() }
            }
            // Tuning lock: tap to lock; flashes when a tune is refused.
            Item {
                visible: info.showControls
                implicitWidth: info.singleLine ? Math.max(info.iconW, lockIcon.width + 12)
                                               : Math.max(info.rowH * 0.9, lockIcon.width + 12)
                implicitHeight: info.rowH
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: Theme.warning
                    opacity: 0
                    NumberAnimation on opacity { id: lockFlashAnim; from: 0.8; to: 0; duration: 500; running: false }
                }
                Canvas {
                    id: lockIcon
                    anchors.centerIn: parent
                    width: Math.round(info.fontPx * 1.0)
                    height: Math.round(info.fontPx * 1.25)
                    property bool locked: Engine.locked
                    onLockedChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = width, h = height
                        const col = locked ? Theme.warning : info.rxColor
                        ctx.strokeStyle = col
                        ctx.fillStyle = col
                        const lw = Math.max(1.5, w * 0.13)
                        ctx.lineWidth = lw
                        const bodyTop = h * 0.48
                        if (locked) {
                            ctx.fillRect(0, bodyTop, w, h - bodyTop)
                        } else {
                            ctx.strokeRect(lw / 2, bodyTop, w - lw, h - bodyTop - lw / 2)
                        }
                        // Shackle: closed over the body, or swung open.
                        const r = w * 0.3
                        const cx = locked ? w / 2 : w * 0.72
                        ctx.beginPath()
                        ctx.moveTo(cx - r, locked ? bodyTop : h * 0.36)
                        ctx.lineTo(cx - r, h * 0.3)
                        ctx.arc(cx, h * 0.3, r, Math.PI, 0, false)
                        ctx.lineTo(cx + r, bodyTop)
                        ctx.stroke()
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: Engine.locked = !Engine.locked }
                Connections {
                    target: Engine
                    function onTuneBlocked() { lockFlashAnim.restart() }
                }
            }
            // The radio's quick record and playback (SmartSDR's REC / PLAY):
            // a dot that records the slice's receive audio (red while it
            // does), and a triangle that plays it back (lit once there is a
            // recording); played while transmitting, it goes on the air.
            Item {
                objectName: "recordHit"
                Accessible.ignored: info.measureOnly
                visible: info.showControls
                implicitWidth: info.singleLine ? Math.max(info.iconW, recIcon.width + 12) : recIcon.width + 16
                implicitHeight: info.rowH
                Accessible.role: Accessible.Button
                Accessible.name: Engine.recordOn ? qsTr("Stop recording") : qsTr("Record")
                Canvas {
                    id: recIcon
                    anchors.centerIn: parent
                    width: Math.round(info.fontPx * 0.9)
                    height: width
                    property bool on: Engine.recordOn
                    property color col: on ? Theme.transmit : info.rxColor
                    onColChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const lw = Math.max(1.5, width * 0.13)
                        ctx.beginPath()
                        ctx.arc(width / 2, height / 2, width / 2 - lw / 2, 0, Math.PI * 2)
                        if (on) {
                            ctx.fillStyle = col
                            ctx.fill()
                        } else {
                            ctx.strokeStyle = col
                            ctx.lineWidth = lw
                            ctx.stroke()
                        }
                    }
                    SequentialAnimation on opacity {
                        running: recIcon.on
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) recIcon.opacity = 1
                        NumberAnimation { to: 0.35; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: Engine.toggleRecord() }
            }
            Item {
                objectName: "playHit"
                Accessible.ignored: info.measureOnly
                visible: info.showControls
                implicitWidth: info.singleLine ? Math.max(info.iconW, playIcon.width + 12) : playIcon.width + 16
                implicitHeight: info.rowH
                Accessible.role: Accessible.Button
                Accessible.name: Engine.playOn ? qsTr("Stop playback") : qsTr("Play the recording")
                Canvas {
                    id: playIcon
                    anchors.centerIn: parent
                    width: Math.round(info.fontPx * 0.9)
                    height: width
                    property bool on: Engine.playOn
                    property color col: on ? Theme.accent : Engine.playAvailable ? info.rxColor : info.dim
                    onColChanged: requestPaint()
                    onOnChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const lw = Math.max(1.5, width * 0.13)
                        ctx.beginPath()
                        ctx.moveTo(lw, lw)
                        ctx.lineTo(width - lw / 2, height / 2)
                        ctx.lineTo(lw, height - lw)
                        ctx.closePath()
                        ctx.lineJoin = "round"
                        if (on) {
                            ctx.fillStyle = col
                            ctx.fill()
                        } else {
                            ctx.strokeStyle = col
                            ctx.lineWidth = lw
                            ctx.stroke()
                        }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: Engine.togglePlay() }
            }
        }

        // Optional: SPLIT and the DSP flags.
        Row {
            visible: info.showOptions
            spacing: 0
            // Split, as the SPLIT badge of the SmartSDR/AetherSDR slice
            // flag: tap for a TX slice beside this one, tap again to end it.
            Hit {
                objectName: "splitHit"
                visible: (info.showOptions) && (Engine.splitAvailable)
                text: "SPLIT"
                color: Engine.splitActive ? Theme.warning : info.dim
                fontPx: info.fontPx
                rowH: info.rowH
                pad: Math.round((info.singleLine ? 6 : 12) * info.fontScale)
                onTapped: Engine.toggleSplit()
                // Press and hold: split up 1, 5 or 10 kHz (the desktop's Split Up).
                onHeld: splitChooser.open()
            }
            // DSP flags: tap for the filter/DSP chooser.
            Item {
                implicitWidth: flagRow.implicitWidth + (info.singleLine ? 6 : 12)
                implicitHeight: info.rowH
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "#ffffff"
                    opacity: flagArea.pressed ? 0.15 : 0
                }
                Row {
                    id: flagRow
                    anchors.centerIn: parent
                    spacing: info.gap
                    Repeater {
                        model: info.flags
                        delegate: Text {
                            required property var modelData
                            Accessible.ignored: info.measureOnly
                            text: modelData.text
                            font.pixelSize: info.fontPx
                            font.bold: modelData.on
                            color: modelData.on ? Theme.text : info.dim
                            opacity: modelData.idle ? 0.4 : 1
                        }
                    }
                }
                MouseArea { id: flagArea; anchors.fill: parent; onClicked: filterChooser.open() }
            }
        }
    }

    // ---- choosers ----
    FilterChooser {
        id: filterChooser
        y: info.height + 4
    }
    ChooserPopup {
        id: stepChooser
        objectName: "stepChooser"
        title: qsTr("Tuning step")
        options: Engine.stepList.map(hz => info.stepLabel(hz))
        current: info.stepLabel(Engine.stepHz)
        y: info.height + 4
        onPicked: (name) => {
            const list = Engine.stepList
            for (let i = 0; i < list.length; ++i) {
                if (info.stepLabel(list[i]) === name) {
                    Engine.setStepHz(list[i])
                }
            }
        }
    }
    ChooserPopup {
        id: splitChooser
        objectName: "splitChooser"
        title: Engine.splitActive ? qsTr("Split") : qsTr("Split up")
        options: Engine.splitActive ? [qsTr("Off")] : ["+1 kHz", "+5 kHz", "+10 kHz"]
        y: info.height + 4
        onPicked: (name) => {
            if (Engine.splitActive) Engine.toggleSplit()
            else Engine.toggleSplitUp(parseInt(name.replace("+", "")))
        }
    }
    ChooserPopup {
        id: rxChooser
        objectName: "rxAntennaChooser"
        title: qsTr("RX antenna")
        options: Engine.rxAntennas
        current: Engine.rxAntenna
        y: info.height + 4
        onPicked: (name) => Engine.rxAntenna = name
    }
    ChooserPopup {
        id: txChooser
        objectName: "txAntennaChooser"
        title: qsTr("TX antenna")
        options: Engine.txAntennas
        current: Engine.txAntenna
        y: info.height + 4
        onPicked: (name) => Engine.txAntenna = name
    }
    // Volume: every slice's AF gain, mute and pan, from the speaker.
    Popup {
        id: volumePopup
        objectName: "volumePopup"
        modal: true
        dim: false
        y: info.height + 4
        // From the window, not the panel's parent: in the wrapped phone
        // header each group of the panel is a piece of its own, a few
        // icons wide (beta 45 squeezed the sliders and Mute into it).
        width: Math.min(420, Theme.windowWidth - 2 * Theme.spacing)
        margins: Theme.spacing
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
        // Every slice's audio, as the desktop's RX applet: AF gain, mute
        // and pan (left-right), the active slice marked.
        contentItem: PanelContent {
            title: qsTr("Volume")
            panel: volumePopup
            rowSpacing: Theme.spacing / 2
            // The rows follow the slice ids only: a status echo (each volume
            // change sends one) must not rebuild the rows mid-drag, or the
            // slider loses the finger and jumps to the echoed value.
            QtObject {
                id: sliceAudioIds
                property var ids: []
                function refresh() {
                    const now = Engine.slices.map(s => s.id)
                    if (JSON.stringify(now) !== JSON.stringify(ids)) {
                        ids = now
                    }
                }
                Component.onCompleted: refresh()
            }
            Connections {
                target: Engine
                function onSlicesChanged() { sliceAudioIds.refresh() }
            }
            TextMetrics {
                id: txBadgeWidth
                text: "W TX"
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
                font.bold: true
            }
            Repeater {
                model: sliceAudioIds.ids
                delegate: ColumnLayout {
                    id: sliceAudio
                    required property var modelData
                    readonly property int sliceId: modelData
                    readonly property var info: Engine.slices.find(s => s.id === sliceAudio.sliceId) || ({})
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        // One width for "A" and "C TX", so the rows' − / +
                        // line up.
                        Rectangle {
                            implicitWidth: Math.max(sliceLetter.implicitWidth, txBadgeWidth.width) + 10
                            implicitHeight: sliceLetter.implicitHeight + 4
                            radius: 4
                            color: sliceAudio.info.active ? Theme.sliceTag : Theme.surfaceRaised
                            Text {
                                id: sliceLetter
                                anchors.centerIn: parent
                                text: sliceAudio.info.letter + (sliceAudio.info.tx ? " TX" : "")
                                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
                                font.bold: true
                                color: "#ffffff"
                            }
                        }
                        // − / + for the last step (Larry Averbeck).
                        FineSlider {
                            id: gainSlider
                            Layout.fillWidth: true
                            from: 0; to: 100
                            value: sliceAudio.info.gain || 0
                            onMoved: (v) => Engine.setSliceAudio(sliceAudio.sliceId, "gain", v)
                        }
                        Label {
                            text: Math.round(gainSlider.shown)
                            color: Theme.textDim
                            font.pixelSize: Math.round(Theme.buttonFont * 0.8)
                            Layout.preferredWidth: 28
                            horizontalAlignment: Text.AlignRight
                        }
                        AppButton {
                            text: sliceAudio.info.mute ? qsTr("Unmute") : qsTr("Mute")
                            checked: sliceAudio.info.mute
                            Layout.preferredHeight: Theme.touchTarget
                            onClicked: Engine.setSliceAudio(sliceAudio.sliceId, "mute",
                                                            sliceAudio.info.mute ? 0 : 1)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "L"; color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.75) }
                        // − / + as the volume's (the user): one step left or
                        // right, held to keep going.
                        RepeatButton {
                            id: panLeft
                            text: "−"
                            Accessible.name: qsTr("Pan one step left")
                            Layout.preferredWidth: Theme.touchTarget
                            Layout.preferredHeight: Theme.touchTarget
                            onStepped: {
                                const v = Math.max(0, Math.round(panSlider.value) - 1)
                                panSlider.value = v
                                Engine.setSliceAudio(sliceAudio.sliceId, "pan", v)
                            }
                        }
                        Slider {
                            id: panSlider
                            Layout.fillWidth: true
                            from: 0; to: 100; stepSize: 1
                            focusPolicy: Qt.NoFocus
                            // Within 3 of the middle it snaps to centre, so
                            // centre is easy to find by feel.
                            onMoved: {
                                if (Math.abs(value - 50) <= 3 && value !== 50) {
                                    value = 50
                                }
                                Engine.setSliceAudio(sliceAudio.sliceId, "pan", value)
                            }
                            // Double-tap centres.
                            TapHandler {
                                onDoubleTapped: Engine.setSliceAudio(sliceAudio.sliceId, "pan", 50)
                            }
                            Binding on value {
                                when: !panSlider.pressed && !panLeft.pressed && !panRight.pressed
                                value: sliceAudio.info.pan
                                restoreMode: Binding.RestoreNone
                            }
                            // Centre mark, above and below the track.
                            Rectangle {
                                x: panSlider.leftPadding + panSlider.availableWidth / 2 - width / 2
                                y: panSlider.topPadding + panSlider.availableHeight / 2 - height / 2
                                width: 2
                                height: Math.round(panSlider.availableHeight * 0.9)
                                color: Math.round(panSlider.value) === 50 ? Theme.accent : Theme.textDim
                                z: -1
                            }
                        }
                        RepeatButton {
                            id: panRight
                            text: "+"
                            Accessible.name: qsTr("Pan one step right")
                            Layout.preferredWidth: Theme.touchTarget
                            Layout.preferredHeight: Theme.touchTarget
                            onStepped: {
                                const v = Math.min(100, Math.round(panSlider.value) + 1)
                                panSlider.value = v
                                Engine.setSliceAudio(sliceAudio.sliceId, "pan", v)
                            }
                        }
                        Label { text: "R"; color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.75) }
                        // The balance as read: C at centre, else L/R and how far.
                        Label {
                            readonly property int v: Math.round(panSlider.value)
                            text: v === 50 ? qsTr("C") : (v < 50 ? qsTr("L %1").arg(50 - v) : qsTr("R %1").arg(v - 50))
                            color: v === 50 ? Theme.accent : Theme.text
                            font.pixelSize: Math.round(Theme.buttonFont * 0.8)
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: Math.round(Theme.buttonFont * 2.6)
                        }
                    }
                }
            }
        }
    }
    AgcChooser {
        id: agcChooser
        y: info.height + 4
    }
}
