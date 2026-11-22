import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Spots (roadmap 134, the user: "Pitäisikö tuo spots olla ihan oma controls
// painike kun vievät paljon tilaa"): which spots the display shows, by where
// they came from, mode, age and the spotter's place, and the way to the
// sources this device reads (Setup › Spots). Until build 203 on the View tab.
ColumnLayout {
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Spots on the display (roadmap 131): every spot is the radio's, from
    // every program on it; shown by where it came from: Cluster (a DX
    // cluster's), POTA, WWFF, Other (SmartSDR's, another program's). The
    // sources this device reads are set in Setup › Spots (Spot sources…).
    Label { text: qsTr("Spots shown"); color: Theme.textDim; font.pixelSize: Theme.px(16) }
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: [{ key: "cluster", label: qsTr("Cluster") }, { key: "pota", label: "POTA" },
                    { key: "wwff", label: "WWFF" }, { key: "other", label: qsTr("Other") }]
            delegate: AppButton {
                id: spotsShowButton
                required property var modelData
                // How many the radio has now: 0 says nothing comes from it.
                readonly property int count: Engine.spotCounts[modelData.key] || 0
                objectName: "spotsShow_" + modelData.key
                text: modelData.label + " " + count
                // The count on a second, smaller line inside the button (the
                // user, build 202 on the iPad: "Cluster 182" in the pinned
                // panel's narrow buttons read "Cluster …"); the button keeps
                // its size, the two lines fit its height at every scale.
                topPadding: 2
                bottomPadding: 2
                contentItem: Item {
                    implicitWidth: Math.max(spotsName.implicitWidth, spotsCount.implicitWidth)
                    implicitHeight: spotsName.implicitHeight + spotsCount.implicitHeight
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        Text {
                            id: spotsName
                            width: parent.width
                            text: spotsShowButton.modelData.label
                            font.pixelSize: Theme.buttonFont
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            color: spotsShowButton.checked ? Theme.buttonTextOn : Theme.buttonText
                            opacity: spotsShowButton.enabled ? 1.0 : 0.4
                        }
                        Text {
                            id: spotsCount
                            objectName: "spotsCount_" + spotsShowButton.modelData.key
                            width: parent.width
                            text: spotsShowButton.count
                            font.pixelSize: Theme.smallFont
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            color: spotsShowButton.checked ? Theme.buttonTextOn : Theme.buttonText
                            opacity: spotsShowButton.enabled ? 0.85 : 0.35
                        }
                    }
                }
                checked: Engine.spotSources.indexOf(modelData.key) >= 0
                Accessible.name: (checked ? qsTr("%1 spots, shown") : qsTr("%1 spots, hidden")).arg(modelData.label)
                                 + ", " + qsTr("%n on the radio", "", count)
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.setSpotSourceShown(modelData.key, !checked)
            }
        }
    }
    // What Cluster and Other hold (the user, build 201's preview: "Other"
    // has to say what it is): every program on the radio puts spots there,
    // each with its source.
    // A chosen source that brings nothing: none on the radio and not read
    // here; says where it is turned on (the user: someone may pick them
    // here without knowing that nothing comes until Spot sources has them).
    Label {
        objectName: "spotsNoneFrom"
        readonly property var missing: {
            const out = []
            const c = Engine.spotCounts
            const shown = Engine.spotSources
            if (shown.indexOf("cluster") >= 0 && !(c.cluster > 0) && Engine.dxCluster.state !== "connected") out.push(qsTr("Cluster"))
            if (shown.indexOf("pota") >= 0 && !(c.pota > 0) && !Engine.parkSpots.pota) out.push("POTA")
            if (shown.indexOf("wwff") >= 0 && !(c.wwff > 0) && !Engine.parkSpots.wwff) out.push("WWFF")
            return out
        }
        visible: missing.length > 0
        Layout.fillWidth: true
        text: qsTr("No spots from %1: this device does not read them; turn them on in Spot sources… below.").arg(missing.join(", "))
        color: Theme.warning
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    Label {
        objectName: "spotsShownHint"
        Layout.fillWidth: true
        text: qsTr("Spots are the radio's, from every program on it. Cluster: a DX cluster's. Other: the rest, such as RBN / CW Skimmer, WSJT-X and FreeDV Reporter from the desktop AetherSDR, N1MM+, Log4OM or SpotCollector.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.showSpots
        Repeater {
            model: [{ key: "all", label: qsTr("All") }, { key: "cw", label: qsTr("CW") },
                    { key: "phone", label: qsTr("Phone") }, { key: "digital", label: qsTr("Digital") }]
            delegate: AppButton {
                required property var modelData
                text: modelData.label
                checked: Engine.spotModes === modelData.key
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: Engine.spotModes = modelData.key
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.showSpots
        Repeater {
            model: [0, 5, 15, 30]
            delegate: AppButton {
                required property int modelData
                text: modelData === 0 ? qsTr("Any age") : qsTr("%1 min").arg(modelData)
                checked: Engine.spotMaxAgeMin === modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: Engine.spotMaxAgeMin = modelData
            }
        }
    }
    // Spotted from (roadmap 132): a spot given near you is one you can hear.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.showSpots
        Label { text: qsTr("From"); color: Theme.textDim; font.pixelSize: Theme.px(14) }
        Repeater {
            model: [{ key: "anywhere", label: qsTr("Anywhere") },
                    { key: "continent", label: Engine.spotAlertSettings.ownContinent || qsTr("Continent") },
                    { key: "country", label: Engine.spotAlertSettings.ownPrefix || qsTr("Country") }]
            delegate: AppButton {
                required property var modelData
                objectName: "spotFrom_" + modelData.key
                text: modelData.label
                checked: Engine.spotFrom === modelData.key
                Accessible.name: modelData.key === "anywhere" ? qsTr("Spotted anywhere")
                                 : modelData.key === "continent" ? qsTr("Spotted on your continent, %1").arg(modelData.label)
                                 : qsTr("Spotted in your country, %1").arg(Engine.spotAlertSettings.ownCountry || modelData.label)
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: Engine.spotFrom = modelData.key
            }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: !Engine.showSpots
        text: qsTr("No spots shown: pick where from above.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    AppButton {
        objectName: "openSpotsSetup"
        Layout.fillWidth: true
        // What this device reads, all three (the user: the button said only
        // the cluster's state).
        text: qsTr("Spot sources: DX cluster %1 · POTA %2 · WWFF %3…")
              .arg(Engine.dxCluster.state === "connected" ? "✓" : (Engine.dxCluster.state === "connecting" ? "…" : "–"))
              .arg(Engine.parkSpots.pota ? "✓" : "–").arg(Engine.parkSpots.wwff ? "✓" : "–")
        focusPolicy: Qt.NoFocus
        onClicked: Engine.openSetup(12)
    }
}
