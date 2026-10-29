import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A slider with − and + at its ends, for a value a finger cannot hit
// exactly (Larry Averbeck): drag near it, then a tap on − or + moves one
// step, and a held one keeps stepping. value is the radio's; while a finger
// is on the slider or a button the slider keeps its own, so a status echo
// cannot pull it back mid-change. moved() as the Slider's, on every step.
RowLayout {
    id: fine
    property real value: 0
    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    // What the slider shows now (the value label follows it mid-drag).
    readonly property real shown: slider.value
    readonly property bool busy: slider.pressed || down.pressed || up.pressed
    // False for a value the radio takes once, on release (a DSP level, a
    // Radio Setup level): a drag then sends only where it ends; − / + still
    // send each step.
    property bool liveDrag: true
    signal moved(real value)
    spacing: 2

    function nudge(dir) {
        const before = slider.value
        if (dir < 0) {
            slider.decrease()
        } else {
            slider.increase()
        }
        if (slider.value !== before) {
            fine.moved(slider.value)
        }
    }

    RepeatButton {
        id: down
        text: "−"
        Accessible.name: qsTr("One step down")
        Layout.preferredWidth: Theme.touchTarget
        Layout.preferredHeight: Theme.touchTarget
        onStepped: fine.nudge(-1)
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        stepSize: 1
        snapMode: Slider.SnapAlways
        focusPolicy: Qt.NoFocus
        onMoved: if (fine.liveDrag) fine.moved(value)
        onPressedChanged: if (!pressed && !fine.liveDrag) fine.moved(value)
        Binding on value {
            when: !fine.busy
            value: fine.value
            restoreMode: Binding.RestoreNone
        }
    }
    RepeatButton {
        id: up
        text: "+"
        Accessible.name: qsTr("One step up")
        Layout.preferredWidth: Theme.touchTarget
        Layout.preferredHeight: Theme.touchTarget
        onStepped: fine.nudge(1)
    }
}
