import QtQuick
import QtQuick.Layouts
import AetherMobile

// The band buttons or the step buttons for an own layout's block (roadmap
// 113): as many columns as give the largest buttons in the block's shape,
// a row, a few rows or a column (Larry Averbeck W9FLA: "smaller Step
// buttons would be easier to use in one corner").
Item {
    id: gb

    // "bands" or "steps".
    required property string kind
    readonly property var buttons: kind === "bands" ? Engine.bands
                                                    : Theme.stepButtons(Engine.stepHz, Engine.radioStepButtons)
    // The steps keep their pairs: 8, 4, 2 or 1 columns; the bands any.
    readonly property var columnChoices: kind === "steps" ? [8, 4, 2, 1] : null
    readonly property real minButtonW: kind === "steps" ? 44 : 48
    readonly property real minButtonH: 34
    readonly property real gap: Theme.spacing / 2

    // The best arrangement that keeps the buttons at least their least
    // size; null when none does (the editor stops a resize there).
    function gridFor(w, h) { return arrange(w, h, true) }
    // On a smaller screen than the layout was made on the block can come
    // out under that: then the arrangement with the largest buttons anyway.
    function arrange(w, h, strict) {
        const n = buttons.length
        if (n <= 0) return null
        let best = null
        const choices = columnChoices || Array.from({ length: n }, (_, i) => i + 1)
        for (const c of choices) {
            const cols = Math.min(c, n)
            const rows = Math.ceil(n / cols)
            const bw = (w - (cols - 1) * gap) / cols
            const bh = (h - (rows - 1) * gap) / rows
            if (strict && (bw < minButtonW || bh < minButtonH)) continue
            // The larger buttons; a touch target's height counts as much as
            // its width up to their usual size.
            const score = Math.min(bw / 60, bh / Theme.touchTarget, 1.5)
            if (!best || score > best.score + 1e-6) best = { cols: cols, rows: rows, score: score }
        }
        return best
    }
    function fits(w, h) { return gridFor(w, h) !== null }

    readonly property var grid: arrange(width, height, true) || arrange(width, height, false)

    GridLayout {
        anchors.fill: parent
        columns: gb.grid ? gb.grid.cols : 1
        rowSpacing: gb.gap
        columnSpacing: gb.gap
        Repeater {
            model: gb.buttons
            delegate: AppButton {
                required property var modelData
                objectName: gb.kind === "bands" ? "blockBand" : "blockStep"
                text: gb.kind === "bands" ? modelData
                      : (modelData > 0 ? "+" : "−") + Theme.stepLabel(Math.abs(modelData))
                leftPadding: 2
                rightPadding: 2
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 0
                Layout.preferredWidth: 60
                Layout.preferredHeight: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                checked: gb.kind === "bands" && Engine.activeBand === modelData
                // Held: steps on, as on the page (from 0.4 s, 8 a second).
                autoRepeat: gb.kind === "steps"
                autoRepeatDelay: 400
                autoRepeatInterval: 125
                onClicked: {
                    if (gb.kind === "bands") {
                        Engine.selectBand(modelData)
                    } else {
                        Engine.haptic("step")
                        Engine.tuneByHz(modelData)
                    }
                }
            }
        }
    }
}
