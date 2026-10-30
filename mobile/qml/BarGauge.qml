import QtQuick
import AetherMobile
import "MeterMarks.js" as MeterMarks

// Bar meter face: the reading's name (blue) and value over a bar with the
// analog face's scale under it (S1…S9 then +20/+40/+60 in red; other
// meters five labels from min to max, red from the alarm level), and a
// peak hold marker that stays at the highest recent reading and falls back
// as the chosen peak hold says. A low face keeps the ticks, not the labels.
Item {
    id: gauge
    property string kind: "S"
    property real value: 0
    property real min: 0
    property real max: 1
    property real red: 1e9
    property bool live: false
    property string text: ""
    // Peak hold value; the marker shows while showPeak and reading.
    property real peak: min
    property bool showPeak: false
    // Room the host keeps at the header's right (the chooser's caret).
    property real headerRightInset: 0

    // Size unit from the height (40 px = 1) and the width (110 px = 1):
    // text, bar and scale follow it.
    readonly property real unit: Math.max(0.75, Math.min(1.4, height / 40, width / 110))
    readonly property real headerFont: Math.max(10, Math.round(12 * unit))
    readonly property real headerHeight: Math.round(headerFont * 1.2)
    readonly property bool labels: height >= 28
    readonly property real labelFont: Math.max(7, Math.round(8 * unit))
    readonly property real tickLength: 3
    readonly property real scaleHeight: tickLength + (labels ? labelFont + 1 : 0)
    readonly property real barHeight: Math.max(4, Math.min(14, height - headerHeight - 2 - scaleHeight))
    readonly property real barY: headerHeight + 2
    readonly property real span: (max - min) || 1
    function xOf(v) {
        return width * Math.max(0, Math.min(1, (v - min) / span))
    }
    readonly property var marks: MeterMarks.marks(kind, min, max, red)
    readonly property real redFrom: MeterMarks.redFrom(kind, red)

    // Eased as the analog needle, so jitter reads like a movement.
    property real shown: live ? Math.max(min, Math.min(max, value)) : min
    Behavior on shown { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

    // The track, its red zone and the scale: painted when the size or the
    // meter changes, not on every reading (the fill and the peak marker
    // are items).
    onWidthChanged: track.requestPaint()
    onHeightChanged: track.requestPaint()
    onMarksChanged: track.requestPaint()
    onLiveChanged: track.requestPaint()
    Canvas {
        id: track
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const ctx = getContext("2d")
            const w = width
            ctx.reset()
            const top = gauge.barY, bh = gauge.barHeight
            ctx.fillStyle = "#1f2a36"
            ctx.fillRect(0, top, w, bh)
            // The red zone, dim under the fill.
            const rx = gauge.xOf(gauge.redFrom)
            if (rx < w) {
                ctx.fillStyle = "#4a1f24"
                ctx.fillRect(rx, top, w - rx, bh)
            }
            // Ticks under the bar, labels under the ticks: first and last
            // first, then the rest where they do not touch a drawn one.
            const ticksY = top + bh
            ctx.font = "bold " + gauge.labelFont + "px sans-serif"
            ctx.textBaseline = "top"
            const m = gauge.marks
            const order = m.length > 1 ? [0, m.length - 1].concat(m.map((_, i) => i).slice(1, -1)) : [0]
            const drawn = []
            for (const i of order) {
                const x = gauge.xOf(m[i][0])
                const col = m[i][2] ? "#ff5a5f" : "#e6edf3"
                ctx.fillStyle = col
                ctx.fillRect(Math.max(0, Math.min(w - 1.5, x - 0.75)), ticksY, 1.5, gauge.tickLength)
                if (!gauge.labels) continue
                const tw = ctx.measureText(m[i][1]).width
                const left = Math.max(0, Math.min(w - tw, x - tw / 2))
                if (drawn.some(d => left < d[1] + 3 && left + tw + 3 > d[0])) continue
                drawn.push([left, left + tw])
                ctx.fillText(m[i][1], left, ticksY + gauge.tickLength + 1)
            }
        }
    }

    // The fill: blue, red past the alarm level.
    Rectangle {
        x: 0
        y: gauge.barY
        height: gauge.barHeight
        width: gauge.xOf(Math.min(gauge.shown, gauge.redFrom))
        visible: gauge.live
        color: Theme.accent
    }
    Rectangle {
        readonly property real from: gauge.xOf(gauge.redFrom)
        x: from
        y: gauge.barY
        height: gauge.barHeight
        width: Math.max(0, gauge.xOf(gauge.shown) - from)
        visible: gauge.live && gauge.shown > gauge.redFrom
        color: Theme.transmit
    }
    // Peak hold: an amber marker over the bar's edges.
    Rectangle {
        visible: gauge.showPeak && gauge.live && gauge.peak > gauge.min + gauge.span * 0.01
        width: 3
        height: gauge.barHeight + 4
        y: gauge.barY - 2
        x: Math.max(0, Math.min(gauge.width - width, gauge.xOf(gauge.peak) - width / 2))
        radius: 1
        color: "#f0b429"
    }

    // Header: the S unit or the meter's name (left, blue), the value alone
    // (right), as on the analog face.
    readonly property string shortName: kind === "PA temp" ? (width < 120 ? qsTr("PA") : qsTr("PA Temp")) : kind
    readonly property var parts: text.trim().split(/\s+/)
    readonly property string valueText: {
        if (!live) return "–"
        if (kind === "S") return parts.slice(1).join(" ")
        return /^[A-Za-z]+$/.test(parts[0]) ? parts.slice(1).join(" ") : parts.join(" ")
    }
    // Where "-91 dBm" does not fit beside the S unit: "-91" (not "… dBm").
    readonly property string valueShort: kind === "S" ? valueText.replace(/\s*dBm$/, "") : valueText
    TextMetrics {
        id: valueMetrics
        font.pixelSize: gauge.headerFont
        font.bold: true
        text: gauge.valueText
    }
    Text {
        id: nameText
        Accessible.ignored: true
        x: 0
        height: gauge.headerHeight
        verticalAlignment: Text.AlignVCenter
        text: gauge.kind === "S" && gauge.live ? gauge.parts[0] : gauge.shortName
        color: "#3fb6ff"
        font.pixelSize: gauge.headerFont
        font.bold: true
    }
    Text {
        Accessible.ignored: true
        anchors.right: parent.right
        anchors.rightMargin: gauge.headerRightInset
        height: gauge.headerHeight
        verticalAlignment: Text.AlignVCenter
        // Never over the name: at most the space left of it.
        readonly property real room: Math.max(0, gauge.width - gauge.headerRightInset - nameText.implicitWidth - 6)
        width: Math.min(implicitWidth, room)
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideLeft
        text: valueMetrics.advanceWidth <= room ? gauge.valueText : gauge.valueShort
        color: gauge.live && gauge.value >= gauge.red ? Theme.transmit : Theme.text
        font.pixelSize: gauge.headerFont
        font.bold: true
    }
}
