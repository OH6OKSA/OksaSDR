import QtQuick
import AetherMobile

// One selectable meter on the slice row (tap to choose what it shows).
// The host gives the dial height and the most width it may take; an analog
// face is as wide as its dial and labels need (lower where the width is
// short, keeping its shape), a bar face as wide as allowed.
Rectangle {
    id: badge
    property alias side: meterView.side
    property real dialHeight: 66
    property real maxWidth: 240
    // A bar face may reach further (into width the chips leave free): it
    // keeps its height, where a wider analog face would grow taller.
    property real barMaxWidth: maxWidth
    // A bar face's height (0: the normal).
    property real barHeight: 0
    // An analog face keeps its proportions (about 1.6 : 1, as on a tablet):
    // where the width runs out it gets lower, not squarer.
    readonly property real faceHeight: Math.min(dialHeight, (maxWidth - 8) / 1.6)
    implicitWidth: meterView.analog ? Math.min(maxWidth, meterView.analogFitWidth + 8)
                            : barMaxWidth
    implicitHeight: meterView.implicitHeight + 8
    radius: Theme.radius
    color: Theme.surface
    MeterView {
        id: meterView
        compact: true
        analogAllowed: badge.faceHeight >= 46
        slimHeight: badge.barHeight
        faceHeight: badge.faceHeight
        anchors.fill: parent
        anchors.margins: 4
    }
}
