import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    id: page
    focus: true

    // Hardware keyboard (Magic Keyboard): arrows tune by step (Shift x10),
    // +/- zoom, F opens direct entry, F1-F12 send the CW memories, Esc stops
    // CW text. Space is hold-to-talk: press keys,
    // release unkeys, auto-repeat is ignored, and losing focus unkeys.
    // A hardware keyboard: tuning, zoom, frequency entry, F-key macros,
    // Space PTT and the CW paddles (shared with the phone page).
    OperateKeys {
        id: operateKeys
        page: page
        freqDialog: freqDialog
        drawer: controlsDrawer
    }
    Keys.onPressed: (event) => { if (operateKeys.press(event)) event.accepted = true }
    Keys.onReleased: (event) => { if (operateKeys.release(event)) event.accepted = true }
    // The engine keys Space PTT from GameController (iOS) only while the
    // page has the keyboard.
    Binding { target: Engine; property: "keyboardOnPage"; value: page.activeFocus }
    onActiveFocusChanged: operateKeys.pageFocusChanged(activeFocus)
    background: Rectangle { color: Theme.background }

    // Layout adapts from the 12.9" iPad Pro (1366x1024 pt) down to 1024x768
    // and portrait: smaller type below 1200 pt, controls stacked under the
    // spectrum in portrait, and a scrolling control column so nothing is cut.
    readonly property bool compact: width < 1200
    // The info panel stays beside the frequency in two rows, as in a wide
    // window: where the row does not fit, the frequency and the panel's
    // type shrink to fit it (a foldable's inner screen upright has 676 of
    // the 700 the row needs; the panel went under the frequency there and
    // the two orientations looked different). Under the frequency only
    // below 66 %: the iPhone Duo's inner screen upright (604 pt for the
    // row) needs 68 % in iOS's monospace, about the phone row's type. Measured on the left column, which the
    // pinned panel narrows, at full size (an HF frequency) and from two
    // probes, so the scale never feeds back into its own measure: the
    // panel's width is a fixed part (badge and icon paddings) plus a part
    // that follows the type, w(s) = fixed + perScale * s.
    readonly property real infoRoom: leftWidth - modeColumn.implicitWidth - Theme.spacing - 8
                                     - 3 * (compact ? Theme.spacing : 2 * Theme.spacing)
    readonly property real infoPerScale: (infoProbe.implicitWidth - infoProbe70.implicitWidth) / 0.3
    readonly property real infoFixed: infoProbe.implicitWidth - infoPerScale
    // 3 % to spare for the type's whole pixels (rounded, a scaled row came
    // out a few pixels wider than the line through the probes).
    readonly property real infoRaw: (infoRoom - infoFixed) / (freqMetrics.advanceWidth + infoPerScale)
    readonly property real infoFit: infoRaw >= 1 ? 1 : infoRaw * 0.97
    // The panel measured at that scale (infoProbeFit): the line through the
    // probes is only nearly straight (whole-pixel type, the lock's least
    // width), and at 683 x 735 it came out 13 px short, cutting off the play
    // triangle. The scale steps down by what the measured panel lacks, 2 px
    // to spare; no loop, as the probe follows infoFit, not infoScale. Under
    // the frequency is still decided from infoFit, and the step stops at
    // 0.66: in USB the iPhone Duo's inner screen fits at 0.66 with a pixel
    // short, and stepping below dropped the panel under the frequency,
    // leaving the room beside it empty.
    readonly property real infoShort: infoProbeFit.implicitWidth + 2
                                      - (infoRoom - freqMetrics.advanceWidth * infoFit)
    readonly property real infoFitted: infoShort > 0
        ? infoFit - infoShort / (freqMetrics.advanceWidth + infoPerScale) : infoFit
    readonly property bool infoUnder: infoFit < 0.66
    readonly property real infoScale: infoUnder ? 1 : Math.max(0.66, infoFitted)
    // The probes, measured and never seen: shown (opacity 0, no input, no
    // screen reader, no room) rather than hidden. In a hidden item a part
    // that appears later (the TX tag, the TX antenna, SPLIT, as the radio's
    // status comes in) is not laid out, so a probe could stay short and the
    // panel overflow: 626 x 890 cut off the play triangle in one run of
    // three, the same code fitted in the others.
    Item {
        width: 0
        height: 0
        clip: true
        opacity: 0
        enabled: false
        InfoPanel {
            id: infoProbe
            objectName: "infoProbe"
            measureOnly: true
            showStep: false
        }
        InfoPanel {
            id: infoProbe70
            objectName: "infoProbe70"
            measureOnly: true
            showStep: false
            fontScale: 0.7
        }
        InfoPanel {
            id: infoProbeFit
            objectName: "infoProbeFit"
            measureOnly: true
            showStep: false
            fontScale: page.infoFit
        }
    }
    // PTT height, shared with the drawer's copy of the button.
    // PTT runs the full width along the bottom in every orientation.
    readonly property real pttHeight: Math.round(Theme.touchTarget * 1.5)
    // Upright, the controls drawer can take the whole height (▲ beside
    // Close, as on the phone): FTx then lists its decodes on the full height,
    // e.g. on an iPhone Duo's inner screen, where half showed none. Kept
    // while the app runs.
    property bool controlsFull: false
    // Taller than wide in the room the page has (less the status and
    // navigation bars): a near-square foldable screen is then laid out the
    // same in both orientations (the OnePlus Open's 698 x 751 counted the
    // bars and turned to the portrait layout with 668 left).
    readonly property bool portrait: Theme.windowHeight > Theme.windowWidth
    // Landscape with the panel pinned: two columns, the panel the full
    // height at the right, PTT under the display only. Otherwise one column
    // and PTT the full width.
    readonly property bool twoColumns: !portrait && panelPinned
    // Landscape side column: the pinned control panel (none when hidden).
    // Fixed, so content can never widen it off screen. At least 320 as the
    // drawer: 287 on a 1024 pt window cut "TX 2170 Hz", "New DXCC" and the
    // voice keyer's F10 short.
    readonly property int sideWidth: Math.max(320, Math.min(360, width * 0.28))
    // The room inside the safe area, and the left column's share of it.
    readonly property real innerWidth: width - SafeArea.margins.left - SafeArea.margins.right - 2 * Theme.spacing
    readonly property real leftWidth: twoColumns ? innerWidth - sideWidth - Theme.spacing : innerWidth
    // In landscape the panel pins at the side only where the band buttons
    // still fit beside it; else (the iPhone Duo's inner screen, a narrow
    // window) it opens as the drawer. The grid could not shrink the left
    // column below them and pushed the panel off the right edge.
    readonly property bool sideFits: innerWidth - sideWidth - Theme.spacing
                                     >= Math.max(560, Engine.bands.length * 40 + (Engine.bands.length - 1) * Theme.spacing / 2)
    // In portrait it pins under the display only in a tall window (an
    // iPad, the mini's 1089 pt too): lower, the panel's share left the
    // display no height (640 x 700, 683 x 1004); the drawer then.
    readonly property bool bottomFits: Theme.windowHeight >= 1050
    readonly property bool pinFits: portrait ? bottomFits : sideFits
    readonly property bool panelPinned: Engine.controlsVisible && pinFits

    function formatMhz(mhz) {
        // 14.074.500 style: MHz . kHz . Hz
        const hz = Math.round(mhz * 1e6)
        const mhzPart = Math.floor(hz / 1e6)
        const khzPart = Math.floor((hz % 1e6) / 1e3)
        const hzPart = hz % 1e3
        return mhzPart + "." + String(khzPart).padStart(3, "0") + "." + String(hzPart).padStart(3, "0")
    }

    // A narrow window (a foldable's inner screen, an iPad in portrait or
    // Split View): Setup, Manual, Log and Disconnect go behind ⋯ as on a
    // phone and the badge drops its words. The full row needs about 900 pt
    // in the devices' type; narrower, the radio's name ran under the badge.
    readonly property bool narrowTop: width < 1000

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            Label {
                text: Engine.radioName
                color: Theme.textDim
                font.pixelSize: 18
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Network quality; tap for the details.
            NetworkBadge {
                objectName: "networkBadge"
                compact: page.narrowTop
                Layout.rightMargin: Theme.spacing
                TapHandler { onTapped: networkDialog.open() }
            }
            // App version and build, so a screenshot or report says which.
            Label {
                text: "v" + Engine.appVersion
                color: Theme.textDim
                font.pixelSize: 13
            }
            TopButton {
                // Pinned panel: hide it. Hidden: open it as a drawer over the
                // waterfall.
                text: page.panelPinned ? qsTr("Hide controls") : qsTr("Controls")
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (page.panelPinned) {
                        Engine.controlsVisible = false
                    } else {
                        controlsDrawer.open()
                    }
                }
            }
            TopButton {
                visible: !page.narrowTop
                text: qsTr("Setup")
                focusPolicy: Qt.NoFocus
                onClicked: radioSetup.open()
            }
            TopButton {
                visible: !page.narrowTop
                text: qsTr("Manual")
                focusPolicy: Qt.NoFocus
                onClicked: Qt.openUrlExternally(Engine.manualUrl)
            }
            TopButton {
                visible: !page.narrowTop
                text: qsTr("Log")
                focusPolicy: Qt.NoFocus
                onClicked: logPage.open()
            }
            TopButton {
                visible: !page.narrowTop
                text: qsTr("Disconnect")
                onClicked: Engine.disconnectRadio()
            }
            TopButton {
                id: moreButton
                visible: page.narrowTop
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
        // Stay clear of the home indicator and rounded corners: iOS defers or
        // cancels touches at the screen edges, which would end a held PTT.
        // The page's safe area, not the grid's own (that is a binding loop:
        // the margins move the grid, which changes its own safe area).
        anchors.leftMargin: Theme.spacing + page.SafeArea.margins.left
        anchors.rightMargin: Theme.spacing + page.SafeArea.margins.right
        anchors.topMargin: Theme.spacing
        anchors.bottomMargin: Theme.spacing + Math.max(page.SafeArea.margins.bottom, 12)
        columnSpacing: Theme.spacing
        rowSpacing: Theme.spacing
        columns: page.twoColumns ? 2 : 1

        // Left (top in portrait): frequency, S-meter and the panadapter area.
        // Cells are placed by hand: a hidden panel must not let PTT slide
        // into its cell.
        ColumnLayout {
            Layout.row: 0
            Layout.column: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 320
            spacing: page.compact ? Theme.spacing / 2 : Theme.spacing

            RowLayout {
                spacing: page.compact ? Theme.spacing : 2 * Theme.spacing
                TextMetrics {
                    id: freqMetrics
                    font.pixelSize: page.compact ? 40 : 56
                    font.family: "monospace"
                    // At least an HF frequency, so 7 -> 14 MHz does not move
                    // the panel; a VHF one (one digit more) as it is.
                    text: {
                        const now = page.formatMhz(Engine.frequencyMhz)
                        return now.length > 10 ? now : "00.000.000"
                    }
                }
                Label {
                    text: page.formatMhz(Engine.frequencyMhz)
                    color: Theme.text
                    font.pixelSize: Math.round((page.compact ? 40 : 56) * page.infoScale)
                    font.family: "monospace"
                    // Tap for direct entry.
                    TapHandler { onTapped: freqDialog.open() }
                }
                // Mode, and the tuning step under it.
                ColumnLayout {
                    id: modeColumn
                    spacing: 0
                    ModeButton {
                        fontSize: page.compact ? 24 : 32
                    }
                    StepButton {
                        fontSize: page.compact ? 13 : 16
                    }
                }
                // The slice at a glance and in reach: antennas, width,
                // speaker, lock, DSP, AGC (each opens its choices).
                // Clipped at its room should the fit be off by a pixel or
                // two (rounding): a row wider than the column widened the page.
                InfoPanel {
                    visible: !page.infoUnder
                    showStep: false
                    fontScale: page.infoScale
                    clip: true
                    Layout.leftMargin: Theme.spacing
                    Layout.maximumWidth: Math.max(0, page.infoRoom - freqMetrics.advanceWidth * page.infoScale)
                }
                Item { Layout.fillWidth: true }
            }
            // Clipped rather than widening the page if even one line runs
            // out of room.
            InfoPanel {
                visible: page.infoUnder
                showStep: false
                singleLine: true
                clip: true
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }

            // Band stack (Flex): one row, buttons share the width. Each band
            // comes back to its last frequency and mode.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                visible: Engine.bandsAvailable && Engine.tuneRowsShown
                Repeater {
                    model: Engine.bands
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        leftPadding: 2
                        rightPadding: 2
                        Layout.fillWidth: true
                        // 13 bands fit a 600 pt window ("160m" in its type).
                        Layout.minimumWidth: 34
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: Theme.touchTarget
                        checked: Engine.activeBand === modelData
                        onClicked: Engine.selectBand(modelData)
                    }
                }
            }

            SliceBar {
                id: sliceBar
                Layout.fillWidth: true
                compact: page.compact
            }

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                rfLabelWidth: sliceBar.rightMeterWidth
                // Close under the slice row: the meters at its top corners
                // sit right below the buttons.
                Layout.topMargin: -Math.round(Theme.spacing / 2)
                // A swipe in the open drawer must not tune.
                covers: [controlsDrawer.background]
                // B/S hides the band row and the step buttons: Band then
                // picks the bands.
                bandPicker: !Engine.tuneRowsShown
                tuneRowsToggle: true
            }

            // Step buttons: land on round frequencies (tuneByHz).
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                visible: Engine.tuneRowsShown
                Repeater {
                    model: Theme.stepButtons(Engine.stepHz, Engine.radioStepButtons)
                    delegate: AppButton {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "−") + Theme.stepLabel(Math.abs(modelData))
                        Layout.fillWidth: true
                        Layout.minimumWidth: 40
                        Layout.preferredWidth: Theme.buttonWidth
                        Layout.preferredHeight: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        // Held: steps on (Larry Averbeck), from 0.4 s, 8 a second.
                        autoRepeat: true
                        autoRepeatDelay: 400
                        autoRepeatInterval: 125
                        onClicked: {
                            Engine.haptic("step")
                            Engine.tuneByHz(modelData)
                        }
                    }
                }
            }
        }

        // Right (bottom in portrait): the pinned controls, the full height
        // of the side; they scroll when they do not fit.
        ColumnLayout {
            visible: page.panelPinned
            Layout.row: page.twoColumns ? 0 : 1
            Layout.column: page.twoColumns ? 1 : 0
            // Beside the display and PTT; with PTT hidden (FT8 / FT4) the
            // display's row only, so the empty PTT row does not take height.
            Layout.rowSpan: page.twoColumns && ptt.visible ? 2 : 1
            Layout.preferredWidth: page.portrait ? -1 : page.sideWidth
            Layout.maximumWidth: page.portrait ? Number.POSITIVE_INFINITY : page.sideWidth
            Layout.fillWidth: page.portrait
            Layout.fillHeight: !page.portrait
            Layout.preferredHeight: page.portrait ? page.height * 0.42 : -1
            spacing: Theme.spacing

            // Pinned panel: section toggles; the chosen sections stack below.
            ControlsPanel {
                multiSelect: true
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        // PTT along the bottom: under the display (the pinned panel keeps
        // the side's full height), the full width otherwise.
        // In portrait the drawer covers this and carries a copy; hidden then
        // so no second PTT peeks out under the drawer.
        PttButton {
            id: ptt
            visible: !(controlsDrawer.visible && page.portrait) && !digiHidden
            Layout.row: page.twoColumns ? 1 : (page.panelPinned ? 2 : 1)
            Layout.column: 0
            Layout.fillWidth: true
            Layout.preferredHeight: page.pttHeight
            fontPx: 32
        }
    }

    // With the panel hidden, controls slide in from the right over the
    // waterfall (no dimming; tap outside or swipe back to close). Edge swipes
    // do not open it, so a PTT press near the edge never becomes a drag.
    Drawer {
        id: controlsDrawer
        objectName: "controlsDrawer"
        edge: page.portrait ? Qt.BottomEdge : Qt.RightEdge
        // At least 320 pt beside the display: a narrow window's 40 % cut the
        // panel's buttons short.
        width: page.portrait ? page.width : Math.min(Math.max(page.width * 0.4, 320), 460)
        // Landscape: ends above the page's PTT row, which stays usable, so
        // the drawer needs no PTT of its own. Portrait (from the bottom)
        // covers it and carries a copy.
        height: page.portrait ? (page.controlsFull ? (parent ? parent.height : page.height) : Math.min(page.height * 0.5, 560))
                              : page.height - page.pttHeight - 2 * Theme.spacing
                                - Math.max(page.SafeArea.margins.bottom, 12)
        // From the window's edge: Qt otherwise keeps popups inside the safe
        // area (above Android's navigation bar).
        margins: 0
        dim: false
        dragMargin: 0
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
            // Qt already pads a popup's content by the safe area.
            anchors.bottomMargin: Theme.spacing + Math.max(0, 12 - controlsDrawer.SafeArea.margins.bottom)
            spacing: Theme.spacing
            ControlsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Pinning would not show where the panel does not fit.
                showPinButton: page.pinFits
                onPinRequested: {
                    controlsDrawer.close()
                    Engine.controlsVisible = true
                }
            }
            // Upright: Close, full height and the PTT copy, as on the phone.
            RowLayout {
                visible: page.portrait
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                AppButton {
                    objectName: "closeControls"
                    Layout.fillWidth: drawerPtt.digiHidden
                    Accessible.name: qsTr("Close controls")
                    Layout.preferredWidth: Math.max(110, page.width * 0.18)
                    Layout.preferredHeight: drawerPtt.digiHidden ? Theme.touchTarget : page.pttHeight
                    onClicked: controlsDrawer.close()
                    contentItem: Label {
                        text: qsTr("Close")
                        color: Theme.buttonText
                        font.pixelSize: 15
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                AppButton {
                    objectName: "fullControls"
                    Accessible.name: page.controlsFull ? qsTr("Smaller controls") : qsTr("Full-screen controls")
                    Layout.preferredWidth: Theme.touchTarget
                    Layout.preferredHeight: drawerPtt.digiHidden ? Theme.touchTarget : page.pttHeight
                    onClicked: page.controlsFull = !page.controlsFull
                    contentItem: Item {
                        Caret {
                            anchors.centerIn: parent
                            size: 16
                            rotation: page.controlsFull ? 0 : 180
                        }
                    }
                }
                PttButton {
                    id: drawerPtt
                    visible: !digiHidden
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 32
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
    Component.onCompleted: page.forceActiveFocus()
    // A layout switch (rotation, Split View, fold/unfold) destroys this page;
    // a held PTT must not outlive its button.
    Component.onDestruction: {
        Engine.keyboardOnPage = false
        Engine.stopTransmit("layout changed")
    }
}
