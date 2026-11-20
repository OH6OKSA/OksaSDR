import QtQuick
import AetherMobile

// The mode (a tap chooses another) for an own layout's block (roadmap 113):
// its type follows the block ("DIGU ▾" about 3.4 times its type wide).
Item {
    id: mb

    function fontFor(w, h) { return Math.min(h * 0.62, (w - 8) / 3.4, 48) }
    function fits(w, h) { return fontFor(w, h) >= 14 }

    ModeButton {
        objectName: "blockMode"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        fontSize: Math.max(12, Math.round(mb.fontFor(mb.width, mb.height)))
    }
}
