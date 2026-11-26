import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AetherMobile

// The radio's profiles, as in SmartSDR/AetherSDR: choose one to load, save
// the current settings under a name, delete the selected one.
ColumnLayout {
    id: profiles
    spacing: Theme.spacing
    implicitWidth: 200

    component ProfileSection: ColumnLayout {
        id: section
        property string type
        property string title
        property var names: []
        property string active
        property string hint
        Layout.fillWidth: true
        spacing: Theme.spacing / 2

        Label { text: section.title; Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16) }
        AppComboBox {
            id: picker
            Accessible.name: section.title
            // Its text field would be read again, without a name.
            Component.onCompleted: contentItem.Accessible.ignored = true
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            model: section.names
            currentIndex: section.names.indexOf(section.active)
            displayText: currentIndex >= 0 ? currentText : qsTr("(none)")
            font.pixelSize: Theme.buttonFont
            onActivated: (index) => Engine.loadProfile(section.type, section.names[index])
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: nameField
                objectName: "profileName_" + section.type
                Accessible.name: qsTr("New %1 profile name").arg(section.title)
                Layout.fillWidth: true
                placeholderText: qsTr("Save as…")
                font.pixelSize: Theme.px(15)
                // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
                EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
                onAccepted: saveButton.clicked()
            }
            AppButton {
                id: saveButton
                objectName: "profileSave_" + section.type
                text: qsTr("Save")
                // A typed name saves under it. With the field empty Save goes
                // over the profile chosen above, after a question that names
                // it (roadmap 162, Larry Averbeck W9FLA: a small change saved
                // again needed the whole name typed again).
                enabled: nameField.text.trim().length > 0 || picker.currentIndex >= 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    const typed = nameField.text.trim()
                    const name = typed.length > 0 ? typed : picker.currentText
                    if (section.names.indexOf(name) >= 0) {
                        confirmSaveOver.type = section.type
                        confirmSaveOver.kind = section.title
                        confirmSaveOver.name = name
                        confirmSaveOver.field = nameField
                        confirmSaveOver.open()
                        return
                    }
                    Engine.saveProfile(section.type, name)
                    nameField.text = ""
                }
            }
            AppButton {
                text: qsTr("Delete")
                enabled: picker.currentIndex >= 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    confirmDelete.type = section.type
                    confirmDelete.name = picker.currentText
                    confirmDelete.open()
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: section.hint.length > 0
            text: section.hint
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.Wrap
        }
    }

    ProfileSection {
        type: "global"
        title: qsTr("Global profile")
        names: Engine.globalProfiles
        active: Engine.activeGlobalProfile
        hint: qsTr("Loading a global profile changes bands, panadapters and slices. Save with no name saves over the profile chosen above.")
    }
    ProfileSection {
        type: "transmit"
        title: qsTr("TX profile")
        names: Engine.txProfiles
        active: Engine.activeTxProfile
        hint: qsTr("Save with no name saves over the profile chosen above.")
    }
    ProfileSection {
        type: "mic"
        title: qsTr("Mic profile")
        names: Engine.micProfiles
        active: Engine.activeMicProfile
    }

    // ---- App setups (Larry Averbeck W9FLA, roadmap 17): this app's own
    // settings under a name, switched here, exported as a file and imported
    // on another device. The radio's settings are the profiles above. ----
    ColumnLayout {
        id: setups
        objectName: "appSetups"
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing
        spacing: Theme.spacing / 2
        property string result: ""
        property bool resultBad: false
        function show(r, done) {
            setups.resultBad = !r.ok
            setups.result = r.ok ? done : r.error
        }

        Label { text: qsTr("App setups"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16) }
        AppComboBox {
            id: setupPicker
            objectName: "appSetupPicker"
            Accessible.name: qsTr("App setup")
            Component.onCompleted: contentItem.Accessible.ignored = true
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            model: Engine.appSetups
            currentIndex: Engine.appSetups.indexOf(Engine.activeAppSetup)
            displayText: currentIndex >= 0 ? currentText : qsTr("(none saved)")
            font.pixelSize: Theme.buttonFont
            onActivated: (index) => {
                const name = Engine.appSetups[index]
                const r = Engine.applyAppSetup(name)
                setups.show(r, r.later && r.later.length > 0
                    ? qsTr("Using “%1”. At the next start of the app: %2.").arg(name).arg(r.later)
                    : qsTr("Using “%1”.").arg(name))
                // A refused switch shows the setup still in use.
                currentIndex = Qt.binding(() => Engine.appSetups.indexOf(Engine.activeAppSetup))
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: setupName
                objectName: "appSetupName"
                Accessible.name: qsTr("New app setup name")
                Layout.fillWidth: true
                placeholderText: qsTr("Save as…")
                maximumLength: 40
                font.pixelSize: Theme.px(15)
                // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
                EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
                onAccepted: setupSave.clicked()
            }
            AppButton {
                id: setupSave
                objectName: "appSetupSave"
                text: qsTr("Save")
                // With no name: over the setup chosen above (roadmap 162).
                enabled: setupName.text.trim().length > 0 || setupPicker.currentIndex >= 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    const typed = setupName.text.trim()
                    const name = typed.length > 0 ? typed : setupPicker.currentText
                    if (Engine.appSetups.indexOf(name) >= 0) {
                        confirmReplace.name = name
                        confirmReplace.open()
                        return
                    }
                    setups.show(Engine.saveAppSetup(name), qsTr("Saved “%1”.").arg(name))
                    setupName.text = ""
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                objectName: "appSetupExport"
                text: qsTr("Export")
                enabled: setupPicker.currentIndex >= 0
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                onClicked: confirmExport.open()
            }
            AppButton {
                objectName: "appSetupImport"
                text: qsTr("Import")
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                // Android: its own picker, which can give a Drive file (129).
                onClicked: {
                    profiles.picking = Engine.pickDocument("appSetup")
                    if (!profiles.picking) importSetup.open()
                }
            }
            AppButton {
                objectName: "appSetupDelete"
                text: qsTr("Delete")
                enabled: setupPicker.currentIndex >= 0
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                onClicked: {
                    confirmDelete.type = "app"
                    confirmDelete.name = setupPicker.currentText
                    confirmDelete.open()
                }
            }
        }
        Label {
            objectName: "appSetupResult"
            Layout.fillWidth: true
            visible: setups.result.length > 0
            text: setups.result
            color: setups.resultBad ? Theme.warning : Theme.text
            font.pixelSize: Theme.px(13)
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("A setup holds this app's own choices: display, meters, panels, audio and the TX chain, macros, logbooks, devices and the radio list, with their passwords and keys. Voice recordings stay on this device. Pick a setup above to switch to it; Save stores the current choices.")
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.Wrap
        }
    }

    FileDialog {
        id: importSetup
        title: qsTr("Import an app setup")
        fileMode: FileDialog.OpenFile
        // Android without a filter, as the ADIF import (Kurt DL5EAG).
        nameFilters: Qt.platform.os === "android" ? []
                     : [qsTr("OksaSDR setups (*.json)"), qsTr("All files (*)")]
        onAccepted: profiles.importSetupFrom(selectedFile)
    }
    function importSetupFrom(url) {
        const r = Engine.importAppSetup(url)
        setups.show(r, qsTr("Imported “%1”: pick it above to use it.").arg(r.name))
    }
    // Android's picker answers every copy of this tab: only the one that
    // asked takes the file.
    property bool picking: false
    Connections {
        target: Engine
        function onDocumentPicked(purpose, url) {
            if (purpose !== "appSetup" || !profiles.picking) return
            profiles.picking = false
            profiles.importSetupFrom(url)
        }
    }

    Dialog {
        id: confirmExport
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 2 * Theme.spacing : 400, 420)
        modal: true
        title: qsTr("Export “%1”?").arg(setupPicker.currentText)
        standardButtons: Dialog.Ok | Dialog.Cancel
        // The text in a layout as the app's other questions (AmpControls'):
        // a wrapping Label as the contentItem itself set the dialog's
        // implicitHeight and its own width from each other (a binding loop
        // logged each time the drawer opened).
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("The file holds the setup's passwords and keys (QRZ.com, logbooks, 4O3A devices) as plain text. Keep it to your own devices.")
                wrapMode: Text.Wrap
                color: Theme.text
            }
        }
        onAccepted: setups.show(Engine.exportAppSetup(setupPicker.currentText), "")
    }

    Dialog {
        id: confirmReplace
        property string name
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Replace the setup “%1”?").arg(name)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            setups.show(Engine.saveAppSetup(name), qsTr("Saved “%1”.").arg(name))
            setupName.text = ""
        }
    }

    // Save over a profile that exists: the three kinds of the radio's
    // profiles (roadmap 162). The radio writes the settings in use over it.
    Dialog {
        id: confirmSaveOver
        objectName: "confirmSaveOver"
        property string type
        property string kind
        property string name
        property var field: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 2 * Theme.spacing : 400, 420)
        modal: true
        title: qsTr("Save over “%1”?").arg(name)
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("%1: the settings now in use replace the ones saved in it.").arg(confirmSaveOver.kind)
                wrapMode: Text.Wrap
                color: Theme.text
            }
        }
        onAccepted: {
            Engine.saveProfile(type, name)
            if (field) field.text = ""
        }
    }

    Dialog {
        id: confirmDelete
        property string type
        property string name
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: type === "app" ? qsTr("Delete the app setup “%1”?").arg(name) : qsTr("Delete profile “%1”?").arg(name)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (type === "app") setups.show(Engine.deleteAppSetup(name), qsTr("Deleted “%1”.").arg(name))
            else Engine.deleteProfile(type, name)
        }
    }
}
