import QtQuick
import QtQuick.Controls
import ReolinkApp
import ReolinkApp.Core

// Round PTZ pad: eight directions around a centre button. Press and hold to
// move at `speed`; release stops. The centre returns the camera to its home
// position when it has one (showHome).
Item {
    id: pad
    property int deviceRow: -1
    property int speed: 32 // PtzCtrl speed, 1..64
    property bool showHome: false
    signal moved()
    signal homeRequested()

    implicitWidth: 156
    implicitHeight: 156

    function move(op) {
        if (deviceRow < 0)
            return;
        Devices.ptzMove(deviceRow, op, speed);
        moved();
    }
    function stop() { if (deviceRow >= 0) Devices.ptzStop(deviceRow); }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceAlt
        border.color: Theme.border
    }

    Repeater {
        model: [{ op: "Up", a: -90 }, { op: "RightUp", a: -45 }, { op: "Right", a: 0 },
                { op: "RightDown", a: 45 }, { op: "Down", a: 90 }, { op: "LeftDown", a: 135 },
                { op: "Left", a: 180 }, { op: "LeftUp", a: -135 }]
        delegate: Item {
            required property var modelData
            readonly property real reach: pad.width / 2 - 25
            width: 38; height: 38
            x: pad.width / 2 + reach * Math.cos(modelData.a * Math.PI / 180) - width / 2
            y: pad.height / 2 + reach * Math.sin(modelData.a * Math.PI / 180) - height / 2
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: dirArea.pressed ? Theme.accentDim : (dirArea.containsMouse ? Theme.border : "transparent")
            }
            // Chevron drawn from two rounded bars meeting at the tip, so all
            // eight directions look the same (a rotated glyph doesn't). The
            // point is 70°, not 90°: a right-angle chevron turned 45° lies
            // flat along the axes and reads as a corner, not an arrow.
            Item {
                id: chevron
                anchors.centerIn: parent
                width: 14; height: 14
                rotation: parent.modelData.a
                readonly property real arm: 35 // degrees from the pointing axis
                readonly property real rise: 4.5 * Math.sin(arm * Math.PI / 180) - 0.15
                readonly property color ink: dirArea.pressed || dirArea.containsMouse ? Theme.text : Theme.textMuted
                Rectangle {
                    width: 9; height: 2.5; radius: 1.25
                    x: (parent.width - width) / 2; y: parent.height / 2 - chevron.rise - height / 2
                    rotation: chevron.arm
                    color: chevron.ink
                    antialiasing: true
                }
                Rectangle {
                    width: 9; height: 2.5; radius: 1.25
                    x: (parent.width - width) / 2; y: parent.height / 2 + chevron.rise - height / 2
                    rotation: -chevron.arm
                    color: chevron.ink
                    antialiasing: true
                }
            }
            MouseArea {
                id: dirArea
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onPressed: pad.move(parent.modelData.op)
                onReleased: pad.stop()
                onCanceled: pad.stop()
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 54; height: 54
        radius: width / 2
        color: homeArea.pressed ? Theme.accentDim
             : (homeArea.containsMouse && pad.showHome ? Theme.border : Theme.surface)
        border.color: homeArea.containsMouse && pad.showHome ? Theme.accent : Theme.border
        Text {
            anchors.centerIn: parent
            text: "⌂"
            font.pixelSize: 22
            color: pad.showHome ? Theme.text : Theme.textMuted
            opacity: pad.showHome ? 1 : 0.35
        }
        MouseArea {
            id: homeArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: pad.showHome
            cursorShape: Qt.PointingHandCursor
            onClicked: pad.homeRequested()
        }
        ToolTip.visible: homeArea.containsMouse && pad.showHome
        ToolTip.delay: 500
        ToolTip.text: qsTr("Return to home")
    }
}
