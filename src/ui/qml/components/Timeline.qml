import QtQuick
import QtQuick.Controls
import ReolinkApp

// 24-hour recording timeline: two-tone segments (grey = timer/continuous,
// blue = alarm/AI), hour ticks, a draggable playhead, and click-to-seek.
// Segments are {start,end,type,name} with start/end in seconds into the day.
// Interactions: wheel zooms at the cursor; when zoomed, dragging the track pans
// the view (Shift+wheel / horizontal scroll pan too); dragging the playhead
// scrubs it; a plain click seeks.
Rectangle {
    id: root
    // The scrub track keeps a fixed height; per-camera lanes extend the widget
    // downwards, so adding cameras never shrinks the thing you actually drag.
    readonly property real trackHeight: 64
    readonly property real laneHeight: 7
    readonly property real lanesHeight:
        lanes.length > 0 ? lanes.length * (laneHeight + 3) + 5 : 0
    height: trackHeight + lanesHeight
    color: Theme.surface
    border.color: Theme.border
    radius: Theme.radius
    clip: true

    property real duration: 86400        // seconds shown (a full day)
    property real position: 0            // playhead, seconds
    property var segments: []
    // One entry per camera in the grid: { name, segments }. The main track shows
    // the union of these, so you can scrub to anything visible in the grid; the
    // lanes show which camera actually has footage where, because a union alone
    // can't tell you that half the panes will be blank at a given moment.
    property var lanes: []
    property real zoom: 1.0              // 1 = whole day; >1 zooms in
    property real viewStart: 0           // left edge, seconds

    // seek fires continuously while pressing/dragging (move the playhead);
    // commit fires once on release (start playback there).
    signal seek(real seconds)
    signal commit(real seconds)

    readonly property real visibleSpan: duration / zoom
    function xForSec(sec) { return (sec - viewStart) / visibleSpan * width; }
    function secForX(x) { return viewStart + x / width * visibleSpan; }

    // Detection-event markers: accent ticks where the inbox saw motion/AI on
    // this camera, so incidents are findable at a glance.
    property var alarmTicks: []
    Repeater {
        model: root.alarmTicks
        Rectangle {
            required property var modelData
            x: root.xForSec(modelData) - 1
            visible: x >= -1 && x <= root.width
            y: 0
            width: 2
            height: root.trackHeight * 0.35
            color: Theme.danger
            opacity: 0.9
        }
    }

    // Recording segments
    Repeater {
        model: root.segments
        Rectangle {
            required property var modelData
            property real segStart: modelData.start
            property real segEnd: modelData.end
            x: root.xForSec(segStart)
            width: Math.max(2, root.xForSec(segEnd) - root.xForSec(segStart))
            y: 14
            height: root.trackHeight - 28
            visible: segEnd >= root.viewStart && segStart <= root.viewStart + root.visibleSpan
            color: modelData.type === "alarm" ? Theme.accent : "#4a5a68"
            opacity: 0.85
        }
    }

    // Hour ticks + labels
    Repeater {
        model: 25
        Item {
            required property int index
            property real sec: index * 3600
            x: root.xForSec(sec)
            visible: sec >= root.viewStart - 1 && sec <= root.viewStart + root.visibleSpan + 1
            Rectangle {
                y: 0; width: 1; height: index % 6 === 0 ? root.trackHeight : 8
                color: Theme.border
            }
            Text {
                visible: parent.index % 2 === 0
                y: root.trackHeight - 14
                x: 2
                text: String(parent.index).padStart(2, "0") + ":00"
                color: Theme.textMuted
                font.pixelSize: 9
            }
        }
    }

    // Per-camera coverage lanes, in grid order, under the scrub track.
    Column {
        y: root.trackHeight
        width: parent.width
        spacing: 3
        Repeater {
            model: root.lanes
            Item {
                id: laneItem
                required property var modelData
                width: root.width
                height: root.laneHeight
                // Empty track behind, so a camera with no footage at all still
                // reads as "present but blank" rather than silently missing.
                Rectangle {
                    anchors.fill: parent
                    color: Theme.border
                    opacity: 0.25
                }
                Repeater {
                    model: parent.modelData.segments
                    Rectangle {
                        required property var modelData
                        x: root.xForSec(modelData.start)
                        width: Math.max(1, root.xForSec(modelData.end) - root.xForSec(modelData.start))
                        height: parent.height
                        visible: modelData.end >= root.viewStart
                                 && modelData.start <= root.viewStart + root.visibleSpan
                        color: modelData.type === "alarm" ? Theme.accent : "#4a5a68"
                        opacity: 0.9
                    }
                }
                ToolTip {
                    visible: laneHover.hovered
                    delay: 400
                    x: 4
                    y: -22
                    contentItem: Text {
                        // By id, not parent-chain: a popup's contentItem is
                        // reparented, so parent.parent lands on null mid-teardown.
                        text: laneItem.modelData ? laneItem.modelData.name : ""
                        color: Theme.text
                        font.pixelSize: 10
                    }
                    background: Rectangle {
                        color: Theme.surfaceAlt; border.color: Theme.border; radius: 3
                    }
                }
                HoverHandler { id: laneHover }
            }
        }
    }

    // Playhead
    Rectangle {
        x: root.xForSec(root.position)
        y: 0
        width: 2
        height: root.height
        color: "#ff5a5a"
        z: 6
        visible: root.position >= root.viewStart && root.position <= root.viewStart + root.visibleSpan
    }

    // Hover time readout
    Rectangle {
        id: hoverTip
        visible: hover.hovered
        x: Math.min(Math.max(0, hover.point.position.x - width / 2), root.width - width)
        y: 2
        z: 5
        radius: 3
        color: "#cc000000"
        width: tipText.implicitWidth + 8
        height: tipText.implicitHeight + 4
        Text {
            id: tipText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 10
            text: {
                var s = root.secForX(hover.point.position.x);
                var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
                return String(h).padStart(2, "0") + ":" + String(m).padStart(2, "0");
            }
        }
    }
    HoverHandler { id: hover }

    MouseArea {
        id: track
        anchors.fill: parent
        preventStealing: true
        property real lastSec: 0
        property real pressX: 0
        property real pressViewStart: 0
        // 0 = undecided (click seeks on release), 1 = scrub playhead, 2 = pan view
        property int mode: 0
        cursorShape: pressed && mode === 2 ? Qt.ClosedHandCursor
                    : (root.zoom > 1 ? Qt.OpenHandCursor : Qt.ArrowCursor)
        function secAt(x) { return Math.max(0, Math.min(root.duration, root.secForX(x))); }
        function clampView(v) {
            return Math.max(0, Math.min(root.duration - root.visibleSpan, v));
        }
        onPressed: (m) => {
            pressX = m.x;
            pressViewStart = root.viewStart;
            // Grabbing the playhead scrubs it; dragging anywhere else pans the
            // zoomed view; a motionless press-release is a click-to-seek.
            mode = Math.abs(m.x - root.xForSec(root.position)) <= 8 ? 1 : 0;
            if (mode === 1) { lastSec = secAt(m.x); root.seek(lastSec); }
        }
        onPositionChanged: (m) => {
            if (!(m.buttons & Qt.LeftButton))
                return;
            if (mode === 0 && Math.abs(m.x - pressX) > 4)
                mode = 2;
            if (mode === 1) {
                lastSec = secAt(m.x);
                root.seek(lastSec);
            } else if (mode === 2) {
                root.viewStart = clampView(pressViewStart
                                           - (m.x - pressX) / root.width * root.visibleSpan);
            }
        }
        onReleased: (m) => {
            if (mode === 1) {
                root.commit(lastSec);
            } else if (mode === 0) { // plain click: seek there
                lastSec = secAt(m.x);
                root.seek(lastSec);
                root.commit(lastSec);
            }
        }
        onWheel: (w) => {
            // Shift+wheel or a horizontal scroll (trackpads) pans the view.
            var panTicks = Math.abs(w.angleDelta.x) > Math.abs(w.angleDelta.y)
                         ? w.angleDelta.x
                         : ((w.modifiers & Qt.ShiftModifier) ? w.angleDelta.y : 0);
            if (panTicks !== 0) {
                root.viewStart = clampView(root.viewStart
                                           - panTicks / 120 * root.visibleSpan * 0.15);
                return;
            }
            var focus = root.secForX(w.x);
            root.zoom = Math.max(1, Math.min(48, root.zoom * (w.angleDelta.y > 0 ? 1.25 : 0.8)));
            // Keep the focus point under the cursor.
            root.viewStart = clampView(focus - w.x / root.width * root.visibleSpan);
        }
    }

    // Empty-state hint
    Text {
        anchors.centerIn: parent
        visible: root.segments.length === 0
        text: qsTr("No recordings for this day")
        color: Theme.textMuted
        font.pixelSize: 12
    }
}
