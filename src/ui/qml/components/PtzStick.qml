import QtQuick
import ReolinkApp
import ReolinkApp.Core

// Analog PTZ joystick: drag the knob; the angle picks one of the camera's eight
// directions and the distance sets the speed (a little = slow, all the way =
// maxSpeed). Releasing springs the knob back and stops the camera. The drag is
// relative to where it starts, so grabbing off-centre never jumps.
Item {
    id: stick
    property int deviceRow: -1
    property int maxSpeed: 64
    signal moved()

    implicitWidth: 156
    implicitHeight: 156

    // Knob centre's reach; leaves the rim marks visible at full push.
    readonly property real travel: width / 2 - knob.width / 2 - 14
    property real kx: 0 // knob offset from the centre, px
    property real ky: 0
    readonly property real deflection: Math.min(1, Math.sqrt(kx * kx + ky * ky) / travel)
    readonly property real deadZone: 0.15
    property string op: ""   // direction being sent; "" when still
    property int speed: 0    // speed last sent

    readonly property var ops: ["Right", "RightDown", "Down", "LeftDown", "Left", "LeftUp", "Up", "RightUp"]

    // Gentle at first, so a small push is a fine adjustment.
    function speedFor(m) {
        if (m <= deadZone)
            return 0;
        var t = (m - deadZone) / (1 - deadZone);
        return Math.max(1, Math.round(1 + (maxSpeed - 1) * t * t));
    }
    function opFor(x, y) {
        var a = Math.atan2(y, x) * 180 / Math.PI; // 0 = right, 90 = down
        // Stay on the current direction until 30° off its centre (sectors are
        // 45° wide), so a wobble on a boundary doesn't flip between two.
        if (op !== "") {
            var c = ops.indexOf(op) * 45;
            if (Math.abs(((a - c) % 360 + 540) % 360 - 180) <= 30)
                return op;
        }
        return ops[((Math.round(a / 45) % 8) + 8) % 8];
    }
    function update() {
        if (deviceRow < 0)
            return;
        var s = speedFor(deflection);
        if (s === 0) {
            if (op !== "")
                Devices.ptzStop(deviceRow);
            op = "";
            speed = 0;
            return;
        }
        var o = opFor(kx, ky);
        if (o !== op || Math.abs(s - speed) >= 2) {
            Devices.ptzMove(deviceRow, o, s);
            op = o;
            speed = s;
            moved();
        }
    }
    function release() {
        if (op !== "" && deviceRow >= 0)
            Devices.ptzStop(deviceRow);
        op = "";
        speed = 0;
        springBack.start();
    }

    // Base.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceAlt
        border.color: stick.op !== "" ? Theme.accentDim : Theme.border
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
    // Guide ring: how far the knob can go.
    Rectangle {
        anchors.centerIn: parent
        width: 2 * stick.travel + knob.width
        height: width
        radius: width / 2
        color: "transparent"
        border.color: Theme.border
        opacity: 0.7
    }
    // Direction marks around the rim; the one being sent lights up.
    Repeater {
        model: stick.ops
        delegate: Rectangle {
            required property string modelData
            required property int index
            readonly property real angle: index * 45 * Math.PI / 180
            readonly property bool live: stick.op === modelData
            width: live ? 8 : 5
            height: width
            radius: width / 2
            x: stick.width / 2 + (stick.width / 2 - 9) * Math.cos(angle) - width / 2
            y: stick.height / 2 + (stick.height / 2 - 9) * Math.sin(angle) - height / 2
            color: live ? Theme.accent : Theme.textMuted
            opacity: live ? 1 : 0.45
        }
    }
    // Stem from the centre to the knob, stronger the further it's pushed.
    Rectangle {
        readonly property real len: Math.sqrt(stick.kx * stick.kx + stick.ky * stick.ky)
        visible: len > 2
        x: stick.width / 2
        y: stick.height / 2 - height / 2
        width: len
        height: 6
        radius: 3
        transformOrigin: Item.Left
        rotation: Math.atan2(stick.ky, stick.kx) * 180 / Math.PI
        color: Theme.accent
        opacity: 0.15 + 0.45 * stick.deflection
        antialiasing: true
    }
    // Knob.
    Rectangle {
        id: knob
        width: 50
        height: 50
        radius: width / 2
        x: stick.width / 2 - width / 2 + stick.kx
        y: stick.height / 2 - height / 2 + stick.ky
        color: dragArea.pressed ? Theme.border : Theme.surface
        border.color: stick.op !== "" ? Theme.accent : (dragArea.containsMouse ? Theme.accentDim : Theme.border)
        border.width: stick.op !== "" ? 2 : 1
        Text { // speed while moving; a grip mark otherwise
            anchors.centerIn: parent
            text: stick.op !== "" ? stick.speed : "⠿"
            color: stick.op !== "" ? Theme.text : Theme.textMuted
            font.pixelSize: stick.op !== "" ? 15 : 18
            font.bold: stick.op !== ""
            font.family: stick.op !== "" ? "monospace" : ""
        }
    }

    ParallelAnimation {
        id: springBack
        NumberAnimation { target: stick; property: "kx"; to: 0; duration: 140; easing.type: Easing.OutQuad }
        NumberAnimation { target: stick; property: "ky"; to: 0; duration: 140; easing.type: Easing.OutQuad }
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real startX: 0
        property real startY: 0
        // Only the round base responds, not the square's corners.
        function inside(m) {
            var dx = m.x - width / 2, dy = m.y - height / 2;
            return dx * dx + dy * dy <= (width / 2) * (width / 2);
        }
        onPressed: (mouse) => {
            if (!inside(mouse)) {
                mouse.accepted = false;
                return;
            }
            springBack.stop();
            startX = mouse.x - stick.kx;
            startY = mouse.y - stick.ky;
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return;
            var dx = mouse.x - startX, dy = mouse.y - startY;
            var d = Math.sqrt(dx * dx + dy * dy);
            if (d > stick.travel) {
                dx *= stick.travel / d;
                dy *= stick.travel / d;
            }
            stick.kx = dx;
            stick.ky = dy;
            stick.update();
        }
        onReleased: stick.release()
        onCanceled: stick.release()
    }
}
