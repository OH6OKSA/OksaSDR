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

    // name: kept in the settings (pinned sections); button: the short label,
    // so every button can be the same size; title: the section's heading.
    readonly property var sections: [
        { name: "DSP", button: qsTr("DSP"), title: qsTr("DSP"), component: dspSection },
        { name: "Audio", button: qsTr("Audio"), title: qsTr("Audio"), component: audioSection },
        { name: "TX", button: qsTr("TX"), title: qsTr("TX"), component: txSection },
        { name: "ESSB", button: qsTr("eSSB"), title: qsTr("eSSB"), component: essbSection },
        { name: "CW", button: qsTr("CW"), title: qsTr("CW"), component: cwSection },
        { name: "View", button: qsTr("View"), title: qsTr("View"), component: viewSection },
        { name: "Profiles", button: qsTr("Prof"), title: qsTr("Profiles"), component: profileSection },
        { name: "Memory", button: qsTr("Mem"), title: qsTr("Memory"), component: memorySection },
        { name: "Log", button: qsTr("Log"), title: qsTr("Log"), component: logSection },
        { name: "Digi", button: qsTr("FTx"), title: qsTr("FT8 / FT4"), component: digiSection },
        { name: "FreeDV", button: qsTr("FreeDV"), title: qsTr("FreeDV"), component: freedvSection },
        { name: "RTTY", button: qsTr("Digi"), title: qsTr("Digi (RTTY, PSK)"), component: rttySection },
    ].concat(ampSet ? [{ name: "Amp", button: qsTr("Amp"), title: qsTr("Amplifier"), component: ampSection }] : [])
    // A bool of its own: Engine.amp changes with every reading, and a new
    // sections list would rebuild every tab.
    readonly property bool ampSet: !!Engine.amp.set || !!Engine.kpa.set || !!Engine.pgxl.present || !!Engine.tgxl.present || !!Engine.ag.present
    // One row of all on a wide panel, else rows of six (6 + 6); with the
    // Amp tab rows of seven (7 + 6) where the widest label still fits at
    // 12 px, else 5 + 5 + 3 (a low side drawer, 667 x 375, cut "FreeDV").
    readonly property int buttonColumns: {
        if (panel.width >= 700) return sections.length
        if (sections.length <= 12) return 6
        // The label at 12 px and the buttons' 2 + 2 px padding.
        const need = widestButton * 12 / Theme.buttonFont + 4
        return (panel.width - 6 * 4) / 7 >= need ? 7 : 5
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
    readonly property real buttonCellWidth: (panel.width - (buttonColumns - 1) * 4) / buttonColumns
    readonly property int sectionButtonFont: buttonFont(buttonCellWidth)

    function toggleSection(name) {
        const list = Engine.pinnedSections.slice()
        const i = list.indexOf(name)
        if (i >= 0) list.splice(i, 1)
        else list.push(name)
        Engine.pinnedSections = list
    }

    AppButton {
        visible: panel.showPinButton
        text: qsTr("Pin Controls")
        Layout.fillWidth: true
        onClicked: panel.pinRequested()
    }

    // ---- Drawer: one section at a time, chosen from a button grid ----
    // A grid rather than a tab bar: on an iPhone all the sections show at
    // once (6 + 6) instead of hiding behind a scrolling strip.
    Item {
        id: tabs
        objectName: "controlsTabs"
        property int currentIndex: 0
        visible: !panel.multiSelect
        Layout.fillWidth: true
        implicitHeight: tabGrid.implicitHeight
        GridLayout {
            id: tabGrid
            width: parent.width
            columns: panel.buttonColumns
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: panel.sections
                delegate: AppButton {
                    required property int index
                    required property var modelData
                    text: modelData.button
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
                    // A tab that can fill the view (FTx's List only) gets its height.
                    onLoaded: if (item.hasOwnProperty("viewHeight"))
                                  item.viewHeight = Qt.binding(() => tabScroll.availableHeight)
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
                    required property var modelData
                    readonly property bool shown: Engine.pinnedSections.indexOf(modelData.name) >= 0
                    visible: shown
                    Layout.fillWidth: true
                    spacing: Theme.spacing / 2
                    Label { text: modelData.title; color: Theme.accent; font.pixelSize: 15; font.bold: true }
                    Loader {
                        Layout.fillWidth: true
                        active: parent.shown
                        sourceComponent: modelData.component
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
                text: qsTr("Auto uses Opus over SmartLink or a VPN, uncompressed on the local network. This connection: %1. A change applies from the next connection.")
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
    Component { id: profileSection; ProfileControls {} }
    Component { id: memorySection; MemoryControls {} }
    Component { id: logSection; LogControls {} }
    Component { id: digiSection; DigiControls {} }
    Component { id: freedvSection; FreeDvControls {} }
    Component { id: rttySection; RttyControls {} }
    // The RF2K-S and the KPA1500 set up in Radio Setup, and the 4O3A devices
    // the radio reports.
    Component {
        id: ampSection
        ColumnLayout {
            spacing: Theme.spacing
            AmpControls { visible: !!Engine.amp.set; Layout.fillWidth: true }
            AmpControls { a: Engine.kpa; visible: !!Engine.kpa.set; Layout.fillWidth: true }
            GeniusControls { visible: !!Engine.pgxl.present || !!Engine.tgxl.present || !!Engine.ag.present; Layout.fillWidth: true }
        }
    }
}
