import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AetherMobile

// Setup › Logging (roadmap 95): the logbooks and accounts each logged QSO
// goes to, set once, moved here from the Log tab, which keeps the QSO entry
// and the recent contacts (the user, 2026-11-17: "kertaluonteisia"). The
// same Engine calls and stored values as before.
ColumnLayout {
    id: loggingSetup
    spacing: Theme.spacing / 2
    // The own station as the Log tab shows it (the radio's callsign).
    property var defaults: Engine.qsoDefaults()
    onVisibleChanged: if (visible) defaults = Engine.qsoDefaults()

    // Android's picker answers every copy of this page: only the one that
    // asked takes the file.
    property string picking: ""
    function pick(purpose, dialog) {
        if (Engine.pickDocument(purpose)) {
            picking = purpose
        } else {
            dialog.open()
        }
    }
    function importFrom(url) {
        importResult.text = qsTr("Importing…")
        Engine.importAdif(url)
    }
    // Android's own picker (roadmap 129: QML's FileDialog there dropped a
    // file from Google Drive, OneDrive and the like) answers here, as the
    // dialogs below do elsewhere.
    Connections {
        target: Engine
        function onDocumentPicked(purpose, url) {
            if (purpose !== loggingSetup.picking) return
            loggingSetup.picking = ""
            if (purpose === "adifImport") {
                loggingSetup.importFrom(url)
            } else if (purpose === "sharedAdif") {
                Engine.setSharedAdif(url)
            }
        }
    }

    // This device's log as a file to share, and where it is (roadmap 131:
    // moved here from the Log tab, whose Logbooks… opens this page).
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
        // Android: its own picker, which can give a Drive file (129).
        onClicked: loggingSetup.pick("adifImport", importDialog)
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
        onAccepted: loggingSetup.importFrom(selectedFile)
    }
    Connections {
        target: Engine
        function onAdifImported(r) {
            importResult.text = r.error ? r.error
                : qsTr("Imported %1 contact(s); %2 already in the log.").arg(r.imported).arg(r.duplicates)
        }
    }

    // ---- After logging (roadmap 141, Christoph Steinert DM5HF: a run by
    // keyboard without the mouse) ----
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing
        Label {
            text: qsTr("After logging")
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "logBackToCallOff"
            text: qsTr("Keyboard away")
            checked: !Engine.logBackToCall
            focusPolicy: Qt.NoFocus
            onClicked: Engine.logBackToCall = false
        }
        AppButton {
            objectName: "logBackToCallOn"
            text: qsTr("Back to the call")
            checked: Engine.logBackToCall
            focusPolicy: Qt.NoFocus
            onClicked: Engine.logBackToCall = true
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Back to the call keeps the cursor in the Log tab's call field after Log (or Return), so the next call is typed at once; Space then types there, and Esc or a tap outside the field gives Space back to PTT. Keyboard away (the default) gives it back at once.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }

    // ---- Station and QRZ.com ----
    Label { text: qsTr("Station"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: (loggingSetup.defaults.myCall || qsTr("(radio callsign not set)"))
            color: Theme.text
            font.pixelSize: Theme.px(15)
            Layout.preferredWidth: 120
        }
        TextField {
            id: gridField
            Accessible.name: qsTr("My grid")
            Layout.fillWidth: true
            // Sized to what it holds, about 8 characters (roadmap 158)
            Layout.maximumWidth: Math.ceil(8 * font.pixelSize * 0.62) + leftPadding + rightPadding
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
        color: Engine.qrzStatusKind === "ok" ? Theme.good
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
    Label { text: qsTr("Wavelog / Cloudlog"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
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
        color: Engine.onlineLog.statusKind === "ok" ? Theme.good
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
            color: Engine.onlineLog.readStatusKind === "ok" ? Theme.good
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
    Label { text: qsTr("QRZ.com Logbook"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
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
        color: Engine.qrzLog.statusKind === "ok" ? Theme.good
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
            color: Engine.qrzLog.readStatusKind === "ok" ? Theme.good
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
    Label { text: qsTr("Shared ADIF file"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
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
            onClicked: loggingSetup.pick("sharedAdif", sharedDialog)
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
            color: Engine.sharedAdif.statusKind === "ok" ? Theme.good
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
        text: qsTr("For a logging program without a cloud connection (Log4OM, N1MM, DXKeeper, MacLoggerDX…): put an ADIF file in iCloud Drive, Google Drive, OneDrive, Dropbox or Nextcloud (export one from the program, or share this app's log there) and choose it here. Its QSOs count as worked before, read at each start and with Read now, and every QSO you log is added to its end for the program to import. A contact you edit or delete in the Log tab is changed or deleted there too; the rest of the file stays as it is.")
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
    Label { text: qsTr("Logger32"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
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
            // Sized to what it holds, about 21 characters (roadmap 158)
            Layout.maximumWidth: Math.ceil(21 * font.pixelSize * 0.62) + leftPadding + rightPadding
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
        color: Engine.logger32.statusKind === "ok" ? Theme.good
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

    // ---- A logging program on the network, as WSJT-X sends to it
    // (roadmap 89: RUMlogNG, Christoph Steinert DM5HF) ----
    Label { text: qsTr("Logging program (WSJT-X UDP)"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(14); Layout.topMargin: Theme.spacing }
    GridLayout {
        id: loggerUdpRow
        Layout.fillWidth: true
        columns: width - loggerUdpSave.implicitWidth - columnSpacing >= loggerUdpHint.advanceWidth + 24 ? 2 : 1
        TextMetrics { id: loggerUdpHint; font: loggerUdpField.font; text: loggerUdpField.placeholderText }
        TextField {
            id: loggerUdpField
            Accessible.name: qsTr("Logging program's computer address")
            objectName: "loggerUdpHost"
            Layout.fillWidth: true
            // Sized to what it holds, about 21 characters (roadmap 158)
            Layout.maximumWidth: Math.ceil(21 * font.pixelSize * 0.62) + leftPadding + rightPadding
            placeholderText: qsTr("Computer address, e.g. 192.168.1.10:2237")
            font.pixelSize: Theme.px(15)
            text: Engine.loggerUdp.host || ""
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        }
        AppButton {
            id: loggerUdpSave
            objectName: "loggerUdpSave"
            Layout.alignment: Qt.AlignRight
            text: qsTr("Save")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.setLoggerUdp(loggerUdpField.text)
        }
    }
    Label {
        objectName: "loggerUdpStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: Engine.loggerUdp.status || ""
        color: Engine.loggerUdp.statusKind === "ok" ? Theme.good
             : Engine.loggerUdp.statusKind === "failed" ? Theme.transmit : Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("For a logging program that takes QSOs from WSJT-X (RUMlogNG, MacLoggerDX, Log4OM, N1MM+, GridTracker…): every QSO you log goes to it as WSJT-X sends one, over UDP to its WSJT-X port (2237 unless you give another after a colon; on the same Mac, 127.0.0.1). In RUMlogNG: Preferences › UDP › WSJT-X, the same Data port, Save QSOs to logbook on, Switch PTT off. Nothing answers over UDP, so a QSO that does not arrive is not sent again. Clear the address to stop.")
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
}
