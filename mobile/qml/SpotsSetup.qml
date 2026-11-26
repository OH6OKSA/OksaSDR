import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Setup › Spots (roadmap 131): where this device reads spots from and puts
// them on the radio, the DX cluster and the park programs, moved here from
// the View tab (the Spots tab keeps what the display shows since build
// 203). Every program on the
// radio shows the spots this device sends.
ColumnLayout {
    spacing: Theme.spacing / 2
    Label { text: qsTr("DX cluster"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16) }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            objectName: "dxClusterToggle"
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
            objectName: "dxClusterCallsign"
            Accessible.name: qsTr("DX cluster callsign")
            Layout.fillWidth: true
            // Sized to what it holds, about 16 characters (roadmap 158)
            Layout.maximumWidth: Math.ceil(16 * font.pixelSize * 0.62) + leftPadding + rightPadding
            placeholderText: qsTr("Callsign (the radio's if empty)")
            text: Engine.dxCluster.callsignFromRadio ? "" : (Engine.dxCluster.callsign || "")
            font.pixelSize: Theme.px(14)
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            onEditingFinished: Engine.setDxCluster("callsign", text)
        }
        AppButton {
            // "Auto connects with the radio" (the hint below).
            Accessible.name: qsTr("%1: %2").arg(qsTr("DX cluster")).arg(qsTr("Auto connect"))
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
                Accessible.name: qsTr("%1: %2").arg(qsTr("Park spots")).arg(text)
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
    // ---- RBN and PSK Reporter, Heard me (roadmap 136) ----
    Label { text: qsTr("RBN and PSK Reporter"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16); Layout.topMargin: Theme.spacing }
    Repeater {
        model: [{ key: "rbn", label: qsTr("RBN skimmer spots") },
                { key: "psk", label: qsTr("PSK Reporter spots") },
                { key: "heardMe", label: qsTr("Heard me") }]
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: parent.modelData.label
                color: Theme.text
                font.pixelSize: Theme.px(15)
                wrapMode: Text.WordWrap
            }
            AppButton {
                objectName: "spotFeed_" + parent.modelData.key
                Accessible.name: qsTr("%1: %2").arg(parent.modelData.label).arg(text)
                text: checked ? qsTr("On") : qsTr("Off")
                checked: !!Engine.spotFeeds[parent.modelData.key]
                Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                focusPolicy: Qt.NoFocus
                onClicked: Engine.setSpotFeed(parent.modelData.key, !checked)
            }
        }
    }
    Label {
        objectName: "spotFeedsStatus"
        Layout.fillWidth: true
        readonly property var f: Engine.spotFeeds
        visible: text.length > 0
        text: [f.rbn ? (f.clusterOn ? qsTr("RBN: through the DX cluster, %1 skimmer spots so far.").arg(f.rbnSpots)
                                     : qsTr("RBN: comes through the DX cluster, which is off.")) : "",
               (f.psk || f.heardMe) ? (f.pskState === "connected" ? qsTr("PSK Reporter: live, %1 reports so far.").arg(f.pskReports)
                                       : f.pskState === "off" ? qsTr("PSK Reporter: starts with the radio.")
                                       : qsTr("PSK Reporter: connecting…") + (f.pskError ? " (" + f.pskError + ")" : "")) : ""]
              .filter(t => t.length > 0).join(" ")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("RBN: the Reverse Beacon Network's CW, RTTY and PSK skimmers, through your DX cluster (DX Spider; as RBN asks), from skimmers near you (Spots › From: your country, else your continent). PSK Reporter: digital stations heard by receivers in your country, live. Heard me: who hears your CQ, by band, from both. All go to the radio like the cluster's spots.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    // 2D waterfall or the 3D stacked trace.

    // ---- Alerts (roadmap 132): a call you hunt, or a DXCC you need ----
    Label { text: qsTr("Spot alerts"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16); Layout.topMargin: Theme.spacing }
    readonly property var al: Engine.spotAlertSettings
    Label { text: qsTr("Watch list"); color: Theme.textDim; font.pixelSize: Theme.px(14) }
    TextField {
        objectName: "spotWatch"
        Accessible.name: qsTr("Watch list")
        Layout.fillWidth: true
        placeholderText: qsTr("Calls: VK9XY, 3Y0* (* for any)")
        text: al.watch || ""
        font.pixelSize: Theme.px(14)
        inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
        onEditingFinished: if (text !== (al.watch || "")) Engine.setSpotAlertSetting("watch", text)
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Needed DXCC"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Repeater {
            model: [{ key: "off", label: qsTr("Off") }, { key: "band", label: qsTr("On the band") }, { key: "any", label: qsTr("New") }]
            delegate: AppButton {
                required property var modelData
                objectName: "spotDxcc_" + modelData.key
                Accessible.name: qsTr("%1: %2").arg(qsTr("Needed DXCC")).arg(text)
                text: modelData.label
                checked: (al.dxcc || "off") === modelData.key
                focusPolicy: Qt.NoFocus
                onClicked: Engine.setSpotAlertSetting("dxcc", modelData.key)
            }
        }
    }
    Label {
        objectName: "spotDxccHint"
        Layout.fillWidth: true
        text: qsTr("On the band: a country not yet in your log on the spot's band (all-time new ones too). New: only a country not in your log on any band. By this device's log: worked, not confirmed.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Sound"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.fillWidth: true }
        AppButton {
            objectName: "spotAlertSound"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Sound")).arg(text)
            text: al.sound ? qsTr("On") : qsTr("Off")
            checked: !!al.sound
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.setSpotAlertSetting("sound", !al.sound)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Notify while the app is in the background"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.fillWidth: true; wrapMode: Text.WordWrap }
        AppButton {
            objectName: "spotAlertNotify"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Notify while the app is in the background")).arg(text)
            text: al.notify ? qsTr("On") : qsTr("Off")
            checked: !!al.notify
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.setSpotAlertSetting("notify", !al.notify)
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("A spot of a call on the watch list, or of a DXCC not in your log (on its band, or not at all), pops up in the middle of the screen with Tune, plays the sound, and with the app in the background comes as a notification. Each call alerts once per band in 30 minutes. Only spots from where the Spots tab's From allows (anywhere, %1, %2) alert.")
              .arg(al.ownContinent || qsTr("your continent")).arg(al.ownPrefix || qsTr("your country"))
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
}
