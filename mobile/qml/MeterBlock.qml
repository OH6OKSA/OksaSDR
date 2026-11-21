import QtQuick
import QtQuick.Layouts
import AetherMobile

// An own layout's Meter 1 or Meter 2 block (roadmap 117, Larry Averbeck
// W9FLA): Classic's left or right meter of the slice row, its face as View
// sets it (analog, a bar, two bars, or two faces side by side), sized from
// the block: a larger block, a larger face. A tap chooses what it shows, as
// on Classic.
Item {
    id: mb
    // "left" (Meter 1) or "right" (Meter 2).
    required property string side
    readonly property string style: side === "left" ? Engine.meterStyleLeft : Engine.meterStyleRight
    readonly property real dialHeight: Math.max(30, height - 8)
    // Two faces side by side ("analog2") where each still gets a face of
    // 60 px; else the same two readings as two bars, one over the other.
    readonly property real pairWidth: (width - 4) / 2
    readonly property bool pairFits: Math.min(dialHeight, (pairWidth - 8) / 1.6) >= 60
    readonly property bool pair: style === "analog2" && pairFits
    readonly property bool stack: style === "two" || style === "analog2" && !pairFits
    readonly property real stackBarHeight: Math.max(22, (dialHeight - 4) / 2 - 8)
    readonly property real singleBarHeight: Math.max(22, Math.min(dialHeight - 8, 52))
    // The editor's limit: a bar face's least size.
    function fits(w, h) { return w >= 80 && h >= 40 }

    GridLayout {
        id: faces
        // At the top, as on Classic's slice row; centred across.
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        transformOrigin: Item.Top
        // An analog face takes the block's height and is as wide as that
        // face needs (as on Classic, where the width follows the face); where
        // the block is narrower, the whole meter is scaled down to it, so it
        // never draws outside the block.
        scale: Math.min(1, mb.width / Math.max(1, implicitWidth), mb.height / Math.max(1, implicitHeight))
        columns: mb.pair ? 2 : 1
        columnSpacing: 4
        rowSpacing: 4
        // Mirrored at the right: a pair's main face at the outer edge.
        layoutDirection: mb.side === "right" && mb.pair ? Qt.RightToLeft : Qt.LeftToRight
        MeterBadge {
            objectName: mb.side === "left" ? "blockMeterLeft" : "blockMeterRight"
            side: mb.side
            dialHeight: mb.dialHeight
            maxWidth: mb.pair ? mb.pairWidth : 4 * mb.width
            barMaxWidth: mb.pair ? mb.pairWidth : mb.width
            analogRoom: mb.style !== "analog2" || mb.pair
            barHeight: mb.stack ? mb.stackBarHeight : mb.singleBarHeight
        }
        MeterBadge {
            objectName: mb.side === "left" ? "blockMeterLeft2" : "blockMeterRight2"
            side: mb.side + "2"
            visible: mb.stack || mb.pair
            dialHeight: mb.dialHeight
            maxWidth: mb.pair ? mb.pairWidth : 4 * mb.width
            barMaxWidth: mb.pair ? mb.pairWidth : mb.width
            analogRoom: mb.pair
            barHeight: mb.stackBarHeight
        }
    }
}
