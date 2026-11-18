import QtQuick
import QtQuick.Controls
import ReolinkApp

// 24-hour recording timeline: two-tone segments (grey = timer/continuous,
// blue = alarm/AI), time ticks that refine as you zoom, a draggable playhead,
// and click-to-seek.
// Segments are {start,end,type,name} with start/end in seconds into the day.
// Interactions: wheel zooms at the cursor; when zoomed, dragging the track pans
// the view (Shift+wheel / horizontal scroll pan too); dragging the playhead
// scrubs it; a plain click seeks. A click in a gap near a recording snaps to
// that recording's start, since at day zoom a short clip is barely a pixel wide.
Rectangle {
    id: root
    // The scrub track keeps a fixed height; per-camera lanes extend the widget
    // downwards, so adding cameras never shrinks the thing you actually drag.
    readonly property real trackHeight: 64
    readonly property real laneHeight: 7
    readonly property real lanesHeight:
        lanes.length > 0 ? lanes.length * (laneHeight + 3) + 5 : 0
    // Implicit, not height: the page's ColumnLayout sizes by implicit height,
    // and with a plain height the lanes were laid out but clipped away.
    implicitHeight: trackHeight + lanesHeight
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
    readonly property real maxZoom: 288  // a 5-minute window, for following one movement

    // Zoom to `z` with `sec` in the middle, as far as the day's edges allow.
    function centerOn(sec, z) {
        zoom = Math.max(1, Math.min(maxZoom, z));
        viewStart = Math.max(0, Math.min(duration - visibleSpan, sec - visibleSpan / 2));
    }

    // seek fires continuously while pressing/dragging (move the playhead);
    // commit fires once on release (start playback there).
    signal seek(real seconds)
    signal commit(real seconds)

    readonly property real visibleSpan: duration / zoom
    function xForSec(sec) { return (sec - viewStart) / visibleSpan * width; }
    function secForX(x) { return viewStart + x / width * visibleSpan; }

    // How far (in pixels) a click may land from a recording and still snap to it.
    property real snapPx: 18

    // The recording a click at `sec` should play: the one containing it, else
    // the nearest one within `toleranceSec` (an upcoming one wins a tie).
    // Returns the segment or null.
    function segmentNear(sec, toleranceSec) {
        var best = null, bestDist = toleranceSec;
        for (var i = 0; i < segments.length; ++i) {
            var s = segments[i];
            if (sec >= s.start && sec <= s.end)
                return s;
            var d = sec < s.start ? s.start - sec : sec - s.end;
            if (d < bestDist || (d === bestDist && best && sec < s.start)) {
                best = s;
                bestDist = d;
            }
        }
        return best;
    }
    // Where a click at `sec` starts playback: inside a recording, right there;
    // in a gap near one, at that recording's start; elsewhere, unchanged.
    function snapSec(sec) {
        var s = segmentNear(sec, snapPx / width * visibleSpan);
        return s && !(sec >= s.start && sec <= s.end) ? s.start : sec;
    }
    function clock(sec) {
        var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = Math.floor(sec % 60);
        return String(h).padStart(2, "0") + ":" + String(m).padStart(2, "0")
             + (zoom > 4 ? ":" + String(s).padStart(2, "0") : "");
    }
    // The recording under the pointer's snap reach, for the hover highlight.
    readonly property var hoverSegment: hover.hovered
        ? segmentNear(secForX(hover.point.position.x), snapPx / width * visibleSpan) : null

    // Detection markers: one bar per detection, spanning it, coloured by kind,
    // in a strip above the recordings. events: [{ start, end, kinds: [...] }]
    // (seconds into the day); shownKinds switches kinds off without refetching.
    property var events: []
    property var shownKinds: ({ person: true, vehicle: true, animal: true, motion: true, other: true })
    // Same colours as the Events page.
    readonly property var kindColors: ({ person: Theme.accent, vehicle: "#e08a3c", animal: Theme.online,
                                         motion: Theme.textMuted, other: "#b07fd6" })
    // A detection that is both motion and a person reads as a person.
    readonly property var kindOrder: ["person", "vehicle", "animal", "other", "motion"]
    function shownKind(kinds) {
        for (var i = 0; i < kindOrder.length; i++)
            if (shownKinds[kindOrder[i]] && kinds.indexOf(kindOrder[i]) >= 0)
                return kindOrder[i];
        return "";
    }
    component Marker: Rectangle {
        required property var modelData
        readonly property string kind: root.shownKind(modelData.kinds || [])
        readonly property real x0: root.xForSec(modelData.start)
        x: x0 - 1
        width: Math.max(3, root.xForSec(modelData.end) - x0)
        visible: kind !== "" && x + width >= 0 && x <= root.width
        color: kind !== "" ? root.kindColors[kind] : "transparent"
    }
    Repeater {
        model: root.events
        Marker {
            y: 2
            height: 10
            radius: 1.5
            opacity: 0.95
        }
    }

    // Recording segments
    Repeater {
        model: root.segments
        Rectangle {
            required property var modelData
            property real segStart: modelData.start
            property real segEnd: modelData.end
            readonly property bool targeted: root.hoverSegment === modelData
            // Short clips get a minimum width so they stay visible and aimable.
            x: root.xForSec(segStart) - (targeted ? 1 : 0)
            width: Math.max(3, root.xForSec(segEnd) - root.xForSec(segStart)) + (targeted ? 2 : 0)
            y: targeted ? 10 : 14
            height: root.trackHeight - (targeted ? 20 : 28)
            visible: segEnd >= root.viewStart && segStart <= root.viewStart + root.visibleSpan
            color: modelData.type === "alarm" ? Theme.accent : "#4a5a68"
            opacity: targeted ? 1.0 : 0.85
            border.color: targeted ? Theme.text : "transparent"
            border.width: targeted ? 1 : 0
        }
    }

    // Time ticks + labels. The step adapts to the zoom: the finest one that
    // keeps ticks 24px apart, labels on the finest multiple of it that keeps
    // them 70px apart. At whole-day zoom that is hour ticks labelled every two
    // hours; zoomed in to five minutes, ten-second ticks labelled every 30s.
    readonly property var tickSteps: [10, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 21600]
    function stepFor(minPx, multipleOf) {
        for (var i = 0; i < tickSteps.length; ++i) {
            var step = tickSteps[i];
            if (step % multipleOf === 0 && step / visibleSpan * width >= minPx)
                return step;
        }
        return tickSteps[tickSteps.length - 1];
    }
    readonly property int tickStep: stepFor(24, 1)
    readonly property int labelStep: stepFor(70, tickStep)
    readonly property var tickSecs: {
        var out = [];
        if (width <= 0)
            return out;
        var end = Math.min(duration, viewStart + visibleSpan);
        for (var s = Math.ceil(viewStart / tickStep) * tickStep; s <= end; s += tickStep)
            out.push(s);
        return out;
    }
    Repeater {
        model: root.tickSecs
        Item {
            required property var modelData
            x: root.xForSec(modelData)
            Rectangle {
                y: 0; width: 1; height: modelData % 21600 === 0 ? root.trackHeight : 8
                color: Theme.border
            }
            Text {
                visible: modelData % root.labelStep === 0
                y: root.trackHeight - 14
                x: 2
                text: {
                    var h = Math.floor(modelData / 3600), m = Math.floor((modelData % 3600) / 60);
                    return String(h).padStart(2, "0") + ":" + String(m).padStart(2, "0")
                         + (root.labelStep < 60 ? ":" + String(modelData % 60).padStart(2, "0") : "");
                }
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
                // This camera's detections over its footage.
                Repeater {
                    model: laneItem.modelData.events || []
                    Marker { height: laneItem.height }
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
                // Show where a click will actually start: "▶ 14:32" when it
                // snaps to a nearby recording's start.
                var s = root.secForX(hover.point.position.x);
                var target = root.snapSec(s);
                return target !== s ? "\u25B6 " + root.clock(target) : root.clock(s);
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
            } else if (mode === 0) { // plain click: seek there (snapped to a nearby recording)
                lastSec = root.snapSec(secAt(m.x));
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
            root.zoom = Math.max(1, Math.min(root.maxZoom, root.zoom * (w.angleDelta.y > 0 ? 1.25 : 0.8)));
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
