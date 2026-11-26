import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive extras of the active slice: RIT, XIT, squelch and the notch
// filters (antennas, AGC, AGC-T and RF gain are on the info panel). Shared by the tablet side panel
// and the drawer.
ColumnLayout {
    id: rx
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // RIT / XIT rows: on/off, -/+ (hold to run faster), value (tap resets to 0).
    component OffsetRow: RowLayout {
        id: row
        property string label
        property bool on
        property int hz
        signal toggled(bool on)
        signal changed(int hz)
        signal edit()
        Layout.fillWidth: true
        property int acc: 0
        // On the step's grid: from an odd value (9999 at the limit) plain
        // ±10 ran …19, 9, −1 past zero (the user on Android, build 152).
        function step(n, dir) {
            if (n === 0) acc = hz
            const s = n < 10 ? 10 : n < 25 ? 50 : 100
            acc = (dir > 0 ? Math.floor(acc / s) : Math.ceil(acc / s)) * s + dir * s
            changed(acc)
        }
        AppButton {
            objectName: row.label.toLowerCase() + "Toggle"
            text: row.label
            checked: row.on
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: row.toggled(!row.on)
        }
        // Hold to run: 10 Hz steps, then 50 Hz, then 100 Hz the longer it is
        // held. Counted from the value at the press, not the radio's echo.
        RepeatButton {
            text: "−"
            Accessible.name: qsTr("%1, one step down").arg(row.label)
            Layout.preferredWidth: 44
            onStepped: (n) => row.step(n, -1)
        }
        Label {
            text: (row.hz > 0 ? "+" : "") + row.hz + " Hz"
            color: row.on ? Theme.text : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.px(15)
            Layout.fillWidth: true
            // A tap types the offset (roadmap 80); Reset there sets 0.
            objectName: row.label.toLowerCase() + "Value"
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("%1 %2 Hz, type it").arg(row.label).arg(row.hz)
            Accessible.onPressAction: row.edit()
            TapHandler { onTapped: row.edit() }
        }
        RepeatButton {
            text: "+"
            Accessible.name: qsTr("%1, one step up").arg(row.label)
            Layout.preferredWidth: 44
            onStepped: (n) => row.step(n, 1)
        }
    }

    OffsetRow {
        label: qsTr("RIT")
        on: Engine.ritOn
        hz: Engine.ritHz
        visible: Engine.mode !== "FM"
        onToggled: (on) => Engine.ritOn = on
        onChanged: (hz) => Engine.ritHz = hz
        onEdit: {
            offsetEditor.target = "rit"
            offsetEditor.ask(qsTr("RIT offset"), Engine.ritHz)
        }
    }
    OffsetRow {
        label: qsTr("XIT")
        on: Engine.xitOn
        hz: Engine.xitHz
        visible: Engine.mode !== "FM"
        onToggled: (on) => Engine.xitOn = on
        onChanged: (hz) => Engine.xitHz = hz
        onEdit: {
            offsetEditor.target = "xit"
            offsetEditor.ask(qsTr("XIT offset"), Engine.xitHz)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: Engine.squelchAvailable
        AppButton {
            text: qsTr("SQL")
            checked: Engine.squelchOn
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: Engine.squelchOn = !Engine.squelchOn
        }
        // − / + for the last step (Larry Averbeck), and the level read out.
        FineSlider {
            id: squelchSlider
            accessibleName: qsTr("Squelch level")
            Layout.fillWidth: true
            from: 0
            to: 100
            value: Engine.squelchLevel
            enabled: Engine.squelchOn
            onMoved: (v) => Engine.squelchLevel = Math.round(v)
        }
        Label {
            text: Math.round(squelchSlider.shown)
            color: Engine.squelchOn ? Theme.text : Theme.textDim
            font.pixelSize: Theme.px(15)
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 28
        }
    }

    // Tracking notch filters: the global switch (all notches on / off,
    // kept) and + TNF (at the passband's centre, as the desktop); + TNF and
    // a finger held on the display open a new notch's settings to confirm
    // with Add, a tap on a notch opens its settings.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.tnfAvailable
        AppButton {
            text: qsTr("TNF")
            checked: Engine.tnfEnabled
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            onClicked: Engine.tnfEnabled = !Engine.tnfEnabled
        }
        AppButton {
            objectName: "tnfNew"
            text: qsTr("+ TNF")
            enabled: Engine.canAddTnf
            Layout.preferredWidth: 72
            onClicked: Engine.requestTnfCreate()
        }
        Label {
            Layout.fillWidth: true
            text: Engine.tnfs.length === 0 ? qsTr("Hold the display to add a notch.")
                : Engine.tnfs.length === 1 ? qsTr("1 notch; tap it on the display to adjust.")
                : qsTr("%1 notches; tap one on the display to adjust.").arg(Engine.tnfs.length)
            color: Theme.textDim
            font.pixelSize: Theme.smallFont
            wrapMode: Text.WordWrap
        }
    }
    // The RIT / XIT offset typed (roadmap 80, Sebastian Schumann DG5CST): a
    // value set turns it on; Reset sets 0 and leaves on / off as it is.
    OffsetEditor {
        id: offsetEditor
        property string target: "xit"
        onApplied: (hz) => {
            if (target === "rit") {
                Engine.ritHz = hz
                if (hz !== 0) Engine.ritOn = true
            } else {
                Engine.xitHz = hz
                if (hz !== 0) Engine.xitOn = true
            }
        }
    }
}
