import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// FreeDV in the app: RADE digital voice (the slice in DIGU / DIGL, decoded
// speech to the speaker, PTT through the RADE encoder), FreeDV Reporter
// (qso.freedv.org) reporting this station, and the stations it lists; a tap
// on one tunes there, as the desktop's FreeDV Reporter window.
ColumnLayout {
    id: fdv
    spacing: Theme.spacing / 2

    // The list is fetched only while a FreeDV tab shows; the drawer's and
    // the pinned panel's report apart, each once.
    property bool reportedShown: false
    function reportShown(shown) {
        if (shown !== reportedShown) {
            reportedShown = shown
            Engine.freedvViewer(shown)
        }
    }
    onVisibleChanged: reportShown(visible)
    Component.onCompleted: reportShown(visible)
    Component.onDestruction: reportShown(false)

    // ---- RADE digital voice ----
    Label {
        text: qsTr("RADE digital voice")
        color: Theme.textDim
        font.pixelSize: 14
    }
    Label {
        visible: !Engine.radeAvailable
        Layout.fillWidth: true
        text: qsTr("RADE is not in this build.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    RowLayout {
        visible: Engine.radeAvailable
        Layout.fillWidth: true
        AppButton {
            objectName: "radeButton"
            text: qsTr("RADE")
            checked: Engine.rade
            enabled: Engine.connectionState === "connected"
            Layout.preferredWidth: 80
            focusPolicy: Qt.NoFocus
            onClicked: Engine.rade = !Engine.rade
        }
        Label {
            Layout.fillWidth: true
            text: {
                const r = Engine.radeStatus
                if (!r.active)
                    return qsTr("FreeDV RADE in the app: talk with PTT as usual; the radio goes to DIGU / DIGL.")
                let line = r.synced ? qsTr("Synced, SNR %1 dB").arg(r.snr) : qsTr("Listening for RADE…")
                if (r.callsign)
                    line += " · " + qsTr("last: %1").arg(r.callsign)
                return line
            }
            color: Engine.radeStatus.synced ? "#00cc66" : Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    // ---- FreeDV Reporter: this station on qso.freedv.org (off by default) ----
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing
        Label {
            text: qsTr("Report to FreeDV Reporter")
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "freedvReporterOff"
            text: qsTr("Off")
            checked: !Engine.freedvReporter
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.freedvReporter = false
        }
        AppButton {
            objectName: "freedvReporterOn"
            text: qsTr("On")
            checked: Engine.freedvReporter
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.freedvReporter = true
        }
    }
    Label {
        objectName: "freedvStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: Engine.freedvStatus
        color: Engine.freedvLive ? "#00cc66" : Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    TextField {
        objectName: "freedvMessage"
        visible: Engine.freedvReporter
        Layout.fillWidth: true
        placeholderText: qsTr("Message shown there (optional)")
        text: Engine.freedvMessage
        maximumLength: 80
        font.pixelSize: 14
        onEditingFinished: Engine.freedvMessage = text
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("While RADE is on, your callsign, grid, frequency, when you transmit and the stations you hear (callsign and SNR) go to qso.freedv.org, as FreeDV does.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // ---- Stations on FreeDV Reporter ----
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing
        spacing: 4
        Label {
            text: qsTr("Stations (%1)").arg(Engine.freedvStations.length)
            color: Theme.textDim
            font.pixelSize: 14
            Layout.fillWidth: true
        }
        AppButton {
            text: qsTr("This band")
            checked: !Engine.freedvAllBands
            focusPolicy: Qt.NoFocus
            onClicked: Engine.freedvAllBands = false
        }
        AppButton {
            text: qsTr("All bands")
            checked: Engine.freedvAllBands
            focusPolicy: Qt.NoFocus
            onClicked: Engine.freedvAllBands = true
        }
    }
    Label {
        visible: Engine.freedvStations.length === 0
        Layout.fillWidth: true
        text: Engine.freedvAllBands ? qsTr("No stations on FreeDV Reporter right now.")
                                    : qsTr("No stations on this band right now; All bands shows the rest.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: Engine.freedvStations
        delegate: Rectangle {
            id: station
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: rows.implicitHeight + 12
            radius: Theme.radius
            // Transmitting now in red; this station outlined.
            color: modelData.tx ? Qt.alpha(Theme.transmit, 0.25) : Qt.alpha(Theme.text, 0.06)
            border.color: modelData.you ? Theme.accent : "transparent"
            border.width: modelData.you ? 1 : 0
            ColumnLayout {
                id: rows
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: station.modelData.callsign + (station.modelData.you ? " " + qsTr("(you)") : "")
                        color: Theme.text
                        font.pixelSize: 15
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: [station.modelData.grid,
                               station.modelData.km !== undefined ? qsTr("%1 km").arg(station.modelData.km) : ""]
                              .filter(s => s && s.length > 0).join(" · ")
                        color: Theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label {
                        text: station.modelData.mhz.toFixed(3)
                        color: Theme.text
                        font.pixelSize: 15
                        font.family: "monospace"
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: [station.modelData.tx ? qsTr("TX now") : (station.modelData.rxOnly ? qsTr("receive only") : ""),
                           station.modelData.mode || "",
                           station.modelData.heard ? qsTr("hears %1 (%2 dB)").arg(station.modelData.heard).arg(station.modelData.snr) : "",
                           station.modelData.message || ""]
                          .filter(s => s.length > 0).join(" · ")
                    color: station.modelData.tx ? Theme.transmit : Theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
            // A tap tunes there (RADE on for a RADE station).
            TapHandler {
                enabled: Engine.connectionState === "connected"
                onTapped: Engine.freedvTune(station.modelData.mhz, station.modelData.mode || "")
            }
        }
    }
}
