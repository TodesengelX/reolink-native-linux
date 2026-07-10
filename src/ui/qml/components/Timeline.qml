import QtQuick
import ReolinkApp

// 24-hour recording timeline: two-tone segments (grey = timer/continuous,
// blue = alarm/AI), hour ticks, a draggable playhead, and click-to-seek.
// Segments are {start,end,type,name} with start/end in seconds into the day.
Rectangle {
    id: root
    height: 64
    color: Theme.surface
    border.color: Theme.border
    radius: Theme.radius
    clip: true

    property real duration: 86400        // seconds shown (a full day)
    property real position: 0            // playhead, seconds
    property var segments: []
    property real zoom: 1.0              // 1 = whole day; >1 zooms in
    property real viewStart: 0           // left edge, seconds

    signal seekRequested(real seconds)
    signal segmentActivated(string name, real seconds)

    readonly property real visibleSpan: duration / zoom
    function xForSec(sec) { return (sec - viewStart) / visibleSpan * width; }
    function secForX(x) { return viewStart + x / width * visibleSpan; }

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
            height: root.height - 28
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
                y: 0; width: 1; height: index % 6 === 0 ? root.height : 8
                color: Theme.border
            }
            Text {
                visible: parent.index % 2 === 0
                y: root.height - 14
                x: 2
                text: String(parent.index).padStart(2, "0") + ":00"
                color: Theme.textMuted
                font.pixelSize: 9
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
        anchors.fill: parent
        onClicked: (m) => {
            var sec = root.secForX(m.x);
            root.seekRequested(sec);
            // Activate the segment under the click, if any.
            for (var i = 0; i < root.segments.length; i++) {
                var seg = root.segments[i];
                if (sec >= seg.start && sec <= seg.end) {
                    root.segmentActivated(seg.name, sec);
                    return;
                }
            }
        }
        onWheel: (w) => {
            var focus = root.secForX(w.x);
            root.zoom = Math.max(1, Math.min(48, root.zoom * (w.angleDelta.y > 0 ? 1.25 : 0.8)));
            // Keep the focus point under the cursor.
            root.viewStart = Math.max(0, Math.min(root.duration - root.visibleSpan,
                                                  focus - w.x / root.width * root.visibleSpan));
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
