import QtQuick
import QtQuick.Controls
import AetherMobile

// A held slice chip's menu: make it the TX slice, or close it. One for the
// main screen's chips and Big buttons'.
Menu {
    property var slice
    modal: true
    dim: false
    MenuItem {
        text: qsTr("Make TX slice")
        enabled: !!slice && !slice.tx
        onTriggered: Engine.makeTxSlice(slice.id)
    }
    MenuItem {
        objectName: "sliceClose" + (slice ? slice.letter : "")
        text: qsTr("Close slice")
        enabled: Engine.slices.length > 1
        onTriggered: Engine.removeSlice(slice.id)
    }
}
