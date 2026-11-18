import QtQuick
import QtQuick.Controls
import AetherMobile

// A Slider that only a drag along it moves (roadmap 101). Qt's own Slider
// puts the value where a finger lifts even when it did not drag (a tap on
// the track), and where a mouse presses: a finger that only meant to stop or
// scroll the page set the waterfall speed and the FFT rate to their slow ends
// (Rich Yost N2RY). Here a MouseArea over the slider takes the press: a tap
// does nothing; a drag along the slider past the drag threshold moves the
// value from where it was by the finger's travel, as an iOS slider's thumb;
// a drag across it is left to the page, which scrolls. pressed is true only
// while it drags and moved() comes on each step, as the Slider's own, so a
// value sent on release or at each step is sent as before. The mouse wheel
// is not taken (it scrolls the page), nor the keys or a screen reader's steps.
// A vertical slider (the EQ's bands) runs along the page's own scrolling:
// it drags only from its handle, so a scroll that starts on its track
// scrolls the page instead of moving the band, as an iOS slider moves only
// by its thumb.
Slider {
    id: control
    // Only a drag that starts on the handle (a finger's width round it)
    // moves the value; elsewhere the press is the page's.
    property bool handleOnly: vertical

    MouseArea {
        id: dragArea
        anchors.fill: parent
        // Over the style's handle and track, which come first.
        z: 100
        enabled: control.enabled
        // The drag's start: where the finger was and the slider's position
        // when the drag began (past the threshold, so the value does not jump
        // by the threshold's distance).
        property real startCoord: 0
        property real startPosition: 0
        property bool dragging: false
        drag.target: dragProxy
        drag.axis: control.horizontal ? Drag.XAxis : Drag.YAxis
        drag.threshold: Qt.styleHints.startDragDistance

        function along(mouse) {
            return control.horizontal ? mouse.x : mouse.y
        }
        function finish() {
            dragging = false
            dragProxy.x = 0
            dragProxy.y = 0
            drag.target = dragProxy
            control.pressed = false
        }

        function onHandle(mouse) {
            const h = control.handle
            if (!h) {
                return true
            }
            const reach = Math.max(control.horizontal ? h.width : h.height, Theme.touchTarget) / 2
            const middle = control.horizontal ? h.x + h.width / 2 : h.y + h.height / 2
            return Math.abs(along(mouse) - middle) <= reach
        }

        onPressed: (mouse) => {
            dragProxy.x = 0
            dragProxy.y = 0
            dragging = false
            // No drag target: the MouseArea keeps the tap (nothing happens)
            // and lets the page take a drag.
            drag.target = !control.handleOnly || onHandle(mouse) ? dragProxy : null
        }
        onPositionChanged: (mouse) => {
            if (!drag.active) {
                return
            }
            if (!dragging) {
                dragging = true
                startCoord = along(mouse)
                startPosition = control.position
                control.pressed = true
                return
            }
            const handleSize = control.handle ? (control.horizontal ? control.handle.width : control.handle.height) : 0
            const extent = (control.horizontal ? control.availableWidth : control.availableHeight) - handleSize
            if (extent <= 0) {
                return
            }
            let travel = along(mouse) - startCoord
            // Up is more on a vertical slider; a mirrored one runs right to left.
            if (control.vertical || control.mirrored) {
                travel = -travel
            }
            const position = Math.max(0, Math.min(1, startPosition + travel / extent))
            const value = control.valueAt(position)
            if (value !== control.value) {
                control.value = value
                control.moved()
            }
        }
        onReleased: finish()
        onCanceled: finish()
    }
    // What the MouseArea's drag moves: only so that the drag starts along the
    // slider's own axis past the threshold, and keeps the press from the page
    // once it has.
    Item {
        id: dragProxy
        visible: false
    }
}
