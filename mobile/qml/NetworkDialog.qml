import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Network and performance, as the desktop's diagnostics: quality, link
// timing, packet integrity per stream, traffic, and the app's CPU and frame
// rate. Refreshed once a second while open.
//
// No size in it follows its text (roadmap 100): on iOS a row layout with
// labels whose text changed every second and a note wrapped in the column
// set each other's size without end (Qt Quick's polish loop: the app froze
// and iOS ended it, Scott W5SLS's iPhone and the developer's iPad). Each row
// is a fixed height, its value right-aligned over the label; the note has a
// fixed width; the per-stream rows stay, only their values change.
Popup {
    id: dialog
    objectName: "networkDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, (parent ? parent.width : 460) - 24)
    height: Math.min(implicitHeight, (parent ? parent.height : 800) - 16)
    modal: true
    padding: Theme.spacing
    property var d: ({})
    function refresh() { d = Engine.networkDetails() }
    onOpened: refresh()
    Timer { interval: 1000; repeat: true; running: dialog.opened; onTriggered: dialog.refresh() }
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }

    // The rows' heights come from the font, never from their text.
    FontMetrics { id: rowFont; font.pixelSize: Theme.px(14); font.bold: true }
    FontMetrics { id: sectionFont; font.pixelSize: Theme.px(13); font.bold: true }

    // A label and its value on one line: the value at the right in its own
    // width, the label elided before it.
    component Row2: Item {
        id: row
        property string label
        property string value
        property color valueColor: Theme.text
        Layout.fillWidth: true
        implicitWidth: 0
        implicitHeight: Math.ceil(rowFont.height) + 2
        Label {
            anchors.left: parent.left
            anchors.right: valueText.left
            anchors.rightMargin: Theme.spacing / 2
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            elide: Text.ElideRight
        }
        Label {
            id: valueText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, row.width * 0.7)
            horizontalAlignment: Text.AlignRight
            text: row.value
            color: row.valueColor
            font.pixelSize: Theme.px(14)
            font.bold: true
            elide: Text.ElideLeft
        }
    }
    component Section: Item {
        id: section
        property string text
        Layout.fillWidth: true
        implicitWidth: 0
        implicitHeight: Math.ceil(sectionFont.height) + 6
        Label {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: section.text
            Accessible.role: Accessible.Heading
            color: Theme.accent
            font.pixelSize: Theme.px(13)
            font.bold: true
        }
    }

    // The rows scroll where the window is lower than the dialog (a phone on
    // its side cut off the performance lines).
    contentItem: PanelContent {
        title: qsTr("Network & performance")
        panel: dialog
        rowSpacing: 4
        // Under the title: beside it, the title ran over it on a phone.
        // In an item of a fixed size: its width changes with the CPU and
        // memory figures every second.
        Item {
            Layout.fillWidth: true
            implicitWidth: 0
            implicitHeight: badge.implicitHeight
            NetworkBadge {
                id: badge
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                quality: dialog.d.quality || "Off"
                rttMs: dialog.d.rttMs || 0
            }
        }
        Row2 { label: qsTr("Connection"); value: dialog.d.path || "–" }
        Row2 { label: qsTr("Radio"); value: dialog.d.radio || "–" }

        Section { text: qsTr("LINK TIMING") }
        Row2 { label: qsTr("Round trip (RTT)"); value: (dialog.d.rttMs || 0) + " ms" }
        Row2 { label: qsTr("Audio jitter"); value: (dialog.d.jitterMs || 0) + " ms" }
        Row2 {
            label: qsTr("Audio packet gap")
            value: qsTr("%1 ms · max %2 ms").arg(dialog.d.audioGapMs || 0).arg(dialog.d.audioGapMaxMs || 0)
        }

        Section { text: qsTr("PACKET INTEGRITY") }
        Row2 {
            label: qsTr("Recent loss (%1 s)").arg(dialog.d.lossWindowS || 0)
            value: qsTr("%1 %  (%2 / %3)").arg((dialog.d.lossPercent || 0).toFixed(2))
                       .arg(dialog.d.lossDrops || 0).arg(dialog.d.lossPackets || 0)
            valueColor: (dialog.d.lossPercent || 0) >= 2 ? Theme.transmit
                      : (dialog.d.lossPercent || 0) >= 0.5 ? Theme.warning : Theme.text
        }
        Row2 {
            label: qsTr("Session lost packets")
            value: (dialog.d.sessionDrops || 0) + " / " + (dialog.d.sessionPackets || 0)
        }
        // The four stream kinds (audio, FFT, waterfall, meters): the rows
        // stay, their values change (a model of new objects every second
        // made the rows anew each time).
        Repeater {
            model: 4
            delegate: Row2 {
                required property int index
                readonly property var stream: (dialog.d.streams || [])[index]
                visible: stream !== undefined
                label: "   " + (stream ? stream.name : "")
                value: stream ? stream.errors + " / " + stream.packets : ""
            }
        }

        Section { text: qsTr("TRAFFIC") }
        Row2 {
            label: qsTr("Receiving")
            value: qsTr("%1 kbit/s · %2 MB").arg(Math.round(dialog.d.rxKbps || 0)).arg((dialog.d.rxMB || 0).toFixed(1))
        }
        Row2 {
            label: qsTr("Sending")
            value: qsTr("%1 kbit/s · %2 MB").arg(Math.round(dialog.d.txKbps || 0)).arg((dialog.d.txMB || 0).toFixed(1))
        }
        Section { text: qsTr("PERFORMANCE") }
        Row2 {
            label: qsTr("App CPU, all %1 cores").arg(Engine.cpuCores)
            value: Math.round(Engine.cpuPercent) + " %"
            valueColor: Engine.cpuPercent >= 80 ? Theme.transmit : Engine.cpuPercent >= 50 ? Theme.warning : Theme.text
        }
        // All threads summed, one core = 100 %: 102 % is about one core's
        // worth of work, not one core full. The busiest thread tells that.
        Row2 {
            label: qsTr("App CPU, one core = 100 %")
            value: Math.round(Engine.cpuPercentOfCore) + " %"
        }
        Row2 {
            visible: Engine.busyThreads.length > 0
            label: qsTr("Busiest threads")
            value: Engine.busyThreads.slice(0, 2).join(", ")
            valueColor: Engine.busyThreads.length > 0 && parseInt(Engine.busyThreads[0].split(" ").slice(-2)[0]) >= 90
                        ? Theme.warning : Theme.text
        }
        Row2 {
            visible: Engine.memoryMB > 0
            label: qsTr("App memory")
            value: Math.round(Engine.memoryMB) + " MB"
        }
        Row2 {
            label: qsTr("Screen frames drawn")
            value: Engine.uiFps + " /s"
            valueColor: Engine.uiFps > 0 && Engine.uiFps < 20 ? Theme.warning : Theme.text
        }
        // A fixed width (the room beside a scroll bar, shown or not), so the
        // wrapped note's height never changes the column's width back.
        Label {
            Layout.preferredWidth: Math.max(0, dialog.availableWidth - 14)
            Layout.maximumWidth: Math.max(0, dialog.availableWidth - 14)
            text: qsTr("App CPU adds up all the app's threads; a thread near 100 % is one core full. %1 shows apps no GPU load: frames drawn per second falling well below the display's rate means the graphics are not keeping up.")
                  .arg(Qt.platform.os === "ios" ? "iOS" : Qt.platform.os === "android" ? "Android" : qsTr("The system"))
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.Wrap
        }
    }
}
