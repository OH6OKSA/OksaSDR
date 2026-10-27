import QtQuick
import QtQuick.Controls

// A password or key field with an eye inside it that shows what was typed:
// a mistyped password is hard to see on a phone (asked by Larry Averbeck,
// SmartLink sign-in). Dots again when the field loses focus.
TextField {
    id: field
    property bool revealed: false
    echoMode: revealed ? TextInput.Normal : TextInput.Password
    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
    rightPadding: eye.width
    onActiveFocusChanged: {
        if (!activeFocus) {
            revealed = false
        }
    }

    Item {
        id: eye
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(40, parent.height)
        visible: field.enabled
        Accessible.role: Accessible.Button
        Accessible.name: field.revealed ? qsTr("Hide password") : qsTr("Show password")

        Canvas {
            id: icon
            anchors.centerIn: parent
            width: Math.round(field.font.pixelSize * 1.3)
            height: Math.round(width * 0.7)
            property color color: field.revealed ? field.color : field.placeholderTextColor
            property bool slashed: !field.revealed
            onColorChanged: requestPaint()
            onSlashedChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width, h = height, lw = Math.max(1.5, h / 10)
                ctx.strokeStyle = icon.color
                ctx.fillStyle = icon.color
                ctx.lineWidth = lw
                ctx.lineCap = "round"
                // The eye: two arcs meeting at the corners, and the pupil.
                ctx.beginPath()
                ctx.moveTo(lw, h / 2)
                ctx.quadraticCurveTo(w / 2, -h * 0.3, w - lw, h / 2)
                ctx.quadraticCurveTo(w / 2, h * 1.3, lw, h / 2)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(w / 2, h / 2, h * 0.2, 0, Math.PI * 2)
                ctx.fill()
                if (slashed) {
                    ctx.beginPath()
                    ctx.moveTo(w * 0.15, h - lw)
                    ctx.lineTo(w * 0.85, lw)
                    ctx.stroke()
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: field.revealed = !field.revealed
        }
    }
}
