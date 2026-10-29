import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's memory channels (as AetherSDR's Memory panel): tap one to tune
// to it, save the current slice under a name, press and hold to edit its
// name, frequency, mode and, in FM, its repeater shift and CTCSS tone, or to
// delete it.
ColumnLayout {
    id: mem
    spacing: Theme.spacing / 2
    implicitWidth: 200

    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name for the current frequency")
            font.pixelSize: 15
            onAccepted: saveButton.clicked()
        }
        AppButton {
            id: saveButton
            text: qsTr("Save")
            enabled: Engine.connectionState === "connected"
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.saveMemory(nameField.text)
                nameField.text = ""
            }
        }
    }
    Label {
        Layout.fillWidth: true
        text: Engine.memories.length === 0 ? qsTr("No memories on the radio yet.")
            : Engine.memoriesWritable ? qsTr("Tap to tune; press and hold to edit or delete.")
            : qsTr("Tap to tune.")
        color: Theme.textDim
        font.pixelSize: 14
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: Engine.memories
        delegate: AppButton {
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            contentItem: RowLayout {
                spacing: Theme.spacing
                Label {
                    text: (modelData.freq).toFixed(4)
                    font.family: "monospace"
                    font.pixelSize: 15
                    color: Theme.buttonText
                }
                Label { text: modelData.mode; font.pixelSize: 13; color: Theme.buttonTextDim }
                // FM repeater: the shift and the CTCSS tone, e.g. "−7.6 88.5".
                Label {
                    visible: text.length > 0
                    text: (modelData.dir === "down" ? "\u2212" + modelData.offset.toFixed(1)
                           : modelData.dir === "up" ? "+" + modelData.offset.toFixed(1) : "")
                          + (modelData.ctcss && modelData.tone ? (modelData.dir !== "simplex" ? " " : "") + modelData.tone : "")
                    font.pixelSize: 13
                    color: Theme.accent
                }
                Label {
                    text: modelData.name
                    font.pixelSize: 14
                    color: Theme.buttonText
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            onClicked: Engine.recallMemory(modelData.index)
            onPressAndHold: {
                if (Engine.memoriesWritable) {
                    editDialog.edit(modelData)
                }
            }
        }
    }

    Dialog {
        id: editDialog
        objectName: "memoryEditDialog"
        property int index: -1
        property string mode: ""
        // FM repeater: direction, shift (MHz), CTCSS on and its tone.
        property string dir: "simplex"
        property real offset: 0
        property bool ctcss: false
        property string tone: ""
        readonly property bool fm: mode === "FM" || mode === "NFM" || mode === "DFM"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 24)
        height: Math.min(implicitHeight, parent.height - 16)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }

        function edit(m) {
            index = m.index
            nameEdit.text = m.name
            freqEdit.text = m.freq.toFixed(6).replace(/0{1,3}$/, "")
            mode = m.mode
            dir = m.dir || "simplex"
            offset = m.offset || 0
            ctcss = !!m.ctcss
            tone = m.tone || ""
            shiftEdit.text = offset.toFixed(3)
            open()
        }
        // Screenshots (--popup memoryEditDialog): the first memory, or a
        // sample that cannot be saved (index -1) when there is none.
        function preview() {
            edit(Engine.memories.length > 0 ? Engine.memories[0]
                 : { index: -1, name: "Net 80 m", freq: 3.699, mode: "LSB" })
        }
        // MHz typed with a dot or a comma; NaN when it is not a frequency.
        readonly property real freqValue: {
            const v = Number(freqEdit.text.trim().replace(",", "."))
            return freqEdit.text.trim().length > 0 && v > 0 && v < 1000 ? v : NaN
        }
        readonly property var modeChoices: {
            const list = Engine.modes.slice()
            if (mode.length > 0 && list.indexOf(mode) < 0) list.push(mode)
            return list
        }
        function save() {
            if (isNaN(freqValue)) return
            if (fm) {
                const v = parseFloat(shiftEdit.text.replace(",", "."))
                Engine.editMemory(index, nameEdit.text, freqValue, mode, dir,
                                  !isNaN(v) && v >= 0 && v <= 100 ? v : offset, ctcss ? 1 : 0, tone)
            } else {
                Engine.editMemory(index, nameEdit.text, freqValue, mode)
            }
            close()
        }

        contentItem: Flickable {
            id: editScroll
            readonly property bool overflows: editColumn.implicitHeight > height + 1
            implicitHeight: editColumn.implicitHeight
            contentHeight: editColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // A bar while the editor is taller than the window, the fields
            // clear of it.
            ScrollBar.vertical: ScrollBar {
                id: editBar
                policy: editScroll.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            ColumnLayout {
                id: editColumn
                width: parent.width - (editScroll.overflows ? editBar.width + 2 : 0)
                spacing: Theme.spacing / 2

                Label {
                    text: qsTr("Edit memory")
                    color: Theme.text
                    font.pixelSize: Theme.labelFont
                    font.bold: true
                }
                Label { text: qsTr("Name"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
                TextField {
                    id: nameEdit
                    Layout.fillWidth: true
                    font.pixelSize: 15
                    onAccepted: freqEdit.forceActiveFocus()
                }
                Label { text: qsTr("Frequency (MHz)"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: freqEdit
                        Layout.fillWidth: true
                        font.pixelSize: 15
                        font.family: "monospace"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        color: isNaN(editDialog.freqValue) ? "#b3261e" : "#1a1a1a"
                        onAccepted: editDialog.save()
                    }
                    AppButton {
                        text: qsTr("Current")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        onClicked: {
                            freqEdit.text = Engine.frequencyMhz.toFixed(6).replace(/0{1,3}$/, "")
                            editDialog.mode = Engine.mode
                        }
                    }
                }
                Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 4
                    rowSpacing: 4
                    Repeater {
                        model: editDialog.modeChoices
                        delegate: AppButton {
                            required property string modelData
                            text: modelData
                            checked: editDialog.mode === modelData
                            focusPolicy: Qt.NoFocus
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.touchTarget
                            onClicked: editDialog.mode = modelData
                        }
                    }
                }
                // FM modes: the repeater the memory recalls (as the TX tab).
                Label {
                    visible: editDialog.fm
                    text: qsTr("FM repeater")
                    color: Theme.textDim
                    font.pixelSize: Theme.smallFont
                }
                RowLayout {
                    visible: editDialog.fm
                    Layout.fillWidth: true
                    Repeater {
                        model: [{ key: "simplex", label: qsTr("Simplex") }, { key: "down", label: "\u2212" }, { key: "up", label: "+" }]
                        delegate: AppButton {
                            required property var modelData
                            text: modelData.label
                            checked: editDialog.dir === modelData.key
                            Layout.fillWidth: true
                            Layout.preferredWidth: modelData.key === "simplex" ? 2 : 1
                            Layout.preferredHeight: Theme.touchTarget
                            focusPolicy: Qt.NoFocus
                            onClicked: editDialog.dir = modelData.key
                        }
                    }
                }
                RowLayout {
                    visible: editDialog.fm && editDialog.dir !== "simplex"
                    Layout.fillWidth: true
                    Repeater {
                        model: [0.6, 1.6, 5.0, 7.6]
                        delegate: AppButton {
                            required property real modelData
                            text: modelData.toFixed(1)
                            checked: Math.abs(parseFloat(shiftEdit.text.replace(",", ".")) - modelData) < 0.0005
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: Theme.touchTarget
                            leftPadding: 2
                            rightPadding: 2
                            focusPolicy: Qt.NoFocus
                            onClicked: shiftEdit.text = modelData.toFixed(3)
                        }
                    }
                    TextField {
                        id: shiftEdit
                        Layout.preferredWidth: 80
                        font.pixelSize: 15
                        horizontalAlignment: TextInput.AlignRight
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    Label { text: "MHz"; color: Theme.textDim; font.pixelSize: 13 }
                }
                RowLayout {
                    visible: editDialog.fm
                    Layout.fillWidth: true
                    AppButton {
                        text: qsTr("No tone")
                        checked: !editDialog.ctcss
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        onClicked: editDialog.ctcss = false
                    }
                    AppButton {
                        text: "CTCSS"
                        checked: editDialog.ctcss
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        onClicked: editDialog.ctcss = true
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 2
                        Layout.preferredHeight: Theme.touchTarget
                        enabled: editDialog.ctcss
                        model: Engine.ctcssTones
                        currentIndex: Engine.ctcssTones.indexOf(editDialog.tone)
                        displayText: currentIndex >= 0 ? currentText + " Hz" : qsTr("tone")
                        font.pixelSize: Theme.buttonFont
                        onActivated: (index) => editDialog.tone = Engine.ctcssTones[index]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacing / 2
                    spacing: Theme.spacing / 2
                    AppButton {
                        text: qsTr("Delete")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        onClicked: {
                            confirmDelete.index = editDialog.index
                            confirmDelete.label = nameEdit.text.length > 0 ? nameEdit.text : freqEdit.text
                            editDialog.close()
                            confirmDelete.open()
                        }
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: qsTr("Cancel")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        onClicked: editDialog.close()
                    }
                    AppButton {
                        text: qsTr("Save")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        enabled: !isNaN(editDialog.freqValue)
                        onClicked: editDialog.save()
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmDelete
        property int index: -1
        property string label
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete memory “%1”?").arg(label)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Engine.removeMemory(index)
    }
}
