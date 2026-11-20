import QtQuick
import AetherMobile

// The tuning step (STEP 1 kHz; a tap chooses another) for an own layout's
// block (roadmap 113, Larry Averbeck W9FLA: "The ANT1 and STEP lines would
// be more easily accessible below the Waterfall"): its type follows the
// block, as large as the block lets it.
Item {
    id: sb

    // The button's width is about 6.5 times its type ("STEP 1 kHz"), its
    // height about twice it.
    function fontFor(w, h) { return Math.min(h * 0.5, (w - 12) / 6.8, 28) }
    function fits(w, h) { return fontFor(w, h) >= 12 }

    StepButton {
        objectName: "blockStepSize"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        fontSize: Math.max(11, Math.round(sb.fontFor(sb.width, sb.height)))
        heightScale: Math.min(1, sb.height / Theme.touchTarget)
    }
}
