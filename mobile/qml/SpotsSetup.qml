import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Setup › Spots (roadmap 131): where this device reads spots from and puts
// them on the radio, the DX cluster and the park programs, moved here from
// the View tab (which keeps what the display shows). Every program on the
// radio shows the spots this device sends.
ColumnLayout {
    spacing: Theme.spacing / 2
    Label { text: qsTr("DX cluster"); color: Theme.textDim; font.pixelSize: Theme.px(16) }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            readonly property string state: Engine.dxCluster.state || "off"
            text: state === "off" ? qsTr("DX cluster") : state === "connected" ? qsTr("DX cluster ✓") : qsTr("Connecting…")
            checked: state !== "off"
            enabled: Engine.connectionState === "connected"
            Layout.preferredWidth: 140
            onClicked: state === "off" ? Engine.connectDxCluster() : Engine.disconnectDxCluster()
        }
        Label {
            Layout.fillWidth: true
            text: (Engine.dxCluster.host || "") + ":" + (Engine.dxCluster.port || "")
            color: Theme.textDim
            font.pixelSize: Theme.px(13)
            elide: Text.ElideRight
        }
    }
    // Whether the cluster is on, as the logbook's and PSK Reporter's lines.
    Label {
        objectName: "dxClusterStatus"
        Layout.fillWidth: true
        visible: Engine.connectionState === "connected" || Engine.dxClusterStatusKind === "failed"
        text: Engine.dxClusterStatus
        color: Engine.dxClusterStatusKind === "ok" ? Theme.good
             : Engine.dxClusterStatusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: dxHost
            Accessible.name: qsTr("DX cluster server")
            Layout.fillWidth: true
            placeholderText: qsTr("Cluster server")
            text: Engine.dxCluster.host || ""
            font.pixelSize: Theme.px(14)
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
            onEditingFinished: if (text !== Engine.dxCluster.host) Engine.setDxCluster("host", text)
        }
        TextField {
            Accessible.name: qsTr("DX cluster port")
            Layout.preferredWidth: 72
            placeholderText: qsTr("Port")
            text: Engine.dxCluster.port || ""
            font.pixelSize: Theme.px(14)
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1; top: 65535 }
            onEditingFinished: Engine.setDxCluster("port", Number(text))
        }
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            Accessible.name: qsTr("DX cluster callsign")
            Layout.fillWidth: true
            placeholderText: qsTr("Callsign (the radio's if empty)")
            text: Engine.dxCluster.callsignFromRadio ? "" : (Engine.dxCluster.callsign || "")
            font.pixelSize: Theme.px(14)
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            onEditingFinished: Engine.setDxCluster("callsign", text)
        }
        AppButton {
            text: qsTr("Auto")
            checked: !!Engine.dxCluster.autoConnect
            leftPadding: 10
            rightPadding: 10
            onClicked: Engine.setDxCluster("autoConnect", !checked)
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Logs in as %1. Spots go to the radio, so every program on it shows them; tap one on the display to tune to it. Auto connects with the radio.")
              .arg(Engine.dxCluster.callsign || qsTr("(no callsign)"))
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    // POTA and WWFF activators' spots, read every minute (roadmap 109).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Park spots"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        Repeater {
            model: ["POTA", "WWFF"]
            delegate: AppButton {
                required property string modelData
                objectName: "parkSpots" + modelData
                text: modelData
                checked: modelData === "POTA" ? !!Engine.parkSpots.pota : !!Engine.parkSpots.wwff
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                focusPolicy: Qt.NoFocus
                onClicked: Engine.setParkSpots(modelData, !checked)
            }
        }
    }
    Label {
        objectName: "parkSpotsStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: Engine.parkSpots.status || ""
        color: Engine.parkSpots.ok ? Theme.textDim : Theme.transmit
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Activators from pota.app and wwff.co, read every minute, go to the radio like the cluster's spots. Tap one to tune to it; the Log tab then offers their call with their park.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    // 2D waterfall or the 3D stacked trace.
}
