import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
            Component.onCompleted: text = Engine.workedCall
            onTextChanged: {
                Engine.workedCall = text
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
    // Calls the CW decoder heard (after DE, or twice), newest first: a tap
    // puts one in the Call field.
    RowLayout {
        id: cwCallRow
        Layout.fillWidth: true
        visible: Engine.cwDecoderEnabled && cwCallRepeater.count > 0
        spacing: 4
        Label {
            text: qsTr("Heard in CW")
            color: Theme.textDim
            font.pixelSize: 12
        }
        Repeater {
            id: cwCallRepeater
            // Two where three long portable calls would not fit (the iPad's pinned
            // panel).
            model: Engine.cwCalls.filter(c => c !== callField.text.toUpperCase() && c !== Engine.lastSpotCall)
                   .slice(0, cwCallRow.width >= 420 ? 3 : 2)
            delegate: AppButton {
                required property string modelData
                objectName: "cwCall_" + modelData
                text: modelData
                font.family: "monospace"
                focusPolicy: Qt.NoFocus
                Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
                onClicked: callField.text = modelData
            }
        }
        Item { Layout.fillWidth: true }
    }
    // The CW tab's pick (and another copy of this tab) lands here too.
    Connections {
        target: Engine
        function onWorkedCallChanged() {
            if (callField.text.toUpperCase() !== Engine.workedCall)
                callField.text = Engine.workedCall
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
    Label {
        Layout.fillWidth: true
        text: qsTr("Log file: %1").arg(Engine.qsoLogLocation)
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    // Contacts from another logbook (or another device's OksaSDR log): an
    // ADIF file; contacts already in the log are skipped.
    AppButton {
        objectName: "importAdif"
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchTarget
        text: qsTr("Import contacts (ADIF)")
        focusPolicy: Qt.NoFocus
        onClicked: importDialog.open()
    }
    Label {
        id: importResult
        Layout.fillWidth: true
        visible: text.length > 0
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import contacts (ADIF)")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("ADIF files (*.adi *.adif *.ADI)"), qsTr("All files (*)")]
        onAccepted: {
            importResult.text = qsTr("Importing…")
            Engine.importAdif(selectedFile)
        }
    }
    Connections {
        target: Engine
        function onAdifImported(r) {
            importResult.text = r.error ? r.error
                : qsTr("Imported %1 contact(s); %2 already in the log.").arg(r.imported).arg(r.duplicates)
        }
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
    // One row where both hints fit beside Save, else the user over the
    // password and Save (a narrow panel cut "password (saved)" short).
    GridLayout {
        id: qrzRow
        Layout.fillWidth: true
        readonly property bool oneRow: (width - qrzSave.implicitWidth - 2 * columnSpacing) / 2
                                       >= Math.max(qrzUserHint.advanceWidth, qrzPassHint.advanceWidth + 40) + 24
        columns: oneRow ? 3 : 2
        TextMetrics { id: qrzUserHint; font: qrzUserField.font; text: qrzUserField.placeholderText }
        TextMetrics { id: qrzPassHint; font: qrzPassField.font; text: qsTr("password (saved)") }
        TextField {
            id: qrzUserField
            Layout.fillWidth: true
            Layout.columnSpan: qrzRow.oneRow ? 1 : 2
            placeholderText: qsTr("QRZ.com user")
            font.pixelSize: 15
            text: Engine.qrzUser
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        }
        PasswordField {
            id: qrzPassField
            Layout.fillWidth: true
            placeholderText: Engine.qrzUser.length > 0 ? qsTr("password (saved)") : qsTr("password")
            font.pixelSize: 15
        }
        AppButton {
            id: qrzSave
            text: qsTr("Save")
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.setQrzAccount(qrzUserField.text, qrzPassField.text)
                qrzPassField.text = ""
            }
        }
    }
    Label {
        objectName: "qrzStatus"
        Layout.fillWidth: true
        visible: Engine.qrzStatus.length > 0
        text: Engine.qrzStatus
        color: Engine.qrzStatusKind === "ok" ? "#00cc66"
             : Engine.qrzStatusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("QRZ.com's full records (name, QTH, grid) need their XML subscription; without it the card shows the country from the prefix. The password stays in this device's secure store.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // ---- Wavelog / Cloudlog: each logged QSO to the online logbook ----
    Label { text: qsTr("Wavelog / Cloudlog"); color: Theme.textDim; font.pixelSize: 14; Layout.topMargin: Theme.spacing }
    TextField {
        id: onlineUrlField
        objectName: "onlineLogUrl"
        Layout.fillWidth: true
        placeholderText: qsTr("Logbook address, e.g. https://log.example.com")
        font.pixelSize: 15
        text: Engine.onlineLog.url || ""
        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
    }
    // One row where the key's hint fits beside the station ID and Save, else
    // the key over them (a narrow panel cut it to "API key (sa…").
    GridLayout {
        id: onlineKeyRow
        Layout.fillWidth: true
        readonly property bool oneRow: width - 90 - onlineSave.implicitWidth - 2 * columnSpacing
                                       >= onlineKeyHint.advanceWidth + 40 + 24
        columns: oneRow ? 3 : 2
        TextMetrics { id: onlineKeyHint; font: onlineKeyField.font; text: qsTr("API key (read & write)") }
        PasswordField {
            id: onlineKeyField
            Layout.fillWidth: true
            Layout.columnSpan: onlineKeyRow.oneRow ? 1 : 2
            placeholderText: Engine.onlineLog.hasKey ? qsTr("API key (saved)") : qsTr("API key (read & write)")
            font.pixelSize: 15
        }
        TextField {
            id: onlineStationField
            Layout.preferredWidth: 90
            Layout.fillWidth: !onlineKeyRow.oneRow
            placeholderText: qsTr("Station ID")
            font.pixelSize: 15
            text: Engine.onlineLog.stationId || ""
            inputMethodHints: Qt.ImhDigitsOnly
        }
        AppButton {
            id: onlineSave
            text: qsTr("Save")
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.setOnlineLog(onlineUrlField.text, onlineKeyField.text, onlineStationField.text)
                onlineKeyField.text = ""
            }
        }
    }
    Label {
        objectName: "onlineLogStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: (Engine.onlineLog.status || "")
              + (Engine.onlineLog.pending > 0 && (Engine.onlineLog.status || "").indexOf("wait") < 0
                 ? "  (" + qsTr("%1 waiting").arg(Engine.onlineLog.pending) + ")" : "")
        color: Engine.onlineLog.statusKind === "ok" ? "#00cc66"
             : Engine.onlineLog.statusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Every QSO you log (and FT8 / FT4 logs) also goes to your Wavelog or Cloudlog logbook. The station ID is the station location's number there (Station Locations). QSOs that cannot be sent wait and go later. Clear the address to stop. The key stays in this device's secure store.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    // ---- Logger32 on the operator's PC: each logged QSO over TCP (Juho
    // Kontio) ----
    Label { text: qsTr("Logger32"); color: Theme.textDim; font.pixelSize: 14; Layout.topMargin: Theme.spacing }
    // One row where the hint fits beside Save, else Save under the field.
    GridLayout {
        id: logger32Row
        Layout.fillWidth: true
        columns: width - logger32Save.implicitWidth - columnSpacing >= logger32Hint.advanceWidth + 24 ? 2 : 1
        TextMetrics { id: logger32Hint; font: logger32Field.font; text: logger32Field.placeholderText }
        TextField {
            id: logger32Field
            objectName: "logger32Host"
            Layout.fillWidth: true
            placeholderText: qsTr("PC address, e.g. 192.168.1.10:52001")
            font.pixelSize: 15
            text: Engine.logger32.host || ""
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        }
        AppButton {
            id: logger32Save
            Layout.alignment: Qt.AlignRight
            text: qsTr("Save")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.setLogger32(logger32Field.text)
        }
    }
    Label {
        objectName: "logger32Status"
        Layout.fillWidth: true
        visible: text.length > 0
        text: (Engine.logger32.status || "")
              + (Engine.logger32.pending > 0 && (Engine.logger32.status || "").indexOf("wait") < 0
                 ? "  (" + qsTr("%1 waiting").arg(Engine.logger32.pending) + ")" : "")
        color: Engine.logger32.statusKind === "ok" ? "#00cc66"
             : Engine.logger32.statusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Every QSO you log (and FT8 / FT4 logs) also goes to Logger32 on your PC, over its TCP port (52001 unless you give another after a colon). Away from home this needs your VPN to reach the PC, and Windows' firewall must let the port in. QSOs that cannot be sent wait and go later. Clear the address to stop.")
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
