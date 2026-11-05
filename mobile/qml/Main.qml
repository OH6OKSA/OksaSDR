import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

ApplicationWindow {
    id: window
    width: 1366
    height: 1024
    visible: true
    title: qsTr("OksaSDR")
    color: Theme.background
    // The Basic style takes sliders' fill and track, switches, menus, text
    // fields and default label text from the palette, which differs by
    // platform (Android's drew a slider's filled part dark and the rest
    // bright; a light system palette made the ⋯ menu and the text fields
    // white, while an iPad in dark mode has them black): the same
    // everywhere, the app's blue left of a slider's handle on a dark track.
    palette.dark: Theme.accent
    palette.midlight: Theme.surfaceRaised
    palette.light: "#2d3947"
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: "#05080b"
    palette.text: Theme.text
    palette.placeholderText: Theme.textDim

    // Phone-sized windows (iPhone, folded iPhone Duo, narrow Split View)
    // get the phone layout; it switches live on rotation or fold/unfold.
    // Adaptive metrics follow the room the pages have (Theme.qml): the
    // window less its safe area (status and navigation bars, cutouts), which
    // matters on a phone's outer screen in landscape.
    Binding {
        target: Theme; property: "windowWidth"
        value: pages.width - pages.SafeArea.margins.left - pages.SafeArea.margins.right
    }
    Binding {
        target: Theme; property: "windowHeight"
        value: pages.height - pages.SafeArea.margins.top - pages.SafeArea.margins.bottom
    }

    // VoiceOver / TalkBack: what changed is said without being asked: the
    // mode (the band plan lock's too) and transmit on / off. Quiet for the
    // first seconds of a connection, while the radio's state arrives.
    Item {
        id: announcer
        property bool ready: false
        function say(text) {
            if (ready) {
                Accessible.announce(text)
            }
        }
        Timer { id: announceSettle; interval: 3000; onTriggered: announcer.ready = true }
        Connections {
            target: Engine
            function onConnectionStateChanged() {
                announcer.ready = false
                if (Engine.connectionState === "connected") {
                    announceSettle.restart()
                }
            }
            function onModeChanged() {
                if (Engine.mode.length > 0) {
                    announcer.say(qsTr("Mode %1").arg(Engine.mode))
                }
            }
            function onTransmittingChanged() {
                announcer.say(Engine.transmitting ? qsTr("Transmitting") : qsTr("Receiving"))
            }
        }
    }
    readonly property bool phoneSized: Theme.phoneLayout
    // The page's layout, settled: the two bindings above update one after
    // the other, so a phone turning passed through a square window that
    // read as a tablet, and the page, its 3D view and waterfall were built
    // twice (the display went dark for seconds on rotation, WA2N).
    // The start value only: the assignment at completion ends the binding.
    property bool pagePhone: phoneSized
    Component.onCompleted: pagePhone = phoneSized
    onPhoneSizedChanged: Qt.callLater(() => { window.pagePhone = window.phoneSized })

    // The page follows the connection: radio list until connected.
    Loader {
        id: pages
        anchors.fill: parent
        sourceComponent: Engine.connectionState !== "connected" ? connectPage
                       : window.pagePhone ? phoneOperatePage : operatePage
    }

    // A SmartLink radio presented a different TLS certificate than the one
    // pinned for it on first use. The connect waits for the operator.
    Dialog {
        id: certDialog
        property string host
        property string expected
        property string presented
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 24)
        // Fixed size: a wrapped label inside an auto-sized dialog loops.
        height: Math.min(340, parent.height - 24)
        title: qsTr("Radio certificate changed")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            // From the dialog's fixed width, not availableWidth, which would
            // feed the wrapped height back into the dialog's own size.
            width: certDialog.width - certDialog.leftPadding - certDialog.rightPadding
            wrapMode: Text.Wrap
            text: qsTr("The radio at %1 now presents a different security certificate than before. This is normal after a firmware update or reset, but it can also mean someone is intercepting the connection.\n\nTrust the new certificate and connect?").arg(certDialog.host)
        }
        onAccepted: Engine.answerCertificate(true)
        onRejected: Engine.answerCertificate(false)
    }
    Connections {
        target: Engine
        function onCertificateChanged(host, expected, presented) {
            certDialog.host = host
            certDialog.expected = expected
            certDialog.presented = presented
            certDialog.open()
        }
    }

    // The radio takes one station at a time (multiFLEX off, or a one-client
    // licence over SmartLink) and another is on it: the connect waits, as
    // the desktop's Connected stations dialog asks before going on.
    Popup {
        id: conflictDialog
        objectName: "stationConflict"
        readonly property var c: Engine.stationConflict
        visible: !!c.open
        modal: true
        closePolicy: Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, window.width - 2 * Theme.spacing)
        margins: Theme.spacing
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
        // Esc, or the popup going away any other way while asked: cancel.
        onClosed: if (Engine.stationConflict.open) Engine.resolveStationConflict(0)
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: conflictDialog.c.full ? qsTr("All station slots are in use") : qsTr("Another station is on the radio")
                color: Theme.text
                font.bold: true
                font.pixelSize: Math.round(Theme.buttonFont * 1.6)
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: (conflictDialog.c.full
                       ? qsTr("%1 has no station slot left. These stations are on it:")
                       : conflictDialog.c.wan
                       ? qsTr("%1 takes one station at a time over SmartLink, and this one is on it:")
                       : qsTr("%1 takes one station at a time (multiFLEX is off), and this one is on it:"))
                      .arg(conflictDialog.c.radio || qsTr("The radio"))
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Repeater {
                model: conflictDialog.c.clients || []
                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: Theme.spacing / 2
                    Label {
                        Layout.fillWidth: true
                        text: ([modelData.program, modelData.station].filter(t => !!t).join(" — ") || qsTr("A station"))
                              + (modelData.txMhz > 0 ? qsTr(", TX %1 MHz").arg(Number(modelData.txMhz).toFixed(3)) : "")
                        color: Theme.text
                        font.bold: true
                        wrapMode: Text.Wrap
                    }
                    AppButton {
                        objectName: "stationConflictDisconnect" + index
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        text: qsTr("Disconnect it and connect")
                        focusPolicy: Qt.NoFocus
                        onClicked: Engine.resolveStationConflict(modelData.handle)
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: conflictDialog.c.full
                      ? qsTr("Disconnecting ends that station's session on the radio. Several devices of yours tell apart by their names: Radio Setup › Radio › This station's name.")
                      : qsTr("Disconnecting ends that station's session on the radio. To share the radio instead, turn multiFLEX on in SmartSDR's Radio Setup.")
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.8)
                wrapMode: Text.Wrap
            }
            AppButton {
                objectName: "stationConflictCancel"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                text: qsTr("Cancel")
                focusPolicy: Qt.NoFocus
                onClicked: Engine.resolveStationConflict(0)
            }
        }
    }

    Component { id: connectPage; ConnectPage {} }
    Component { id: operatePage; OperatePage {} }
    Component { id: phoneOperatePage; PhoneOperatePage {} }
}
