import QtQuick
import AetherMobile

// The info line (antennas, width, AGC, speaker, lock, DSP; the step is a
// block of its own)
// for an own layout's block (roadmap 113): in two rows where the block is
// tall enough for them, else one, its type scaled to the block's width.
// Measured from probes at full size, so the scale never feeds back into
// its own measure (as the tablet page measures its info panel).
Item {
    id: ib

    // The probes: shown but never seen (a hidden item's later parts, the TX
    // tag or SPLIT as the radio's status comes in, would not be laid out).
    Item {
        width: 0
        height: 0
        clip: true
        opacity: 0
        enabled: false
        InfoPanel {
            id: probeTwo
            measureOnly: true
            showStep: false
        }
        InfoPanel {
            id: probeOne
            measureOnly: true
            showStep: false
            singleLine: true
        }
    }
    function scaleFor(single, w, h) {
        const p = single ? probeOne : probeTwo
        if (p.implicitWidth <= 0 || p.implicitHeight <= 0) return 0
        // 3 % to spare: the type and the icons round to whole pixels.
        return Math.min(1, w * 0.97 / p.implicitWidth, h / p.implicitHeight)
    }
    // The form that gives the larger type; two rows on a tie.
    function formFor(w, h) {
        const two = scaleFor(false, w, h)
        const one = scaleFor(true, w, h)
        return one > two * 1.05 ? { single: true, scale: one } : { single: false, scale: two }
    }
    // Its words open their choosers (antenna, AGC, the filter): below 70 %
    // of their size they were too small to tap and to read.
    function fits(w, h) { return formFor(w, h).scale >= 0.7 }

    readonly property var form: formFor(width, height)

    InfoPanel {
        objectName: "blockInfo"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, ib.width)
        singleLine: ib.form.single
        showStep: false
        fontScale: Math.max(0.5, ib.form.scale)
        rowScale: Math.max(0.5, ib.form.scale)
        clip: true
    }
}
