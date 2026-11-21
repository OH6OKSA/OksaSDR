import QtQuick
import AetherMobile

// An own layout's Slices block (roadmap 116, Larry Averbeck W9FLA): the
// slice chips in one line where the width gives each its least width,
// else in as many rows as the block's height holds, else in one line that
// scrolls sideways. Always clipped to the block: in build 197 the chips
// were Classic's two columns growing downward, and the third and later ran
// under the display.
Item {
    id: sb
    clip: true
    readonly property var slices: Engine.slices
    readonly property int count: slices.length
    readonly property real gap: 4
    readonly property real minChipW: Theme.phoneLayout ? 96 : 150
    readonly property real maxChipW: Theme.phoneLayout ? 140 : 210
    readonly property real chipH: Theme.touchTarget
    readonly property real minChipH: 34
    // cols x rows of cellW x cellH, or one scrolling line.
    function arrange(w, h) {
        const n = Math.max(1, count)
        const perRow = Math.max(1, Math.min(n, Math.floor((w + gap) / (minChipW + gap))))
        const rows = Math.ceil(n / perRow)
        const rowH = Math.min(chipH, (h - (rows - 1) * gap) / rows)
        if (rowH >= minChipH) {
            return { cols: perRow, rows: rows, scroll: false,
                     cellW: Math.min(maxChipW, (w - (perRow - 1) * gap) / perRow), cellH: rowH }
        }
        return { cols: n, rows: 1, scroll: true, cellW: minChipW, cellH: Math.max(minChipH, Math.min(chipH, h)) }
    }
    // The editor's limit: one chip at its least size.
    function fits(w, h) { return w >= minChipW && h >= minChipH }
    readonly property var plan: arrange(width, height)

    Flickable {
        id: flick
        objectName: "slicesBlockFlick"
        anchors.fill: parent
        contentWidth: Math.max(width, grid.width)
        contentHeight: height
        interactive: sb.plan.scroll
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        Grid {
            id: grid
            objectName: "slicesBlockGrid"
            // Centred across, at the top as on Classic's slice row (a scrolling
            // line in the middle of a low block).
            x: Math.max(0, (flick.width - width) / 2)
            y: sb.plan.scroll ? Math.max(0, (flick.height - height) / 2) : 0
            columns: sb.plan.cols
            spacing: sb.gap
            Repeater {
                model: sb.slices
                delegate: SliceChip {
                    required property var modelData
                    slice: modelData
                    width: sb.plan.cellW
                    height: sb.plan.cellH
                }
            }
        }
    }
}
