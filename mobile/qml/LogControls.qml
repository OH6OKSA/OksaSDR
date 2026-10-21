import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Quick QSO logging: type the call (or take the spot tapped last), check the
// lookup card (QRZ.com with an account, else the country from cty.dat), set
// the reports and Log. Frequency, band, mode, power, UTC and the own station
// come from the radio; POTA / WWFF references go into ADIF's SIG fields. The
// log is an ADIF file on this device.
ColumnLayout {
    id: logTab
    spacing: Theme.spacing / 2
    implicitWidth: 200

    property var defaults: Engine.qsoDefaults()
    property var worked: ({ worked: 0, dupe: false })
    property string timeOn: ""
    function refreshDefaults() {
        defaults = Engine.qsoDefaults()
        // Follows the mode until the operator types a report.
        if (!rstSent.activeFocus && !rstSent.edited) rstSent.text = defaults.rst || ""
        if (!rstRcvd.activeFocus && !rstRcvd.edited) rstRcvd.text = defaults.rst || ""
    }
    function reset() {
        callField.text = ""
        comment.text = ""
        theirRef.text = ""
        rstSent.edited = false
        rstRcvd.edited = false
        timeOn = ""
        refreshDefaults()
    }
    Timer {
        interval: 1000
        repeat: true
        running: logTab.visible
        onTriggered: logTab.refreshDefaults()
    }
    Component.onCompleted: refreshDefaults()

    // ---- Call ----
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: callField
            Layout.fillWidth: true
            placeholderText: qsTr("Callsign")
            font.pixelSize: 20
            font.family: "monospace"
            font.capitalization: Font.AllUppercase
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            onTextChanged: {
                if (text.length > 0 && logTab.timeOn.length === 0)
                    logTab.timeOn = Qt.formatDateTime(new Date(new Date().getTime() + new Date().getTimezoneOffset() * 60000), "HHmm")
                if (text.length === 0) logTab.timeOn = ""
                lookupDelay.restart()
            }
            onAccepted: logButton.clicked()
        }
        AppButton {
            visible: Engine.lastSpotCall.length > 0 && callField.text.toUpperCase() !== Engine.lastSpotCall
            text: Engine.lastSpotCall
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
            onClicked: callField.text = Engine.lastSpotCall
        }
    }
    Timer {
        id: lookupDelay
        interval: 600
        onTriggered: {
            Engine.lookupCallsign(callField.text)
            logTab.worked = Engine.workedInfo(callField.text)
        }
    }

    // ---- Lookup card ----
    Rectangle {
        visible: callField.text.length >= 3
        Layout.fillWidth: true
        implicitHeight: card.implicitHeight + Theme.spacing
        radius: Theme.radius
        color: Theme.surfaceRaised
        ColumnLayout {
            id: card
            anchors.fill: parent
            anchors.margins: Theme.spacing / 2
            spacing: 2
            readonly property var info: Engine.callLookup
            Label {
                Layout.fillWidth: true
                text: card.info.name ? card.info.name : (card.info.error ? card.info.error : (card.info.call ? "" : qsTr("Looking up…")))
                visible: text.length > 0
                color: Theme.text
                font.pixelSize: 16
                font.bold: !!card.info.name
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: [card.info.qth, card.info.qth ? "" : card.info.country].filter(s => s && s.length > 0).join(", ")
                visible: text.length > 0
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: {
                    const parts = []
                    if (card.info.grid) parts.push(card.info.grid)
                    if (card.info.distanceKm !== undefined) parts.push(card.info.distanceKm + " km " + card.info.bearing + "°")
                    if (card.info.continent) parts.push(card.info.continent)
                    if (card.info.cqZone) parts.push("CQ " + card.info.cqZone)
                    if (card.info.lotw) parts.push("LoTW")
                    if (card.info.eqsl) parts.push("eQSL")
                    if (card.info.mailQsl) parts.push(qsTr("QSL card"))
                    return parts.join(" · ")
                }
                visible: text.length > 0
                color: Theme.textDim
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: logTab.worked.worked > 0
                text: logTab.worked.dupe ? qsTr("DUPE on this band and mode · worked %n time(s)", "", logTab.worked.worked)
                                         : qsTr("Worked %n time(s) before", "", logTab.worked.worked)
                color: logTab.worked.dupe ? Theme.transmit : Theme.warning
                font.pixelSize: 13
                font.bold: logTab.worked.dupe
            }
            Label {
                Layout.fillWidth: true
                visible: !!card.info.prefixOnly && Engine.qrzUser.length === 0
                text: qsTr("Country from the prefix; a QRZ.com account (below) adds name and QTH.")
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---- Reports and what the radio gives ----
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Sent"); color: Theme.textDim; font.pixelSize: 15 }
        TextField {
            id: rstSent
            property bool edited: false
            Layout.preferredWidth: 64
            font.pixelSize: 16
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhDigitsOnly
            onTextEdited: edited = true
        }
        Label { text: qsTr("Rcvd"); color: Theme.textDim; font.pixelSize: 15 }
        TextField {
            id: rstRcvd
            property bool edited: false
            Layout.preferredWidth: 64
            font.pixelSize: 16
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhDigitsOnly
            onTextEdited: edited = true
        }
        Item { Layout.fillWidth: true }
    }
    Label {
        Layout.fillWidth: true
        text: {
            const d = logTab.defaults
            const parts = [(d.freq || 0).toFixed(3) + " MHz", d.mode, d.band]
            if (d.power > 0) parts.push(d.power + " W")
            parts.push((logTab.timeOn.length === 4 ? logTab.timeOn.slice(0, 2) + ":" + logTab.timeOn.slice(2) : d.utc) + " UTC")
            return parts.filter(s => s && String(s).length > 0).join(" · ")
        }
        color: Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    TextField {
        id: comment
        Layout.fillWidth: true
        placeholderText: qsTr("Comment")
        font.pixelSize: 15
    }

    // ---- POTA / WWFF ----
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Their park"); color: Theme.textDim; font.pixelSize: 14; Layout.preferredWidth: 84 }
        Repeater {
            model: ["", "POTA", "WWFF"]
            delegate: AppButton {
                required property string modelData
                text: modelData.length > 0 ? modelData : qsTr("None")
                checked: theirSig.value === modelData
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                onClicked: theirSig.value = modelData
            }
        }
    }
    QtObject { id: theirSig; property string value: "" }
    TextField {
        id: theirRef
        visible: theirSig.value.length > 0
        Layout.fillWidth: true
        placeholderText: theirSig.value === "POTA" ? qsTr("Park, e.g. US-1234") : qsTr("Reference, e.g. OHFF-0001")
        font.pixelSize: 15
        inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("My park"); color: Theme.textDim; font.pixelSize: 14; Layout.preferredWidth: 84 }
        Repeater {
            model: ["", "POTA", "WWFF"]
            delegate: AppButton {
                required property string modelData
                text: modelData.length > 0 ? modelData : qsTr("None")
                checked: mySig.value === modelData
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                onClicked: mySig.value = modelData
            }
        }
    }
    // Kept while activating: the own park stays for every contact.
    QtObject { id: mySig; property string value: "" }
    TextField {
        id: myRef
        visible: mySig.value.length > 0
        Layout.fillWidth: true
        placeholderText: qsTr("My reference")
        font.pixelSize: 15
        inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
    }

    AppButton {
        id: logButton
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchTarget
        text: qsTr("Log %1").arg(callField.text.toUpperCase())
        enabled: callField.text.trim().length >= 3
        focusPolicy: Qt.NoFocus
        onClicked: {
            const ok = Engine.logQso({
                call: callField.text, rstSent: rstSent.text, rstRcvd: rstRcvd.text,
                comment: comment.text, timeOn: logTab.timeOn,
                sig: theirSig.value, sigInfo: theirSig.value.length > 0 ? theirRef.text : "",
                mySig: mySig.value, mySigInfo: mySig.value.length > 0 ? myRef.text : ""
            })
            if (ok) logTab.reset()
        }
    }

    // ---- Recent contacts ----
    Label {
        text: Engine.qsoCount > 0 ? qsTr("Log: %n contact(s)", "", Engine.qsoCount) : qsTr("No contacts logged yet.")
        color: Theme.textDim
        font.pixelSize: 14
    }
    AppButton {
        visible: Engine.qsoCount > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchTarget
        text: qsTr("Share the log (ADIF)")
        focusPolicy: Qt.NoFocus
        onClicked: Engine.shareQsoLog()
    }
    Repeater {
        model: Engine.recentQsos.slice(0, 10)
        delegate: Label {
            required property var modelData
            Layout.fillWidth: true
            text: (modelData.TIME_ON || "").slice(0, 2) + ":" + (modelData.TIME_ON || "").slice(2, 4) + "  "
                  + modelData.CALL + "  " + (modelData.BAND || "") + " " + (modelData.SUBMODE || modelData.MODE || "")
                  + "  " + (modelData.RST_SENT || "") + "/" + (modelData.RST_RCVD || "")
                  + (modelData.SIG_INFO ? "  " + modelData.SIG_INFO : "")
            color: Theme.text
            font.pixelSize: 14
            font.family: "monospace"
            elide: Text.ElideRight
        }
    }

    // ---- Station and QRZ.com ----
    Label { text: qsTr("Station"); color: Theme.textDim; font.pixelSize: 14; Layout.topMargin: Theme.spacing }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: (logTab.defaults.myCall || qsTr("(radio callsign not set)"))
            color: Theme.text
            font.pixelSize: 15
            Layout.preferredWidth: 120
        }
        TextField {
            id: gridField
            Layout.fillWidth: true
            placeholderText: qsTr("My grid, e.g. KP22")
            font.pixelSize: 15
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            text: Engine.myGrid
            onEditingFinished: Engine.myGrid = text
        }
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: qrzUserField
            Layout.fillWidth: true
            placeholderText: qsTr("QRZ.com user")
            font.pixelSize: 15
            text: Engine.qrzUser
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        }
        TextField {
            id: qrzPassField
            Layout.fillWidth: true
            placeholderText: Engine.qrzUser.length > 0 ? qsTr("password (saved)") : qsTr("password")
            font.pixelSize: 15
            echoMode: TextInput.Password
        }
        AppButton {
            text: qsTr("Save")
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.setQrzAccount(qrzUserField.text, qrzPassField.text)
                qrzPassField.text = ""
            }
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("QRZ.com's full records (name, QTH, grid) need their XML subscription; without it the card shows the country from the prefix. The password stays in this device's secure store.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
