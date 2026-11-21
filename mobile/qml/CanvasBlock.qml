import QtQuick
import QtQuick.Controls
import AetherMobile

// One block of an own layout (roadmap 113): placed where the layout says,
// its content filling it. While the layout is edited the content takes no
// input (nothing transmits or tunes) and a frame lies over it: the block
// moves by a drag anywhere on it, resizes by its corners and edges, and ×
// hides it. The rules (the minimum, snapping to the edges and the other
// blocks) are the desktop's Workspace Canvas's, in MobileWorkspace; a
// resize the content would not fit into stops where it still fits.
Item {
    id: cb

    required property string block
    required property LayoutCanvas canvas
    property string title
    // The smallest size at all, in px; fits(w, h) may ask more of a shape
    // (the steps: a row or a column, not a square).
    property real minW: 80
    property real minH: 36
    property var fits: null
    // The content, and where portaled items (the display, PTT) go.
    default property alias content: area.data
    readonly property alias contentArea: area

    readonly property var model: canvas.itemFor(cb.block)
    readonly property bool shown: model !== null && !model.closed
    // The rect during a gesture, not yet stored.
    property var live: null
    readonly property var rect: live !== null ? live : model

    visible: shown
    x: rect ? Math.round(rect.x * canvas.width) : 0
    y: rect ? Math.round(rect.y * canvas.height) : 0
    width: rect ? Math.round((rect.x + rect.w) * canvas.width) - x : 0
    height: rect ? Math.round((rect.y + rect.h) * canvas.height) - y : 0
    // The selected block above all while editing: its handles are the only
    // ones, and on top (two blocks sharing an edge had their handles in
    // one place, and the upper block took the other's resize).
    readonly property bool selected: canvas.editing && canvas.selected === block
    onSelectedChanged: {
        if (selected) {
            canvas.selectedBlock = cb
        } else if (canvas.selectedBlock === cb) {
            canvas.selectedBlock = null
        }
    }
    z: selected ? 10000 : (model ? model.z : 0)

    Item {
        id: area
        anchors.fill: parent
        enabled: !cb.canvas.editing
        // While editing only a picture of it shows (below): hidden, its
        // controls are out of a screen reader's way too (about 50 locked
        // ones), which hears the blocks' names and the arrange buttons.
        visible: !cb.canvas.editing
    }
    ShaderEffectSource {
        anchors.fill: parent
        visible: cb.canvas.editing
        sourceItem: cb.canvas.editing ? area : null
        live: true
    }

    // ── Editing ─────────────────────────────────────────────────────────
    // A block is never dropped on another (on a touch screen an overlap is a
    // slip of the finger more often than a wish): while dragged it may pass
    // over the others, and let go over one it goes back to the last free
    // place on its way (lastFree), or stays where it started.
    property var startRect: null
    property var lastFree: null
    // The last translation whose rect was taken (a resize stops there).
    property var lastTaken: null
    readonly property bool overFree: live === null || !canvas.overlapsAny(block, live)

    // Where the block is, in words for a screen reader: its left and top
    // edges and its size in points.
    function where() {
        if (!rect) return ""
        return qsTr("left %1, top %2, %3 by %4").arg(Math.round(rect.x * canvas.width)).arg(Math.round(rect.y * canvas.height))
            .arg(Math.round(rect.w * canvas.width)).arg(Math.round(rect.h * canvas.height))
    }
    function select() {
        canvas.selected = block
        canvas.announce(qsTr("%1 selected, %2. The arrange buttons move and size it.").arg(title).arg(where()))
    }
    // One arrange-button step (a screen reader, Switch Control, or a finger
    // that wants exactness): `zone` as a drag's ("move", "e" the width,
    // "s" the height), by dx, dy points; no snapping, the same limits as a
    // drag (the minimum, what the content fits, no other block under it).
    // Said aloud either way.
    function step(zone, dx, dy) {
        if (!model) return
        const start = { x: model.x, y: model.y, w: model.w, h: model.h }
        const r = canvas.ws.drag(canvas.orientation, cb.block, zone, start, dx / canvas.width, dy / canvas.height,
                                 minW / canvas.width, minH / canvas.height, 0, 0)
        const same = Math.abs(r.x - start.x) < 1e-6 && Math.abs(r.y - start.y) < 1e-6
                     && Math.abs(r.w - start.w) < 1e-6 && Math.abs(r.h - start.h) < 1e-6
        let why = ""
        if (same) {
            why = zone === "move" ? qsTr("at the edge") : qsTr("at its smallest or the edge")
        } else if (zone !== "move" && !fitsRect(r)) {
            why = qsTr("its content would not fit")
        } else if (canvas.overlapsAny(block, r)) {
            why = qsTr("another block is there")
        }
        if (why.length > 0) {
            canvas.announce(qsTr("%1 not moved: %2.").arg(title).arg(why))
            return
        }
        canvas.ws.checkpoint()
        canvas.ws.setRect(canvas.orientation, cb.block, { x: r.x, y: r.y, w: r.w, h: r.h })
        canvas.announce(qsTr("%1: %2").arg(title).arg(where()))
    }

    function begin() {
        canvas.selected = block
        startRect = { x: model.x, y: model.y, w: model.w, h: model.h }
        lastTaken = { x: 0, y: 0 }
        lastFree = canvas.overlapsAny(block, startRect) ? null : startRect
        canvas.ws.checkpoint()
        canvas.ws.raise(canvas.orientation, cb.block)
    }
    function candidate(zone, tx, ty) {
        return canvas.ws.drag(canvas.orientation, cb.block, zone, startRect,
                              tx / canvas.width, ty / canvas.height,
                              minW / canvas.width, minH / canvas.height,
                              canvas.snapPx / canvas.width, canvas.snapPx / canvas.height)
    }
    function fitsRect(r) { return !fits || fits(r.w * canvas.width, r.h * canvas.height) }
    function update(zone, tx, ty) {
        if (!startRect) return
        let r = candidate(zone, tx, ty)
        // A resize stops where the content still fits; a move keeps the size.
        // A fast finger can pass the limit between two events: the limit is
        // then found between the last size taken and this one.
        if (zone !== "move" && !fitsRect(r)) {
            let lo = 0, hi = 1, best = null
            for (let i = 0; i < 10; ++i) {
                const t = (lo + hi) / 2
                const c = candidate(zone, lastTaken.x + (tx - lastTaken.x) * t, lastTaken.y + (ty - lastTaken.y) * t)
                if (fitsRect(c)) { best = c; lo = t } else { hi = t }
            }
            if (best === null) return
            r = best
        } else {
            lastTaken = { x: tx, y: ty }
        }
        live = { x: r.x, y: r.y, w: r.w, h: r.h }
        if (!canvas.overlapsAny(block, live)) lastFree = live
        canvas.guides = { v: r.vguides, h: r.hguides }
    }
    function end() {
        if (live !== null) {
            const drop = canvas.overlapsAny(block, live) && lastFree !== null ? lastFree : live
            canvas.ws.setRect(canvas.orientation, cb.block, drop)
        }
        live = null
        startRect = null
        lastFree = null
        lastTaken = null
        canvas.guides = null
    }

    Rectangle {
        id: frame
        objectName: "edit:" + cb.block
        visible: cb.canvas.editing
        anchors.fill: parent
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, cb.live !== null ? 0.22 : 0.12)
        // Orange while moving; red where it would not stay (over another
        // block), and while it overlaps one.
        border.color: !cb.overFree || cb.canvas.overlapping.indexOf(cb.block) >= 0 ? Theme.transmit
                      : cb.live !== null ? Theme.warning : Theme.accent
        border.width: cb.selected ? 3 : 2
        radius: 4
        // Not an element of its own for a screen reader: its title is the
        // block's button (one name, one action).
        Accessible.ignored: true

        // A tap selects the block: its resize handles show.
        TapHandler {
            onTapped: cb.select()
        }

        // Anywhere on the block: move it.
        DragHandler {
            target: null
            dragThreshold: 6
            onActiveChanged: active ? cb.begin() : cb.end()
            onTranslationChanged: if (active) cb.update("move", translation.x, translation.y)
        }

        // The name and the hide button along the top.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Math.min(30, parent.height)
            // Light: a light band under the dark title (roadmap 126).
            color: Theme.light ? Qt.rgba(1, 1, 1, 0.85) : Qt.rgba(0, 0, 0, 0.55)
            radius: 4
            Label {
                objectName: "edit:" + cb.block + ":select"
                anchors.left: parent.left
                // Clear of the top-left corner's handle.
                anchors.leftMargin: cb.canvas.gripPx / 2 + 6
                anchors.right: hide.left
                anchors.verticalCenter: parent.verticalCenter
                text: cb.title
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                // A screen reader selects the block here (a tap anywhere on
                // it does for a finger) and hears where it is; the arrange
                // buttons then move and size it.
                Accessible.role: Accessible.Button
                Accessible.name: cb.selected ? qsTr("%1 block, selected").arg(cb.title) : qsTr("%1 block").arg(cb.title)
                Accessible.description: cb.where()
                Accessible.onPressAction: cb.select()
            }
            AppButton {
                id: hide
                objectName: "edit:" + cb.block + ":hide"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: Math.min(30, cb.height)
                padding: 0
                text: "×"
                font.pixelSize: 18
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Hide %1").arg(cb.title)
                onClicked: {
                    cb.canvas.ws.checkpoint()
                    cb.canvas.ws.setClosed(cb.canvas.orientation, cb.block, true)
                }
            }
        }

        // Handles at the corners and the middle of each edge, straddling
        // the edge (half outside), so all of the block's inside moves it,
        // a small block too (a 38 px Step size was mostly edge bands).
        Repeater {
            model: [
                { zone: "nw", ax: 0, ay: 0 }, { zone: "n", ax: 0.5, ay: 0 }, { zone: "ne", ax: 1, ay: 0 },
                { zone: "w", ax: 0, ay: 0.5 }, { zone: "e", ax: 1, ay: 0.5 },
                { zone: "sw", ax: 0, ay: 1 }, { zone: "s", ax: 0.5, ay: 1 }, { zone: "se", ax: 1, ay: 1 }
            ]
            delegate: Item {
                required property var modelData
                objectName: "edit:" + cb.block + ":" + modelData.zone
                visible: cb.selected
                readonly property bool corner: modelData.zone.length === 2
                readonly property bool horizontal: modelData.zone === "n" || modelData.zone === "s"
                // The touch area; the visible mark is smaller.
                width: corner ? cb.canvas.gripPx : (horizontal ? 52 : 26)
                height: corner ? cb.canvas.gripPx : (horizontal ? 26 : 52)
                x: modelData.ax * frame.width - width / 2
                y: modelData.ay * frame.height - height / 2
                z: 2
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.corner ? 18 : (parent.horizontal ? 36 : 8)
                    height: parent.corner ? 18 : (parent.horizontal ? 8 : 36)
                    radius: parent.corner ? 4 : 3
                    color: Theme.accent
                    border.color: Theme.text
                    border.width: 1
                }
                DragHandler {
                    target: null
                    dragThreshold: 2
                    onActiveChanged: active ? cb.begin() : cb.end()
                    onTranslationChanged: if (active) cb.update(modelData.zone, translation.x, translation.y)
                }
            }
        }
    }
}
