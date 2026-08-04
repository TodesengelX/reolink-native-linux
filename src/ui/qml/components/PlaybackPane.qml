import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtMultimedia
import ReolinkApp
import ReolinkApp.Core

// One cell of the synced playback grid: sub-stream FLV video for a single
// camera, its own recordings for the selected day, and an honest per-camera
// answer when the shared playhead sits where THIS camera has no footage.
// The page owns the playhead and drives every pane from one timeline; sync
// comes from asking each camera for the same wall-clock start time — the
// protocol has no cross-stream sync mechanism, and the official client does
// exactly this (its multi-pane playback is likewise forced to the sub stream).
Rectangle {
    id: root
    color: Theme.paneBackground
    border.color: selected ? Theme.accent : Theme.border
    border.width: 1
    clip: true

    property int deviceRow: -1
    property int paneIndex: -1     // cell in the playback grid
    property string label: ""
    property bool selected: false
    // This camera's recordings for the selected day ({start,end,type} seconds).
    property var segments: []
    // Bound to the page's shared playhead — drives the "no footage" verdict.
    property real playheadSecs: 0

    signal clicked()
    // Double-click toggles maximize (the page owns which pane is maximized).
    signal doubleClicked()
    // The pane's hover combo picked a different camera; the page reassigns,
    // re-searches, and replays (the pane can't do that alone — the day's
    // segments and the union track live above it).
    signal cameraRequested(int row)
    // A camera was dropped onto this pane — from the sidebar or another pane.
    signal cameraDropped(int pane, int row)

    // True while a drag hovers this pane.
    property bool dropTarget: false

    readonly property bool hasSource: deviceRow >= 0
    readonly property bool streaming: player.state === StreamPlayer.Streaming

    // ---- Maximized-pane HD ------------------------------------------------
    // Full-resolution main stream over native Baichuan, exactly like the
    // single-pane page's HD mode. The page enables the toggle only on the
    // maximized pane: the device allows just 2 concurrent main streams, and
    // DeviceManager's in-place-seek slot holds one playback session — one HD
    // pane at a time is the constraint, not a style choice.
    property bool showHdToggle: false
    property bool hdActive: false
    // Epoch at 00:00 of the selected day, so the pane can turn playhead
    // seconds into wall-clock time without reaching into the page.
    property real dayEpoch: 0

    function epochOf(sec) { return Math.floor(dayEpoch + sec); }

    function playHd(sec) {
        // Seek the running Baichuan session in place when possible (no
        // reconnect); otherwise open one at this moment.
        if (streaming && Devices.seekBaichuanPlayback(deviceRow, epochOf(sec)))
            return;
        player.loop = false;
        Devices.startBaichuanPlayback(deviceRow, epochOf(sec), player, true);
    }

    function setHd(on) {
        if (hdActive === on)
            return;
        hdActive = on;
        // Mid-playback, carry the watched moment across the transport switch.
        if (streaming || player.state === StreamPlayer.Connecting)
            playAtSecs(playheadSecs, epochOf(playheadSecs));
    }
    // Restoring the grid (or losing maximize any other way) drops back to the
    // sub stream — a grid cell must not keep holding a main-stream session.
    onShowHdToggleChanged: if (!showHdToggle && hdActive) setHd(false)

    function covered(sec) {
        for (var i = 0; i < segments.length; i++)
            if (sec >= segments[i].start && sec <= segments[i].end)
                return true;
        return false;
    }

    // Play this camera from the given moment (epoch = wall-clock seconds).
    // A gap is a normal outcome, not an error: stop and let the overlay say so.
    function playAtSecs(sec, epoch) {
        if (!hasSource)
            return;
        if (!covered(sec)) {
            player.stop();
            return;
        }
        if (hdActive) {
            playHd(sec);
            return;
        }
        var url = Devices.playbackUrl(deviceRow, epoch, false); // sub stream
        if (url.length === 0)
            return;
        player.loop = false;
        player.expectedSize = Devices.declaredSize(deviceRow, false);
        player.source = url;
        player.start();
    }

    function stopPlayback() { player.stop(); }

    // A pane that leaves the layout (loses its slot, or its page/grid hides)
    // must not keep a playback stream open on the connection-limited NVR.
    onVisibleChanged: if (!visible) player.stop()

    // Retry on connection error: NVRs are connection-limited and may
    // momentarily refuse a playback stream while others are opening.
    StreamPlayer { id: player; videoSink: video.videoSink; retryOnError: true }
    Component.onDestruction: player.stop()

    // ---- Video with digital zoom (same interaction as a live pane) --------
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0

    Item {
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            visible: root.streaming
            transform: [
                Scale {
                    origin.x: video.width / 2
                    origin.y: video.height / 2
                    xScale: root.zoom
                    yScale: root.zoom
                },
                Translate { x: root.panX; y: root.panY }
            ]
        }
    }

    // Name + zoom badge (matches the live grid's).
    Rectangle {
        visible: root.hasSource
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        radius: 3
        color: "#80000000"
        width: nameRow.implicitWidth + 12
        height: nameRow.implicitHeight + 6
        Row {
            id: nameRow
            anchors.centerIn: parent
            spacing: 6
            Text { text: root.label; color: "white"; font.pixelSize: 11 }
            Text {
                visible: root.zoom > 1.01
                text: root.zoom.toFixed(1) + "×"
                color: Theme.accent
                font.pixelSize: 11
            }
        }
    }

    // Per-camera state at the shared playhead.
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing
        visible: !root.streaming
        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: player.state === StreamPlayer.Connecting
            visible: running; width: 28; height: 28
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, root.width - 20)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: player.state === StreamPlayer.Error ? Theme.danger : Theme.textMuted
            font.pixelSize: 11
            text: {
                if (!root.hasSource) return qsTr("No camera");
                if (player.state === StreamPlayer.Connecting) return qsTr("Connecting…");
                if (player.state === StreamPlayer.Error) return player.errorString;
                if (!root.covered(root.playheadSecs))
                    return qsTr("No footage at this time");
                return qsTr("Ready");
            }
        }
    }

    // ---- Drag and drop (mirrors the live grid) ----------------------------
    // Dropping re-points this cell; the page swaps if the camera is already
    // placed, so a drag can never silently knock a camera off the grid.
    DropArea {
        anchors.fill: parent
        keys: ["reolink/camera"]
        onEntered: (drag) => {
            root.dropTarget = !(drag.source && drag.source.sourcePane === root.paneIndex);
        }
        onExited: root.dropTarget = false
        onDropped: (drop) => {
            root.dropTarget = false;
            if (drop.source && drop.source.deviceRow >= 0)
                root.cameraDropped(root.paneIndex, drop.source.deviceRow);
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.dropTarget
        color: Theme.accent
        opacity: 0.18
        border.color: Theme.accent
        border.width: 2
        z: 50
    }

    // Drag ghost, parented to the window so the pane's clip can't cut it off.
    Item {
        id: paneDrag
        parent: Window.contentItem
        width: 190
        height: 108
        visible: Drag.active
        z: 100
        Drag.active: false
        Drag.keys: ["reolink/camera"]
        Drag.hotSpot: Qt.point(width / 2, height / 2)
        property int deviceRow: root.deviceRow
        property int sourcePane: root.paneIndex

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.accent
            border.width: 2
            opacity: 0.95
            Text {
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.label
                color: Theme.text
                font.pixelSize: 12
            }
        }
    }

    // Quality toggle, maximized pane only.
    Rectangle {
        visible: root.showHdToggle
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 10
        width: 48
        height: 28
        radius: Theme.radius
        color: root.hdActive ? Theme.accent : (hdHover.hovered ? Theme.surfaceAlt : "#cc0d141b")
        border.color: root.hdActive ? Theme.accent : Theme.border
        z: 60
        Text {
            anchors.centerIn: parent
            text: root.hdActive ? qsTr("HD") : qsTr("SD")
            color: root.hdActive ? Theme.window : Theme.text
            font.pixelSize: 12
            font.bold: true
        }
        HoverHandler { id: hdHover }
        // ReleaseWithinBounds takes an exclusive grab on press so the pane's
        // own MouseArea can't read the tap as a click or a drag start.
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: root.setHd(!root.hdActive)
        }
        ToolTip {
            visible: hdHover.hovered
            delay: 500
            x: (parent.width - width) / 2
            y: -height - 8
            contentItem: Text {
                text: root.hdActive ? qsTr("Switch to SD (light scrubbing)")
                                    : qsTr("Switch to HD (full resolution)")
                color: Theme.text
                font.pixelSize: 11
            }
            background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: 4 }
        }
    }

    HoverHandler { id: paneHover }
    MouseArea {
        anchors.fill: parent
        // Let the combo above receive its own clicks.
        z: -1

        property real pressX: 0
        property real pressY: 0
        property real lastX: 0
        property real lastY: 0
        property bool dragging: false
        // Zoomed in, dragging pans the image — the older and more frequent
        // gesture — so a pane can only be picked up at 1× (same rule as live).
        readonly property bool canDrag: root.hasSource && root.zoom <= 1.0

        // Keep the zoomed footage pinned to the viewport edges.
        function clampPan() {
            var mx = Math.max(0, (video.contentRect.width * root.zoom - video.width) / 2);
            var my = Math.max(0, (video.contentRect.height * root.zoom - video.height) / 2);
            root.panX = Math.max(-mx, Math.min(mx, root.panX));
            root.panY = Math.max(-my, Math.min(my, root.panY));
        }

        onClicked: {
            if (dragging)      // the release that ended a drag isn't a click
                return;
            root.clicked();
        }
        onDoubleClicked: if (root.hasSource) root.doubleClicked()
        onPressed: (m) => {
            pressX = m.x; pressY = m.y;
            lastX = m.x; lastY = m.y;
            dragging = false;
        }
        onPositionChanged: (m) => {
            if (!(m.buttons & Qt.LeftButton))
                return;
            if (root.zoom > 1.0) {
                root.panX += (m.x - lastX);
                root.panY += (m.y - lastY);
                lastX = m.x; lastY = m.y;
                clampPan();
                return;
            }
            if (!canDrag)
                return;
            if (!dragging
                && Math.hypot(m.x - pressX, m.y - pressY) > Theme.dragThreshold) {
                dragging = true;
                paneDrag.Drag.active = true;
            }
            if (dragging) {
                var p = mapToItem(paneDrag.parent, m.x, m.y);
                paneDrag.x = p.x - paneDrag.width / 2;
                paneDrag.y = p.y - paneDrag.height / 2;
            }
        }
        onReleased: {
            if (!dragging)
                return;
            paneDrag.Drag.drop();
            paneDrag.Drag.active = false;
        }
        onCanceled: {
            if (!dragging)
                return;
            paneDrag.Drag.active = false;
            dragging = false;
        }
        onWheel: (w) => {
            if (!root.hasSource)
                return;
            var z = root.zoom * (w.angleDelta.y > 0 ? 1.15 : 0.87);
            root.zoom = Math.max(1.0, Math.min(8.0, z));
            if (root.zoom <= 1.0) { root.panX = 0; root.panY = 0; }
            else clampPan();
        }
    }

    // Hover camera selector, top-right — how a pane is re-pointed at another
    // camera (the playback grid has no sidebar drag; this mirrors the official
    // client's per-pane camera choice).
    CameraComboBox {
        id: paneCombo
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        width: Math.min(170, root.width - 12)
        implicitHeight: 26
        visible: paneHover.hovered || popup.visible
        onActivated: (index) => { if (index !== root.deviceRow) root.cameraRequested(index); }
    }
    // Imperative sync, not a binding: the ComboBox writes currentIndex itself on
    // user interaction, which would sever a declarative binding the first time.
    onDeviceRowChanged: if (deviceRow >= 0) paneCombo.currentIndex = deviceRow
    Component.onCompleted: if (deviceRow >= 0) paneCombo.currentIndex = deviceRow
}
