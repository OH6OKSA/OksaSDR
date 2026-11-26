import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    id: page
    focus: true

    // ── Own layouts (roadmap 113, Larry Averbeck W9FLA) ─────────────────
    // The operator's arrangement of the blocks below (LayoutCanvas), the
    // desktop's Workspace Canvas on a touch screen; Classic (the grid) when
    // none is in use or the layout has none for this orientation yet.
    readonly property QtObject ws: Engine.workspace
    readonly property string orientation: portrait ? "port" : "land"
    property bool layoutEditing: false
    // Classic on screen for a moment, to be measured into a new arrangement.
    property bool measuringClassic: false
    readonly property bool canvasOn: ws.canvas && !measuringClassic && (ws.revision, ws.hasArrangement(orientation))
    readonly property bool canvasControls: canvasOn && blockControls.shown
    readonly property var blockTitles: ({
        frequency: qsTr("Frequency"), mode: qsTr("Mode"), stepsize: qsTr("Step size"), info: qsTr("Info line"), bands: qsTr("Bands"),
        slices: qsTr("Slices"), meter1: qsTr("Meter 1"), meter2: qsTr("Meter 2"),
        display: qsTr("Display"), steps: qsTr("Steps"),
        controls: qsTr("Controls"), macros: qsTr("Macros"), ptt: qsTr("PTT")
    })
    readonly property var closedBlocks: {
        const out = []
        for (const it of canvasArea.items) {
            if (it.closed) out.push({ block: it.block, title: blockTitles[it.block] || it.block })
        }
        return out
    }

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
        macros: macrosPanel
    }
    Keys.onPressed: (event) => { if (operateKeys.press(event)) event.accepted = true }
    Keys.onReleased: (event) => { if (operateKeys.release(event)) event.accepted = true }
    // The engine keys Space PTT from GameController (iOS) only while the
    // page has the keyboard.
    Binding { target: Engine; property: "keyboardOnPage"; value: page.activeFocus && !page.layoutEditing }
    // While a layout is edited the learned PTT key keys nothing either (121).
    Binding { target: Engine; property: "layoutEditing"; value: page.layoutEditing }
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
            // Never below half: as a band change drops the slice for a
            // moment, infoFit went negative and so did the probe's icon
            // sizes (Canvas "Incorrect argument radius", build 149 on an
            // iPad). Below 0.66 the panel goes under the frequency and this
            // measure is not used.
            fontScale: Math.max(0.5, page.infoFit)
        }
    }
    // PTT height, shared with the drawer's copy of the button.
    // PTT runs the full width along the bottom in every orientation.
    readonly property real pttHeight: Math.round(Theme.touchTarget * 1.5)
    // What is hidden gives its room away (roadmap 169, the user on the
    // OnePlus Open and the iPad with FT8 running: "aina tulisi maksimoida
    // tilan käyttö ... skaalautuminen on aidosti dynaamista laite kuin
    // laite"). FT8 / FT4 hide PTT and Macros:
    // the bottom row is then a button's height, not PTT's;
    readonly property real bottomRowHeight: ptt.digiHidden ? Theme.touchTarget : pttHeight
    // the side drawer runs to the window's bottom (it ended above a PTT row
    // that had no PTT), and while it is open the row's buttons gather in
    // the room left of it, smaller where six do not fit at their own size
    // (an opened OnePlus Open upright: 314 pt for about 346).
    readonly property bool sideDrawerFull: !stacked && ptt.digiHidden
    readonly property bool rowBesideDrawer: sideDrawerFull && !canvasOn && controlsDrawer.visible
    readonly property real rowRoom: leftWidth - (rowBesideDrawer ? Math.max(0, controlsDrawer.width - barRight) : 0)
    readonly property real rowNeed: rowLabels.advanceWidth + 4 * 12 + 2 * 44 + 5 * Theme.spacing / 2
    readonly property real rowFit: rowBesideDrawer ? Math.max(0.6, Math.min(1, rowRoom / rowNeed)) : 1
    TextMetrics {
        id: rowLabels
        font.pixelSize: macrosButton.font.pixelSize
        text: qsTr("Band") + qsTr("B/S ▲") + qsTr("+ Slice") + qsTr("+ Pan")
    }
    // Upright, the controls drawer can take the whole height (▲ beside
    // Close, as on the phone): FTx then lists its decodes on the full height,
    // e.g. on an iPhone Duo's inner screen, where half showed none. Kept
    // while the app runs.
    property bool controlsFull: false
    // Taller than wide in the room the page has (less the status and
    // navigation bars): a near-square foldable screen is then laid out the
    // same in both orientations (the OnePlus Open's 698 x 751 counted the
    // bars and turned to the portrait layout with 668 left).
    // The whole window's height (Theme.classHeight): the on-screen keyboard's
    // room turned an upright OnePlus Open and iPad to landscape under it, the
    // Controls drawer came from the side and the page's PTT row went under
    // the keyboard (the user, run 149: "sisänäytön kummassakaan asennossa ei
    // ptt näy").
    readonly property bool portrait: Theme.classHeight > Theme.windowWidth
    // Landscape with the panel pinned: two columns, the panel the full
    // height at the right, PTT under the display only. Otherwise one column
    // and PTT the full width.
    readonly property bool twoColumns: !stacked && panelPinned
    // The controls stacked under the display (the pinned panel below it, the
    // drawer from the bottom with its own PTT): upright, and only where the
    // panel does not fit beside the display (roadmap 156, the user on the
    // iPad: the side arrangement is the better one upright too, "iPad ja
    // Mac"). A 13" iPad and a Mac window upright take it; an 11" iPad and a
    // mini (834 and 744 wide) keep the controls under the display, as their
    // band buttons do not fit beside a 320 panel. Own layouts keep the true
    // orientation (orientation above).
    readonly property bool stacked: portrait && !sideFits
    // The waterfall's − + Band B/S and + Slice / + Pan on the PTT row, the
    // row's height, either side of Macros and PTT (roadmap 158, the user's
    // model): nothing over the waterfall, and PTT no longer the width of the
    // page. Classic only; an own layout's display keeps its corner buttons.
    readonly property bool panButtonsOnRow: !canvasOn
    readonly property bool panButtonsBelow: panButtonsOnRow && pttRow.visible
    // The row's small buttons at most a twelfth of it, PTT the rest: the
    // shares an iPad's row has, kept on a wide Linux or Mac window ("venyttäisin
    // PTT:tä vähän enemmän ja muita nappeja vähemmän", the user; the whole
    // interface in proportion is roadmap 160). Under FT8 / FT4 they keep
    // their places, PTT's room left empty.
    // From the column's computed width, not the row's own: the buttons'
    // limits feed the row's size, which fed them back (Qt: "recursive
    // rearrange" while the window was resized, classic-check's sweep).
    readonly property real rowButtonWidth: leftWidth / 12
    // The on-screen keyboard up (not a hardware keyboard on a Mac or Linux).
    readonly property bool keyboardUp: !!(page.Window.window && page.Window.window.keyboardCover > 0)
    // Its cover of the window's bottom, and the device's bars (Main.qml):
    // the status bar's share at the top, the navigation bar's (or home
    // indicator's) at the bottom. The page lies inside them, so the window's
    // height less the page's is not the keyboard: it took the bars for one
    // (roadmap 169).
    readonly property real keyboardCover: page.Window.window ? page.Window.window.keyboardCover || 0 : 0
    readonly property real barTop: page.Window.window ? page.Window.window.barTop || 0 : 0
    readonly property real barBottom: page.Window.window ? page.Window.window.barBottom || 0 : 0
    readonly property real barRight: page.Window.window ? page.Window.window.barRight || 0 : 0
    // With the on-screen keyboard up PTT stands right above it, and the
    // presses on its lower part are taken under its edge (the user's iPad in
    // landscape, build 213: 0-24 pt under it): that much and a little more
    // under the button keys too (PttButton.touchBelow).
    readonly property real pttTouchBelow: keyboardUp ? 32 : 0
    // Under the bottom row: 12 from a window's edge; none above a navigation
    // bar, whose inset the page ends above already, nor over the keyboard.
    readonly property real rowInset: keyboardUp ? 0 : Math.max(page.SafeArea.margins.bottom, 12 - barBottom, 0)
    // K1 (the user's review, 2026-11-23): in landscape the band row and the
    // step buttons give their height to the waterfall while the keyboard is
    // up (nothing is tuned while a call is typed); back when it goes. On an
    // 11" iPad the waterfall was gone with them.
    readonly property bool tuneRowsOnPage: Engine.tuneRowsShown && !(keyboardUp && (!portrait || keyboardDrawerShort))
    // K2: upright, the drawer above the keyboard only as high as its tabs,
    // three rows of fields and PTT need, so the waterfall stays above it
    // (the band and step rows hidden there too, as in K1);
    // the whole page as before where that would leave too little (an opened
    // OnePlus Open).
    readonly property real keyboardDrawerNeed: Math.round(2 * Theme.touchTarget + 3 * (Theme.touchTarget + Theme.spacing)
                                                          + pttHeight + 5 * Theme.spacing)
    readonly property bool keyboardDrawerShort: stacked && !controlsFull && keyboardUp
                                               && page.height - keyboardDrawerNeed >= 400
    // Landscape side column: the pinned control panel (none when hidden).
    // Fixed, so content can never widen it off screen. At least 320 as the
    // drawer: 287 on a 1024 pt window cut "TX 2170 Hz", "New DXCC" and the
    // voice keyer's F10 short. A big screen's larger type (Theme.bigScale)
    // widens it up to 26 % of the window, or its rows cut their labels
    // ("Text size"); beyond that the info panel beside the frequency shrank
    // (a 13" iPad keeps its 360).
    readonly property int sideWidth: Math.max(320, Math.min(360, width * 0.28), Math.min(360 * Theme.bigScale, width * 0.26))
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
    readonly property bool bottomFits: Theme.classHeight >= 1050
    readonly property bool pinFits: stacked ? bottomFits : sideFits
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
        // Two rows, one shown: a Pane sizes to its content only with one.
        contentHeight: Math.max(headerRow.implicitHeight, editHeaderRow.implicitHeight)
        RowLayout {
            id: headerRow
            visible: !page.layoutEditing
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
                Accessible.onPressAction: networkDialog.open()
                TapHandler { onTapped: networkDialog.open() }
            }
            // App version and build, so a screenshot or report says which.
            Label {
                text: "v" + Engine.appVersion
                color: Theme.textDim
                font.pixelSize: 13
            }
            TopButton {
                // Pinned controls (Classic's panel, or an own layout's
                // Controls block): hide them. Hidden: open them as a drawer
                // over the waterfall; its Pin Controls brings them back
                // (roadmap 114: an own layout works as Classic does).
                id: controlsTopButton
                objectName: "topControls"
                readonly property bool pinnedShown: page.canvasOn ? page.canvasControls : page.panelPinned
                text: pinnedShown ? qsTr("Hide controls") : qsTr("Controls")
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (!pinnedShown) {
                        controlsDrawer.open()
                    } else if (page.canvasOn) {
                        page.hideControlsBlock()
                    } else {
                        Engine.controlsVisible = false
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
                onClicked: Engine.disconnectRadio("the Disconnect button")
            }
            TopButton {
                id: moreButton
                visible: page.narrowTop
                text: "⋯"
                Accessible.name: qsTr("More")
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
                    MenuItem {
                        // Already there, it leaves (roadmap 161).
                        text: Engine.bigButtons ? qsTr("Exit Big buttons") : qsTr("Big buttons")
                        onTriggered: Engine.bigButtons = !Engine.bigButtons
                    }
                    MenuItem { text: qsTr("Edit layout"); onTriggered: page.startEditing(false) }
                    MenuItem { text: qsTr("Disconnect"); onTriggered: Engine.disconnectRadio("the ⋯ menu's Disconnect") }
                }
            }
        }
        // Editing an own layout: its name (tap to rename), + Add for a
        // hidden block, Undo, Reset (to Classic's arrangement), Delete, Done.
        RowLayout {
            id: editHeaderRow
            visible: page.layoutEditing
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            Label {
                objectName: "layoutName"
                text: qsTr("Layout: %1").arg(page.ws.activeLabel)
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Rename layout %1").arg(page.ws.activeLabel)
                Accessible.onPressAction: renameDialog.open()
                TapHandler { onTapped: renameDialog.open() }
            }
            TopButton {
                id: addBlockButton
                objectName: "layoutAdd"
                text: qsTr("+ Add")
                Accessible.name: qsTr("Add a hidden block")
                enabled: page.closedBlocks.length > 0
                focusPolicy: Qt.NoFocus
                onClicked: addMenu.popup(addBlockButton, 0, addBlockButton.height)
                Menu {
                    id: addMenu
                    modal: true
                    dim: false
                    Instantiator {
                        model: page.closedBlocks
                        delegate: MenuItem {
                            required property var modelData
                            objectName: "layoutAdd:" + modelData.block
                            text: modelData.title
                            onTriggered: page.showBlock(modelData.block)
                        }
                        onObjectAdded: (index, object) => addMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => addMenu.removeItem(object)
                    }
                }
            }
            TopButton {
                objectName: "layoutUndo"
                text: qsTr("Undo")
                Accessible.name: qsTr("Undo the last change")
                enabled: page.ws.canUndo
                focusPolicy: Qt.NoFocus
                onClicked: page.ws.undo()
            }
            TopButton {
                objectName: "layoutReset"
                text: qsTr("Reset")
                Accessible.name: qsTr("Reset to Classic's arrangement")
                focusPolicy: Qt.NoFocus
                onClicked: page.measureClassic(false)
            }
            TopButton {
                objectName: "layoutDelete"
                text: qsTr("Delete")
                Accessible.name: qsTr("Delete layout")
                focusPolicy: Qt.NoFocus
                onClicked: deleteLayoutDialog.open()
            }
            TopButton {
                objectName: "layoutDone"
                text: qsTr("Done")
                Accessible.name: qsTr("Done editing the layout")
                focusPolicy: Qt.NoFocus
                onClicked: page.stopEditing()
            }
        }
    }

    // ── Own layouts: editing ───────────────────────────────────────────
    function startEditing(newLayout) {
        controlsDrawer.close()
        macrosPanel.close()
        Engine.setPtt(false, "layout editing")
        Engine.setCwKey(false, "layout editing")
        if (newLayout || !ws.canvas) {
            measureClassic(true)
        } else if (!canvasOn) {
            // An own layout without this orientation yet: Classic's
            // arrangement to start from.
            measureClassic(false)
        } else {
            layoutEditing = true
        }
    }
    // The top row's Hide controls in an own layout (roadmap 114): the
    // Controls block hidden and the blocks beside it grown into its room
    // (24 px of gap at most), as Classic's display takes the panel's width.
    function hideControlsBlock() {
        ws.hideAndFill(orientation, "controls", 24 / Math.max(1, canvasArea.width), 24 / Math.max(1, canvasArea.height))
        ws.flush()
    }
    // The drawer's Pin Controls in an own layout: the block back where it
    // was. Where that place is taken now (or the block was never placed,
    // Classic's controls being a drawer when the layout was made), never
    // over another block: the editor opens with it selected to place it.
    function pinControlsBlock() {
        const it = canvasArea.itemFor("controls")
        if (!it) return
        const rect = { x: it.x, y: it.y, w: it.w, h: it.h }
        // The blocks grown for it go back first (unless edited since).
        ws.showAndRestore(orientation, "controls")
        if (canvasArea.overlapsAny("controls", rect)) {
            startEditing(false)
            blockControls.select()
        } else {
            ws.flush()
        }
    }
    // + Add's choice: the menu closed first (a screen reader's press leaves
    // it open where a tap closes it, and the block shown changes the menu's
    // list, which takes the item away), then the block shown again.
    function showBlock(block) {
        addMenu.close()
        ws.checkpoint()
        // Hidden by Hide controls: the blocks grown for it go back too.
        ws.showAndRestore(orientation, block)
    }
    function stopEditing() {
        addMenu.close()
        layoutEditing = false
        ws.flush()
        page.forceActiveFocus()
    }
    // Classic shown for a frame and measured into blocks: a new layout, or
    // this orientation of the active one (its first edit, or Reset).
    function measureClassic(newLayout) {
        seedTimer.newLayout = newLayout
        measuringClassic = true
        seedTimer.restart()
    }
    Timer {
        id: seedTimer
        property bool newLayout: false
        interval: 80
        onTriggered: {
            const blocks = page.classicBlocks()
            if (newLayout) {
                page.ws.createLayout(page.orientation, blocks)
            } else {
                page.ws.checkpoint()
                page.ws.seed(page.orientation, blocks)
            }
            page.measuringClassic = false
            page.layoutEditing = true
        }
    }
    onOrientationChanged: {
        if (layoutEditing) stopEditing()
        // Each orientation is a surface of its own: build 197's slice row
        // is split there too when first shown.
        Qt.callLater(migrateSlices)
    }
    Connections {
        target: page.ws
        function onEditRequested(newLayout) { page.startEditing(newLayout) }
    }
    // Where Classic shows a part, in fractions of the page's area (the
    // grid's, which the canvas takes too); null when it is not shown.
    function classicRect(item) {
        if (!item || !item.visible || item.width <= 0 || item.height <= 0 || grid.width <= 0 || grid.height <= 0) {
            return null
        }
        const p = item.mapToItem(grid, 0, 0)
        return { x: p.x / grid.width, y: p.y / grid.height, w: item.width / grid.width, h: item.height / grid.height }
    }
    function unionRect(a, b) {
        if (!a) return b
        if (!b) return a
        const x = Math.min(a.x, b.x)
        const y = Math.min(a.y, b.y)
        return { x: x, y: y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y }
    }
    function classicBlocks() {
        const list = []
        const add = (block, r) => {
            const item = { block: block, closed: !r }
            if (r) Object.assign(item, r)
            list.push(item)
        }
        add("frequency", classicRect(freqLabel))
        add("mode", classicRect(classicMode))
        add("stepsize", classicRect(classicStep))
        add("info", classicRect(page.infoUnder ? infoUnderPanel : infoBeside))
        add("bands", classicRect(bandRow))
        // The slice row as three blocks (roadmap 117): its meters where they
        // are, the chips' column the row's full height.
        const row = classicRect(sliceBar)
        const chipsRect = classicRect(sliceBar.chipAreaItem)
        add("meter1", classicRect(sliceBar.leftMeterItem))
        add("slices", chipsRect && row ? { x: chipsRect.x, y: row.y, w: chipsRect.w, h: row.h } : chipsRect)
        add("meter2", classicRect(sliceBar.rightMeterItem))
        add("display", classicRect(panSlot))
        add("steps", classicRect(stepRow))
        add("controls", page.panelPinned ? classicRect(controlsSlot) : null)
        // With PTT hidden (FT8 / FT4 running) its row is not shown: where
        // it would be, under the display.
        const pttH = page.pttHeight / Math.max(1, grid.height)
        const rowY = 1 - pttH
        const leftW = page.leftWidth / Math.max(1, grid.width)
        const macrosW = Math.max(110, macrosButton.implicitWidth) / Math.max(1, grid.width)
        add("macros", classicRect(macrosSlot)
                      || (macrosPanel.available ? { x: 0, y: rowY, w: macrosW, h: pttH } : null))
        add("ptt", classicRect(pttSlot) || { x: macrosPanel.available ? macrosW + 0.005 : 0, y: rowY,
                                             w: leftW - (macrosPanel.available ? macrosW + 0.005 : 0), h: pttH })
        return list
    }

    GridLayout {
        id: grid
        visible: !page.canvasOn
        anchors.fill: parent
        // Off under Big buttons, so no tap there reaches it.
        enabled: !Engine.bigButtons
        // Stay clear of the home indicator and rounded corners: iOS defers or
        // cancels touches at the screen edges, which would end a held PTT.
        // The page's safe area, not the grid's own (that is a binding loop:
        // the margins move the grid, which changes its own safe area).
        anchors.leftMargin: Theme.spacing + page.SafeArea.margins.left
        anchors.rightMargin: Theme.spacing + page.SafeArea.margins.right
        anchors.topMargin: Theme.spacing
        // K2: the display ends above the short drawer, so the waterfall (not
        // only the spectrum over it) shows above it.
        // With the on-screen keyboard up the page already ends at its top:
        // Android's navigation bar lies under the keyboard, and its inset
        // left a band of 60 pt between PTT and the keyboard (the OnePlus
        // Open, beta 117: "musta raita ... ihan turhaa"; in landscape the
        // Log drawer then showed only its tabs).
        anchors.bottomMargin: Theme.spacing + page.rowInset
                              + (page.keyboardDrawerShort && controlsDrawer.visible ? page.keyboardDrawerNeed : 0)
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
            // With the on-screen keyboard up the page is short (382 of an
            // opened OnePlus Open in landscape): the display gives way so
            // the PTT row stays above the keyboard.
            Layout.minimumHeight: page.Window.window && page.Window.window.keyboardCover > 0 ? 100 : 320
            spacing: page.compact ? Theme.spacing / 2 : Theme.spacing

            RowLayout {
                id: freqRow
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
                    id: freqLabel
                    text: page.formatMhz(Engine.frequencyMhz)
                    color: Theme.text
                    font.pixelSize: Math.round((page.compact ? 40 : 56) * page.infoScale)
                    font.family: "monospace"
                    // Tap for direct entry.
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Frequency %1 kHz").arg((Engine.frequencyMhz * 1000).toFixed(3))
                    Accessible.description: qsTr("Opens frequency entry")
                    Accessible.onPressAction: freqDialog.open()
                    TapHandler { onTapped: freqDialog.open() }
                }
                // Mode, and the tuning step under it.
                ColumnLayout {
                    id: modeColumn
                    spacing: 0
                    ModeButton {
                        id: classicMode
                        fontSize: page.compact ? 24 : 32
                    }
                    StepButton {
                        id: classicStep
                        fontSize: page.compact ? 13 : 16
                    }
                }
                // The slice at a glance and in reach: antennas, width,
                // speaker, lock, DSP, AGC (each opens its choices).
                // Clipped at its room should the fit be off by a pixel or
                // two (rounding): a row wider than the column widened the page.
                InfoPanel {
                    id: infoBeside
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
                id: infoUnderPanel
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
                id: bandRow
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                visible: Engine.bandsAvailable && page.tuneRowsOnPage
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

            // The display (panView, portaled: one instance, here or in an
            // own layout's Display block).
            Item {
                id: panSlot
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Close under the slice row: the meters at its top corners
                // sit right below the buttons.
                Layout.topMargin: -Math.round(Theme.spacing / 2)
            }

            // Step buttons: land on round frequencies (tuneByHz).
            RowLayout {
                id: stepRow
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                visible: page.tuneRowsOnPage
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
            // Beside the display and the bottom row, to the page's bottom:
            // also while FT8 / FT4 hides PTT, as the row stays for the
            // waterfall's buttons (the panel ended at the display and left
            // 93 pt under the decode list on an iPad, roadmap 169).
            Layout.rowSpan: page.twoColumns && pttRow.visible ? 2 : 1
            Layout.preferredWidth: page.stacked ? -1 : page.sideWidth
            Layout.maximumWidth: page.stacked ? Number.POSITIVE_INFINITY : page.sideWidth
            Layout.fillWidth: page.stacked
            Layout.fillHeight: !page.stacked
            // With the on-screen keyboard up a lower panel, its field
            // scrolled into sight there, so the waterfall stays (an 11" iPad
            // upright kept a strip of spectrum under a 0.42 panel; the user:
            // the waterfall as large as it can be).
            Layout.preferredHeight: page.stacked ? page.height * (page.Window.window && page.Window.window.keyboardCover > 0 ? 0.30 : 0.42) : -1
            spacing: Theme.spacing

            // Pinned panel: section toggles; the chosen sections stack below
            // (pinnedControls, portaled as the display).
            Item {
                id: controlsSlot
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        // PTT along the bottom: under the display (the pinned panel keeps
        // the side's full height), the full width otherwise, the macros
        // button beside it (MacrosPanel).
        // In portrait the drawer covers this and carries a copy; hidden then
        // so no second PTT peeks out under the drawer.
        RowLayout {
            id: pttRow
            // It stays while FT8 / FT4 hides PTT and Macros: the waterfall's
            // buttons are on it.
            visible: !(controlsDrawer.visible && page.stacked) && (!ptt.digiHidden || page.panButtonsOnRow)
            Layout.row: page.twoColumns ? 1 : (page.panelPinned ? 2 : 1)
            Layout.column: 0
            Layout.fillWidth: true
            // A layout in a layout fills the height by default: the row took
            // the display's room (1366 x 1024).
            Layout.fillHeight: false
            Layout.preferredHeight: page.bottomRowHeight
            // Left of the open side drawer (rowBesideDrawer).
            Layout.rightMargin: page.rowBesideDrawer ? Math.max(0, controlsDrawer.width * controlsDrawer.position - page.barRight) : 0
            spacing: Theme.spacing / 2
            // − + Band B/S beside Macros (+ Slice / + Pan with View › Display
            // buttons › Right, as on the waterfall).
            Repeater {
                model: page.panButtonsBelow ? (Engine.displayButtonsRight ? ["pan", "slice"] : ["zoomOut", "zoomIn", "band", "bs"]) : []
                delegate: panRowButton
            }
            // Macros and PTT (portaled as the display).
            Item {
                id: macrosSlot
                visible: macrosPanel.available && !ptt.digiHidden
                Layout.preferredWidth: Math.max(110, macrosButton.implicitWidth)
                // Its whole label: a 13" iPad upright with the panel pinned
                // cut it to "Ma…" beside the row's buttons.
                Layout.minimumWidth: macrosButton.implicitWidth
                Layout.fillWidth: page.panButtonsBelow
                Layout.maximumWidth: page.panButtonsBelow ? Math.max(Layout.preferredWidth, page.rowButtonWidth * 1.1) : Number.POSITIVE_INFINITY
                Layout.fillHeight: true
            }
            Item {
                id: pttSlot
                visible: !ptt.digiHidden
                Layout.fillWidth: true
                // The rest of the row (rowButtonWidth).
                Layout.minimumWidth: 140
                Layout.preferredWidth: page.panButtonsBelow ? 300 : -1
                Layout.fillHeight: true
            }
            // Where PTT is while FT8 / FT4 runs (not beside the open side
            // drawer: the six then stand together at the left).
            Item {
                visible: ptt.digiHidden && page.panButtonsBelow && !page.rowBesideDrawer
                Layout.fillWidth: true
            }
            // + Slice / + Pan on PTT's other side.
            Repeater {
                model: page.panButtonsBelow ? (Engine.displayButtonsRight ? ["bs", "band", "zoomOut", "zoomIn"] : ["slice", "pan"]) : []
                delegate: panRowButton
            }
        }
    }

    // ── Own layout: the blocks (roadmap 113) ───────────────────────────
    LayoutCanvas {
        id: canvasArea
        objectName: "layoutCanvas"
        anchors.fill: parent
        anchors.leftMargin: grid.anchors.leftMargin
        anchors.rightMargin: grid.anchors.rightMargin
        anchors.topMargin: grid.anchors.topMargin
        anchors.bottomMargin: grid.anchors.bottomMargin
        visible: page.canvasOn
        enabled: !Engine.bigButtons
        ws: page.ws
        orientation: page.orientation
        editing: page.layoutEditing

        CanvasBlock {
            id: blockFreq
            block: "frequency"
            canvas: canvasArea
            title: page.blockTitles.frequency
            minW: 160
            minH: 36
            fits: (w, h) => freqBlock.fits(w, h)
            FrequencyBlock {
                id: freqBlock
                anchors.fill: parent
                formatMhz: page.formatMhz
                onTapped: freqDialog.open()
            }
        }
        CanvasBlock {
            id: blockMode
            block: "mode"
            canvas: canvasArea
            title: page.blockTitles.mode
            minW: 60
            minH: 26
            fits: (w, h) => modeBlock.fits(w, h)
            ModeBlock {
                id: modeBlock
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockStepSize
            block: "stepsize"
            canvas: canvasArea
            title: page.blockTitles.stepsize
            minW: 90
            minH: 26
            fits: (w, h) => stepSizeBlock.fits(w, h)
            StepSizeBlock {
                id: stepSizeBlock
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockInfo
            block: "info"
            canvas: canvasArea
            title: page.blockTitles.info
            minW: 160
            minH: 30
            fits: (w, h) => infoBlock.fits(w, h)
            InfoBlock {
                id: infoBlock
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockBands
            block: "bands"
            canvas: canvasArea
            title: page.blockTitles.bands
            minW: 48
            minH: 34
            fits: (w, h) => bandsBlock.fits(w, h)
            ButtonGridBlock {
                id: bandsBlock
                kind: "bands"
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockSlices
            block: "slices"
            canvas: canvasArea
            title: page.blockTitles.slices
            minW: 96
            minH: 34
            fits: (w, h) => slicesBlock.fits(w, h)
            SlicesBlock {
                id: slicesBlock
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockMeter1
            block: "meter1"
            canvas: canvasArea
            title: page.blockTitles.meter1
            minW: 80
            minH: 40
            fits: (w, h) => meter1Block.fits(w, h)
            MeterBlock {
                id: meter1Block
                side: "left"
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockMeter2
            block: "meter2"
            canvas: canvasArea
            title: page.blockTitles.meter2
            minW: 80
            minH: 40
            fits: (w, h) => meter2Block.fits(w, h)
            MeterBlock {
                id: meter2Block
                side: "right"
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockDisplay
            block: "display"
            canvas: canvasArea
            title: page.blockTitles.display
            // Its own corner buttons (− + Band, + Slice + Pan) side by side
            // with room between, and the spectrum over the waterfall: at
            // 240 x 120 the two button rows met.
            minW: 300
            minH: 160
        }
        CanvasBlock {
            id: blockSteps
            block: "steps"
            canvas: canvasArea
            title: page.blockTitles.steps
            minW: 44
            minH: 34
            fits: (w, h) => stepsBlock.fits(w, h)
            ButtonGridBlock {
                id: stepsBlock
                kind: "steps"
                anchors.fill: parent
            }
        }
        CanvasBlock {
            id: blockControls
            block: "controls"
            canvas: canvasArea
            title: page.blockTitles.controls
            minW: 300
            minH: 200
        }
        CanvasBlock {
            id: blockMacros
            block: "macros"
            canvas: canvasArea
            title: page.blockTitles.macros
            minW: 80
            minH: 36
        }
        CanvasBlock {
            id: blockPtt
            block: "ptt"
            canvas: canvasArea
            title: page.blockTitles.ptt
            minW: 100
            minH: 40
        }
    }

    // ── Build 197's layouts: the slice row split into three (117) ───────
    // A surface with the old "Slices and meters" block (Classic's slice row
    // with its meters) gets Meter 1, Slices and Meter 2 where the meters and
    // the chips were in it: measured on an unseen slice row of the block's
    // size (shown at opacity 0: a hidden one does not lay out), not guessed.
    Loader {
        id: sliceProbe
        property real probeWidth: 0
        property real probeHeight: 0
        active: false
        sourceComponent: Item {
            width: sliceProbe.probeWidth
            height: sliceProbe.probeHeight
            opacity: 0
            enabled: false
            property alias bar: probeBar
            SliceBar {
                id: probeBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                compact: width < 1100
            }
        }
    }
    function migrateSlices() {
        if (!canvasOn || sliceMigrate.running || canvasArea.width <= 0 || canvasArea.height <= 0) return
        const old = canvasArea.itemFor("slices")
        if (!old || canvasArea.itemFor("meter1") || canvasArea.itemFor("meter2")) return
        sliceProbe.probeWidth = old.w * canvasArea.width
        sliceProbe.probeHeight = old.h * canvasArea.height
        sliceProbe.active = true
        sliceMigrate.restart()
    }
    Timer {
        id: sliceMigrate
        interval: 150
        onTriggered: {
            const host = sliceProbe.item
            const old = canvasArea.itemFor("slices")
            if (host && old && page.canvasOn) {
                const cw = canvasArea.width
                const ch = canvasArea.height
                const part = (block, it, fullHeight) => {
                    // The chips' column takes the row's height: only its width counts.
                    if (!it || !it.visible || it.width <= 0 || (!fullHeight && it.height <= 0)) return null
                    const p = it.mapToItem(host, 0, 0)
                    return { block: block, x: old.x + p.x / cw, y: fullHeight ? old.y : old.y + p.y / ch,
                             w: it.width / cw, h: fullHeight ? old.h : it.height / ch }
                }
                const parts = [part("meter1", host.bar.leftMeterItem, false),
                               part("slices", host.bar.chipAreaItem, true),
                               part("meter2", host.bar.rightMeterItem, false)].filter(p => p !== null)
                if (parts.length > 0) page.ws.splitBlock(page.orientation, "slices", parts)
            }
            sliceProbe.active = false
        }
    }
    onCanvasOnChanged: Qt.callLater(migrateSlices)
    Connections {
        target: page.ws
        function onChanged() { Qt.callLater(page.migrateSlices) }
    }

    // ── The parts that are one instance in either arrangement ──────────
    // The display, the pinned controls, Macros and PTT keep their state
    // (the waterfall, a held PTT) and their cost once: in Classic's cell
    // or in the own layout's block.
    // One of the waterfall's buttons on the PTT row (panButtonsBelow): the
    // row's height, the width shared with Macros and PTT.
    Component {
        id: panRowButton
        AppButton {
            required property string modelData
            readonly property bool zoomButton: modelData === "zoomOut" || modelData === "zoomIn"
            objectName: ({ zoomOut: "zoomOutButtonBelow", zoomIn: "zoomInButtonBelow", band: "bandZoomButtonBelow",
                           bs: "tuneRowsButtonBelow", slice: "addSliceButtonBelow", pan: "addPanButtonBelow" })[modelData]
            visible: modelData === "slice" ? Engine.canAddSlice : (modelData === "pan" ? Engine.canAddPanadapter : true)
            text: ({ zoomOut: "−", zoomIn: "+", band: qsTr("Band"),
                     bs: Engine.tuneRowsShown ? qsTr("B/S") : qsTr("B/S ▲"),
                     slice: qsTr("+ Slice"), pan: qsTr("+ Pan") })[modelData]
            Accessible.name: ({ zoomOut: qsTr("Zoom out"), zoomIn: qsTr("Zoom in"), band: qsTr("Band"),
                                bs: qsTr("Band row and step buttons"), slice: qsTr("+ Slice"), pan: qsTr("+ Pan") })[modelData]
            Accessible.description: modelData === "bs" ? (Engine.tuneRowsShown ? qsTr("Shown; tap to hide them") : qsTr("Hidden; tap to show them")) : ""
            checked: modelData === "band" ? !!Engine.panDisplay.bandZoom : (modelData === "bs" ? Engine.tuneRowsShown : false)
            // As Macros beside them; smaller beside an open side drawer
            // where the six do not fit at that size (rowFit).
            font.pixelSize: Math.round((zoomButton ? Theme.buttonFont * 1.3 : macrosButton.font.pixelSize) * page.rowFit)
            leftPadding: zoomButton ? 0 : Math.round(6 * page.rowFit)
            rightPadding: zoomButton ? 0 : Math.round(6 * page.rowFit)
            focusPolicy: Qt.NoFocus
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: zoomButton ? Math.round(44 * page.rowFit) : implicitContentWidth + Math.round(12 * page.rowFit)
            Layout.preferredWidth: Math.max(Theme.buttonWidth, implicitContentWidth + 16)
            Layout.maximumWidth: Math.max(Layout.minimumWidth, page.rowButtonWidth)
            onClicked: {
                if (modelData === "zoomOut") Engine.zoom(2.0)
                else if (modelData === "zoomIn") Engine.zoom(0.5)
                else if (modelData === "band") {
                    if (panView.bandPicker) panView.openBandChooser()
                    else Engine.setPanDisplay("bandZoom", checked ? 0 : 1)
                } else if (modelData === "bs") Engine.tuneRowsShown = !Engine.tuneRowsShown
                else if (modelData === "slice") Engine.addSlice()
                else Engine.addPanadapter()
            }
            // B/S hidden says so, as on the waterfall (Michael Walker).
            Rectangle {
                anchors.fill: parent
                visible: parent.modelData === "bs" && !Engine.tuneRowsShown
                radius: Theme.radius
                color: "transparent"
                border.color: Theme.warning
                border.width: 2
            }
        }
    }
    PanadapterView {
        id: panView
        parent: page.canvasOn ? blockDisplay.contentArea : panSlot
        anchors.fill: parent
        rfLabelWidth: sliceBar.rightMeterWidth
        // A swipe in the open drawer must not tune.
        covers: [controlsDrawer.background]
        // B/S hides the band row and the step buttons: Band then picks the
        // bands. In an own layout B/S is not there (the layout decides
        // what shows) and Band picks when the Bands block is hidden.
        bandPicker: page.canvasOn ? !blockBands.shown : !page.tuneRowsOnPage
        tuneRowsToggle: !page.canvasOn
        buttonsBelow: page.panButtonsBelow
    }
    ControlsPanel {
        id: pinnedControls
        parent: page.canvasOn ? blockControls.contentArea : controlsSlot
        anchors.fill: parent
        multiSelect: true
    }
    // A spot's Tune (roadmap 137): its mode's Controls tab, in the pinned
    // panel when it shows, else in the drawer (opened by an alert's Tune).
    Connections {
        target: Engine
        function onControlsSectionRequested(name, open) {
            const pinned = page.canvasOn ? page.canvasControls : page.panelPinned
            if (pinned) {
                pinnedControls.showSection(name)
            } else {
                drawerPanel.showSection(name)
                if (open && !controlsDrawer.opened)
                    controlsDrawer.open()
            }
        }
    }
    AppButton {
        id: macrosButton
        objectName: "macrosButton"
        parent: page.canvasOn ? blockMacros.contentArea : macrosSlot
        anchors.fill: parent
        visible: macrosPanel.available
        text: Engine.macroRepeatSlot.length > 0 ? "↻ " + Engine.macroRepeatSlot : qsTr("Macros")
        Accessible.name: Engine.macroRepeatSlot.length > 0 ? qsTr("Macros, %1 repeating").arg(Engine.macroRepeatSlot) : qsTr("Macros")
        warn: Engine.macroRepeatSlot.length > 0
        font.pixelSize: 17
        checked: macrosPanel.opened
        focusPolicy: Qt.NoFocus
        onClicked: macrosPanel.opened ? macrosPanel.close() : macrosPanel.open()
    }
    PttButton {
        id: ptt
        objectName: "pagePtt"
        touchBelow: page.pttTouchBelow
        parent: page.canvasOn ? blockPtt.contentArea : pttSlot
        anchors.fill: parent
        // FT8 / FT4 running hides it (Classic hides its whole row).
        visible: !(page.canvasOn && digiHidden)
        fontPx: 32
    }

    // The mode's macros over the display, above the PTT row (Tyler Akerman
    // K0OTC), as wide as the display's side at most; the drawer opening
    // closes it.
    MacrosPanel {
        id: macrosPanel
        anchorItem: page.canvasOn ? blockMacros : pttRow
        maxWidth: Math.min(page.canvasOn ? canvasArea.width : page.leftWidth, 720)
    }

    // With the panel hidden, controls slide in from the right over the
    // waterfall (no dimming; tap outside or swipe back to close). Edge swipes
    // do not open it, so a PTT press near the edge never becomes a drag.
    Drawer {
        id: controlsDrawer
        objectName: "controlsDrawer"
        edge: page.stacked ? Qt.BottomEdge : Qt.RightEdge
        // At least 320 pt beside the display: a narrow window's 40 % cut the
        // panel's buttons short.
        // In landscape with no Pin Controls the × sits beside the section
        // buttons (ControlsPanel.closeInGrid): the drawer is that much wider.
        width: page.stacked ? page.width
                             : Math.min(Math.max(page.width * 0.4, 320 * Theme.bigScale), 460 * Theme.bigScale)
                               + (drawerPanel.closeInGrid ? drawerPanel.closeColumn : 0)
        // Landscape: ends above the page's PTT row, which stays usable, so
        // the drawer needs no PTT of its own; to the window's bottom while
        // FT8 / FT4 hides PTT (sideDrawerFull). Portrait (from the bottom)
        // covers it and carries a copy.
        // Upright with the on-screen keyboard up it takes the whole page above
        // the keyboard, as Full-screen controls: half of a short page left
        // the Log tab's call field one line high (an opened OnePlus Open).
        // Its own height counts above what covers the window's bottom (the
        // keyboard, or the navigation bar: Qt pads the content by that). At
        // the side it starts at the window's top, the status bar's height
        // above the page.
        height: page.stacked ? (page.keyboardDrawerShort
                                 ? page.keyboardDrawerNeed + page.keyboardCover
                                 : page.controlsFull || page.keyboardUp
                                 ? (parent ? parent.height : page.height)
                                 : Math.min(page.height * 0.5, 560) + page.barBottom)
                              : page.sideDrawerFull ? (parent ? parent.height : page.height)
                              : page.barTop + page.height - page.pttHeight - 2 * Theme.spacing - page.rowInset
        // From the window's edge: Qt otherwise keeps popups inside the safe
        // area (above Android's navigation bar).
        margins: 0
        // The on-screen keyboard over the drawer's lower part (roadmap 142):
        // a drawer reaching the window's bottom keeps its controls and PTT
        // above it (Qt places a bottom drawer at the window's edge whatever
        // its margins, so it grows by the keyboard instead and pads that).
        readonly property real keyboardOver: (edge === Qt.BottomEdge || height >= page.Window.height - 1)
                                             ? page.keyboardCover : 0
        dim: false
        dragMargin: 0
        onAboutToShow: {
            macrosPanel.close()
            Engine.setPtt(false, "controls drawer opened")
            Engine.setCwKey(false, "controls drawer opened")
        }
        onOpened: Engine.controlsDrawerOpen = true
        onClosed: if (!page.leaving) Engine.controlsDrawerOpen = false
        background: Rectangle {
            color: Theme.surface
            // Light: opaque, as the page's dark text showed through.
            opacity: Theme.light ? 1 : 0.96
            radius: Theme.radius
        }

        // The controls scroll above the drawer's own PTT/KEY, which sits where
        // the page's button is: the drawer never covers it.
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            // Qt already pads a popup's content by the safe area. A drawer
            // that reaches the window's bottom keeps the page's own margin
            // there (its Close and PTT level with the page's row). The side
            // drawer that ends above the PTT row has no edge under it: half
            // a spacing, as the phone's drawer. It had the full margin and
            // 12 more, 23 pt of nothing between its last line and PTT (the
            // user, the preview of build 213, marked in red: "controls
            // laatikon alalaitaa ennen ptt ... jos tuota mustaa tyhjää siinä
            // ei olisi teksti näkyisi kokonaan").
            anchors.bottomMargin: controlsDrawer.keyboardOver > 0
                                  ? Theme.spacing + Math.max(0, controlsDrawer.keyboardOver - controlsDrawer.SafeArea.margins.bottom)
                                  : controlsDrawer.height >= page.Window.height - 1
                                  ? Theme.spacing + Math.max(0, 12 - controlsDrawer.SafeArea.margins.bottom)
                                  : Theme.spacing / 2
            spacing: Theme.spacing
            ControlsPanel {
                id: drawerPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Pinning would not show where the panel does not fit, nor in
                // an own layout (its Controls block is the pinned panel).
                // Pinning would not show where Classic's panel does not fit;
                // in an own layout it brings the Controls block back.
                showPinButton: page.canvasOn ? !page.canvasControls : page.pinFits
                onPinRequested: {
                    controlsDrawer.close()
                    if (page.canvasOn) {
                        page.pinControlsBlock()
                    } else {
                        Engine.controlsVisible = true
                    }
                }
                // Landscape has no Close at the bottom: a × at the top.
                showCloseButton: !page.stacked
                onCloseRequested: controlsDrawer.close()
            }
            // Upright: Close, full height and the PTT copy, as on the phone.
            RowLayout {
                visible: page.stacked
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
                    objectName: "drawerPtt"
                    touchBelow: page.pttTouchBelow
                    visible: !digiHidden
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 32
                }
            }
        }
    }

    // PTT while the side drawer is open. The drawer is modal: a press outside
    // it is not passed on and closes it, so the page's PTT, in full view
    // under the drawer's lower edge, did nothing but close the drawer (the
    // user, an opened OnePlus Open with the Log tab and the keyboard up:
    // "PTT poistaa loginäkymän"; the call field and its keyboard went with
    // it). As the upright drawer carries a PTT of its own, the side drawer
    // has one too: a copy over the page's button, in a popup above the
    // drawer, which takes the press. Everything else outside the drawer
    // stays as it was (a tap closes the drawer and does nothing more).
    // Not where the drawer covers the page's PTT (an own layout may put it
    // there), and kept while held, should the drawer close under the finger.
    readonly property bool sidePttClear: {
        if (!canvasOn)
            return true     // Classic: the drawer ends above the PTT row
        // What moves the button or the drawer in an own layout.
        page.width; page.height; ptt.width; ptt.height; ptt.parent
        controlsDrawer.width; controlsDrawer.height; page.ws.active; page.layoutEditing
        const at = ptt.mapToItem(page, 0, 0)
        return at.x + ptt.width <= page.width - controlsDrawer.width || at.y >= controlsDrawer.height
    }
    readonly property bool sidePttWanted: controlsDrawer.visible && !stacked && ptt.visible && !ptt.digiHidden
                                          && !Engine.bigButtons && sidePttClear
    readonly property bool sidePttShown: sidePttWanted || sidePtt.held
    Popup {
        id: sidePttPopup
        objectName: "sidePttPopup"
        parent: ptt
        x: 0
        y: 0
        // Open as long as the drawer is, from the moment it has opened: its
        // place among the popups is then right above the drawer and under
        // every dialog opened from the drawer later (Logbooks…, Setup),
        // which cover it as they cover the page's PTT. Whether the button
        // shows only sizes it: opened anew (a turned device, FT8 stopped) it
        // would land above such a dialog.
        visible: controlsDrawer.opened || sidePtt.held
        width: page.sidePttShown ? ptt.width : 0
        height: page.sidePttShown ? ptt.height : 0
        margins: -1
        padding: 0
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        enter: null
        exit: null
        background: null
        contentItem: PttButton {
            id: sidePtt
            objectName: "sidePtt"
            touchBelow: ptt.touchBelow
            visible: page.sidePttShown
            fontPx: ptt.fontPx
        }
    }

    // Big buttons (roadmap 55) over the whole page, which keeps the keyboard
    // (Space and a Bluetooth PTT button key, the arrows tune). Made only
    // while on, so it costs nothing hidden.
    Loader {
        anchors.fill: parent
        z: 100
        active: Engine.bigButtons
        sourceComponent: BigButtonsView {
            formatMhz: page.formatMhz
            marginLeft: page.SafeArea.margins.left
            marginRight: page.SafeArea.margins.right
            marginTop: page.SafeArea.margins.top
            marginBottom: page.SafeArea.margins.bottom
            onFrequencyTapped: freqDialog.open()
        }
    }
    Connections {
        target: Engine
        function onBigButtonsChanged() {
            if (Engine.bigButtons) {
                controlsDrawer.close()
                macrosPanel.close()
            }
            page.forceActiveFocus()
        }
    }
    // Own layouts: a name of the operator's, and deleting one (the app's
    // own buttons, as SetupSuggestion: Qt's standard ones came in the
    // system's language and light).
    Dialog {
        id: renameDialog
        objectName: "renameLayout"
        // Named on its item: Accessible on a Popup itself is refused ("must be
        // attached to an object deriving from Item").
        Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => qsTr("Rename layout"))
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(400, page.width - 48)
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        onAboutToShow: nameField.text = page.ws.activeLabel
        onOpened: {
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        function done(ok) {
            if (ok) page.ws.rename(page.ws.active, nameField.text)
            close()
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                text: qsTr("Rename layout")
                Accessible.role: Accessible.Heading
                color: Theme.text
                font.pixelSize: Theme.px(20)
                font.bold: true
            }
            TextField {
                id: nameField
                objectName: "layoutNameField"
                Layout.fillWidth: true
                selectByMouse: true
                // Android: Return, as its Done hides the keyboard before Enter arrives (Enter lost) and Next makes it a Tab (roadmap 142)
                EnterKey.type: Qt.platform.os === "android" ? Qt.EnterKeyReturn : Qt.EnterKeyDefault
                onAccepted: renameDialog.done(true)
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: renameDialog.done(false)
                }
                AppButton {
                    objectName: "layoutRenameOk"
                    text: qsTr("Rename")
                    Layout.fillWidth: true
                    onClicked: renameDialog.done(true)
                }
            }
        }
    }
    Dialog {
        id: deleteLayoutDialog
        objectName: "deleteLayout"
        // Named on its item: Accessible on a Popup itself is refused ("must be
        // attached to an object deriving from Item").
        Component.onCompleted: contentItem.parent.Accessible.name = Qt.binding(() => qsTr("Delete layout"))
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(420, page.width - 48)
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                text: qsTr("Delete layout")
                Accessible.role: Accessible.Heading
                color: Theme.text
                font.pixelSize: Theme.px(20)
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Delete %1? The screen goes back to Classic.").arg(page.ws.activeLabel)
                color: Theme.textDim
                font.pixelSize: Theme.px(15)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: deleteLayoutDialog.close()
                }
                AppButton {
                    objectName: "layoutDeleteYes"
                    text: qsTr("Delete")
                    warn: true
                    Layout.fillWidth: true
                    onClicked: {
                        deleteLayoutDialog.close()
                        page.ws.remove(page.ws.active)
                        page.ws.canvas = false
                        page.stopEditing()
                    }
                }
            }
        }
    }
    LogPage { id: logPage }
    NetworkDialog { id: networkDialog }
    RadioSetupPage { id: radioSetup }
    Connections {
        target: Engine
        function onSetupRequested(section) {
            radioSetup.section = section
            radioSetup.open()
        }
    }
    SetupSuggestion { onSetupRequested: radioSetup.open() }
    HintsDialog { onClosed: page.forceActiveFocus() }

    FrequencyDialog {
        id: freqDialog
        onClosed: page.forceActiveFocus()
    }
    // The Controls drawer open again on a page built anew while it was open
    // (a foldable's other screen): see Engine.controlsDrawerOpen. Not where
    // the controls are pinned instead.
    property bool leaving: false
    Component.onCompleted: {
        page.forceActiveFocus()
        Qt.callLater(page.migrateSlices)
        if (Engine.controlsDrawerOpen)
            Qt.callLater(() => { if (!(page.canvasOn ? page.canvasControls : page.panelPinned)) controlsDrawer.open() })
    }
    // A layout switch (rotation, Split View, fold/unfold) destroys this page;
    // a held PTT must not outlive its button.
    Component.onDestruction: {
        leaving = true
        Engine.keyboardOnPage = false
        Engine.stopTransmit("layout changed")
    }
}
