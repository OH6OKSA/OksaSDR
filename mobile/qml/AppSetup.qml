import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Setup › App (roadmap 131): this app's look and behaviour, set seldom,
// moved here from the View tab (the user, 2026-11-22: the Controls tabs
// keep what is used while operating, and one button leads to where the rest
// is set). The same Engine calls and stored values as before.
ColumnLayout {
    spacing: Theme.spacing / 2
    // Theme (roadmap 118, Larry Averbeck W9FLA; 125): the page dark (as it
    // has always been), light for daylight and tired eyes, or Auto as the
    // device's appearance; the display stays dark in all. Switched at once
    // and remembered.
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Theme"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        AppButton {
            objectName: "themeDark"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Theme")).arg(text)
            text: qsTr("Dark")
            checked: Engine.theme === "dark"
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.theme = "dark"
        }
        AppButton {
            objectName: "themeLight"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Theme")).arg(text)
            text: qsTr("Light")
            checked: Engine.theme === "light"
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.theme = "light"
        }
        // The device's own appearance, switching with it (roadmap 125).
        AppButton {
            objectName: "themeAuto"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Theme")).arg(text)
            text: qsTr("Auto")
            checked: Engine.theme === "auto"
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.theme = "auto"
        }
    }
    // Text size: the system's (iOS Dynamic Type, Android's font scale) or a
    // fixed one; every text in the app follows (Theme.textScale), held to
    // 88-135 % so the layouts still fit.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Text size"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
            Repeater {
                model: [{ v: "system", l: qsTr("System") }, { v: "100", l: "100 %" }, { v: "115", l: "115 %" }, { v: "130", l: "130 %" }]
                delegate: AppButton {
                    required property var modelData
                    objectName: "textSize" + modelData.v
                    Accessible.name: qsTr("%1: %2").arg(qsTr("Text size")).arg(text)
                    text: modelData.l
                    checked: Engine.textSize === modelData.v
                    Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.textSize = modelData.v
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: Engine.textSize === "system"
            text: Engine.systemTextScale > 1.351
                  ? qsTr("The system's text size is %1 %; the app stops at 135 % so its rows still fit.").arg(Math.round(Engine.systemTextScale * 100))
                  : qsTr("Follows the system's text size, now %1 %.").arg(Math.round(Engine.systemTextScale * 100))
            color: Theme.textDim
            font.pixelSize: Theme.px(12)
            wrapMode: Text.Wrap
        }
    }
    // The display's corner buttons: zoom, Band and B/S at the left (as the
    // desktop) or mirrored to the right for a right hand (Larry Averbeck),
    // + Slice / + Pan then at the left.
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Zoom buttons"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        AppButton {
            objectName: "displayButtonsLeft"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Zoom buttons")).arg(text)
            text: qsTr("Left")
            checked: !Engine.displayButtonsRight
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.displayButtonsRight = false
        }
        AppButton {
            objectName: "displayButtonsRight"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Zoom buttons")).arg(text)
            text: qsTr("Right")
            checked: Engine.displayButtonsRight
            Layout.preferredWidth: Theme.buttonWidth
            focusPolicy: Qt.NoFocus
            onClicked: Engine.displayButtonsRight = true
        }
    }
    // Where a phone's PTT sits (roadmap 191, Antonio Ferrulli IZ7LDG: a
    // big phone held in the left hand): at the left (the bottom row
    // mirrored, the Controls drawer's own row with it), across the page
    // upright, or at the right as always. In that order, as the user put
    // it: for the left hand, the middle one, for the right hand. A tablet's
    // page places PTT in its layouts.
    RowLayout {
        Layout.fillWidth: true
        visible: Theme.phoneLayout
        Label { text: qsTr("PTT"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        Repeater {
            model: [{ v: "left", t: qsTr("Left") }, { v: "wide", t: qsTr("Wide") }, { v: "right", t: qsTr("Right") }]
            delegate: AppButton {
                required property var modelData
                objectName: "pttPlace_" + modelData.v
                Accessible.name: qsTr("%1: %2").arg(qsTr("PTT")).arg(text)
                text: modelData.t
                checked: Engine.pttPlace === modelData.v
                Layout.preferredWidth: Theme.buttonWidth
                focusPolicy: Qt.NoFocus
                onClicked: Engine.pttPlace = modelData.v
            }
        }
    }
    // What the app does at a start (roadmap 189, Tyler Akerman K0OTC): the
    // radio list as always, or straight to the radio used last when it
    // shows up within a few seconds (found on this network, a saved address
    // or SmartLink, whichever way it was used).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("At start"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        AppButton {
            objectName: "startRadioList"
            Accessible.name: qsTr("%1: %2").arg(qsTr("At start")).arg(text)
            text: qsTr("Radio list")
            checked: !Engine.connectAtStart
            Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
            focusPolicy: Qt.NoFocus
            onClicked: Engine.connectAtStart = false
        }
        AppButton {
            objectName: "startLastRadio"
            Accessible.name: qsTr("%1: %2").arg(qsTr("At start")).arg(text)
            text: qsTr("Last radio")
            checked: Engine.connectAtStart
            Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
            focusPolicy: Qt.NoFocus
            onClicked: Engine.connectAtStart = true
        }
    }
    // Band plan strip along the bottom of the spectrum.
    Label { text: qsTr("Band plan"); Accessible.role: Accessible.Heading; color: Theme.textDim; font.pixelSize: Theme.px(16) }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: [""].concat(Engine.bandPlans)
            delegate: AppButton {
                required property string modelData
                Accessible.name: qsTr("%1: %2").arg(qsTr("Band plan")).arg(text)
                text: modelData.length > 0 ? modelData : qsTr("Off")
                checked: Engine.bandPlan === modelData
                leftPadding: 10
                rightPadding: 10
                focusPolicy: Qt.NoFocus
                onClicked: Engine.bandPlan = modelData
            }
        }
    }
    // Band plan lock (Tyler K0OTC): the band plan sets and limits the mode
    // (HF); also in the mode chooser, and PLAN by the mode while it is on.
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Band plan lock (mode)")
            color: Theme.textDim
            font.pixelSize: Theme.px(16)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        AppButton {
            objectName: "bandPlanLock"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Band plan lock (mode)")).arg(text)
            text: Engine.bandPlanLock ? qsTr("On") : qsTr("Off")
            checked: Engine.bandPlanLock
            leftPadding: 10
            rightPadding: 10
            focusPolicy: Qt.NoFocus
            onClicked: Engine.bandPlanLock = !Engine.bandPlanLock
        }
    }
    // Felt taps (iPhone, Android phones): PTT, the step buttons, − / + and a
    // slider's drag; the system's own setting still applies.
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Felt taps (phones)")
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        AppButton {
            objectName: "hapticsOn"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Felt taps (phones)")).arg(text)
            text: Engine.hapticsOn ? qsTr("On") : qsTr("Off")
            checked: Engine.hapticsOn
            Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
            focusPolicy: Qt.NoFocus
            onClicked: Engine.hapticsOn = !Engine.hapticsOn
        }
    }
    // The first-run hints again (the gestures nothing on screen shows).
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Hints"); color: Theme.textDim; font.pixelSize: Theme.px(15); Layout.fillWidth: true }
        AppButton {
            objectName: "showHints"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Hints")).arg(text)
            text: qsTr("Show again")
            Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
            focusPolicy: Qt.NoFocus
            onClicked: Engine.showHints()
        }
    }
    // New Android betas from GitHub (roadmap 111): only in the GitHub APK
    // (TestFlight, the App Store and Play update theirs).
    RowLayout {
        visible: Engine.updateCheckBuilt
        Layout.fillWidth: true
        Label {
            text: qsTr("New versions")
            color: Theme.textDim
            font.pixelSize: Theme.px(15)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        AppButton {
            objectName: "updateCheckOn"
            Accessible.name: qsTr("%1: %2").arg(qsTr("New versions")).arg(text)
            text: Engine.updateCheck ? qsTr("On") : qsTr("Off")
            checked: Engine.updateCheck
            Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitWidth)
            focusPolicy: Qt.NoFocus
            onClicked: Engine.updateCheck = !Engine.updateCheck
        }
    }
    Label {
        visible: Engine.updateCheckBuilt
        Layout.fillWidth: true
        text: (Engine.updateCheck
               ? qsTr("Asks GitHub once a day whether a newer beta is out (GitHub sees this device's address). ")
               : qsTr("Off: no new beta is looked for. "))
              + Engine.updateStatus
        color: Theme.textDim
        font.pixelSize: Theme.px(13)
        wrapMode: Text.Wrap
    }
    AppButton {
        objectName: "updateDownload"
        visible: Engine.updateCheckBuilt && Engine.updateBeta > 0
        text: Engine.updateSameBeta ? qsTr("Download the released beta %1").arg(Engine.updateBeta)
                                    : qsTr("Download beta %1").arg(Engine.updateBeta)
        Layout.fillWidth: true
        focusPolicy: Qt.NoFocus
        onClicked: Qt.openUrlExternally(Engine.updateApkUrl)
    }
}
