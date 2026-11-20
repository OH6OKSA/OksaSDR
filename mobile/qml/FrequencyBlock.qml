import QtQuick
import QtQuick.Controls
import AetherMobile

// The frequency for an own layout's block (roadmap 113): its type as large
// as the block lets it, a tap opens frequency entry. The mode and the step
// are blocks of their own (ModeBlock, StepSizeBlock), to put where the
// hand is.
Item {
    id: fb

    property var formatMhz
    signal tapped()

    TextMetrics {
        id: digits
        font.pixelSize: 100
        font.family: "monospace"
        text: "00.000.000"
    }
    // The line about 1.28 times the type.
    function fontFor(w, h) {
        return Math.min(h * 0.78, 120, (w - 8) / (digits.advanceWidth / 100))
    }
    function fits(w, h) { return fontFor(w, h) >= 22 }
    readonly property real fs: Math.max(14, fontFor(width, height))

    Label {
        objectName: "blockFrequency"
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: fb.formatMhz ? fb.formatMhz(Engine.frequencyMhz) : ""
        color: Theme.text
        font.pixelSize: Math.round(fb.fs)
        font.family: "monospace"
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Frequency %1 kHz").arg((Engine.frequencyMhz * 1000).toFixed(3))
        Accessible.description: qsTr("Opens frequency entry")
        Accessible.onPressAction: fb.tapped()
        TapHandler { onTapped: fb.tapped() }
    }
}
