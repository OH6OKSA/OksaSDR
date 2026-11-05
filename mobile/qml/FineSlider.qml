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
    // What it sets, for a screen reader ("Volume", "RF power"); its swipe
    // up or down is one step, sent as the − / + send theirs.
    property string accessibleName: ""
    signal moved(real value)
    spacing: 2

    // A drag is felt as ticks (Engine.haptic): each step on a short scale,
    // else each tenth of it and its ends, at most one tick in 40 ms.
    property int lastTick: -1
    property double lastTickMs: 0
    function feelDrag(v) {
        const steps = Math.round((slider.to - slider.from) / Math.max(slider.stepSize, 1e-9))
        const per = steps <= 20 ? 1 : steps / 10
        const tick = (v <= slider.from || v >= slider.to) ? (v <= slider.from ? -2 : -3)
                                                          : Math.floor((v - slider.from) / slider.stepSize / per)
        const now = Date.now()
        if (tick !== fine.lastTick && now - fine.lastTickMs >= 40) {
            fine.lastTick = tick
            fine.lastTickMs = now
            Engine.haptic("step")
        }
    }

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
        objectName: fine.objectName.length > 0 ? fine.objectName + "Down" : ""
        text: "−"
        Accessible.name: fine.accessibleName ? qsTr("%1, one step down").arg(fine.accessibleName) : qsTr("One step down")
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
        Accessible.name: fine.accessibleName
        Accessible.onIncreaseAction: fine.nudge(1)
        Accessible.onDecreaseAction: fine.nudge(-1)
        onMoved: {
            fine.feelDrag(value)
            if (fine.liveDrag) fine.moved(value)
        }
        onPressedChanged: if (!pressed && !fine.liveDrag) fine.moved(value)
        Binding on value {
            when: !fine.busy
            value: fine.value
            restoreMode: Binding.RestoreNone
        }
    }
    RepeatButton {
        id: up
        objectName: fine.objectName.length > 0 ? fine.objectName + "Up" : ""
        text: "+"
        Accessible.name: fine.accessibleName ? qsTr("%1, one step up").arg(fine.accessibleName) : qsTr("One step up")
        Layout.preferredWidth: Theme.touchTarget
        Layout.preferredHeight: Theme.touchTarget
        onStepped: fine.nudge(1)
    }
}
