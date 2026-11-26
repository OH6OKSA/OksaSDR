import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

// Radio chooser: radios on this network, SmartLink (remote) radios, and a
// connect-by-address field. Scrolls so it fits a phone.
Page {
    id: page
    background: Rectangle { color: Theme.background }
    // + Account pressed: the sign-in form for another SmartLink account,
    // the signed-in one kept until it succeeds.
    property bool addingAccount: false
    Connections {
        target: Engine
        function onSmartLinkChanged() {
            if (Engine.smartLinkState === "signingIn")
                page.addingAccount = false
        }
    }

    // One tappable radio row (LAN or SmartLink).
    // A radio to connect to: a raised card with an accent edge and a
    // Connect chip, so it reads as something to tap (a tester took the plain
    // row for a label).
    component RadioRow: ItemDelegate {
        id: row
        property string title
        property string subtitle
        // A third line when there is one (a saved radio's address, which the
        // radio's own name otherwise pushed out of sight: Mike VA3MW).
        property string detail
        // The title as a screen reader says it: a favourite's "★ " is read
        // "black star", so its card says "Favourite" instead.
        property string spokenTitle: title
        // One button for VoiceOver and TalkBack: the card's lines in one
        // name (an ItemDelegate's name comes from text, which is not set),
        // what it shows as a state ("in use", "not reachable") with them,
        // and a double tap connecting as a tap does.
        Accessible.role: Accessible.Button
        Accessible.name: [spokenTitle, subtitle, detail].filter(t => t && t.length > 0).join(", ")
        Accessible.onPressAction: if (row.enabled) row.clicked()
        Layout.fillWidth: true
        // Taller with a large system text size (Android's font size), so the
        // lines stay inside the card (Ian Gilchrist VK1HF).
        implicitHeight: Math.max(2 * Theme.touchTarget + (detail.length > 0 ? 18 : 0),
                                 implicitContentHeight + topPadding + bottomPadding)
        enabled: Engine.connectionState === "idle"
        background: Rectangle {
            color: row.down ? Qt.lighter(Theme.surfaceRaised, 1.3) : Theme.surfaceRaised
            radius: Theme.radius
            border.width: 2
            border.color: row.enabled ? Theme.accent : Theme.surface
            opacity: row.enabled ? 1.0 : 0.6
        }
        // On a narrow page (page.stackCards) the name has the card's first
        // row for itself, on two lines when it needs them, and "Connect ›"
        // sits under it beside the second line: beside the chip a 344 pt
        // screen left the name room for "★ OT…" at a large text size, and
        // for half a name at any (Ian Gilchrist VK1HF; roadmap 164).
        property bool stacked: false
        contentItem: Loader { sourceComponent: row.stacked ? stackedContent : besideContent }
        Component {
            id: besideContent
            RowLayout {
                spacing: Theme.spacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { Accessible.ignored: true; text: row.title; color: Theme.text; font.pixelSize: Theme.px(20); elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { Accessible.ignored: true; text: row.subtitle; visible: text.length > 0; color: Theme.textDim; font.pixelSize: Theme.px(15); elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { Accessible.ignored: true; text: row.detail; visible: text.length > 0; color: Theme.textDim; font.pixelSize: Theme.px(15); elide: Text.ElideRight; Layout.fillWidth: true }
                }
                ConnectChip { visible: row.enabled; down: row.down }
            }
        }
        Component {
            id: stackedContent
            ColumnLayout {
                spacing: 2
                Label {
                    Accessible.ignored: true
                    text: row.title
                    color: Theme.text
                    font.pixelSize: Theme.px(20)
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { Accessible.ignored: true; text: row.subtitle; visible: text.length > 0; color: Theme.textDim; font.pixelSize: Theme.px(15); elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { Accessible.ignored: true; text: row.detail; visible: text.length > 0; color: Theme.textDim; font.pixelSize: Theme.px(15); elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    ConnectChip { visible: row.enabled; down: row.down }
                }
            }
        }
    }

    // "Connect ›" on a radio's card, beside the name or under it. Its text
    // keeps the buttons' size at a large text size (the proposal's part (b),
    // chosen by the user on 2026-11-25: "164b mukaan").
    component ConnectChip: Rectangle {
        property bool down: false
        implicitWidth: chipLabel.implicitWidth + 2 * Theme.spacing
        implicitHeight: Theme.touchTarget * 0.8
        radius: Theme.radius
        color: down ? Theme.buttonFaceDown : Theme.buttonFace
        Label {
            id: chipLabel
            Accessible.ignored: true
            anchors.centerIn: parent
            text: qsTr("Connect ›")
            color: Theme.buttonText
            // A button's text: it keeps its size at a large system text
            // size, as the page's other buttons do (roadmap 164 b). Grown
            // with the text it took the room from the line beside it: at
            // 135 % on a narrow phone "Available" read "Avail…".
            font.pixelSize: Math.round(16 * Theme.bigScale)
        }
    }

    // The room the narrowest card on the page (a SmartLink radio's: inside
    // the box, ☆ beside it) leaves a name beside "Connect ›". Under some 23
    // characters of it (12 em), every card gives the name its own row
    // (RadioRow.stacked): each phone upright, a narrow Split View pane, and
    // wider windows only at a large text size. One rule for the page, so its
    // cards look alike and none changes as a radio appears.
    FontMetrics { id: chipMetrics; font.pixelSize: Theme.px(16) }
    readonly property real nameRoomBeside: Math.min(scroll.availableWidth, 560)
                                           - 2 * Theme.spacing                  // the SmartLink box's margins
                                           - Theme.spacing - Theme.touchTarget  // ☆, ✕ or ✎ beside the card
                                           - 24                                 // the card's own padding (Basic style)
                                           - Theme.spacing - (chipMetrics.advanceWidth(qsTr("Connect ›")) + 2 * Theme.spacing)
    readonly property bool stackCards: nameRoomBeside < 12 * Theme.px(20)

    PageScroll {
        id: scroll
        anchors.fill: parent
        // Clear of the notch / Dynamic Island and the home indicator.
        // The page's safe area, not the ScrollView's own: margins taken from
        // the item they move made a binding loop (logged on iPad).
        anchors.leftMargin: 2 * Theme.spacing + page.SafeArea.margins.left
        anchors.rightMargin: 2 * Theme.spacing + page.SafeArea.margins.right
        anchors.topMargin: 2 * Theme.spacing + page.SafeArea.margins.top
        anchors.bottomMargin: Theme.spacing + page.SafeArea.margins.bottom

        // At most a phone-and-a-half wide, centred: on an iPad, a Mac or an
        // external display the rows ran the window's whole width.
        ColumnLayout {
            width: Math.min(scroll.availableWidth, 560)
            x: (scroll.availableWidth - width) / 2
            spacing: Theme.spacing

            RowLayout {
                Layout.fillWidth: true
                // Smaller to fit beside the version and the buttons: with a
                // large system text size it ran over the version (Ian VK1HF).
                Label {
                    text: qsTr("OksaSDR")
                    color: Theme.text
                    font.pixelSize: Theme.px(34)
                    font.bold: true
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 16
                    Layout.fillWidth: true
                }
                Label { text: "v" + Engine.appVersion; color: Theme.textDim; font.pixelSize: Theme.px(13) }
                AppButton { text: qsTr("Manual"); onClicked: Qt.openUrlExternally(Engine.manualUrl) }
                AppButton { text: qsTr("Log"); onClicked: logPage.open() }
            }

            // Cancel beside Connecting…: a connect (or a dropped session's
            // retry) that does not come through no longer holds the page.
            RowLayout {
                Layout.fillWidth: true
                // Roadmap 138: a session that ended without the operator's
                // Disconnect says why (the radio, SmartLink, the network).
                readonly property string ended: Engine.disconnectCause.length > 0
                                                ? qsTr("The connection ended: %1.").arg(Engine.disconnectCause) : ""
                // Roadmap 189: at a start with "connect to the radio used
                // last" chosen, the wait for that radio shows here, with
                // the same Cancel.
                readonly property bool waiting: Engine.connectionState === "idle" && Engine.startConnectWaiting.length > 0
                visible: Engine.connectionState !== "idle" || Engine.lastError.length > 0 || ended.length > 0 || waiting
                spacing: 12
                Label {
                    objectName: "connectStatus"
                    Layout.fillWidth: true
                    text: parent.waiting ? qsTr("Waiting for %1…").arg(Engine.startConnectWaiting)
                          : Engine.connectionState === "connecting"
                          ? (parent.ended.length > 0 ? qsTr("Connecting again… (%1)").arg(Engine.disconnectCause) : qsTr("Connecting…"))
                          // The link's own error for a session that was up: said
                          // as its end ("The connection ended: ...").
                          : Engine.lastError.length > 0 && Engine.lastError !== Engine.disconnectCause ? Engine.lastError
                          : parent.ended.length > 0 ? parent.ended : Engine.lastError
                    color: Engine.connectionState === "connecting" || parent.waiting ? Theme.textDim : Theme.warning
                    font.pixelSize: Theme.px(17)
                    wrapMode: Text.Wrap
                }
                AppButton {
                    objectName: "cancelConnect"
                    visible: Engine.connectionState === "connecting" || parent.waiting
                    text: qsTr("Cancel")
                    focusPolicy: Qt.NoFocus
                    onClicked: parent.waiting ? Engine.cancelStartConnect() : Engine.disconnectRadio("Cancel on the connect page")
                }
            }
            // The connection doctor: what blocks the connection, step by step
            // (MobileEngineDoctor.cpp). Offered after a failed connect, and
            // always for the last radio tried.
            AppButton {
                objectName: "checkConnection"
                visible: Engine.connectionState === "idle" && Engine.startConnectWaiting.length === 0
                text: Engine.lastError.length > 0 ? qsTr("Why? Check the connection") : qsTr("Check the connection")
                focusPolicy: Qt.NoFocus
                Layout.preferredHeight: Theme.touchTarget
                onClicked: {
                    Engine.checkConnection()
                    connectionCheck.open()
                }
            }

            // ---- This network ----
            // The demo radio's switch on the heading's line (Ian Gilchrist
            // VK1HF), so hiding it adds no row.
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: Engine.lanRadioCount > 0 || (Engine.showDemoRadio && Engine.radios.count > 0)
                          ? qsTr("On this network") : qsTr("Searching this network…")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(17)
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Demo")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(14)
                }
                AppButton {
                    objectName: "demoRadioShown"
                    text: Engine.showDemoRadio ? qsTr("Shown") : qsTr("Hidden")
                    checked: Engine.showDemoRadio
                    leftPadding: 10
                    rightPadding: 10
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.showDemoRadio = !Engine.showDemoRadio
                }
            }
            Repeater {
                model: Engine.radios
                delegate: RadioRow {
                    stacked: page.stackCards
                    required property int index
                    required property string name
                    required property string nickname
                    required property string model
                    required property string address
                    required property string version
                    required property string transport
                    required property bool inUse
                    visible: transport !== "sim" || Engine.showDemoRadio
                    title: (nickname.length > 0 ? nickname + " — " : "") + (model.length > 0 ? model : name)
                    subtitle: transport === "sim"
                              ? qsTr("Built-in demo radio (receive only)")
                              : address + "   v" + version + (inUse ? qsTr("   in use") : "")
                    onClicked: Engine.connectToRadio(index)
                }
            }

            // ---- SmartLink (remote) ----
            Label {
                text: qsTr("SmartLink (remote)")
                color: Theme.textDim
                font.pixelSize: Theme.px(17)
                Layout.topMargin: Theme.spacing
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: smartLinkBox.implicitHeight + 2 * Theme.spacing
                radius: Theme.radius
                color: Theme.surface

                ColumnLayout {
                    id: smartLinkBox
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacing / 2

                    // Favourite radios of every account (roadmap 62): a tap
                    // connects, changing the account first when it is
                    // another's; ✕ takes one off the list.
                    Repeater {
                        model: page.addingAccount ? [] : Engine.smartLinkFavourites
                        delegate: RowLayout {
                            id: favRow
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spacing
                            RadioRow {
                                objectName: "smartLinkFavourite" + favRow.index
                                stacked: page.stackCards
                                title: "★ " + favRow.modelData.name
                                spokenTitle: qsTr("Favourite %1").arg(favRow.modelData.name)
                                subtitle: favRow.modelData.account
                                          + (favRow.modelData.online ? "" : favRow.modelData.active && Engine.smartLinkState === "signedIn"
                                                                            ? qsTr("   not online") : "")
                                enabled: Engine.connectionState === "idle" && favRow.modelData.canConnect
                                onClicked: Engine.connectSmartLinkFavourite(favRow.index)
                            }
                            AppButton {
                                objectName: "smartLinkFavouriteRemove" + favRow.index
                                text: "✕"
                                font.pixelSize: Theme.px(20)
                                Accessible.name: qsTr("Remove %1 from the favourites").arg(favRow.modelData.name)
                                Layout.preferredWidth: Theme.touchTarget
                                Layout.fillHeight: true
                                focusPolicy: Qt.NoFocus
                                onClicked: Engine.removeSmartLinkFavourite(favRow.index)
                            }
                        }
                    }

                    // The accounts on this device when there are several: the
                    // active one checked, a tap changes to another.
                    Flow {
                        visible: !page.addingAccount && Engine.smartLinkAccounts.length > 1
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: Engine.smartLinkAccounts
                            delegate: AppButton {
                                required property int index
                                required property var modelData
                                objectName: "smartLinkAccount" + index
                                text: modelData.label
                                checked: modelData.active
                                leftPadding: 10
                                rightPadding: 10
                                focusPolicy: Qt.NoFocus
                                enabled: Engine.connectionState === "idle"
                                onClicked: Engine.switchSmartLinkAccount(index)
                            }
                        }
                    }

                    // Signed out: e-mail and password (FlexRadio account).
                    ColumnLayout {
                        visible: Engine.smartLinkState === "signedOut" || (page.addingAccount && Engine.smartLinkState !== "signingIn")
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: page.addingAccount ? qsTr("Sign in with another FlexRadio SmartLink account. The one signed in now is kept: change between them with a tap.")
                                                     : qsTr("Sign in with your FlexRadio SmartLink account to use your radio from anywhere.")
                            color: Theme.textDim
                            font.pixelSize: Theme.px(14)
                            wrapMode: Text.Wrap
                        }
                        TextField {
                            id: emailField
                            objectName: "smartLinkEmail"
                            Accessible.name: qsTr("SmartLink e-mail")
                            Layout.fillWidth: true
                            text: page.addingAccount ? "" : Engine.smartLinkEmail
                            placeholderText: qsTr("E-mail")
                            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                            font.pixelSize: Theme.px(17)
                        }
                        PasswordField {
                            id: passwordField
                            Accessible.name: qsTr("SmartLink password")
                            Layout.fillWidth: true
                            placeholderText: qsTr("Password")
                            font.pixelSize: Theme.px(17)
                            // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
                            EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
                            onAccepted: signIn.clicked()
                        }
                        // Forgot password? (roadmap 157, Bob Manley WB3BOB):
                        // FlexRadio's sign-in service mails its reset link
                        // to the e-mail typed above; the new password is set
                        // on its page, never here. Asked first; the answer
                        // shows under the button, the form's width (beside
                        // the button a narrow phone broke it into a column
                        // of short lines).
                        AppButton {
                            objectName: "smartLinkForgot"
                            text: qsTr("Forgot password?")
                            font.pixelSize: Theme.px(14)
                            leftPadding: 10
                            rightPadding: 10
                            enabled: emailField.text.indexOf("@") > 0 && Engine.smartLinkReset.state !== "sending"
                            focusPolicy: Qt.NoFocus
                            onClicked: confirmReset.open()
                        }
                        Label {
                            objectName: "smartLinkResetText"
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: Engine.smartLinkReset.text || ""
                            color: Engine.smartLinkReset.state === "failed" ? Theme.warning : Theme.textDim
                            font.pixelSize: Theme.px(13)
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            AppButton {
                                id: signIn
                                text: qsTr("Sign in to SmartLink")
                                Layout.fillWidth: true
                                enabled: emailField.text.length > 0 && passwordField.text.length > 0
                                onClicked: {
                                    Engine.signInSmartLink(emailField.text, passwordField.text)
                                    passwordField.text = ""
                                }
                            }
                            AppButton {
                                objectName: "smartLinkAddCancel"
                                visible: page.addingAccount
                                text: qsTr("Cancel")
                                onClicked: {
                                    passwordField.text = ""
                                    page.addingAccount = false
                                }
                            }
                            // An account whose sign-in is gone, with others
                            // on the device: off the list.
                            AppButton {
                                objectName: "smartLinkRemoveAccount"
                                visible: !page.addingAccount && Engine.smartLinkAccounts.length > 1
                                text: qsTr("Remove")
                                onClicked: {
                                    passwordField.text = ""
                                    Engine.signOutSmartLink()
                                }
                            }
                        }
                    }

                    // Signing in.
                    RowLayout {
                        visible: Engine.smartLinkState === "signingIn"
                        BusyIndicator { running: visible; implicitWidth: 32; implicitHeight: 32 }
                        Label { text: qsTr("Signing in to SmartLink…"); color: Theme.textDim }
                    }

                    // Signed in: account, remote radios, sign out.
                    ColumnLayout {
                        visible: !page.addingAccount && (Engine.smartLinkState === "signedIn" || Engine.smartLinkState === "offline")
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                // The server's callsign and name, else the
                                // account's e-mail.
                                readonly property string activeLabel: {
                                    const accounts = Engine.smartLinkAccounts
                                    for (let i = 0; i < accounts.length; ++i)
                                        if (accounts[i].active)
                                            return accounts[i].label
                                    return ""
                                }
                                text: Engine.smartLinkAccount.length > 0 ? Engine.smartLinkAccount
                                      : activeLabel.length > 0 ? activeLabel : qsTr("Signed in")
                                color: Theme.text
                                font.pixelSize: Theme.px(16)
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            AppButton {
                                objectName: "smartLinkAddAccount"
                                text: qsTr("+ Account")
                                focusPolicy: Qt.NoFocus
                                enabled: Engine.connectionState === "idle"
                                onClicked: page.addingAccount = true
                            }
                            // Two taps: the row appears where the sign-in
                            // form or the "Signing in" line was, and one
                            // stray tap there signed a tester out (the kept
                            // sign-in is removed with it).
                            AppButton {
                                id: signOut
                                property bool armed: false
                                text: armed ? qsTr("Sign out?") : qsTr("Sign out")
                                checked: armed
                                focusPolicy: Qt.NoFocus
                                onClicked: {
                                    if (armed) {
                                        armed = false
                                        Engine.signOutSmartLink()
                                    } else {
                                        armed = true
                                        disarm.restart()
                                    }
                                }
                                Timer { id: disarm; interval: 3000; onTriggered: signOut.armed = false }
                            }
                        }
                        Label {
                            visible: Engine.smartLinkState === "offline"
                            text: qsTr("SmartLink server not reachable. Retrying…")
                            color: Theme.warning
                            font.pixelSize: Theme.px(14)
                        }
                        Label {
                            visible: Engine.smartLinkState === "signedIn" && Engine.smartLinkRadios.length === 0
                            text: qsTr("No radios on this account are online.")
                            color: Theme.textDim
                            font.pixelSize: Theme.px(14)
                        }
                        Repeater {
                            model: Engine.smartLinkRadios
                            delegate: RowLayout {
                                id: wanRow
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacing
                                RadioRow {
                                    objectName: "smartLinkRadio" + wanRow.index
                                    stacked: page.stackCards
                                    title: (wanRow.modelData.nickname.length > 0 ? wanRow.modelData.nickname + " — " : "") + wanRow.modelData.model
                                    subtitle: wanRow.modelData.status + (wanRow.modelData.reachable ? "" : qsTr("   not reachable"))
                                    enabled: Engine.connectionState === "idle" && wanRow.modelData.reachable
                                    onClicked: Engine.connectSmartLinkRadio(wanRow.index)
                                }
                                // ☆ makes it a favourite, ★ takes it off.
                                AppButton {
                                    objectName: "smartLinkFavouriteToggle" + wanRow.index
                                    text: wanRow.modelData.favourite ? "★" : "☆"
                                    checked: wanRow.modelData.favourite
                                    font.pixelSize: Theme.px(22)
                                    Accessible.name: wanRow.modelData.favourite ? qsTr("Remove from the favourites")
                                                                                : qsTr("Add to the favourites")
                                    Layout.preferredWidth: Theme.touchTarget
                                    Layout.fillHeight: true
                                    focusPolicy: Qt.NoFocus
                                    onClicked: Engine.toggleSmartLinkFavourite(wanRow.index)
                                }
                            }
                        }
                    }
                }
            }

            // ---- By address ----
            Label {
                text: qsTr("By IP address or host name")
                color: Theme.textDim
                font.pixelSize: Theme.px(17)
                Layout.topMargin: Theme.spacing
            }
            // The radios entered here, in the order added (Mike Walker VA3MW:
            // several, friends' radios shared over Tailscale too, where
            // changing the one address took trial and error): a tap connects,
            // ✎ names, changes or removes one.
            Repeater {
                model: Engine.savedRadios
                delegate: RowLayout {
                    id: savedRow
                    required property int index
                    required property var modelData
                    readonly property string address: modelData.address || ""
                    readonly property string userName: modelData.name || ""
                    readonly property string radioName: modelData.radio || ""
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    RadioRow {
                        objectName: "savedRadio" + savedRow.index
                        stacked: page.stackCards
                        title: savedRow.userName || savedRow.radioName || savedRow.address
                        // The user's name over the radio's own, then the
                        // address on a line of its own.
                        readonly property bool named: savedRow.userName.length > 0 && savedRow.radioName.length > 0
                        readonly property string addressLine: [title === savedRow.address ? "" : savedRow.address,
                                   savedRow.modelData.last ? qsTr("last used") : ""].filter(t => t.length > 0).join("  ·  ")
                        subtitle: named ? savedRow.radioName : addressLine
                        detail: named ? addressLine : ""
                        onClicked: Engine.connectToAddress(savedRow.address)
                    }
                    AppButton {
                        objectName: "savedRadioEdit" + savedRow.index
                        text: "✎"
                        font.pixelSize: Theme.px(22)
                        Accessible.name: qsTr("Edit %1").arg(savedRow.address)
                        Layout.preferredWidth: Theme.touchTarget
                        Layout.fillHeight: true
                        enabled: Engine.connectionState === "idle"
                        onClicked: radioEditor.edit(savedRow.index, savedRow.modelData)
                    }
                }
            }
            AppButton {
                objectName: "addRadio"
                text: qsTr("+ Add a radio")
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                enabled: Engine.connectionState === "idle"
                onClicked: {
                    addressDialog.forEditor = false
                    addressDialog.startEntry = ""
                    addressDialog.actionText = qsTr("Connect")
                    addressDialog.open()
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter a radio's IP address, or a host name that leads to it (ABC on the keypad): a dynamic DNS name, or a Tailscale MagicDNS name with the Tailscale app on. SmartSDR's radio chooser and your router's device list both show the address. This device must reach the radio on the same network or over a VPN. Each radio is kept on this list, on this device only; ✎ names or removes one.")
                color: Theme.textDim
                font.pixelSize: Theme.px(14)
                wrapMode: Text.Wrap
            }
        }
    }

    LogPage { id: logPage }
    AddressDialog {
        id: addressDialog
        // A new radio connects (and joins the list); a saved one's new
        // address goes back to its editor.
        property bool forEditor: false
        onConnectRequested: (address) => forEditor ? (radioEditor.address = address) : Engine.connectToAddress(address)
    }
    // Forgot password? asks before anything is sent (roadmap 157).
    Dialog {
        id: confirmReset
        objectName: "confirmReset"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 2 * Theme.spacing : 400, 420)
        modal: true
        title: qsTr("Send a password reset e-mail?")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("FlexRadio's sign-in service mails a reset link to %1. The new password is set on its page, not in this app.").arg(emailField.text.trim())
                wrapMode: Text.Wrap
                color: Theme.text
            }
        }
        onAccepted: Engine.requestSmartLinkPasswordReset(emailField.text)
    }
    // A saved radio: the user's name for it, its address (on the keypad) and
    // removing it (asked again on the button).
    Dialog {
        id: radioEditor
        objectName: "radioEditor"
        property int index: -1
        property string address: ""
        property string radioName: ""
        property bool removing: false
        function edit(i, radio) {
            index = i
            address = radio.address || ""
            radioName = radio.radio || ""
            nameField.text = radio.name || ""
            removing = false
            open()
        }
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 24)
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
        Timer { id: removeTimer; interval: 3000; onTriggered: radioEditor.removing = false }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label { text: qsTr("Radio"); color: Theme.text; font.pixelSize: Theme.px(20); font.bold: true }
            Label { text: qsTr("Name"); color: Theme.textDim; font.pixelSize: Theme.px(15) }
            TextField {
                id: nameField
                objectName: "radioEditorName"
                Accessible.name: qsTr("Name for this radio")
                Layout.fillWidth: true
                font.pixelSize: Theme.px(18)
                placeholderText: radioEditor.radioName.length > 0 ? radioEditor.radioName : qsTr("e.g. Mike's FLEX")
                inputMethodHints: Qt.ImhNoPredictiveText
                maximumLength: 40
            }
            Label { text: qsTr("IP address or host name"); color: Theme.textDim; font.pixelSize: Theme.px(15) }
            AppButton {
                objectName: "radioEditorAddress"
                text: radioEditor.address
                font.pixelSize: Theme.px(18)
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                onClicked: {
                    addressDialog.forEditor = true
                    addressDialog.startEntry = radioEditor.address
                    addressDialog.actionText = qsTr("OK")
                    addressDialog.open()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing
                AppButton {
                    objectName: "radioEditorRemove"
                    text: radioEditor.removing ? qsTr("Remove it?") : qsTr("Remove")
                    leftPadding: 12
                    rightPadding: 12
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        if (radioEditor.removing) {
                            Engine.removeSavedRadio(radioEditor.index)
                            radioEditor.close()
                        } else {
                            radioEditor.removing = true
                            removeTimer.restart()
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Cancel")
                    leftPadding: 12
                    rightPadding: 12
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: radioEditor.close()
                }
                AppButton {
                    objectName: "radioEditorSave"
                    text: qsTr("Save")
                    leftPadding: 12
                    rightPadding: 12
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: if (Engine.editSavedRadio(radioEditor.index, radioEditor.address, nameField.text)) radioEditor.close()
                }
            }
        }
    }

    // The connection doctor's steps: each a mark, its title and what it found.
    Dialog {
        id: connectionCheck
        objectName: "connectionCheck"
        modal: true
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 24)
        height: Math.min(implicitHeight, parent.height - 24)
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Connection check")
                    color: Theme.text
                    font.pixelSize: Theme.px(20)
                    font.bold: true
                    Layout.fillWidth: true
                }
                AppButton {
                    objectName: "connectionCheckClose"
                    text: qsTr("Close")
                    focusPolicy: Qt.NoFocus
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: connectionCheck.close()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: Engine.connectionCheckTarget.length > 0
                text: Engine.connectionCheckTarget.indexOf("smartlink:") === 0
                      ? qsTr("SmartLink radio %1").arg(Engine.connectionCheckTarget.substring(10))
                      : qsTr("To %1").arg(Engine.connectionCheckTarget)
                color: Theme.textDim
                font.pixelSize: Theme.px(14)
                elide: Text.ElideRight
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Math.min(stepsColumn.implicitHeight, page.height * 0.6)
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    id: stepsColumn
                    width: parent.width
                    spacing: Theme.spacing
                    Repeater {
                        model: Engine.connectionCheck
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spacing
                            // The state as a coloured word, readable without colour too.
                            Label {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 52
                                text: modelData.state === "ok" ? qsTr("OK")
                                    : modelData.state === "fail" ? qsTr("Fails")
                                    : modelData.state === "warn" ? qsTr("Note")
                                    : modelData.state === "running" ? "…" : qsTr("Info")
                                color: modelData.state === "ok" ? Theme.good
                                     : modelData.state === "fail" ? Theme.transmit
                                     : modelData.state === "warn" ? Theme.warning : Theme.textDim
                                font.pixelSize: Theme.px(15)
                                font.bold: true
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: modelData.title; color: Theme.text; font.pixelSize: Theme.px(16); font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label {
                                    visible: text.length > 0
                                    text: modelData.detail
                                    color: Theme.textDim
                                    font.pixelSize: Theme.px(14)
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: Engine.connectionCheckRunning ? qsTr("Checking…") : qsTr("Nothing on the radio is changed by the check.")
                    color: Theme.textDim
                    font.pixelSize: Theme.px(13)
                    wrapMode: Text.Wrap
                }
                AppButton {
                    objectName: "connectionCheckAgain"
                    text: qsTr("Check again")
                    enabled: !Engine.connectionCheckRunning
                    focusPolicy: Qt.NoFocus
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: Engine.checkConnection()
                }
            }
        }
    }
}
