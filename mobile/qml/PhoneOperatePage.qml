import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Operate page for phone-sized windows (iPhone, and a folded iPhone Duo).
// Portrait: frequency, spectrum, a tabbed control area and a full-width PTT.
// Landscape: spectrum on the left, tabs and PTT on the right.
Page {
    id: page
    background: Rectangle { color: Theme.background }
    // A hardware keyboard (Bluetooth on a phone, Android's DeX) or a USB
    // paddle adapter: the same keys as on the tablet page (only the CW
    // paddles worked here).
    focus: true
    OperateKeys {
        id: operateKeys
        page: page
        freqDialog: freqDialog
        drawer: controlsDrawer
    }
    Keys.onPressed: (event) => { if (operateKeys.press(event)) event.accepted = true }
    Keys.onReleased: (event) => { if (operateKeys.release(event)) event.accepted = true }
    Binding { target: Engine; property: "keyboardOnPage"; value: page.activeFocus }
    onActiveFocusChanged: operateKeys.pageFocusChanged(activeFocus)

    // Landscape layout (one header line, the step row with PTT) only where
    // the window is clearly wide: a near-square window (a split or pop-up
    // window on a foldable's inner screen, beta 43) got it, its header line
    // did not fit and pushed the page, PTT with it, off the right edge.
    readonly property bool landscape: width > height * 1.3
    // CW paddles on a phone: dit at the left edge and dah at the right, the
    // Controls button between (portrait) or the step buttons (landscape), so
    // each thumb keys its own paddle. The halves share this state.
    readonly property bool splitPaddles: ptt.paddleMode && ptt.enabled
    QtObject {
        id: paddleState
        property bool dit: false
        property bool dah: false
        onDitChanged: Engine.setCwPaddleState(dit, dah)
        onDahChanged: Engine.setCwPaddleState(dit, dah)
    }
    // PTT height, shared with the drawer's copy of the button. PTT runs
    // along the bottom in both orientations; lower in landscape.
    readonly property real pttHeight: landscape ? Math.round(Theme.touchTarget * 1.2) : 64

    function formatMhz(mhz) {
        const hz = Math.round(mhz * 1e6)
        const mhzPart = Math.floor(hz / 1e6)
        const khzPart = Math.floor((hz % 1e6) / 1e3)
        const hzPart = hz % 1e3
        return mhzPart + "." + String(khzPart).padStart(3, "0") + "." + String(hzPart).padStart(3, "0")
    }

    // A layout switch (rotation, fold/unfold) destroys this page; a held
    // PTT must not outlive its button.
    Component.onCompleted: page.forceActiveFocus()
    Component.onDestruction: {
        Engine.keyboardOnPage = false
        Engine.stopTransmit("layout changed")
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        topPadding: header.SafeArea.margins.top
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            Label {
                text: Engine.radioName
                color: Theme.textDim
                font.pixelSize: 15
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Network quality (dot and ms); tap for the details.
            NetworkBadge {
                compact: true
                TapHandler { onTapped: networkDialog.open() }
            }
            // App version and build, so a screenshot or report says which.
            Label {
                text: Engine.appVersion
                color: Theme.textDim
                font.pixelSize: 11
            }
            // Controls open from the button beside PTT (thumb reach); the
            // top row keeps only the rest.
            // The rest behind one button: a phone's top row has no room.
            TopButton {
                id: moreButton
                text: "⋯"
                font.pixelSize: 22
                focusPolicy: Qt.NoFocus
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                Menu {
                    id: moreMenu
                    modal: true
                    dim: false
                    MenuItem { text: qsTr("Radio Setup"); onTriggered: radioSetup.open() }
                    MenuItem { text: qsTr("Manual"); onTriggered: Qt.openUrlExternally(Engine.manualUrl) }
                    MenuItem { text: qsTr("Log"); onTriggered: logPage.open() }
                    MenuItem { text: qsTr("Disconnect"); onTriggered: Engine.disconnectRadio() }
                }
            }
        }
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        // The page's safe area, not the grid's own: the grid's margins move
        // the grid, which changes its own safe area (a binding loop that left
        // the layout unsettled after a rotation on Android).
        anchors.leftMargin: Theme.spacing / 2 + page.SafeArea.margins.left
        anchors.rightMargin: Theme.spacing / 2 + page.SafeArea.margins.right
        anchors.topMargin: Theme.spacing / 2
        anchors.bottomMargin: Theme.spacing / 2 + Math.max(page.SafeArea.margins.bottom, 8)
        columns: 1
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2

        // ---- receive view: frequency, bands, spectrum, steps; the full
        // width in both orientations ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing / 2

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: page.formatMhz(Engine.frequencyMhz)
                    color: Theme.text
                    font.pixelSize: 30
                    font.family: "monospace"
                    TapHandler { onTapped: freqDialog.open() }
                }
                ModeButton { fontSize: 18 }
                // Landscape: the step first, then the slice at a glance and
                // in reach (one row).
                StepButton {
                    visible: page.landscape
                    fontSize: 13
                    Layout.leftMargin: Theme.spacing / 2
                }
                // Clipped rather than widening the page when the line runs
                // out of room (the rarer flags go first, at the right).
                InfoPanel {
                    visible: page.landscape
                    singleLine: true
                    showStep: false
                    clip: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                // Portrait: the room right of the mode takes the always-on
                // settings, left-aligned: the step, then antennas, filter
                // width and AGC. The row under the frequency has the slice,
                // speaker and lock, then the optional SPLIT and DSP flags.
                ColumnLayout {
                    visible: !page.landscape
                    spacing: 0
                    // No margin on the narrowest phones, or AGC meets the edge.
                    Layout.leftMargin: page.width < 400 ? 0 : Theme.spacing
                    StepButton {
                        Layout.alignment: Qt.AlignLeft
                        fontSize: 13
                        heightScale: 0.55
                    }
                    // The core settings: antennas, filter width, AGC.
                    InfoPanel {
                        Layout.alignment: Qt.AlignLeft
                        singleLine: true
                        showStep: false
                        showIdentity: false
                        showControls: false
                        showOptions: false
                        rowScale: 0.55
                        fontScale: page.width < 400 ? 0.88 : 1.0
                    }
                }
                Item { Layout.fillWidth: true }
            }
            // Tight under the frequency row: no gap, a lower row (still a
            // finger's height for its targets).
            InfoPanel {
                visible: !page.landscape
                showStep: false
                showCore: false
                singleLine: true
                rowScale: 0.75
                Layout.fillWidth: true
                Layout.topMargin: -Theme.spacing / 2
            }

            // Horizontally scrolling band strip. Left out in very low
            // landscape (a phone's outer screen, small iPhones), where the
            // display needs the room; the Band button on it picks bands.
            RowLayout {
                visible: !(page.landscape && Theme.tightHeight) && Engine.tuneRowsShown
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.touchTarget
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    visible: Engine.bandsAvailable
                    model: Engine.bands
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        leftPadding: 2
                        rightPadding: 2
                        width: Theme.buttonWidth
                        height: Theme.touchTarget
                        checked: Engine.activeBand === modelData
                        onClicked: Engine.selectBand(modelData)
                    }
                }
            }

            SliceBar {
                id: sliceBar
                Layout.fillWidth: true
                compact: true
            }

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                rfLabelWidth: sliceBar.rightMeterWidth
                // The band row is left out there, or hidden by F/R: Band
                // picks the bands.
                bandPicker: (page.landscape && Theme.tightHeight) || !Engine.tuneRowsShown
                tuneRowsToggle: true
                // Lower in a short portrait window (a near-square split
                // window), so PTT stays on screen.
                Layout.minimumHeight: page.landscape ? 90 : page.height < 620 ? 100 : 160
                // A swipe in the open drawer must not tune.
                covers: [controlsDrawer.background]
            }

            // Portrait: only the step buttons, gone with F/R. Landscape: the
            // controls button and PTT stay when F/R hides the steps.
            RowLayout {
                visible: page.landscape || Engine.tuneRowsShown
                Layout.fillWidth: true
                spacing: 4
                PttButton {
                    visible: page.landscape && page.splitPaddles && !controlsDrawer.visible
                    paddleSide: "left"
                    paddleState: paddleState
                    Layout.preferredWidth: Math.max(110, page.width * 0.15)
                    Layout.preferredHeight: Theme.touchTarget
                    fontPx: 22
                }
                // Step buttons: land on round frequencies (tuneByHz); FM
                // channel steps take the radio's STEP (Theme.stepButtons).
                Repeater {
                    model: Engine.tuneRowsShown ? Theme.stepButtons(Engine.stepHz, Engine.radioStepButtons) : []
                    delegate: AppButton {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "−") + Theme.stepLabel(Math.abs(modelData))
                        leftPadding: 1
                        rightPadding: 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: 30
                        Layout.preferredWidth: Theme.buttonWidth
                        Layout.preferredHeight: Theme.touchTarget
                        // Held: steps on (Larry Averbeck), from 0.4 s, 8 a second.
                        autoRepeat: true
                        autoRepeatDelay: 400
                        autoRepeatInterval: 125
                        onClicked: Engine.tuneByHz(modelData)
                    }
                }
                // With the steps hidden the controls button and PTT stay at
                // the right, where the thumb finds them.
                Item {
                    visible: !Engine.tuneRowsShown
                    Layout.fillWidth: true
                }
                // Landscape: the controls button and PTT end this row, so
                // the low screen keeps its height for the spectrum.
                AppButton {
                    visible: page.landscape
                    Accessible.name: qsTr("Controls")
                    contentItem: Item {
                        MenuIcon { anchors.centerIn: parent; width: 20; height: 16 }
                    }
                    Layout.preferredWidth: Theme.touchTarget * 1.2
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: controlsDrawer.open()
                }
                // Hidden while the drawer is open: the drawer carries its own,
                // and on some devices (Android insets) this one would peek out
                // under it as a second PTT.
                PttButton {
                    visible: page.landscape && !controlsDrawer.visible && !digiHidden
                    paddleSide: page.splitPaddles ? "right" : "both"
                    paddleState: page.splitPaddles ? paddleState : null
                    Layout.preferredWidth: page.splitPaddles ? Math.max(110, page.width * 0.15) : Math.max(150, page.width * 0.2)
                    Layout.preferredHeight: Theme.touchTarget
                    fontPx: 22
                }
            }
        }

        // ---- Portrait: PTT along the bottom, the controls button beside it
        // (landscape puts both at the end of the step row); the controls
        // live in a drawer. ----
        ColumnLayout {
            visible: !page.landscape
            Layout.fillWidth: true
            spacing: Theme.spacing / 2

            Label {
                Layout.fillWidth: true
                visible: !Engine.transmitAvailable
                text: Engine.transmitUnavailableReason
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                visible: Engine.transmitting
                text: qsTr("%1 W   SWR %2").arg(Engine.forwardPowerW.toFixed(0))
                      .arg(Engine.swrValid ? Engine.swr.toFixed(1) : "–")
                color: Engine.swrValid && Engine.swr > 2.5 ? Theme.transmit : Theme.text
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                PttButton {
                    visible: page.splitPaddles && !controlsDrawer.visible
                    paddleSide: "left"
                    paddleState: paddleState
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 26
                }
                AppButton {
                    Accessible.name: qsTr("Controls")
                    // Without PTT (FT8 / FT4) a plain full-width row, so the
                    // waterfall gets the height.
                    Layout.preferredWidth: Math.max(110, page.width * 0.22)
                    Layout.fillWidth: ptt.digiHidden
                    Layout.preferredHeight: ptt.digiHidden ? Theme.touchTarget : page.pttHeight
                    onClicked: controlsDrawer.open()
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            MenuIcon { width: 18; height: 14; anchors.verticalCenter: parent.verticalCenter }
                            Label {
                                text: qsTr("Controls")
                                color: Theme.buttonText
                                font.pixelSize: 15
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
                PttButton {
                    id: ptt
                    visible: !controlsDrawer.visible && !digiHidden
                    paddleSide: page.splitPaddles ? "right" : "both"
                    paddleState: page.splitPaddles ? paddleState : null
                    Layout.fillWidth: true
                    Layout.preferredWidth: page.splitPaddles ? 1 : -1
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 26
                }
            }
        }
    }

    // Controls slide in over the waterfall: from the right in landscape, from
    // the bottom in portrait. No dimming, so the waterfall stays visible; a
    // tap outside or a swipe back closes it. Edge swipes do not open it
    // (dragMargin 0): in landscape the PTT strip shares that edge, and a PTT
    // press must never turn into a drawer drag.
    Drawer {
        id: controlsDrawer
        objectName: "controlsDrawer"
        edge: page.landscape ? Qt.RightEdge : Qt.BottomEdge
        width: page.landscape ? Math.min(page.width * 0.5, 460) : page.width
        height: page.landscape ? (parent ? parent.height : page.height) : Math.min(page.height * 0.55, 520)
        // To the window's edges: Qt otherwise keeps popups inside the safe
        // area (above Android's navigation bar) and the drawer stopped short
        // of the page's bottom row. The content below keeps the insets.
        margins: 0
        dim: false
        dragMargin: 0
        // A drawer opening while PTT is held would hide the button.
        onAboutToShow: {
            Engine.setPtt(false, "controls drawer opened")
            Engine.setCwKey(false, "controls drawer opened")
        }
        background: Rectangle {
            color: Theme.surface
            opacity: 0.96
            radius: Theme.radius
        }

        // The controls scroll above the drawer's own PTT/KEY, which sits where
        // the page's button is: the drawer never covers it.
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            // Qt already pads a popup's content by the safe area; on top of
            // that the same bottom inset as the page's own bottom row, so the
            // drawer's PTT lands where the page's was.
            anchors.bottomMargin: Theme.spacing / 2 + Math.max(0, 8 - controlsDrawer.SafeArea.margins.bottom)
            spacing: Theme.spacing
            ControlsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            // Close where the Controls button was, so the same spot opens and
            // closes the drawer without a tap on the spectrum.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                PttButton {
                    visible: page.splitPaddles
                    paddleSide: "left"
                    paddleState: paddleState
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 26
                }
                AppButton {
                    objectName: "closeControls"
                    Layout.fillWidth: Engine.digiRunning && !Engine.rade
                    Accessible.name: qsTr("Close controls")
                    Layout.preferredWidth: Math.max(110, page.width * 0.22)
                    Layout.preferredHeight: Engine.digiRunning && !Engine.rade ? Theme.touchTarget : page.pttHeight
                    onClicked: controlsDrawer.close()
                    contentItem: Label {
                        text: qsTr("Close")
                        color: Theme.buttonText
                        font.pixelSize: 15
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                PttButton {
                    visible: !digiHidden
                    paddleSide: page.splitPaddles ? "right" : "both"
                    paddleState: page.splitPaddles ? paddleState : null
                    Layout.fillWidth: true
                    Layout.preferredWidth: page.splitPaddles ? 1 : -1
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 26
                }
            }
        }
    }

    LogPage { id: logPage }
    NetworkDialog { id: networkDialog }
    RadioSetupPage { id: radioSetup }
    SetupSuggestion { onSetupRequested: radioSetup.open() }
    FrequencyDialog {
        id: freqDialog
        onClosed: page.forceActiveFocus()
    }
}
