import QtQuick

// The keypads' delete key: an arrow-shaped tag with an x, drawn (the "⌫"
// glyph is missing from Android's font and showed as a box).
Canvas {
    id: icon
    property color color: Theme.buttonText
    property real size: 28
    implicitWidth: size
    implicitHeight: Math.round(size * 0.7)
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height, lw = Math.max(1.5, h / 12)
        ctx.strokeStyle = icon.color
        ctx.lineWidth = lw
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        const tip = h * 0.45
        ctx.beginPath()
        ctx.moveTo(lw, h / 2)
        ctx.lineTo(tip, lw)
        ctx.lineTo(w - lw, lw)
        ctx.lineTo(w - lw, h - lw)
        ctx.lineTo(tip, h - lw)
        ctx.closePath()
        ctx.stroke()
        const cx = (tip + w - lw) / 2, r = h * 0.2
        ctx.beginPath()
        ctx.moveTo(cx - r, h / 2 - r)
        ctx.lineTo(cx + r, h / 2 + r)
        ctx.moveTo(cx + r, h / 2 - r)
        ctx.lineTo(cx - r, h / 2 + r)
        ctx.stroke()
    }
}
