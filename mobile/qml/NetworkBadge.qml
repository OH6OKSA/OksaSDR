import QtQuick
import AetherMobile

// Network quality at a glance: a coloured dot, the rating and the round
// trip ("● Excellent 4 ms"), in the desktop's colours, then the app's CPU
// and memory.
// compact drops the word (phones).
Row {
    id: badge
    property string quality: Engine.networkQuality
    property int rttMs: Engine.networkRttMs
    property bool compact: false
    signal tapped()
    spacing: 5
    // One button to a screen reader (the page opens the details on its
    // press action), its parts not read one by one.
    Accessible.role: Accessible.Button
    Accessible.name: badge.rttMs > 0 ? qsTr("Network %1, %2 ms").arg(badge.quality).arg(badge.rttMs)
                                     : qsTr("Network %1").arg(badge.quality)
    readonly property color dotColor: quality === "Excellent" || quality === "Very Good" ? "#00cc66"
                                    : quality === "Good" ? "#00b4d8"
                                    : quality === "Fair" ? "#cc9900"
                                    : quality === "Poor" ? "#cc3333" : "#8aa8c0"
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 10; height: 10; radius: 5
        color: badge.dotColor
    }
    Text {
        Accessible.ignored: true
        anchors.verticalCenter: parent.verticalCenter
        visible: !badge.compact
        text: badge.quality
        color: badge.dotColor
        font.pixelSize: 13
        font.bold: true
    }
    Text {
        Accessible.ignored: true
        anchors.verticalCenter: parent.verticalCenter
        visible: badge.rttMs > 0
        text: badge.rttMs + " ms"
        color: "#8aa8c0"
        font.pixelSize: 12
    }
    // The app's CPU (share of all cores, as the desktop's status bar).
    Text {
        Accessible.ignored: true
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: 6
        text: "CPU " + Math.round(Engine.cpuPercent) + " %"
        color: Engine.cpuPercent >= 80 ? "#cc3333" : Engine.cpuPercent >= 50 ? "#cc9900" : "#8aa8c0"
        font.pixelSize: 12
    }
    // The app's memory (phones: the number alone, beside the CPU).
    Text {
        Accessible.ignored: true
        anchors.verticalCenter: parent.verticalCenter
        visible: Engine.memoryMB > 0
        text: (badge.compact ? "" : "RAM ") + Math.round(Engine.memoryMB) + " MB"
        color: "#8aa8c0"
        font.pixelSize: 12
    }
}
