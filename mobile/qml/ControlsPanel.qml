import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The operating controls in sections (DSP, Audio, TX, eSSB, CW, View,
// Profiles, Memory, Log, FTx, FreeDV, Digi, and Amp once an amplifier is
// set up in Radio Setup). In the slide-in drawer they are tabs, one at a
// time.
// Pinned to the iPad's side (multiSelect) they are toggle buttons, as
// AetherSDR's applet buttons: every chosen section shows, one under another.
ColumnLayout {
    id: panel
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Tablet only: offer to put the panel back into the page.
    property bool showPinButton: false
    // Pinned side panel: several sections at once, chosen by toggles.
    property bool multiSelect: false
    signal pinRequested()
    // A tablet's drawer in landscape (roadmap 108, Larry Averbeck W9FLA):
    // a × beside Pin Controls closes it (upright its Close is at the bottom).
    property bool showCloseButton: false
    signal closeRequested()
    // With no Pin Controls the × sits beside the section buttons' first row
    // instead of in a row of its own (an Android foldable's inner screen in
    // landscape, the user 2026-11-21: "rivi jää muuten kokonaan tyhjäksi").
    // The drawer is that much wider then (closeColumn), so the buttons keep
    // their width and rows and every label still fits.
    readonly property bool closeInGrid: showCloseButton && !showPinButton && !multiSelect
    readonly property real closeColumn: Theme.touchTarget + 4
    readonly property real gridWidth: panel.width - (closeInGrid ? closeColumn : 0)

    // name: kept in the settings (pinned sections); button: the short label,
    // so every button can be the same size; title: the section's heading.
    readonly property var sections: [
        { name: "DSP", button: qsTr("DSP"), title: qsTr("DSP"), component: dspSection },
        { name: "Audio", button: qsTr("Audio"), title: qsTr("Audio"), component: audioSection },
        { name: "TX", button: qsTr("TX"), title: qsTr("TX"), component: txSection },
        { name: "ESSB", button: qsTr("eSSB"), title: qsTr("eSSB"), component: essbSection },
        { name: "CW", button: qsTr("CW"), title: qsTr("CW"), component: cwSection },
        { name: "View", button: qsTr("View"), title: qsTr("View"), component: viewSection },
        // Roadmap 134: spots on a tab of their own (until build 203 on View).
        { name: "Spots", button: qsTr("Spots"), title: qsTr("Spots"), component: spotsSection },
        { name: "Profiles", button: qsTr("Prof"), title: qsTr("Profiles"), component: profileSection },
        { name: "Memory", button: qsTr("Mem"), title: qsTr("Memory"), component: memorySection },
        { name: "Log", button: qsTr("Log"), title: qsTr("Log"), component: logSection },
        { name: "Digi", button: qsTr("FTx"), title: qsTr("FT8 / FT4"), component: digiSection },
        { name: "FreeDV", button: qsTr("FreeDV"), title: qsTr("FreeDV"), component: freedvSection },
        { name: "RTTY", button: qsTr("Digi"), title: qsTr("Digi (RTTY, PSK)"), component: rttySection },
    ].concat(ampSet ? [{ name: "Amp", button: qsTr("Amp"), title: qsTr("Amplifier"), component: ampSection }] : [])
    // A bool of its own: Engine.amp changes with every reading, and a new
    // sections list would rebuild every tab.
    readonly property bool ampSet: !!Engine.amp.set || !!Engine.kpa.set || !!Engine.kpa500.set || !!Engine.kat500.set
                                   || !!Engine.pgxl.present || !!Engine.tgxl.present || !!Engine.ag.present
    // One row of all on a wide panel, else rows of six (6 + 6); with the
    // Amp tab rows of seven (7 + 6) where the widest label still fits at
    // 12 px, else 5 + 5 + 3 (a low side drawer, 667 x 375, cut "FreeDV").
    readonly property int buttonColumns: {
        if (panel.gridWidth >= 700) return sections.length
        if (sections.length <= 12) return 6
        // The label at 12 px and the buttons' 2 + 2 px padding.
        const need = widestButton * 12 / Theme.buttonFont + 4
        return (panel.gridWidth - 6 * 4) / 7 >= need ? 7 : 5
    }
    // The widest label at the buttons' type size: where six to a row are
    // narrower (a side drawer, 698 x 668 cut "FreeDV"), every section
    // button's type shrinks alike until it fits, down to 12 px.
    // Measured again when the type size changes: advanceWidth() alone is not
    // a dependency, so a size changed after start kept the old width (the
    // label 58 px at 12 px type, and the Amp tab's 7 + 6 rows fell to
    // 5 + 5 + 3 on a 430 pt phone).
    FontMetrics { id: buttonMetrics; font.pixelSize: Theme.buttonFont }
    readonly property real widestButton: buttonMetrics.font.pixelSize > 0
        ? Math.max(...sections.map(s => buttonMetrics.advanceWidth(s.button))) : 0
    function buttonFont(width) {
        return Math.max(12, Math.min(Theme.buttonFont, Math.floor(Theme.buttonFont * (width - 6) / widestButton)))
    }
    // One button's width from the panel's (the buttons share a row alike):
    // the type follows it, not the button's own width, which the type's
    // implicit width feeds back into while the grid changes (the Amp tab
    // coming or going looped).
    readonly property real buttonCellWidth: (panel.gridWidth - (buttonColumns - 1) * 4) / buttonColumns
    readonly property int sectionButtonFont: buttonFont(buttonCellWidth)

    // A section shown by name (roadmap 137: a spot's Tune brings its mode's
    // tab): the drawer switches to it; the pinned panel adds it to the
    // pinned sections when it is not among them.
    // How many of the pinned sections show: a stored name of a section this
    // radio does not offer now (Amp with no amplifier set, a tab of an older
    // build) is in the list but not on screen, and counted as a second
    // section it took List only from an iPad with FTx alone (the user, the
    // preview of build 213: "ei ole list only valintaa???").
    readonly property int pinnedShown: {
        const pinned = Engine.pinnedSections
        let n = 0
        for (let i = 0; i < sections.length; ++i) {
            if (pinned.indexOf(sections[i].name) >= 0)
                ++n
        }
        return n
    }

    function showSection(name) {
        if (panel.multiSelect) {
            if (Engine.pinnedSections.indexOf(name) < 0)
                toggleSection(name)
            return
        }
        for (let i = 0; i < panel.sections.length; ++i) {
            if (panel.sections[i].name === name) {
                tabs.currentIndex = i
                return
            }
        }
    }

    function toggleSection(name) {
        const list = Engine.pinnedSections.slice()
        const i = list.indexOf(name)
        if (i >= 0) list.splice(i, 1)
        else list.push(name)
        Engine.pinnedSections = list
    }

    RowLayout {
        // Pin Controls, with the × beside it; without Pin Controls the ×
        // is at the end of the section buttons (closeInGrid).
        visible: panel.showPinButton
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            objectName: "pinControls"
            visible: panel.showPinButton
            text: qsTr("Pin Controls")
            Layout.fillWidth: true
            onClicked: panel.pinRequested()
        }
        AppButton {
            objectName: "closeControlsTop"
            visible: panel.showCloseButton
            text: "×"
            font.pixelSize: Theme.buttonFont * 1.3
            Accessible.name: qsTr("Close controls")
            Layout.preferredWidth: Theme.touchTarget
            onClicked: panel.closeRequested()
        }
    }

    // ---- Drawer: one section at a time, chosen from a button grid ----
    // A grid rather than a tab bar: on an iPhone all the sections show at
    // once (6 + 6) instead of hiding behind a scrolling strip.
    Item {
        id: tabs
        objectName: "controlsTabs"
        property int currentIndex: 0
        // The chosen tab outlives the page (Engine.controlsSection): a
        // foldable's other screen builds the page anew.
        onCurrentIndexChanged: if (!panel.multiSelect && panel.sections[currentIndex])
                                   Engine.controlsSection = panel.sections[currentIndex].name
        Component.onCompleted: if (!panel.multiSelect && Engine.controlsSection !== "")
                                   panel.showSection(Engine.controlsSection)
        visible: !panel.multiSelect
        Layout.fillWidth: true
        implicitHeight: tabGrid.implicitHeight
        AppButton {
            objectName: "closeControlsGrid"
            visible: panel.closeInGrid
            anchors.right: parent.right
            anchors.top: parent.top
            width: Theme.touchTarget
            height: 40
            padding: 0
            text: "×"
            font.pixelSize: Theme.buttonFont * 1.3
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Close controls")
            onClicked: panel.closeRequested()
        }
        GridLayout {
            id: tabGrid
            width: panel.gridWidth
            columns: panel.buttonColumns
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: panel.sections
                delegate: AppButton {
                    required property int index
                    required property var modelData
                    text: modelData.button
                    // The short label and then the heading ("Mem, Memory"): "FTx" alone
                    // says little to a screen reader.
                    Accessible.name: modelData.title.indexOf(modelData.button) === 0 ? modelData.title
                                     : qsTr("%1, %2").arg(modelData.button).arg(modelData.title)
                    checked: tabs.currentIndex === index
                    font.pixelSize: panel.sectionButtonFont
                    leftPadding: 2
                    rightPadding: 2
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 40
                    focusPolicy: Qt.NoFocus
                    onClicked: tabs.currentIndex = index
                }
            }
        }
    }
    StackLayout {
        visible: !panel.multiSelect
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabs.currentIndex
        Repeater {
            model: panel.multiSelect ? [] : panel.sections
            delegate: PageScroll {
                id: tabScroll
                objectName: "controlsTabScroll"
                required property var modelData
                Loader {
                    width: tabScroll.availableWidth
                    sourceComponent: tabScroll.modelData.component
                    // A tab that can fill the view (FTx's List only) is
                    // told the view's height, and is made that high when
                    // it asks (viewFill): what stretches in it is its own
                    // matter. Other tabs keep their own height.
                    // Only the tab shown has a view: the stack sizes no
                    // other, so a tab not shown is as high as its content,
                    // and its "view's height" was its own height. The FTx
                    // tab took that for room for the list, became shorter,
                    // lost the room, grew again with every decode as a row,
                    // and so on without end: the app stood still (build
                    // 213's preview, FT8 with decodes and List only on,
                    // another tab open: a OnePlus Open after unfolding,
                    // where the tab went back to DSP).
                    onLoaded: if (item.hasOwnProperty("viewHeight")) {
                                  item.viewHeight = Qt.binding(() => tabScroll.StackLayout.isCurrentItem
                                                                     ? tabScroll.availableHeight : 0)
                                  height = Qt.binding(() => item ? Math.max(item.implicitHeight, item.viewFill) : 0)
                              }
                }
            }
        }
    }

    // ---- Pinned: toggles, then every chosen section ----
    // A grid of equal buttons, as the drawer's.
    GridLayout {
        visible: panel.multiSelect
        Layout.fillWidth: true
        columns: panel.buttonColumns
        columnSpacing: 4
        rowSpacing: 4
        Repeater {
            model: panel.sections
            delegate: AppButton {
                required property var modelData
                text: modelData.button
                Accessible.name: modelData.title.indexOf(modelData.button) === 0 ? modelData.title
                                 : qsTr("%1, %2").arg(modelData.button).arg(modelData.title)
                checked: Engine.pinnedSections.indexOf(modelData.name) >= 0
                font.pixelSize: panel.sectionButtonFont
                leftPadding: 2
                rightPadding: 2
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                focusPolicy: Qt.NoFocus
                onClicked: panel.toggleSection(modelData.name)
            }
        }
    }
    PageScroll {
        id: pinnedScroll
        objectName: "pinnedScroll"
        visible: panel.multiSelect
        Layout.fillWidth: true
        Layout.fillHeight: true
        ColumnLayout {
            width: pinnedScroll.availableWidth
            spacing: Theme.spacing
            Repeater {
                model: panel.multiSelect ? panel.sections : []
                delegate: ColumnLayout {
                    id: pinnedSection
                    required property var modelData
                    readonly property bool shown: Engine.pinnedSections.indexOf(modelData.name) >= 0
                    visible: shown
                    Layout.fillWidth: true
                    spacing: Theme.spacing / 2
                    Label { text: modelData.title; color: Theme.accent; font.pixelSize: 15; font.bold: true }
                    Loader {
                        id: pinnedLoader
                        Layout.fillWidth: true
                        active: parent.shown
                        sourceComponent: modelData.component
                        // The only section shown has the panel's height to
                        // itself: a tab that can fill a view (FTx's List
                        // only) gets it here as in the drawer (roadmap 169:
                        // the pinned panel had no List only at all). With
                        // more sections the panel scrolls as one, as before.
                        onLoaded: if (item.hasOwnProperty("viewHeight")) {
                                      item.viewHeight = Qt.binding(() => panel.pinnedShown === 1
                                                                         ? Math.max(0, pinnedScroll.availableHeight - pinnedSection.y - pinnedLoader.y) : 0)
                                      Layout.preferredHeight = Qt.binding(() => item && item.viewFill > 0
                                                                                ? Math.max(item.implicitHeight, item.viewFill) : -1)
                                  }
                    }
                }
            }
            Label {
                visible: Engine.pinnedSections.length === 0
                text: qsTr("Choose the controls to show with the buttons above.")
                color: Theme.textDim
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: !Engine.transmitAvailable
        text: Engine.transmitUnavailableReason
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    // ---- Sections ----
    Component {
        id: dspSection
        ColumnLayout {
            RxControls {
                Layout.fillWidth: true
            }
        }
    }
    Component {
        id: audioSection
        ColumnLayout {
            // Receive audio coding: Opus when remote (Auto), or fixed.
            Label { text: qsTr("Receive audio"); color: Theme.textDim }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [{ key: "Auto", label: qsTr("Auto") }, { key: "Opus", label: qsTr("Opus") },
                            { key: "None", label: qsTr("Uncompressed") }]
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        checked: Engine.audioCompression === modelData.key
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        onClicked: Engine.audioCompression = modelData.key
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Auto uses Opus over SmartLink, a VPN or another network, uncompressed only on the radio's own network. This connection: %1. A change applies from the next connection.")
                      .arg(Engine.audioCodingInUse.length > 0 ? Engine.audioCodingInUse : "–")
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            // Receive buffer: steadier audio over a jittery link.
            Label { text: qsTr("Audio buffer"); color: Theme.textDim; Layout.topMargin: Theme.spacing / 2 }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [{ key: "Low", label: qsTr("Low latency") }, { key: "Normal", label: qsTr("Normal") },
                            { key: "Smooth", label: qsTr("Smooth") }]
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        checked: Engine.audioBuffer === modelData.key
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        onClicked: Engine.audioBuffer = modelData.key
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("If the audio stutters, try Smooth: a longer buffer evens out a jittery Wi-Fi or mobile link for a little more delay.")
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            // This device or the radio's own outputs (Dennis KM6DF: a headset
            // on the radio): the desktop's PC audio on / off.
            Label {
                visible: Engine.audioRouteSelectable
                text: qsTr("Sound from")
                color: Theme.textDim
                Layout.topMargin: Theme.spacing / 2
            }
            RowLayout {
                visible: Engine.audioRouteSelectable
                Layout.fillWidth: true
                AppButton {
                    objectName: "audioRouteDevice"
                    text: qsTr("This device")
                    checked: !Engine.audioOnRadio
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    onClicked: Engine.audioOnRadio = false
                }
                AppButton {
                    objectName: "audioRouteRadio"
                    text: qsTr("The radio")
                    checked: Engine.audioOnRadio
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    onClicked: Engine.audioOnRadio = true
                }
            }
            Label {
                visible: Engine.audioRouteSelectable
                Layout.fillWidth: true
                text: Engine.audioOnRadio
                      ? qsTr("The radio plays on its own speaker, headphones and line out; nothing plays here. For its microphone too: TX tab › Use the radio's microphone (shared with any other station on the radio), then key with the radio's PTT or VOX.")
                      : qsTr("The radio's own speaker and headphones stay quiet while this device plays. The radio: for a headset or speaker on the radio.")
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            // Where receive audio plays.
            Label { visible: !Engine.audioOnRadio; text: qsTr("Audio output"); color: Theme.textDim; Layout.topMargin: Theme.spacing / 2 }
            RowLayout {
                visible: !Engine.audioOnRadio
                Layout.fillWidth: true
                Repeater {
                    model: [{ key: "Auto", label: qsTr("Auto") }, { key: "Speaker", label: qsTr("Speaker") },
                            { key: "Bluetooth", label: qsTr("Bluetooth media") }]
                    delegate: AppButton {
                        required property var modelData
                        objectName: "audioOutput" + modelData.key
                        text: modelData.label
                        checked: Engine.audioOutput === modelData.key
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        onClicked: Engine.audioOutput = modelData.key
                    }
                }
            }
            Label {
                visible: !Engine.audioOnRadio
                Layout.fillWidth: true
                text: qsTr("Auto: a Bluetooth headset and its microphone when one is connected, else the device's speaker and microphone. Speaker: the device's speaker and microphone, even with Bluetooth on. Bluetooth media: a Bluetooth device's music link (better sound) and the device's microphone; without one, the speaker.")
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            // AF gain and mute are on the info panel's speaker.
            EqControls {
                Layout.topMargin: Theme.spacing
                Layout.fillWidth: true
            }
        }
    }
    Component { id: txSection; TxControls {} }
    Component { id: essbSection; EssbControls {} }
    Component { id: cwSection; CwControls {} }
    Component { id: viewSection; DisplayControls {} }
    Component { id: spotsSection; SpotsControls {} }
    Component { id: profileSection; ProfileControls {} }
    Component { id: memorySection; MemoryControls {} }
    Component { id: logSection; LogControls {} }
    Component { id: digiSection; DigiControls {} }
    Component { id: freedvSection; FreeDvControls {} }
    Component { id: rttySection; RttyControls {} }
    // The RF2K-S, the KPA1500, the KPA500 and the KAT500 set up in Radio
    // Setup, and the 4O3A devices the radio reports.
    Component {
        id: ampSection
        ColumnLayout {
            spacing: Theme.spacing
            AmpControls { visible: !!Engine.amp.set; Layout.fillWidth: true }
            AmpControls { a: Engine.kpa; visible: !!Engine.kpa.set; Layout.fillWidth: true }
            AmpControls { a: Engine.kpa500; visible: !!Engine.kpa500.set; Layout.fillWidth: true }
            Kat500Controls { visible: !!Engine.kat500.set; Layout.fillWidth: true }
            GeniusControls { visible: !!Engine.pgxl.present || !!Engine.tgxl.present || !!Engine.ag.present; Layout.fillWidth: true }
        }
    }
}
