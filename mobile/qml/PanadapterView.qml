import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Panadapter and waterfall of the active pan with the slice marker, filter
// passband and all tuning gestures (tap, drag, Pencil, pinch, wheel). Shared
// by the tablet and phone layouts.
Rectangle {
    id: panArea
    // Width of the right meter above (from the page): the RF GAIN label
    // matches it; 0 sizes the label to its text.
    property real rfLabelWidth: 0
    // Items over the display (from the page: the controls drawer). Qt passes
    // a drag inside a popup on to the drag handlers under it (taps it
    // blocks), so a sideways swipe in the open drawer tuned the radio (a
    // tester's report). The display and each drag area below leave the
    // covered part out of their area; the part still in view works as usual.
    property var covers: []
    function uncovered(item, point) {
        for (const c of covers) {
            if (c && c.visible && c.contains(item.mapToItem(c, point.x, point.y)))
                return false
        }
        return true
    }
    // An item's own rectangle less the covers, as its containmentMask.
    component CoverMask: QtObject {
        required property Item owner
        function contains(point: point): bool {
            return point.x >= 0 && point.y >= 0 && point.x < owner.width && point.y < owner.height
                   && panArea.uncovered(owner, point)
        }
    }
    containmentMask: CoverMask { owner: panArea }
    color: Theme.surface
    radius: Theme.radius
    clip: true

    SpectrumItem {
        id: spectrum
        anchors.fill: parent
        engine: Engine
        // The 3D surface needs height; the desktop gives it about half. It
        // stretches with its area, so it keeps iPad landscape's shape (height
        // 0.158 of the width) in every orientation and the waterfall takes
        // the rest; never lower than two touch targets (phones in portrait).
        // A split dragged at the divider (Engine.spectrumSplit) replaces it,
        // within limits that keep the band plan and the waterfall's buttons.
        readonly property real autoFraction: Engine.spectrum3D
            ? Math.min(0.5, (Math.max(width * 0.158, Theme.touchTarget * 2) + bandPlanHeight)
                            / Math.max(1, height))
            : 0.4
        readonly property real minFraction: Math.min(0.5, (bandPlanHeight + 40) / Math.max(1, height))
        readonly property real maxFraction: Math.max(minFraction, 1 - (Theme.touchTarget + 16) / Math.max(1, height))
        spectrumFraction: Math.max(minFraction, Math.min(maxFraction,
            Engine.spectrumSplit > 0 ? Engine.spectrumSplit / 100 : autoFraction))
    }

    // 3D stacked trace on the GPU in place of the trace (the waterfall stays
    // below), as the desktop's 3DSS. As there, its levels are automatic
    // whatever the 2D trace's mode: the floor (its zero) follows the noise,
    // 3D Floor dB under it, so the noise reads dark blue.
    Spectrum3DItem {
        id: surface3D
        readonly property real floor: spectrum.noiseFloorDbm - Engine.spectrum3DFloorDepth
        // Fixed height span, the spectrum's Range (the desktop's dynamic
        // range, 10-120 dB): ridges do not rescale as stations come and go.
        readonly property real span: Math.max(10, Math.min(120, Engine.traceRangeDb))
        visible: Engine.spectrum3D
        engine: Engine
        x: 0
        y: 0
        width: parent.width
        // Ends above the band plan strip, which stays visible under it.
        height: Math.round(parent.height * spectrum.spectrumFraction - spectrum.bandPlanHeight)
        floorDbm: floor
        rangeDb: span
        colorGain: Engine.spectrum3DGain
    }

    // FT8 / FT4: the transmit tone's band in red, dragged (1 Hz, no steps)
    // or set by a tap to place the TX audio frequency by eye.
    Item {
        id: digiOverlay
        anchors.fill: parent
        visible: panArea.digiPick && Engine.panBandwidthMhz > 0
        readonly property real pxPerMhz: width / Math.max(1e-6, Engine.panBandwidthMhz)
        readonly property real hzSign: Engine.digi.sliceMode === "DIGL" ? -1 : 1
        function xForHz(hz) {
            const rf = Engine.digi.sliceMhz + hzSign * hz / 1e6
            return (rf - Engine.panCenterMhz) * pxPerMhz + width / 2
        }
        readonly property real markTop: Engine.spectrum3D ? height * spectrum.spectrumFraction : 0
        Rectangle {
            objectName: "digiTxMarker"
            readonly property real hzWidth: Engine.digiProtocol === "FT4" ? 90 : 50
            x: Math.min(digiOverlay.xForHz(Engine.digiTxHz), digiOverlay.xForHz(Engine.digiTxHz + hzWidth))
            width: Math.max(2, hzWidth / 1e6 * digiOverlay.pxPerMhz)
            y: digiOverlay.markTop
            height: digiOverlay.height - digiOverlay.markTop
            color: Qt.rgba(0.9, 0.28, 0.3, 0.45)
            border.color: "#ff5a5f"
            border.width: 2
            // A finger-wide grab around the band: drag it to any Hz.
            Item {
                id: txGrab
                objectName: "digiTxGrab"
                containmentMask: CoverMask { owner: txGrab }
                x: parent.width / 2 - width / 2
                width: Math.max(44, parent.width)
                height: parent.height
                DragHandler {
                    id: txDrag
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                    property int startHz: 0
                    onActiveChanged: if (active) startHz = Engine.digiTxHz
                    onTranslationChanged: {
                        if (!active) return
                        const dHz = digiOverlay.hzSign * translation.x / digiOverlay.pxPerMhz * 1e6
                        Engine.digiTxHz = Math.round(startHz + dHz)
                    }
                }
            }
            // Beside the band, on the side with room (clear of the RF GAIN
            // badge at the right edge), on a solid ground so it reads over
            // the waterfall's colours.
            Rectangle {
                x: parent.x + parent.width + width + 90 > digiOverlay.width ? -width - 3 : parent.width + 3
                y: 58  // under the state banner
                width: txLabel.implicitWidth + 10
                height: txLabel.implicitHeight + 4
                radius: 4
                color: "#d7262d"
                border.color: "#ffffff"
                border.width: 1
                Label {
                    id: txLabel
                    anchors.centerIn: parent
                    text: qsTr("TX %1").arg(Engine.digiTxHz)
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.bold: true
                }
            }
        }
        // Where the other station is heard (WSJT-X's green RX mark), always
        // shown: the station in or after the QSO, else our own TX frequency.
        Rectangle {
            objectName: "digiRxMarker"
            readonly property int rxHz: Engine.digi.rxHz || 0
            readonly property real hzWidth: Engine.digiProtocol === "FT4" ? 90 : 50
            visible: rxHz > 0
            x: Math.min(digiOverlay.xForHz(rxHz), digiOverlay.xForHz(rxHz + hzWidth))
            width: Math.max(2, hzWidth / 1e6 * digiOverlay.pxPerMhz)
            y: digiOverlay.markTop
            height: digiOverlay.height - digiOverlay.markTop
            color: Qt.rgba(0.2, 0.85, 0.35, 0.25)
            border.color: "#3ddc5a"
            border.width: 2
            Rectangle {
                x: parent.x + parent.width + width + 90 > digiOverlay.width ? -width - 3 : parent.width + 3
                y: 82
                width: rxLabel.implicitWidth + 10
                height: rxLabel.implicitHeight + 4
                radius: 4
                color: "#2fb84a"
                border.color: "#ffffff"
                border.width: 1
                Label {
                    id: rxLabel
                    anchors.centerIn: parent
                    text: qsTr("RX %1").arg(parent.parent.rxHz)
                    color: "#0b1a0e"
                    font.pixelSize: 12
                    font.bold: true
                }
            }
        }
    }

    // Filter passband between the slice's filter edges (Hz offsets from the
    // carrier, so it sits on the correct side for USB/LSB and centred for
    // CW/AM): a fill over the spectrum and bright edge lines through the
    // waterfall, where a fill would vanish into the colours.
    Item {
        id: passband
        visible: Engine.panBandwidthMhz > 0 && Engine.filterHigh > Engine.filterLow && !panArea.hideSlices
        readonly property real pxPerMhz: parent.width / Engine.panBandwidthMhz
        x: (Engine.frequencyMhz + Engine.filterLow / 1e6 - Engine.panCenterMhz) * pxPerMhz
           + parent.width / 2
        width: Math.max(2, (Engine.filterHigh - Engine.filterLow) / 1e6 * pxPerMhz)
        height: parent.height

        // In 3D the surface itself darkens the passband in perspective (as
        // the desktop's slice shadow), so the flat fill and the edges over
        // the 3D view go; the edges stay through the waterfall.
        readonly property real edgeTop: Engine.spectrum3D ? panArea.height * spectrum.spectrumFraction : 0
        // The fill goes on the FT8 / FT4 slice (its red TX band shows the
        // place); the edges stay.
        Rectangle {
            visible: !Engine.spectrum3D && !(panArea.digiMode && Engine.digi.sliceActive)
            width: parent.width
            height: parent.height * spectrum.spectrumFraction
            color: Theme.passband
            opacity: 0.22
        }
        Rectangle {
            x: 0
            y: parent.edgeTop
            width: 1.5
            height: parent.height - y
            color: Theme.passband
            opacity: 0.9
        }
        Rectangle {
            x: parent.width - width
            y: parent.edgeTop
            width: 1.5
            height: parent.height - y
            color: Theme.passband
            opacity: 0.9
        }

        // Drag an edge to set the filter (10 Hz steps), as on the desktop; a
        // finger's width around each edge. Hidden when the passband is too
        // narrow to leave room for the tuning drag between them.
        component EdgeHandle: Item {
            id: edge
            property bool high: false
            readonly property bool active: edgeDrag.active
            visible: passband.width >= 60
            containmentMask: CoverMask { owner: edge }
            width: 28
            height: passband.height
            x: (high ? passband.width : 0) - width / 2
            z: 3
            property int startLow: 0
            property int startHigh: 0
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: passband.edgeTop
                width: edge.active ? 4 : 0
                height: parent.height - y
                color: Theme.passband
            }
            DragHandler {
                id: edgeDrag
                target: null
                xAxis.enabled: true
                yAxis.enabled: false
                onActiveChanged: {
                    if (active) {
                        edge.startLow = Engine.filterLow
                        edge.startHigh = Engine.filterHigh
                    }
                }
                onTranslationChanged: {
                    if (!active) return
                    const hz = Math.round(translation.x / passband.pxPerMhz * 1e6 / 10) * 10
                    if (edge.high) Engine.setFilterEdges(edge.startLow, edge.startHigh + hz)
                    else Engine.setFilterEdges(edge.startLow + hz, edge.startHigh)
                }
            }
        }
        EdgeHandle { high: false }
        EdgeHandle { high: true }
    }

    // The other slices on this pan: dim markers with their letter; a tap
    // near one selects it.
    readonly property string activePanId: {
        const list = Engine.slices
        for (let i = 0; i < list.length; ++i) {
            if (list[i].active) return list[i].panId
        }
        return ""
    }
    Repeater {
        model: Engine.slices
        delegate: Item {
            required property var modelData
            visible: !modelData.active && modelData.panId === panArea.activePanId && Engine.panBandwidthMhz > 0
                     && !panArea.hideSlices
            x: (modelData.frequency - Engine.panCenterMhz) / Engine.panBandwidthMhz * panArea.width
               + panArea.width / 2 - width / 2
            width: 36
            height: panArea.height
            z: 2
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 2
                y: Engine.spectrum3D ? panArea.height * spectrum.spectrumFraction : 0
                height: parent.height - y
                color: Theme.textDim
                opacity: 0.8
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 4
                text: modelData.letter + (modelData.tx ? " TX" : "")
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
                background: Rectangle { color: Theme.surfaceRaised; radius: 4; opacity: 0.9 }
                padding: 3
            }
            TapHandler { onTapped: Engine.selectSlice(modelData.id) }
        }
    }

    // Tracking notch filters (TNF), as the desktop draws them: a band of the
    // notch's width through spectrum and waterfall (temporary yellow,
    // permanent green, dimmer while TNF is off) with a handle on top, deeper
    // notches a bigger one, outlined so it shows over the waterfall. A tap opens its settings; holding a finger on
    // the display adds one there.
    // Screenshots without a notch on the radio: a sample one, drawn here
    // only (tnfPopup.preview()); nothing is sent.
    property var previewTnfs: []
    readonly property var shownTnfs: Engine.tnfs.concat(previewTnfs)
    Repeater {
        model: Engine.panBandwidthMhz > 0 ? panArea.shownTnfs : []
        delegate: Item {
            id: notch
            required property var modelData
            readonly property real pxPerMhz: panArea.width / Engine.panBandwidthMhz
            readonly property real cx: (modelData.freq - Engine.panCenterMhz) * pxPerMhz + panArea.width / 2
            readonly property real bandWidth: Math.max(3, modelData.width / 1e6 * pxPerMhz)
            readonly property color tint: modelData.permanent ? "#3fb950" : Theme.warning
            readonly property real bandTop: Engine.spectrum3D ? panArea.height * spectrum.spectrumFraction : 0
            visible: cx + bandWidth / 2 >= 0 && cx - bandWidth / 2 <= panArea.width
            // A finger's width to tap, however narrow the notch.
            width: Math.max(bandWidth, 32)
            height: panArea.height
            x: cx - width / 2
            z: 2
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: notch.bandTop
                width: notch.bandWidth
                height: parent.height - y
                // The edges at full strength, so the band reads over a busy
                // waterfall; the fill stays light.
                color: Qt.alpha(notch.tint, Engine.tnfEnabled ? 0.22 : 0.06)
                border.color: Qt.alpha(notch.tint, Engine.tnfEnabled ? 1 : 0.6)
                border.width: Engine.tnfEnabled ? 2 : 1
            }
            // The handle: a triangle outlined in black so it stands out from
            // the waterfall's colours. On: filled with the notch's colour.
            // TNF off: hollow, the colour as its outline only.
            Canvas {
                anchors.horizontalCenter: parent.horizontalCenter
                y: notch.bandTop
                width: 26
                height: 18 + notch.modelData.depth * 4
                readonly property bool on: Engine.tnfEnabled
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const m = 2
                    ctx.beginPath()
                    ctx.moveTo(m, m)
                    ctx.lineTo(width - m, m)
                    ctx.lineTo(width / 2, height - m)
                    ctx.closePath()
                    ctx.lineJoin = "round"
                    ctx.lineWidth = 3
                    ctx.strokeStyle = "#000000"
                    ctx.stroke()
                    ctx.fillStyle = on ? notch.tint : "#0d1117"
                    ctx.fill()
                    if (!on) {
                        ctx.lineWidth = 2
                        ctx.strokeStyle = notch.tint
                        ctx.stroke()
                    }
                }
                onOnChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: notch
                    function onTintChanged() { parent.requestPaint() }
                }
            }
            TapHandler {
                onTapped: {
                    tnfPopup.tnfId = notch.modelData.id
                    tnfPopup.anchorX = notch.cx
                    tnfPopup.open()
                }
            }
            // Drag it along the spectrum (the handle's part; on the waterfall a
            // drag still moves the view), in 10 Hz steps.
            Item {
                id: notchGrab
                containmentMask: CoverMask { owner: notchGrab }
                width: parent.width
                height: panArea.height * spectrum.spectrumFraction
                property real startMhz: 0
                DragHandler {
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    enabled: notch.modelData.id >= 0
                    onActiveChanged: if (active) parent.startMhz = notch.modelData.freq
                    onTranslationChanged: {
                        if (!active) return
                        const hz = Math.round(translation.x / notch.pxPerMhz * 1e6 / 10) * 10
                        Engine.setTnf(notch.modelData.id, "freq", parent.startMhz + hz / 1e6)
                    }
                }
            }
        }
    }

    // Spots the radio holds (the DX cluster's and other programs'):
    // callsigns along the top of the spectrum at their frequency, in up to
    // four rows so they do not overlap (the rest wait for a zoom), clear of
    // the RF GAIN badge; a tap tunes to the spot.
    readonly property var spotLayout: {
        const out = []
        const bw = Engine.panBandwidthMhz
        const list = Engine.spots
        if (!(bw > 0) || list.length === 0) return out
        const rows = Math.max(1, Math.min(4, Math.floor((panArea.height * spectrum.spectrumFraction - 30) / 24)))
        const rowEnd = []
        for (let r = 0; r < rows; ++r) rowEnd.push(-1e9)
        const items = []
        for (let i = 0; i < list.length; ++i) {
            const x = (list[i].freq - Engine.panCenterMhz) / bw * panArea.width + panArea.width / 2
            if (x >= 0 && x <= panArea.width)
                items.push({ spot: list[i], x: x, w: list[i].call.length * 7.5 + 12 })
        }
        items.sort((a, b) => a.x - b.x)
        // The RF GAIN badge at the top right: rows it covers end before it.
        const badgeLeft = rfLabel.visible ? rfLabel.x - 3 : 1e9
        const badgeBottom = rfLabel.y + rfLabel.height
        for (let i = 0; i < items.length && out.length < 80; ++i) {
            const it = items[i]
            for (let r = 0; r < rows; ++r) {
                if (it.x + it.w / 2 > badgeLeft && 4 + r * 24 < badgeBottom) continue
                if (it.x - it.w / 2 > rowEnd[r] + 3) {
                    rowEnd[r] = it.x + it.w / 2
                    out.push({ spot: it.spot, x: it.x, w: it.w, row: r })
                    break
                }
            }
        }
        return out
    }
    // A spot's own colour, or the cluster tan when it has none (a spot from
    // another program can come with a transparent one).
    function spotColor(c) {
        return !c || c.length < 7 || c.toUpperCase().startsWith("#00") ? "#D2B48C" : c
    }
    Repeater {
        model: panArea.spotLayout
        delegate: Item {
            id: spotTag
            required property var modelData
            x: modelData.x - modelData.w / 2
            y: 4 + modelData.row * 24
            width: modelData.w
            height: 20
            z: 2
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: "#d90f1419"
                border.color: panArea.spotColor(spotTag.modelData.spot.color)
                border.width: 1
            }
            Text {
                anchors.centerIn: parent
                text: spotTag.modelData.spot.call
                color: panArea.spotColor(spotTag.modelData.spot.color)
                font.pixelSize: 11
                font.bold: true
            }
            // Points at the spot's frequency.
            Rectangle {
                x: parent.width / 2 - 0.5
                y: parent.height
                width: 1
                height: 5
                color: panArea.spotColor(spotTag.modelData.spot.color)
            }
            TapHandler { onTapped: Engine.tuneToSpot(spotTag.modelData.spot.index) }
        }
    }

    // Latest decoded CW along the top of the waterfall (just under its
    // frequency scale, which stays readable), where no layout's bottom bar
    // can cover it. Two lines that fill
    // as writing does: the top line first, then the bottom one; when that is
    // full, it moves up and a new bottom line starts. Lines break between
    // words (a word longer than the line is cut), as the text comes in, so
    // a line once written stays as it is; the monospace font's width gives
    // the line's length (the decoder's line breaks stacked a column of text
    // over the spectrum, eliding works on single lines only).
    Rectangle {
        visible: Engine.cwDecoderEnabled && Engine.cwMode && Engine.cwDecodedText.length > 0
        // In a low panadapter (a phone in landscape) it would lie under the
        // zoom / Band and + Slice / + Pan buttons: then between them.
        readonly property bool besideButtons: y + height > zoomRow.y - 2
        x: besideButtons ? zoomRow.x + zoomRow.width + 4 : 0
        width: (besideButtons ? addRow.x - 4 : parent.width) - x
        y: parent.height * spectrum.spectrumFraction + spectrum.axisHeight
        // Room for two lines from the first letter, so the bar keeps its height.
        height: 2 * cwFont.height + 8
        color: "#cc000000"
        z: 3
        clip: true
        TextMetrics {
            id: cwChar
            font: cwLine.font
            text: "M"
        }
        FontMetrics {
            id: cwFont
            font: cwLine.font
        }
        Label {
            id: cwLine
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 4
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            readonly property int columns: Math.max(8, Math.floor(width / Math.max(1, cwChar.advanceWidth)))
            // The line above (full) and the line being written; seen is the
            // stream place up to which the strip has been read.
            property string upper: ""
            property string current: ""
            property real seen: 0
            text: upper.length > 0 ? upper + "\n" + current : current
            function feed(chars) {
                for (const c of chars) {
                    if (c === " " && current.length === 0)
                        continue
                    current += c
                    if (current.length > columns) {
                        const cut = current.lastIndexOf(" ")
                        upper = cut > 0 ? current.slice(0, cut) : current.slice(0, columns)
                        current = cut > 0 ? current.slice(cut + 1) : current.slice(columns)
                    }
                }
            }
            // From scratch: the tail of the strip, from a word's start.
            function rewrap() {
                upper = ""
                current = ""
                const strip = Engine.cwStrip
                let tail = strip.slice(-3 * columns)
                if (tail.length < strip.length && tail.indexOf(" ") >= 0)
                    tail = tail.slice(tail.indexOf(" ") + 1)
                feed(tail)
                seen = Engine.cwStripStart + strip.length
            }
            function follow() {
                const start = Engine.cwStripStart
                const end = start + Engine.cwStrip.length
                // Cleared, or the away mark taken back: start over.
                if (end < seen || seen < start || Engine.cwStrip.length === 0) {
                    rewrap()
                    return
                }
                feed(Engine.cwStrip.slice(seen - start))
                seen = end
            }
            onColumnsChanged: rewrap()
            Component.onCompleted: rewrap()
            Connections {
                target: Engine
                function onCwDecoderChanged() { cwLine.follow() }
            }
            wrapMode: Text.NoWrap
            maximumLineCount: 2
            clip: true
            color: "white"
            font.family: "monospace"
            font.pixelSize: 15
            verticalAlignment: Text.AlignTop
        }
    }

    // Slice marker: where the active slice sits inside the pan (in 3D through
    // the waterfall only; the surface draws its own cue in perspective). A
    // white dashed line with the frequency on top, apart from the filter's
    // solid yellow edges (an orange line beside them read as a third edge).
    Item {
        id: sliceMarker
        // Not in voice modes: the passband's edge is the carrier there (the
        // user: the line is not needed in phone); the frequency stays.
        readonly property bool voice: ["USB", "LSB", "AM", "SAM", "DSB", "FM", "NFM", "DFM"].indexOf(Engine.mode) >= 0
        readonly property bool placed: Engine.panBandwidthMhz > 0 && !panArea.hideSlices
        visible: placed && !voice
        z: 2
        readonly property real lineX: (Engine.frequencyMhz - Engine.panCenterMhz) / Engine.panBandwidthMhz * parent.width
                                      + parent.width / 2
        x: lineX - 1
        width: 2
        y: Engine.spectrum3D ? parent.height * spectrum.spectrumFraction : 0
        height: parent.height - y
        // Painted when its height changes, not as it moves.
        Canvas {
            id: dashes
            anchors.fill: parent
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#ffffff"
                for (let y = 0; y < height; y += 10) {
                    ctx.fillRect(0, y, width, Math.min(6, height - y))
                }
            }
        }
    }
    // The frequency beside the line, at the foot of the spectrum (clear of
    // the spots along its top), on the side away from the passband; in 3D at
    // the top of the waterfall.
    Label {
        id: sliceFlag
        visible: sliceMarker.placed && sliceMarker.lineX >= 0 && sliceMarker.lineX <= panArea.width
        z: 2
        readonly property bool passbandLeft: Engine.filterHigh <= 0
        x: Math.max(scaleStrip.width, Math.min(panArea.width - width,
               passbandLeft ? sliceMarker.lineX + 4 : sliceMarker.lineX - width - 4))
        y: Engine.spectrum3D ? sliceMarker.y + 20
                             : panArea.height * spectrum.spectrumFraction - spectrum.bandPlanHeight - height - 4
        text: Engine.frequencyMhz.toFixed(3)
        color: "#ffffff"
        font.pixelSize: 12
        font.bold: true
        padding: 2
        leftPadding: 5
        rightPadding: 5
        background: Rectangle { color: "#0b1118"; opacity: 0.85; radius: 4; border.color: "#ffffff"; border.width: 1 }
    }

    // dBm scale on the left of the spectrum (the reference level control of
    // SmartSDR/AetherSDR): drag it up or down to move the trace. A drag
    // switches the trace to manual; Auto in the View tab gives it back. In 3D
    // it is the desktop's 3D dBm scale: floor to top, and a drag sets 3D Floor
    // (how far under the noise the zero sits, 0-24 dB) instead.
    Item {
        id: scaleStrip
        z: 4
        width: 40
        height: parent.height * spectrum.spectrumFraction
                - (Engine.spectrum3D ? spectrum.bandPlanHeight : 0)
        readonly property real topDbm: Engine.spectrum3D ? surface3D.floor + surface3D.span
                                                         : spectrum.traceTopDbm
        readonly property real bottomDbm: Engine.spectrum3D ? surface3D.floor : spectrum.traceBottomDbm
        readonly property real range: Math.max(1, topDbm - bottomDbm)
        // The smallest round step that keeps labels at least 18 px apart.
        readonly property int step: {
            for (const st of [10, 20, 30, 50, 100]) {
                if (st / range * height >= 18) return st
            }
            return 100
        }
        Rectangle { anchors.fill: parent; color: Theme.surface; opacity: scaleDrag.pressed ? 0.85 : 0.55 }
        Repeater {
            // Bounded: a non-finite scale (no data yet) must never spin here.
            model: {
                const ticks = []
                const top = scaleStrip.topDbm
                const bottom = scaleStrip.bottomDbm
                if (!isFinite(top) || !isFinite(bottom) || top <= bottom) return ticks
                for (let d = Math.ceil(bottom / scaleStrip.step) * scaleStrip.step;
                     d <= top && ticks.length < 30; d += scaleStrip.step) ticks.push(d)
                return ticks
            }
            delegate: Label {
                required property int modelData
                x: 3
                y: (scaleStrip.topDbm - modelData) / scaleStrip.range * scaleStrip.height - height / 2
                visible: y > 0 && y < scaleStrip.height - height
                text: modelData
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
        MouseArea {
            id: scaleDrag
            anchors.fill: parent
            preventStealing: true
            property real startTop: 0
            property real startY: 0
            property real span: 90
            property int startDepth: 0
            onPressed: (mouse) => {
                startY = mouse.y
                if (Engine.spectrum3D) {
                    startDepth = Engine.spectrum3DFloorDepth
                    return
                }
                startTop = scaleStrip.topDbm
                span = scaleStrip.range
                if (Engine.traceAutoRange) {
                    Engine.traceRangeDb = Math.round(span)
                    Engine.traceRefDbm = Math.round(startTop)
                    Engine.traceAutoRange = false
                }
            }
            // The trace follows the finger: dragging down raises the top level.
            // In 3D the surface follows it: up lowers the floor (24 dB over the
            // strip's height, as the desktop) and the noise rises.
            onPositionChanged: (mouse) => {
                if (Engine.spectrum3D) {
                    Engine.spectrum3DFloorDepth = Math.round(startDepth + (startY - mouse.y) / height * 24)
                    return
                }
                Engine.traceRefDbm = Math.round(startTop + (mouse.y - startY) / height * span)
            }
        }
    }


    // Divider between the spectrum and the waterfall, as the desktop's: drag
    // it up or down to give either more room; a double tap goes back to the
    // automatic split. The new split applies when the finger lifts (resizing
    // restarts the waterfall's history); a line shows where it will be.
    Item {
        id: divider
        z: 6
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: scaleStrip.width   // the dBm scale keeps its own drag
        height: Math.round(Theme.touchTarget * 0.5)
        y: Math.round(parent.height * spectrum.spectrumFraction - height / 2)
        // The grip sits off centre, clear of the slice and its passband.
        Rectangle {
            id: dividerGrip
            anchors.verticalCenter: parent.verticalCenter
            x: Math.round(parent.width * 0.75 - width / 2)
            width: 44
            height: 4
            radius: 2
            color: Theme.text
            opacity: dividerDrag.pressed ? 0.9 : 0.4
        }
        MouseArea {
            id: dividerDrag
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeVerCursor
            property real grabOffset: 0
            property real target: -1   // fraction under the finger while dragging
            onPressed: (mouse) => {
                grabOffset = mouse.y - divider.height / 2
                target = spectrum.spectrumFraction
            }
            onPositionChanged: (mouse) => {
                const y = divider.mapToItem(panArea, 0, mouse.y - grabOffset).y
                target = Math.max(spectrum.minFraction, Math.min(spectrum.maxFraction, y / Math.max(1, panArea.height)))
            }
            onReleased: {
                if (target > 0 && Math.abs(target - spectrum.spectrumFraction) * panArea.height >= 4) {
                    Engine.spectrumSplit = Math.round(target * 100)
                }
                target = -1
            }
            onCanceled: target = -1
            onDoubleClicked: Engine.spectrumSplit = 0
        }
    }
    // Where the divider goes, while it is dragged.
    Rectangle {
        visible: dividerDrag.pressed && dividerDrag.target > 0
        z: 6
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        y: Math.round(dividerDrag.target * parent.height) - 1
        color: Theme.accent
    }

    // Zoom out / in, bottom left of the waterfall as in AetherSDR. Together
    // the size of one step button below (eight across the same width), each
    // half of it.
    Row {
        id: zoomRow
        readonly property real stepWidth: (panArea.width - 7 * Theme.spacing / 2) / 8
        z: 5
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        spacing: 2
        Repeater {
            model: [{ label: "−", factor: 2.0 }, { label: "+", factor: 0.5 }]
            delegate: AppButton {
                required property var modelData
                text: modelData.label
                font.pixelSize: Math.round(Theme.buttonFont * 1.3)
                width: Math.max(32, zoomRow.stepWidth / 2 - 1)
                height: Theme.touchTarget
                padding: 0
                opacity: 0.85
                focusPolicy: Qt.NoFocus
                onClicked: Engine.zoom(modelData.factor)
            }
        }
        // Band zoom (the desktop's): the radio fits the pan to the whole
        // band; tap again to leave it. Where the page has no band row (very
        // low landscape) it opens the bands, with the band zoom there too.
        AppButton {
            id: bandButton
            objectName: "bandZoomButton"
            text: qsTr("Band")
            checked: !!Engine.panDisplay.bandZoom
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            height: Theme.touchTarget
            leftPadding: 8
            rightPadding: 8
            opacity: 0.85
            onClicked: panArea.bandPicker ? bandChooser.open() : Engine.setPanDisplay("bandZoom", checked ? 0 : 1)
        }
        // B/S (bands / steps, Larry Averbeck's name; F/R before): the band row
        // and the step buttons, or hidden so the waterfall has their height;
        // Band then opens the bands. On a page that has those rows.
        AppButton {
            objectName: "tuneRowsButton"
            visible: panArea.tuneRowsToggle
            text: qsTr("B/S")
            Accessible.name: qsTr("Band row and step buttons")
            checked: Engine.tuneRowsShown
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            height: Theme.touchTarget
            leftPadding: 8
            rightPadding: 8
            opacity: 0.85
            focusPolicy: Qt.NoFocus
            onClicked: Engine.tuneRowsShown = !Engine.tuneRowsShown
        }
    }

    // Set by a page that leaves its band row out (no band could be picked
    // there before).
    property bool bandPicker: false
    // Set by a page whose band row and step buttons B/S hides.
    property bool tuneRowsToggle: false
    Popup {
        id: bandChooser
        objectName: "bandChooser"
        modal: true
        dim: false
        x: 6
        y: Math.max(0, panArea.height - height - Theme.touchTarget - 16)
        margins: Theme.spacing
        padding: Theme.spacing
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
        // Whole band in the title row, beside Close (a tap outside closes it
        // too; Larry Averbeck).
        contentItem: PanelContent {
            title: qsTr("Band")
            panel: bandChooser
            rowSpacing: 4
            extras: AppButton {
                text: qsTr("Whole band")
                checked: !!Engine.panDisplay.bandZoom
                focusPolicy: Qt.NoFocus
                leftPadding: 12
                rightPadding: 12
                Layout.preferredHeight: Theme.touchTarget
                onClicked: {
                    Engine.setPanDisplay("bandZoom", checked ? 0 : 1)
                    bandChooser.close()
                }
            }
            GridLayout {
                columns: Math.min(8, Math.max(4, Math.floor((panArea.width - 40) / (Theme.buttonWidth + 4))))
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: Engine.bands
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        checked: Engine.activeBand === modelData
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Theme.buttonWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            Engine.selectBand(modelData)
                            bandChooser.close()
                        }
                    }
                }
            }
        }
    }

    // RF gain at the top right of the spectrum, under the right meter, as
    // the desktop shows the preamp/attenuator; tap for the AGC / AGC-T /
    // RF gain chooser (the same as the info panel's AGC).
    Rectangle {
        id: rfLabel
        objectName: "rfGainLabel"
        z: 5
        visible: Engine.connectionState === "connected"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 6
        // Under the meter, edge to edge with it.
        anchors.rightMargin: panArea.rfLabelWidth > 0 ? 0 : 6
        // Half the right meter above it (the page passes its width).
        width: panArea.rfLabelWidth > 0 ? Math.max(rfText.implicitWidth * 0.6 + 12, panArea.rfLabelWidth * 0.5) : rfText.implicitWidth + 16
        height: Math.max(rfText.implicitHeight + 8, Math.round(Theme.touchTarget * 0.7))
        radius: 4
        color: rfArea.pressed ? Theme.surfaceRaised : Theme.surface
        opacity: 0.9
        Text {
            id: rfText
            anchors.centerIn: parent
            width: parent.width - 10
            horizontalAlignment: Text.AlignHCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 9
            text: qsTr("RF GAIN %1%2 dB").arg(Engine.rfGain > 0 ? "+" : "").arg(Engine.rfGain)
            color: Engine.rfGain > 0 ? "#3fb6ff" : Engine.rfGain < 0 ? Theme.warning : Theme.text
            font.pixelSize: Theme.buttonFont
            font.bold: true
        }
        MouseArea {
            id: rfArea
            anchors.fill: parent
            anchors.margins: -6
            onClicked: rfChooser.open()
        }
        AgcChooser {
            id: rfChooser
            objectName: "rfGainChooser"
            width: Math.min(420, panArea.width - 24)
            x: rfLabel.width - width
            y: rfLabel.height + 4
        }
    }

    // "+ Slice" / "+ Pan", bottom right of the waterfall, the size of the
    // zoom buttons (wider only where the label needs it, iPhone).
    Row {
        id: addRow
        z: 5
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        spacing: 2
        readonly property real cellWidth: Math.max(32, zoomRow.stepWidth / 2 - 1)
        // The app shows one panadapter; any others still stream to the device
        // (bandwidth, packet loss) and hold the radio's panadapter slots.
        AppButton {
            visible: Engine.panCount > 1
            text: qsTr("Close %1 other pans").arg(Engine.panCount - 1)
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            leftPadding: 6
            rightPadding: 6
            height: Theme.touchTarget
            opacity: 0.85
            focusPolicy: Qt.NoFocus
            onClicked: Engine.closeOtherPanadapters()
        }
        Repeater {
            model: [{ label: qsTr("+ Slice"), pan: false }, { label: qsTr("+ Pan"), pan: true }]
            delegate: AppButton {
                required property var modelData
                visible: modelData.pan ? Engine.canAddPanadapter : Engine.canAddSlice
                text: modelData.label
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
                leftPadding: 2
                rightPadding: 2
                width: Math.max(addRow.cellWidth, implicitContentWidth + 6)
                height: Theme.touchTarget
                opacity: 0.85
                focusPolicy: Qt.NoFocus
                onClicked: modelData.pan ? Engine.addPanadapter() : Engine.addSlice()
            }
        }
    }

    // Notices and refusals (Engine.lastError) pop up in the middle of the
    // display, where they cannot be missed; a tap or 6 s dismisses them.
    // A popup, so it also shows over an open controls drawer.
    Popup {
        id: noticePopup
        objectName: "noticePopup"
        property string text: ""
        parent: panArea
        x: Math.round((panArea.width - width) / 2)
        y: Math.round((panArea.height - height) / 2)
        width: Math.min(panArea.width - 24, Math.max(240, noticeText.implicitWidth + 2 * Theme.spacing + 2))
        padding: Theme.spacing
        modal: false
        focus: false
        // Never closes on a press elsewhere: that press may be PTT or a
        // tune, and must reach it. A tap on the notice or 6 s closes it.
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.warning
            border.width: 2
            opacity: 0.97
        }
        contentItem: Text {
            id: noticeText
            text: noticePopup.text
            color: Theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Math.round(Theme.buttonFont * 1.05)
            MouseArea { anchors.fill: parent; onClicked: noticePopup.close() }
        }
        Timer {
            id: noticeTimer
            interval: 6000
            onTriggered: noticePopup.close()
        }
        Connections {
            target: Engine
            function onLastErrorChanged() {
                // The no-slice notice already says it, with a retry.
                if (Engine.lastError.length > 0 && !noSliceNotice.visible) {
                    noticePopup.text = Engine.lastError
                    noticePopup.open()
                    noticeTimer.restart()
                } else if (Engine.lastError.length === 0) {
                    noticePopup.close()
                }
            }
        }
    }

    // Connected but without a slice (the radio refused one, e.g. all its
    // slices held by other programs): nothing can tune, so say so over the
    // display, with a retry. A moment's grace while the first slice arrives.
    readonly property bool sliceless: Engine.connectionState === "connected" && Engine.slices.length === 0
    // The No slice preview (screenshots) draws no slice behind its notice.
    readonly property bool hideSlices: noSliceNotice.preview
    Timer {
        id: slicelessGrace
        interval: 4000
        running: panArea.sliceless
        onRunningChanged: if (!running) noSliceNotice.shown = false
        onTriggered: noSliceNotice.shown = true
    }
    Rectangle {
        id: noSliceNotice
        objectName: "noSliceNotice"
        property bool shown: false
        property bool preview: false   // screenshots (--preview-no-slice)
        visible: (shown && panArea.sliceless) || preview
        z: 8
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 460)
        height: noSliceColumn.implicitHeight + 2 * Theme.spacing
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.warning
        ColumnLayout {
            id: noSliceColumn
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spacing / 2
            Label {
                Layout.fillWidth: true
                text: qsTr("No slice")
                color: Theme.warning
                font.pixelSize: Math.round(Theme.buttonFont * 1.2)
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: Engine.lastError.length > 0 ? Engine.lastError
                      : qsTr("The radio has not given this app a slice yet, so it cannot tune.")
            }
            RowLayout {
                AppButton {
                    text: qsTr("Try again")
                    leftPadding: 16
                    rightPadding: 16
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: Engine.addSlice()
                }
                // Another program often holds the radio's slices.
                AppButton {
                    text: qsTr("Stations…")
                    leftPadding: 16
                    rightPadding: 16
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: stationsDialog.open()
                }
            }
        }
    }
    // A notch's settings: width, depth, permanent, a fine move, remove.
    // Over the whole window (a short landscape pan could not hold it), near
    // the notch; four rows of buttons, the row labels only where there is
    // height for them.
    Popup {
        id: tnfPopup
        objectName: "tnfPopup"
        property int tnfId: -1
        property real anchorX: panArea.width / 2
        readonly property var tnf: {
            const list = panArea.shownTnfs
            for (let i = 0; i < list.length; ++i) {
                if (list[i].id === tnfId) return list[i]
            }
            return null
        }
        // Held -/+: 10 Hz a step, counted from the press (not the echo).
        property real nudgeMhz: 0
        function nudge(n, dir) {
            if (!tnf) return
            if (n === 0) nudgeMhz = tnf.freq
            nudgeMhz += dir * 10e-6
            Engine.setTnf(tnfId, "freq", nudgeMhz)
        }
        // Gone (removed here or by another client): close.
        onTnfChanged: if (opened && !tnf) close()
        // The screenshot's sample notch goes with its popup.
        onClosed: panArea.previewTnfs = []
        // Screenshots (--popup tnfPopup): the first notch in view, or a
        // sample one in the passband (the radio's notches can all be on
        // other bands; the manual's picture showed none).
        function preview() {
            const half = Engine.panBandwidthMhz / 2
            let first = Engine.tnfs.find(t => Math.abs(t.freq - Engine.panCenterMhz) < half)
            if (!first) {
                panArea.previewTnfs = [{ id: -1, width: 200, depth: 2, permanent: false,
                                         freq: Engine.frequencyMhz + (Engine.filterLow + Engine.filterHigh) / 2e6 }]
                first = panArea.previewTnfs[0]
            }
            tnfId = first.id
            anchorX = (first.freq - Engine.panCenterMhz) / Engine.panBandwidthMhz * panArea.width + panArea.width / 2
            open()
        }
        readonly property bool roomy: !Theme.compactHeight
        parent: Overlay.overlay
        width: Math.min(parent.width - 16, 420)
        // Placed when it opens (mapToItem is not a live binding).
        onAboutToShow: {
            const p = panArea.mapToItem(Overlay.overlay, anchorX, panArea.height * 0.12)
            x = Math.max(8, Math.min(parent.width - width - 8, p.x - width / 2))
            y = Math.max(8, Math.min(parent.height - implicitHeight - 8, p.y))
        }
        padding: Theme.spacing
        // Kept inside the window, scrolling where it is lower than the panel.
        margins: 8
        modal: false
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
            opacity: 0.97
        }
        contentItem: PanelContent {
            title: tnfPopup.tnf ? qsTr("Notch %1 MHz").arg(tnfPopup.tnf.freq.toFixed(4)) : ""
            panel: tnfPopup
            rowSpacing: Theme.spacing / 2
            // The radio's global switch: notches only work while it is on.
            extras: AppButton {
                text: Engine.tnfEnabled ? qsTr("TNF on") : qsTr("TNF off")
                checked: Engine.tnfEnabled
                Layout.preferredHeight: Theme.touchTarget
                leftPadding: 10
                rightPadding: 10
                onClicked: Engine.tnfEnabled = !Engine.tnfEnabled
            }
            Label {
                visible: tnfPopup.roomy
                text: qsTr("Width")
                color: Theme.textDim
                font.pixelSize: Theme.smallFont
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [50, 100, 200, 500]
                    delegate: AppButton {
                        required property int modelData
                        text: modelData + " Hz"
                        checked: !!tnfPopup.tnf && tnfPopup.tnf.width === modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setTnf(tnfPopup.tnfId, "width", modelData)
                    }
                }
            }
            Label {
                visible: tnfPopup.roomy
                text: Engine.tnfHasDepth ? qsTr("Depth") : ""
                color: Theme.textDim
                font.pixelSize: Theme.smallFont
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: Engine.tnfHasDepth
                           ? [{ d: 1, label: qsTr("Normal") }, { d: 2, label: qsTr("Deep") }, { d: 3, label: qsTr("Very deep") }]
                           : []
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        checked: !!tnfPopup.tnf && tnfPopup.tnf.depth === modelData.d
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setTnf(tnfPopup.tnfId, "depth", modelData.d)
                    }
                }
                AppButton {
                    text: qsTr("Permanent")
                    checked: !!tnfPopup.tnf && tnfPopup.tnf.permanent
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: Engine.setTnf(tnfPopup.tnfId, "permanent", !checked)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                RepeatButton {
                    text: "−10 Hz"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.touchTarget
                    onStepped: (n) => tnfPopup.nudge(n, -1)
                }
                RepeatButton {
                    text: "+10 Hz"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.touchTarget
                    onStepped: (n) => tnfPopup.nudge(n, 1)
                }
                AppButton {
                    text: qsTr("Remove")
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        Engine.removeTnf(tnfPopup.tnfId)
                        tnfPopup.close()
                    }
                }
            }
        }
    }
    StationsDialog { id: stationsDialog }
    // The CW tab's Autotune: the strongest CW signal in the passband, for
    // when the decoder has not heard one (Engine.cwAutotuneNow).
    Connections {
        target: Engine
        function onCwAutotuneRequested() {
            const centre = Engine.frequencyMhz + (Engine.filterLow + Engine.filterHigh) / 2e6
            const half = Math.max(60, (Engine.filterHigh - Engine.filterLow) / 2)
            Engine.cwAutotuneNow(spectrum.cwPeakNear(centre, half))
        }
    }
    Connections {
        target: Engine
        function onStationsRequested() { stationsDialog.open() }
    }

    function mhzAt(x) {
        return Engine.panCenterMhz + (x / panArea.width - 0.5) * Engine.panBandwidthMhz
    }
    // Whether (x, y) is on something with its own tap (a spot, a notch,
    // another slice's marker): the display's tap and hold leave it to that.
    function overlayHit(x, y) {
        const bw = Engine.panBandwidthMhz
        if (!(bw > 0)) return false
        for (const t of spotLayout) {
            const top = 4 + t.row * 24
            if (Math.abs(x - t.x) <= t.w / 2 && y >= top && y <= top + 20) return true
        }
        for (const n of shownTnfs) {
            const cx = (n.freq - Engine.panCenterMhz) / bw * panArea.width + panArea.width / 2
            if (Math.abs(x - cx) <= Math.max(16, n.width / 1e6 / bw * panArea.width / 2)) return true
        }
        for (const sl of Engine.slices) {
            if (sl.active || sl.panId !== activePanId) continue
            const sx = (sl.frequency - Engine.panCenterMhz) / bw * panArea.width + panArea.width / 2
            if (Math.abs(x - sx) <= 18) return true
        }
        return false
    }

    // FT8 / FT4 decoding in DIGU / DIGL: the passband is the audio band the
    // tones are in, so a tap there picks the transmit audio frequency
    // instead of retuning (outside it, a tap tunes as always).
    // FT8 / FT4 run on one slice (Engine.digi.slice*), not always the
    // active one.
    readonly property bool digiMode: Engine.digiRunning && !Engine.rade
        && (Engine.digi.sliceMode === "DIGU" || Engine.digi.sliceMode === "DIGL")
    readonly property bool digiPick: digiMode
    function digiHzAt(x) {
        const hz = (mhzAt(x) - Engine.digi.sliceMhz) * 1e6
        return Engine.digi.sliceMode === "DIGL" ? -hz : hz
    }
    function activeHzAt(x) {
        const hz = (mhzAt(x) - Engine.frequencyMhz) * 1e6
        return Engine.mode === "DIGL" ? -hz : hz
    }

    // Tap (finger, Pencil or pointer) tunes to the touched point on
    // the tuning-step grid.
    TapHandler {
        id: panTap
        onTapped: (eventPoint) => {
            if (Engine.panBandwidthMhz > 0
                    && !panArea.overlayHit(eventPoint.position.x, eventPoint.position.y)) {
                if (panArea.digiPick) {
                    // In another DIGU / DIGL slice's passband: FT8 / FT4
                    // move there, the tone where tapped.
                    const ahz = panArea.activeHzAt(eventPoint.position.x)
                    if (!Engine.digi.sliceActive && (Engine.mode === "DIGU" || Engine.mode === "DIGL")
                            && ahz >= 200 && ahz <= 3000) {
                        Engine.digiBindActiveSlice()
                        Engine.digiTxHz = Math.round(ahz)
                        return
                    }
                    const hz = panArea.digiHzAt(eventPoint.position.x)
                    if (hz >= 200 && hz <= 3000) {
                        Engine.digiTxHz = Math.round(hz)
                        return
                    }
                }
                // In CW: on the CW signal nearest the tap, heard at the
                // pitch (a tap that misses it by a little still finds it);
                // where there is none, the touched point to the hertz.
                if (Engine.cwMode) {
                    const tapped = panArea.mhzAt(eventPoint.position.x)
                    const peak = spectrum.cwPeakNear(tapped)
                    Engine.tuneToCw(peak > 0 ? peak : tapped, peak > 0)
                } else {
                    Engine.tuneToSnapped(panArea.mhzAt(eventPoint.position.x))
                }
            }
        }
        // Press and hold: a notch (TNF) at that point.
        onLongPressed: {
            const p = panTap.point.position
            if (Engine.panBandwidthMhz > 0 && Engine.canAddTnf && !panArea.overlayHit(p.x, p.y)) {
                Engine.addTnf(panArea.mhzAt(p.x))
            }
        }
    }

    // Finger drag, as on the desktop (SpectrumWidget): started inside the
    // filter passband it tunes (the slice follows the finger); started
    // anywhere else it grabs the display and moves the view, slice
    // unchanged. While tuning, holding the finger in either edge zone keeps
    // going, faster the closer to the edge.
    DragHandler {
        id: drag
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Mouse | PointerDevice.TouchPad
        xAxis.enabled: true
        yAxis.enabled: false
        property bool tuning: false
        property real startMhz: 0
        property real edgeMhz: 0
        readonly property real span: Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
        readonly property real edgeZone: 0.12
        // -1..1: how deep the finger is in the left/right edge zone.
        readonly property real edgeDepth: {
            if (!active || !tuning) return 0
            const f = centroid.position.x / panArea.width
            if (f < edgeZone) return -Math.min(1, (edgeZone - f) / edgeZone)
            if (f > 1 - edgeZone) return Math.min(1, (f - 1 + edgeZone) / edgeZone)
            return 0
        }
        // Tuning lands on the tuning step, as the desktop's VFO drag does
        // (a tester had to round off with the step buttons after each drag).
        function apply() {
            const shift = translation.x / panArea.width * span
            if (tuning) Engine.tuneToSnapped(startMhz + shift + edgeMhz)
            else Engine.panTo(startMhz - shift)
        }
        onActiveChanged: {
            if (!active) return
            // The passband, widened to a finger's width.
            const x = centroid.pressPosition.x
            const half = Math.max(passband.width, 48) / 2
            const mid = passband.x + passband.width / 2
            // In the FT8 / FT4 slice's passband a drag never retunes.
            tuning = passband.visible && Math.abs(x - mid) <= half
                     && !(panArea.digiMode && Engine.digi.sliceActive)
            startMhz = tuning ? Engine.frequencyMhz : Engine.panCenterMhz
            edgeMhz = 0
        }
        onTranslationChanged: apply()
    }
    // Edge auto-scroll while tuning: from a quarter span per second at the
    // zone's inner border up to three spans per second at the very edge.
    Timer {
        interval: 50
        repeat: true
        running: drag.edgeDepth !== 0
        onTriggered: {
            const d = drag.edgeDepth
            const spansPerSecond = 0.25 + 2.75 * d * d
            drag.edgeMhz += Math.sign(d) * spansPerSecond * drag.span * interval / 1000
            drag.apply()
        }
    }

    // Apple Pencil drags tune ten times finer, for zero-beating (not on the
    // step: exact).
    DragHandler {
        target: null
        acceptedDevices: PointerDevice.Stylus
        xAxis.enabled: true
        yAxis.enabled: false
        property real startMhz: 0
        onActiveChanged: if (active) startMhz = Engine.frequencyMhz
        onTranslationChanged: {
            const span = Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
            Engine.tuneTo(startMhz + translation.x / panArea.width * span / 10)
        }
    }

    // Pinch changes the span around the slice; applied on release
    // so the radio gets one request per gesture.
    PinchHandler {
        target: null
        rotationAxis.enabled: false
        // A second finger takes the gesture over from the one-finger
        // tuning drag.
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        // activeScale is back to 1 by the time active turns false, so
        // remember the last value seen during the gesture.
        property real lastScale: 1.0
        onActiveScaleChanged: if (active) lastScale = activeScale
        onActiveChanged: {
            if (active) {
                lastScale = 1.0
            } else if (lastScale > 0 && Math.abs(lastScale - 1.0) > 0.05) {
                Engine.zoom(1.0 / lastScale)
            }
        }
    }

    // Trackpad or mouse wheel: tune by steps; with Ctrl or Cmd, zoom.
    WheelHandler {
        target: null
        onWheel: (event) => {
            const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
            if (dy === 0) return
            if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
                Engine.zoom(dy > 0 ? 0.8 : 1.25)
            } else {
                Engine.tuneBySteps(dy > 0 ? 1 : -1)
            }
        }
    }

    // The QSO's state at the top of the display, where it stays in view
    // however the Digi tab is scrolled (asked by a tester): amber while
    // waiting, red while transmitting.
    Rectangle {
        objectName: "digiStateBanner"
        readonly property var qso: Engine.digi
        // Above the slice flags and marks (z), in FT8 / FT4 only.
        z: 100
        visible: panArea.digiMode && qso.state !== "idle" && (qso.status || qso.txMsg)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 30  // below the slice flags
        width: Math.min(parent.width - 16, bannerText.implicitWidth + 16)
        height: bannerText.implicitHeight + 8
        radius: 6
        color: qso.transmitting ? "#d7262d" : "#ffbf00"
        border.color: "#ffffff"
        border.width: 1
        Label {
            id: bannerText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.parent.width - 32)
            text: parent.qso.transmitting && parent.qso.txMsg ? qsTr("TX: %1").arg(parent.qso.txMsg)
                  : (parent.qso.status || "") + (parent.qso.txMsg ? "  ·  " + qsTr("Next: %1").arg(parent.qso.txMsg) : "")
            color: parent.qso.transmitting ? "#ffffff" : "#101010"
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
        }
    }
}
