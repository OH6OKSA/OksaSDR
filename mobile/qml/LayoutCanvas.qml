import QtQuick
import AetherMobile

// The surface of an own layout (roadmap 113): the page's blocks
// (CanvasBlock children) placed by the active layout's arrangement for this
// orientation, and the snapping guides while one is moved or resized.
Item {
    id: canvas

    // Engine.workspace (MobileWorkspace).
    property QtObject ws
    // "land" or "port".
    property string orientation: "land"
    property bool editing: false
    // Snapping reach and the corner handles, in px.
    readonly property real snapPx: 12
    readonly property real gripPx: 30
    // {v: [x fractions], h: [y fractions]} while a gesture holds a snap.
    property var guides: null
    // The block whose handles show (a tap or a drag selects it).
    property string selected: ""
    onEditingChanged: selected = ""

    readonly property var items: ws ? (ws.revision, ws.items(orientation)) : []
    // Blocks may touch and share a few pixels (Classic's display sits 4 px
    // into the slice row); more than that is an overlap.
    readonly property real overlapPx: 6
    function overlaps(a, b) {
        const ox = (Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x)) * width
        const oy = (Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y)) * height
        return ox > overlapPx && oy > overlapPx
    }
    // Whether `rect` for `block` lies over another shown block.
    function overlapsAny(block, rect) {
        for (const it of items) {
            if (it.block !== block && !it.closed && overlaps(rect, it)) return true
        }
        return false
    }
    // The shown blocks that overlap another (a block brought back by + Add
    // where another now is): red until moved.
    readonly property var overlapping: {
        const out = []
        for (const it of items) {
            if (!it.closed && overlapsAny(it.block, it)) out.push(it.block)
        }
        return out
    }
    // Said by the screen reader (VoiceOver, TalkBack); logged too, so a
    // test can read what was said.
    function announce(text) {
        Accessible.announce(text)
        console.info("layout:", text)
    }
    function itemFor(block) {
        for (let i = 0; i < items.length; ++i) {
            if (items[i].block === block) return items[i]
        }
        return null
    }

    // A faint grid while editing, so the free space reads as space.
    Rectangle {
        anchors.fill: parent
        visible: canvas.editing
        color: "transparent"
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
        border.width: 1
        radius: 4
        z: -1
    }

    // The arrange buttons for the selected block, beside it (under it, or
    // over it where the canvas ends): move by 8 points, size by 8 from the
    // right and bottom edges, held to repeat. For a screen reader or Switch
    // Control the only way to arrange; for a finger an exact one.
    property Item selectedBlock: null
    Rectangle {
        id: arrangeBar
        objectName: "arrangeBar"
        visible: canvas.editing && canvas.selectedBlock !== null && canvas.selectedBlock.visible
        z: 20000
        color: Theme.surface
        border.color: Theme.accent
        border.width: 1
        radius: Theme.radius
        width: arrangeRow.implicitWidth + 8
        height: arrangeRow.implicitHeight + 8
        readonly property Item b: canvas.selectedBlock
        x: b ? Math.max(0, Math.min(canvas.width - width, b.x + (b.width - width) / 2)) : 0
        y: b ? (b.y + b.height + 8 + height <= canvas.height ? b.y + b.height + 8
                : Math.max(0, b.y - height - 8)) : 0
        Accessible.role: Accessible.Grouping
        Accessible.name: b ? qsTr("Arrange %1").arg(b.title) : ""
        Row {
            id: arrangeRow
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: [
                    { key: "left", text: "←", zone: "move", dx: -8, dy: 0, name: qsTr("Move %1 left") },
                    { key: "up", text: "↑", zone: "move", dx: 0, dy: -8, name: qsTr("Move %1 up") },
                    { key: "down", text: "↓", zone: "move", dx: 0, dy: 8, name: qsTr("Move %1 down") },
                    { key: "right", text: "→", zone: "move", dx: 8, dy: 0, name: qsTr("Move %1 right") },
                    { key: "narrower", text: "W−", zone: "e", dx: -8, dy: 0, name: qsTr("%1 narrower") },
                    { key: "wider", text: "W+", zone: "e", dx: 8, dy: 0, name: qsTr("%1 wider") },
                    { key: "shorter", text: "H−", zone: "s", dx: 0, dy: -8, name: qsTr("%1 shorter") },
                    { key: "taller", text: "H+", zone: "s", dx: 0, dy: 8, name: qsTr("%1 taller") }
                ]
                delegate: AppButton {
                    required property var modelData
                    objectName: "arrange:" + modelData.key
                    text: modelData.text
                    width: Theme.touchTarget
                    height: Theme.touchTarget
                    padding: 0
                    font.pixelSize: 17
                    focusPolicy: Qt.NoFocus
                    autoRepeat: true
                    autoRepeatDelay: 400
                    autoRepeatInterval: 100
                    Accessible.name: canvas.selectedBlock ? modelData.name.arg(canvas.selectedBlock.title) : ""
                    onClicked: if (canvas.selectedBlock) canvas.selectedBlock.step(modelData.zone, modelData.dx, modelData.dy)
                }
            }
        }
    }

    Repeater {
        model: canvas.guides ? canvas.guides.v : []
        delegate: Rectangle {
            required property real modelData
            x: Math.round(modelData * canvas.width)
            width: 1
            height: canvas.height
            color: Theme.warning
            z: 10000
        }
    }
    Repeater {
        model: canvas.guides ? canvas.guides.h : []
        delegate: Rectangle {
            required property real modelData
            y: Math.round(modelData * canvas.height)
            height: 1
            width: canvas.width
            color: Theme.warning
            z: 10000
        }
    }
}
