import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Setup › FTx (roadmap 131): FT8 / FT4 settings changed seldom, moved here
// from the FTx tab: the presence limit, Answer CQ's calls to a station and
// PSK Reporter. The same Engine calls and values.
ColumnLayout {
    id: ftxSetup
    spacing: Theme.spacing / 2
    // The presence limit's steps (minutes).
    readonly property var presenceSteps: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 30, 45, 60]
    function presenceStep(dir) {
        const now = Engine.digiPresenceMin
        if (dir > 0) {
            for (let i = 0; i < presenceSteps.length; ++i) {
                if (presenceSteps[i] > now) {
                    return presenceSteps[i]
                }
            }
            return now
        }
        for (let j = presenceSteps.length - 1; j >= 0; --j) {
            if (presenceSteps[j] < now) {
                return presenceSteps[j]
            }
        }
        return now
    }
    // The presence limit: Answer CQ and Keep CQ start no new QSO after
    // this long without a touch on the app.
    RowLayout {
        Layout.fillWidth: true
        Label {
            objectName: "digiPresence"
            text: qsTr("Auto stops after %1 min untouched").arg(Engine.digiPresenceMin)
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "digiPresenceDown"
            text: "−"
            Accessible.name: qsTr("Shorter presence limit")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiPresenceMin > 1
            onClicked: Engine.digiPresenceMin = ftxSetup.presenceStep(-1)
        }
        AppButton {
            objectName: "digiPresenceUp"
            text: "+"
            Accessible.name: qsTr("Longer presence limit")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiPresenceMin < 60
            onClicked: Engine.digiPresenceMin = ftxSetup.presenceStep(1)
        }
    }
    // Answer CQ's calls to one station before it moves on to another
    // (roadmap 102, Kurt DL5EAG): 2-20, one at a time.
    RowLayout {
        Layout.fillWidth: true
        Label {
            objectName: "digiAnswerCalls"
            text: qsTr("Answer CQ calls each station %1 times").arg(Engine.digiAnswerCalls)
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "digiAnswerCallsDown"
            text: "−"
            Accessible.name: qsTr("Fewer calls to each station")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiAnswerCalls > 2
            autoRepeat: true
            onClicked: Engine.digiAnswerCalls = Engine.digiAnswerCalls - 1
        }
        AppButton {
            objectName: "digiAnswerCallsUp"
            text: "+"
            Accessible.name: qsTr("More calls to each station")
            Layout.preferredWidth: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            enabled: Engine.digiAnswerCalls < 20
            autoRepeat: true
            onClicked: Engine.digiAnswerCalls = Engine.digiAnswerCalls + 1
        }
    }
    // PSK Reporter: the stations decoded here, reported (off by default).
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: qsTr("Report to PSK Reporter")
            color: Theme.textDim
            font.pixelSize: Theme.px(14)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppButton {
            objectName: "pskReporterOff"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Report to PSK Reporter")).arg(text)
            text: qsTr("Off")
            checked: !Engine.pskReporter
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.pskReporter = false
        }
        AppButton {
            objectName: "pskReporterOn"
            Accessible.name: qsTr("%1: %2").arg(qsTr("Report to PSK Reporter")).arg(text)
            text: qsTr("On")
            checked: Engine.pskReporter
            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 64
            onClicked: Engine.pskReporter = true
        }
    }
    Label {
        Layout.fillWidth: true
        visible: Engine.pskReporter && Engine.pskReporterStatus.length > 0
        text: Engine.pskReporterStatus
        color: Theme.textDim
        font.pixelSize: Theme.px(12)
        wrapMode: Text.WordWrap
    }
}
