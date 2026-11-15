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

    // The edit dialog's rows (roadmap 78).
    component EditLabel: Label { color: Theme.textDim; font.pixelSize: Theme.px(14) }
    component EditField: TextField {
        Layout.fillWidth: true
        font.pixelSize: Theme.px(15)
        inputMethodHints: Qt.ImhNoPredictiveText
    }

    property var defaults: Engine.qsoDefaults()
    property var worked: ({ worked: 0, dupe: false })
    property string timeOn: ""
    function refreshDefaults() {
        defaults = Engine.qsoDefaults()
        // Follows the mode until the operator types a report.
        if (!rstSent.activeFocus && !rstSent.edited) rstSent.text = defaults.rstSent || defaults.rst || ""
        if (!rstRcvd.activeFocus && !rstRcvd.edited) rstRcvd.text = defaults.rstRcvd || defaults.rst || ""
    }
    // The keyboard back to the page (Space is PTT again on an iPad), also
    // when Return in a field logged it or a dialog's field had it.
    function dropKeyboard() {
        const f = logTab.Window.activeFocusItem
        if (f && f.cursorPosition !== undefined) f.focus = false
    }
    function reset() {
        callField.text = ""
        comment.text = ""
        nameField.text = ""
        nameField.edited = false
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
            objectName: "logCall"
            Accessible.name: qsTr("Callsign")
            Layout.fillWidth: true
            placeholderText: qsTr("Callsign")
            font.pixelSize: Theme.px(20)
            font.family: "monospace"
            font.capitalization: Font.AllUppercase
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            Component.onCompleted: text = Engine.workedCall
            onTextChanged: {
                Engine.workedCall = text
                if (text.length > 0 && logTab.timeOn.length === 0)
                    logTab.timeOn = Qt.formatDateTime(new Date(new Date().getTime() + new Date().getTimezoneOffset() * 60000), "HHmm")
                if (text.length === 0) logTab.timeOn = ""
                // Another call: the last one's looked-up name goes.
                if (!nameField.edited) nameField.text = ""
                lookupDelay.restart()
            }
            onAccepted: logButton.clicked()
            rightPadding: clearCall.visible ? clearCall.width + 6 : leftPadding
            // × clears the call (roadmap 76, Christoph Steinert DM5HF); a
            // press inside the field keeps the keyboard where it is.
            AppButton {
                id: clearCall
                objectName: "logCallClear"
                visible: callField.text.length > 0
                text: "×"
                Accessible.name: qsTr("Clear the callsign")
                focusPolicy: Qt.NoFocus
                anchors.right: parent.right
                anchors.rightMargin: 3
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height - 6
                width: height
                padding: 0
                onClicked: callField.text = ""
            }
        }
        VoiceButton {
            objectName: "logVoice"
            Layout.preferredHeight: callField.height
        }
        AppButton {
            visible: Engine.lastSpotCall.length > 0 && callField.text.toUpperCase() !== Engine.lastSpotCall
            text: Engine.lastSpotCall
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: Math.max(implicitWidth + 12, 76)
            onClicked: callField.text = Engine.lastSpotCall
        }
    }
    // What the voice button heard, or why it did not listen (roadmap 79).
    Label {
        objectName: "logVoiceStatus"
        visible: Engine.voiceInputOffered && Engine.voiceInputStatus.length > 0
        Layout.fillWidth: true
        text: Engine.voiceInputStatus
        color: Engine.voiceInputState === "idle" && !Engine.voiceInputStatus.startsWith(qsTr("Heard")) ? Theme.warning : Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }
    Connections {
        target: Engine
        function onVoiceHeard(call, report) {
            callField.text = call
            if (report.length > 0) {
                rstSent.text = report
                rstSent.edited = true
            }
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
            font.pixelSize: Theme.px(12)
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
                font.pixelSize: Theme.px(16)
                font.bold: !!card.info.name
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: [card.info.qth, card.info.qth ? "" : card.info.country].filter(s => s && s.length > 0).join(", ")
                visible: text.length > 0
                color: Theme.text
                font.pixelSize: Theme.px(14)
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
                font.pixelSize: Theme.px(13)
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: logTab.worked.worked > 0
                text: logTab.worked.dupe ? qsTr("DUPE on this band and mode · worked %n time(s)", "", logTab.worked.worked)
                                         : qsTr("Worked %n time(s) before", "", logTab.worked.worked)
                color: logTab.worked.dupe ? Theme.transmit : Theme.warning
                font.pixelSize: Theme.px(13)
                font.bold: logTab.worked.dupe
            }
            Label {
                Layout.fillWidth: true
                visible: !!card.info.prefixOnly && Engine.qrzUser.length === 0
                text: qsTr("Country from the prefix; a QRZ.com account (below) adds name and QTH.")
                color: Theme.textDim
                font.pixelSize: Theme.px(12)
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---- Reports and what the radio gives ----
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Sent"); color: Theme.textDim; font.pixelSize: Theme.px(15) }
        TextField {
            id: rstSent
            Accessible.name: qsTr("Report sent")
            property bool edited: false
            Layout.preferredWidth: 64
            font.pixelSize: Theme.px(16)
            horizontalAlignment: TextInput.AlignHCenter
            inputMethodHints: Qt.ImhDigitsOnly
            onTextEdited: edited = true
        }
        Label { text: qsTr("Rcvd"); color: Theme.textDim; font.pixelSize: Theme.px(15) }
        TextField {
            id: rstRcvd
            Accessible.name: qsTr("Report received")
            property bool edited: false
            Layout.preferredWidth: 64
            font.pixelSize: Theme.px(16)
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
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    // Comment and the station's Name (roadmap 77, Christoph Steinert DM5HF):
    // Name takes the lookup's first name unless typed into; it is logged as
    // ADIF NAME over the lookup's full name.
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: comment
            Accessible.name: qsTr("Comment")
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 2
            Layout.preferredWidth: 2
            placeholderText: qsTr("Comment")
            font.pixelSize: Theme.px(15)
        }
        TextField {
            id: nameField
            objectName: "logName"
            Accessible.name: qsTr("Name")
            property bool edited: false
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 1
            Layout.preferredWidth: 1
            placeholderText: qsTr("Name")
            font.pixelSize: Theme.px(15)
            onTextEdited: edited = text.length > 0
        }
    }
    // The lookup's first name for this call, unless the operator typed one.
    Connections {
        target: Engine
        function onCallLookupChanged() {
            const info = Engine.callLookup
            if (nameField.edited || !info || (info.call || "").toUpperCase() !== callField.text.trim().toUpperCase())
                return
            nameField.text = info.firstName || ""
        }
    }

    // ---- POTA / WWFF ----
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Their park"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.preferredWidth: 84 }
        Repeater {
            model: ["", "POTA", "WWFF"]
            delegate: AppButton {
                required property string modelData
                objectName: "logTheirSig_" + (modelData || "None")
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
        objectName: "logTheirRef"
        Accessible.name: qsTr("Their reference")
        visible: theirSig.value.length > 0
        Layout.fillWidth: true
        placeholderText: theirSig.value === "POTA" ? qsTr("Park, e.g. US-1234") : qsTr("Reference, e.g. OHFF-0001")
        font.pixelSize: Theme.px(15)
        inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("My park"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.preferredWidth: 84 }
        Repeater {
            model: ["", "POTA", "WWFF"]
            delegate: AppButton {
                required property string modelData
                text: modelData.length > 0 ? modelData : qsTr("None")
                objectName: "logMySig_" + (modelData.length > 0 ? modelData : "None")
                checked: Engine.mySig === modelData
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                onClicked: Engine.mySig = modelData
            }
        }
    }
    // Kept while activating: the own park stays for every contact. In the
    // engine, so Big buttons' log and the POTA self-spot read the same.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.mySig.length > 0
        TextField {
            id: myRef
            objectName: "logMyRef"
            Accessible.name: qsTr("My reference")
            Layout.fillWidth: true
            text: Engine.myRef
            onTextEdited: Engine.myRef = text
            placeholderText: qsTr("My reference")
            font.pixelSize: Theme.px(15)
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
        }
        // A self-spot on pota.app (roadmap 67 b), asked first: a public post.
        AppButton {
            objectName: "logSelfSpot"
            visible: Engine.mySig === "POTA"
            text: qsTr("Self-spot")
            focusPolicy: Qt.NoFocus
            Layout.preferredHeight: Theme.touchTarget
            onClicked: potaDialog.ask()
        }
    }
    Label {
        objectName: "logSelfSpotStatus"
        visible: Engine.mySig === "POTA" && Engine.potaSpotStatus.length > 0
        Layout.fillWidth: true
        text: Engine.potaSpotStatus
        color: Engine.potaSpotOk ? Theme.textDim : Theme.warning
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true
        AppButton {
            id: logButton
            objectName: "logLog"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            text: qsTr("Log %1").arg(callField.text.toUpperCase())
            enabled: callField.text.trim().length >= 3
            focusPolicy: Qt.NoFocus
            onClicked: {
                const ok = Engine.logQso({
                    call: callField.text, rstSent: rstSent.text, rstRcvd: rstRcvd.text,
                    comment: comment.text, name: nameField.text, timeOn: logTab.timeOn,
                    sig: theirSig.value, sigInfo: theirSig.value.length > 0 ? theirRef.text : "",
                    mySig: Engine.mySig, mySigInfo: Engine.mySig.length > 0 ? Engine.myRef : ""
                })
                if (ok) {
                    logTab.reset()
                    logTab.dropKeyboard()
                }
            }
        }
        // The call to the DX cluster (roadmap 67, Christoph Steinert DM5HF):
        // asked first with the exact line, as it is a public post under the
        // operator's call. Dimmed without the cluster; a tap then says where
        // it is turned on.
        AppButton {
            objectName: "logSpot"
            text: qsTr("Spot")
            enabled: Engine.dxSpotLine(callField.text, "").length > 0
            opacity: Engine.dxCluster.state === "connected" ? 1.0 : 0.6
            focusPolicy: Qt.NoFocus
            Layout.preferredHeight: Theme.touchTarget
            Layout.preferredWidth: Math.max(implicitWidth + 24, 76)
            Accessible.description: qsTr("Spots the call on the DX cluster")
            onClicked: spotDialog.ask()
        }
    }
    Label {
        objectName: "logSpotStatus"
        visible: Engine.dxSpotStatus.length > 0
        Layout.fillWidth: true
        text: Engine.dxSpotStatus
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.Wrap
    }

    // ---- Recent contacts ----
    Label {
        text: Engine.qsoCount > 0 ? qsTr("Log: %n contact(s)", "", Engine.qsoCount) : qsTr("No contacts logged yet.")
        color: Theme.textDim
        font.pixelSize: Theme.px(14)
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
        font.pixelSize: Theme.px(12)
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
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import contacts (ADIF)")
        fileMode: FileDialog.OpenFile
        // Android: no filter. Qt turns *.adi into application/octet-stream
        // for the system picker, which then greyed out an ADIF file stored
        // as text (from Drive, say) and left only this app's own log to pick
        // (Kurt DL5EAG). The import tells an ADIF file by its contents.
        nameFilters: Qt.platform.os === "android" ? []
                     : [qsTr("ADIF files (*.adi *.adif *.ADI)"), qsTr("All files (*)")]
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
    // A tap on a contact opens it to edit or delete (roadmap 78).
    Repeater {
        model: Engine.recentQsos.slice(0, 10)
        delegate: Label {
            required property var modelData
            required property int index
            objectName: "logRecent" + index
            Layout.fillWidth: true
            text: (modelData.TIME_ON || "").slice(0, 2) + ":" + (modelData.TIME_ON || "").slice(2, 4) + "  "
                  + modelData.CALL + "  " + (modelData.BAND || "") + " " + (modelData.SUBMODE || modelData.MODE || "")
                  + "  " + (modelData.RST_SENT || "") + "/" + (modelData.RST_RCVD || "")
                  + (modelData.SIG_INFO ? "  " + modelData.SIG_INFO : "")
            color: Theme.text
            font.pixelSize: Theme.px(14)
            font.family: "monospace"
            elide: Text.ElideRight
            topPadding: 4
            bottomPadding: 4
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Edit %1").arg(modelData.CALL)
            Accessible.onPressAction: qsoEdit.ask(modelData)
            TapHandler { onTapped: qsoEdit.ask(modelData) }
        }
    }

    // ---- Station and QRZ.com ----
    Label { text: qsTr("Station"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: (logTab.defaults.myCall || qsTr("(radio callsign not set)"))
            color: Theme.text
            font.pixelSize: Theme.px(15)
            Layout.preferredWidth: 120
        }
        TextField {
            id: gridField
            Accessible.name: qsTr("My grid")
            Layout.fillWidth: true
            placeholderText: qsTr("My grid, e.g. KP22")
            font.pixelSize: Theme.px(15)
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
            Accessible.name: qsTr("QRZ.com user")
            Layout.fillWidth: true
            Layout.columnSpan: qrzRow.oneRow ? 1 : 2
            placeholderText: qsTr("QRZ.com user")
            font.pixelSize: Theme.px(15)
            text: Engine.qrzUser
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        }
        PasswordField {
            id: qrzPassField
            Accessible.name: qsTr("QRZ.com password")
            Layout.fillWidth: true
            placeholderText: Engine.qrzUser.length > 0 ? qsTr("password (saved)") : qsTr("password")
            font.pixelSize: Theme.px(15)
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
             : Engine.qrzStatusKind === "failed" ? Theme.transmit
             : Engine.qrzStatusKind === "unreachable" ? Theme.warning : Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("QRZ.com's full records (name, QTH, grid) need their XML subscription; without it the card shows the country from the prefix. The password stays in this device's secure store.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // ---- Wavelog / Cloudlog: each logged QSO to the online logbook ----
    Label { text: qsTr("Wavelog / Cloudlog"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    TextField {
        id: onlineUrlField
        Accessible.name: qsTr("Logbook address")
        objectName: "onlineLogUrl"
        Layout.fillWidth: true
        placeholderText: qsTr("Logbook address, e.g. https://log.example.com")
        font.pixelSize: Theme.px(15)
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
            Accessible.name: qsTr("API key")
            Layout.fillWidth: true
            Layout.columnSpan: onlineKeyRow.oneRow ? 1 : 2
            placeholderText: Engine.onlineLog.hasKey ? qsTr("API key (saved)") : qsTr("API key (read & write)")
            font.pixelSize: Theme.px(15)
        }
        TextField {
            id: onlineStationField
            Accessible.name: qsTr("Station ID")
            Layout.preferredWidth: 90
            Layout.fillWidth: !onlineKeyRow.oneRow
            placeholderText: qsTr("Station ID")
            font.pixelSize: Theme.px(15)
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
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    // The logbook read back (Wavelog): its QSOs count as worked before,
    // apart from the app's own log.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.onlineLog.enabled === true
        Label {
            text: qsTr("Worked before from the logbook")
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "onlineLogReadOff"
            text: qsTr("Off")
            checked: Engine.onlineLog.read !== true
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.setOnlineLogRead(false)
        }
        AppButton {
            objectName: "onlineLogReadOn"
            text: qsTr("On")
            checked: Engine.onlineLog.read === true
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.setOnlineLogRead(true)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.onlineLog.enabled === true && Engine.onlineLog.read === true
        Label {
            objectName: "onlineLogReadStatus"
            Layout.fillWidth: true
            text: Engine.onlineLog.readStatus || ""
            color: Engine.onlineLog.readStatusKind === "ok" ? "#00cc66"
                 : Engine.onlineLog.readStatusKind === "failed" ? Theme.transmit : Theme.textDim
            font.pixelSize: Theme.px(13)
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "onlineLogReadNow"
            text: qsTr("Read now")
            enabled: Engine.onlineLog.reading !== true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.onlineLogRead()
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Every QSO you log (and FT8 / FT4 logs) also goes to your Wavelog or Cloudlog logbook. The station ID is the station location's number there (Station Locations). QSOs that cannot be sent wait and go later. Clear the address to stop. The key stays in this device's secure store.")
              + " " + qsTr("Worked before from the logbook reads your Wavelog logbook's QSOs (every station location; at each start only the new ones), so worked before, dupes, New call / DXCC and the FTx colours know them; they stay apart from this app's log. Needs Wavelog 1.8 or newer; Cloudlog cannot send its QSOs.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // ---- QRZ.com Logbook, both ways: each logged QSO there, its QSOs read
    // back as worked before ----
    Label { text: qsTr("QRZ.com Logbook"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    RowLayout {
        Layout.fillWidth: true
        PasswordField {
            id: qrzLogKeyField
            Accessible.name: qsTr("QRZ.com Logbook API key")
            objectName: "qrzLogKey"
            Layout.fillWidth: true
            placeholderText: Engine.qrzLog.hasKey ? qsTr("API key (saved)") : qsTr("Logbook API key")
            font.pixelSize: Theme.px(15)
        }
        AppButton {
            text: qsTr("Save")
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.setQrzLogKey(qrzLogKeyField.text)
                qrzLogKeyField.text = ""
            }
        }
        AppButton {
            objectName: "qrzLogRemove"
            visible: Engine.qrzLog.hasKey === true
            text: qsTr("Remove")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.clearQrzLog()
        }
    }
    Label {
        objectName: "qrzLogStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: (Engine.qrzLog.status || "")
              + (Engine.qrzLog.pending > 0 && (Engine.qrzLog.status || "").indexOf("wait") < 0
                 ? "  (" + qsTr("%1 waiting").arg(Engine.qrzLog.pending) + ")" : "")
        color: Engine.qrzLog.statusKind === "ok" ? "#00cc66"
             : Engine.qrzLog.statusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.qrzLog.hasKey === true
        Label {
            text: qsTr("Worked before from the logbook")
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "qrzLogReadOff"
            text: qsTr("Off")
            checked: Engine.qrzLog.read !== true
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.setQrzLogRead(false)
        }
        AppButton {
            objectName: "qrzLogReadOn"
            text: qsTr("On")
            checked: Engine.qrzLog.read === true
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.setQrzLogRead(true)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.qrzLog.hasKey === true && Engine.qrzLog.read === true
        Label {
            objectName: "qrzLogReadStatus"
            Layout.fillWidth: true
            text: Engine.qrzLog.readStatus || ""
            color: Engine.qrzLog.readStatusKind === "ok" ? "#00cc66"
                 : Engine.qrzLog.readStatusKind === "failed" ? Theme.transmit : Theme.textDim
            font.pixelSize: Theme.px(13)
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "qrzLogReadNow"
            text: qsTr("Read now")
            enabled: Engine.qrzLog.reading !== true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.qrzLogRead()
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("With the logbook's API key (QRZ.com › Logbook › Settings), every QSO you log also goes to your QRZ.com logbook; QSOs that cannot be sent wait and go later. Worked before from the logbook reads its QSOs back (at each start only the new ones), counted once with this app's log and Wavelog's. QRZ.com's Logbook API needs a QRZ subscription. The key stays in this device's secure store.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // ---- A shared ADIF file (iCloud Drive, Google Drive, OneDrive...), both
    // ways, for loggers without a cloud API ----
    Label { text: qsTr("Shared ADIF file"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    RowLayout {
        Layout.fillWidth: true
        Label {
            objectName: "sharedAdifName"
            Layout.fillWidth: true
            text: Engine.sharedAdif.set === true ? Engine.sharedAdif.name : qsTr("No file chosen")
            color: Engine.sharedAdif.set === true ? Theme.text : Theme.textDim
            font.pixelSize: Theme.px(15)
            elide: Text.ElideMiddle
        }
        AppButton {
            objectName: "sharedAdifPick"
            text: Engine.sharedAdif.set === true ? qsTr("Change") : qsTr("Choose…")
            focusPolicy: Qt.NoFocus
            onClicked: sharedDialog.open()
        }
        AppButton {
            objectName: "sharedAdifRemove"
            visible: Engine.sharedAdif.set === true
            text: qsTr("Remove")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.clearSharedAdif()
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.sharedAdif.set === true
        Label {
            objectName: "sharedAdifStatus"
            Layout.fillWidth: true
            text: (Engine.sharedAdif.status || "")
                  + (Engine.sharedAdif.pending > 0 && (Engine.sharedAdif.status || "").indexOf("wait") < 0
                     ? "  (" + qsTr("%1 waiting").arg(Engine.sharedAdif.pending) + ")" : "")
            color: Engine.sharedAdif.statusKind === "ok" ? "#00cc66"
                 : Engine.sharedAdif.statusKind === "failed" ? Theme.transmit : Theme.textDim
            font.pixelSize: Theme.px(13)
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "sharedAdifReadNow"
            text: qsTr("Read now")
            enabled: Engine.sharedAdif.reading !== true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.sharedAdifRead()
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("For a logging program without a cloud connection (Log4OM, N1MM, DXKeeper, MacLoggerDX…): put an ADIF file in iCloud Drive, Google Drive, OneDrive, Dropbox or Nextcloud (export one from the program, or share this app's log there) and choose it here. Its QSOs count as worked before, read at each start and with Read now, and every QSO you log is added to its end for the program to import. The app only adds to the file, never changes what is in it.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
    FileDialog {
        id: sharedDialog
        title: qsTr("Shared ADIF file")
        fileMode: FileDialog.OpenFile
        // Android without a filter, as the import (Kurt DL5EAG).
        nameFilters: Qt.platform.os === "android" ? []
                     : [qsTr("ADIF files (*.adi *.adif *.ADI)"), qsTr("All files (*)")]
        onAccepted: Engine.setSharedAdif(selectedFile)
    }

    // ---- Logger32 on the operator's PC: each logged QSO over TCP (Juho
    // Kontio) ----
    Label { text: qsTr("Logger32"); color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    // One row where the hint fits beside Save, else Save under the field.
    GridLayout {
        id: logger32Row
        Layout.fillWidth: true
        columns: width - logger32Save.implicitWidth - columnSpacing >= logger32Hint.advanceWidth + 24 ? 2 : 1
        TextMetrics { id: logger32Hint; font: logger32Field.font; text: logger32Field.placeholderText }
        TextField {
            id: logger32Field
            Accessible.name: qsTr("Logger32 PC address")
            objectName: "logger32Host"
            Layout.fillWidth: true
            placeholderText: qsTr("PC address, e.g. 192.168.1.10:52001")
            font.pixelSize: Theme.px(15)
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
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Every QSO you log (and FT8 / FT4 logs) also goes to Logger32 on your PC, over its TCP port (52001 unless you give another after a colon). Away from home this needs your VPN to reach the PC, and Windows' firewall must let the port in. QSOs that cannot be sent wait and go later. Clear the address to stop.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // The spot asked first: the exact line, the remark to edit (the mode,
    // with their park when set), whose call it goes out under.
    Dialog {
        id: spotDialog
        objectName: "logSpotDialog"
        readonly property bool online: Engine.dxCluster.state === "connected"
        // Follows the slice: the tab's defaults are read again every second.
        readonly property string line: {
            logTab.defaults
            return Engine.dxSpotLine(callField.text, remark.text)
        }
        function ask() {
            const parts = []
            if (theirSig.value.length > 0 && theirRef.text.trim().length > 0)
                parts.push(theirSig.value + " " + theirRef.text.trim().toUpperCase())
            parts.push(Engine.dxSpotLine(callField.text, "").split(" ").slice(3).join(" "))
            remark.text = parts.join(" ")
            open()
        }
        onClosed: logTab.dropKeyboard()
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: qsTr("Spot %1 on the DX cluster?").arg(callField.text.trim().toUpperCase())
                color: Theme.text
                font.pixelSize: Theme.px(18)
                font.bold: true
                wrapMode: Text.Wrap
            }
            TextField {
                id: remark
                objectName: "logSpotRemark"
                Accessible.name: qsTr("Spot remark")
                Layout.fillWidth: true
                visible: spotDialog.online
                placeholderText: qsTr("Remark")
                maximumLength: 30
                font.pixelSize: Theme.px(15)
                onAccepted: if (spotDialog.line.length > 0) sendSpot.clicked()
            }
            Label {
                objectName: "logSpotLine"
                Layout.fillWidth: true
                visible: spotDialog.online
                text: spotDialog.line
                color: Theme.text
                font.pixelSize: Theme.px(15)
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
            }
            Label {
                Layout.fillWidth: true
                text: spotDialog.online
                      ? qsTr("Goes out under %1 to everyone on %2: spot a station you hear.").arg(Engine.dxCluster.callsign || "").arg(Engine.dxCluster.host || "")
                      : qsTr("The DX cluster is not connected: turn it on in Controls › View › DX cluster.")
                color: spotDialog.online ? Theme.textDim : Theme.warning
                font.pixelSize: Theme.px(14)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { objectName: "logSpotCancel"; text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: spotDialog.close() }
                AppButton {
                    id: sendSpot
                    objectName: "logSpotSend"
                    text: qsTr("Spot")
                    checked: true
                    enabled: spotDialog.online && spotDialog.line.length > 0
                    Layout.fillWidth: true
                    onClicked: {
                        const call = callField.text
                        const text = remark.text
                        spotDialog.close()
                        Engine.sendDxSpot(call, text)
                    }
                }
            }
        }
    }
    // The POTA self-spot asked first: what goes out, the park when My park
    // holds two, a comment.
    Dialog {
        id: potaDialog
        objectName: "logSelfSpotDialog"
        property var info: ({})
        property string park: ""
        function ask() {
            info = Engine.potaSpotInfo()
            const parks = info.parks || []
            park = parks.length > 0 ? parks[0] : ""
            potaComment.text = ""
            open()
        }
        onClosed: logTab.dropKeyboard()
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: qsTr("Spot yourself on POTA?")
                color: Theme.text
                font.pixelSize: Theme.px(18)
                font.bold: true
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                visible: potaDialog.info.ok && (potaDialog.info.parks || []).length > 1
                Repeater {
                    model: potaDialog.info.parks || []
                    delegate: AppButton {
                        required property string modelData
                        objectName: "logSelfSpotPark_" + modelData.replace(/[^A-Za-z0-9]/g, "")
                        text: modelData
                        checked: potaDialog.park === modelData
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        onClicked: potaDialog.park = modelData
                    }
                }
            }
            TextField {
                id: potaComment
                objectName: "logSelfSpotComment"
                Accessible.name: qsTr("Spot comment")
                Layout.fillWidth: true
                visible: !!potaDialog.info.ok
                placeholderText: qsTr("Comment (optional)")
                maximumLength: 60
                font.pixelSize: Theme.px(15)
            }
            Label {
                objectName: "logSelfSpotLine"
                Layout.fillWidth: true
                visible: !!potaDialog.info.ok
                text: qsTr("%1 at %2, %3 kHz %4").arg(potaDialog.info.call || "").arg(potaDialog.park)
                      .arg(potaDialog.info.kHz || "").arg(potaDialog.info.mode || "")
                color: Theme.text
                font.pixelSize: Theme.px(15)
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
            }
            Label {
                Layout.fillWidth: true
                text: potaDialog.info.ok
                      ? qsTr("A public spot on pota.app under your call, for hunters to find you.")
                      : (potaDialog.info.why || "")
                color: potaDialog.info.ok ? Theme.textDim : Theme.warning
                font.pixelSize: Theme.px(14)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { objectName: "logSelfSpotCancel"; text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: potaDialog.close() }
                AppButton {
                    objectName: "logSelfSpotSend"
                    text: qsTr("Spot")
                    checked: true
                    enabled: !!potaDialog.info.ok && potaDialog.park.length > 0
                    Layout.fillWidth: true
                    onClicked: {
                        const park = potaDialog.park
                        const text = potaComment.text
                        potaDialog.close()
                        Engine.sendPotaSpot(park, text)
                    }
                }
            }
        }
    }
    // A logged contact edited or deleted (roadmap 78, Christoph Steinert
    // DM5HF): the app's own log; the logbooks it went to are named, as an
    // edit here does not change them.
    Dialog {
        id: qsoEdit
        objectName: "logEditDialog"
        property var record: ({})
        property bool confirmDelete: false
        function ask(r) {
            record = r
            confirmDelete = false
            editCall.text = r.CALL || ""
            const d = r.QSO_DATE || ""
            editDate.text = d.length === 8 ? d.slice(0, 4) + "-" + d.slice(4, 6) + "-" + d.slice(6, 8) : d
            const t = r.TIME_ON || ""
            editTime.text = t.length >= 4 ? t.slice(0, 2) + ":" + t.slice(2, 4) : t
            editFreq.text = r.FREQ ? Number(r.FREQ).toFixed(4) : ""
            editMode.text = r.SUBMODE || r.MODE || ""
            editSent.text = r.RST_SENT || ""
            editRcvd.text = r.RST_RCVD || ""
            editName.text = r.NAME || ""
            editComment.text = r.COMMENT || ""
            editTheir.text = r.SIG_INFO || ""
            editMine.text = r.MY_SIG_INFO || ""
            open()
        }
        onClosed: logTab.dropKeyboard()
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 24 : 440)
        height: Math.min(implicitHeight, parent ? parent.height - 24 : implicitHeight)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        contentItem: Flickable {
            implicitHeight: editColumn.implicitHeight
            contentHeight: editColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: editColumn
                width: parent.width
                spacing: Theme.spacing
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Edit the contact")
                    color: Theme.text
                    font.pixelSize: Theme.px(18)
                    font.bold: true
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacing
                    rowSpacing: 4
                    EditLabel { text: qsTr("Call") }
                    EditField { id: editCall; objectName: "logEditCall"; Accessible.name: qsTr("Callsign"); font.family: "monospace"; font.capitalization: Font.AllUppercase; inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText }
                    EditLabel { text: qsTr("Date (UTC)") }
                    EditField { id: editDate; objectName: "logEditDate"; Accessible.name: qsTr("Date"); placeholderText: "YYYY-MM-DD"; inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText }
                    EditLabel { text: qsTr("Time (UTC)") }
                    EditField { id: editTime; objectName: "logEditTime"; Accessible.name: qsTr("Time"); placeholderText: "HH:MM"; inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText }
                    EditLabel { text: qsTr("MHz") }
                    EditField { id: editFreq; objectName: "logEditFreq"; Accessible.name: qsTr("Frequency in MHz"); inputMethodHints: Qt.ImhFormattedNumbersOnly }
                    EditLabel { text: qsTr("Mode") }
                    EditField { id: editMode; objectName: "logEditMode"; Accessible.name: qsTr("Mode"); inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText }
                    EditLabel { text: qsTr("Sent / Rcvd") }
                    RowLayout {
                        Layout.fillWidth: true
                        EditField { id: editSent; objectName: "logEditSent"; Accessible.name: qsTr("Report sent") }
                        EditField { id: editRcvd; objectName: "logEditRcvd"; Accessible.name: qsTr("Report received") }
                    }
                    EditLabel { text: qsTr("Name") }
                    EditField { id: editName; objectName: "logEditName"; Accessible.name: qsTr("Name") }
                    EditLabel { text: qsTr("Comment") }
                    EditField { id: editComment; objectName: "logEditComment"; Accessible.name: qsTr("Comment") }
                    EditLabel { text: qsTr("Their park") }
                    EditField { id: editTheir; objectName: "logEditTheirPark"; Accessible.name: qsTr("Their park reference"); inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText }
                    EditLabel { text: qsTr("My park") }
                    EditField { id: editMine; objectName: "logEditMyPark"; Accessible.name: qsTr("My park reference"); inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText }
                }
                Label {
                    Layout.fillWidth: true
                    readonly property var books: Engine.qsoLogbooks()
                    visible: books.length > 0
                    text: qsTr("Changes this device's log only; not %1, where it may have gone already.").arg(books.join(", "))
                    color: Theme.warning
                    font.pixelSize: Theme.px(13)
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "logEditDelete"
                        text: qsoEdit.confirmDelete ? qsTr("Delete it") : qsTr("Delete")
                        checked: qsoEdit.confirmDelete
                        Layout.fillWidth: true
                        onClicked: {
                            if (!qsoEdit.confirmDelete) {
                                qsoEdit.confirmDelete = true
                                return
                            }
                            if (Engine.deleteQso(qsoEdit.record)) qsoEdit.close()
                        }
                    }
                    AppButton { objectName: "logEditCancel"; text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: qsoEdit.close() }
                    AppButton {
                        objectName: "logEditSave"
                        text: qsTr("Save")
                        checked: true
                        Layout.fillWidth: true
                        onClicked: {
                            const ok = Engine.editQso(qsoEdit.record, {
                                call: editCall.text, date: editDate.text, time: editTime.text, freq: editFreq.text,
                                mode: editMode.text, rstSent: editSent.text, rstRcvd: editRcvd.text, name: editName.text,
                                comment: editComment.text, sigInfo: editTheir.text, mySigInfo: editMine.text
                            })
                            if (ok) qsoEdit.close()
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: qsoEdit.confirmDelete
                    text: qsTr("Tap Delete it to remove the contact from this device's log for good.")
                    color: Theme.warning
                    font.pixelSize: Theme.px(13)
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
