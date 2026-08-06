import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtMultimedia
import ReolinkApp
import ReolinkApp.Core

// One cell of the live grid: video + name overlay + connection-state overlay +
// a hover floating toolbar and an optional PTZ joystick. Double-click toggles
// maximize (handled by the page). Digital zoom via wheel + drag.
Rectangle {
    id: root
    color: Theme.paneBackground
    border.color: selected ? Theme.accent : Theme.border
    border.width: 1
    clip: true

    property int paneIndex: -1     // grid slot
    property int deviceRow: -1     // row in the Devices model (-1 = empty slot)
    property string label: ""
    property bool selected: false
    property bool forceMain: false // maximizing DEFAULTS a pane to the main stream
    // Manual per-camera view rotation (degrees; sidebar right-click sets it).
    property int viewRotation: 0
    property bool pageActive: true // false when the Live View page isn't on screen

    // Capabilities (from the Devices model; false for empty slots). Named cap*
    // to avoid colliding with the identically-named model roles in the delegate.
    property bool capPtz: false
    property bool capZoom: false
    property bool capAudio: false
    property bool capSiren: false
    property bool capFloodlight: false
    property bool capTalk: false
    property bool talkActive: false
    property bool floodOn: false // white-LED/floodlight on state (optimistic)

    // User stream-quality preference: false = Fluent (sub), true = Clear (main).
    // This alone decides which stream plays — so the SD/HD toolbar toggle works in
    // every layout, including a maximized pane. Maximizing/restoring just seeds a
    // sensible default (main when big, sub in the grid) via onForceMainChanged; the
    // user can still override it with the toggle afterwards.
    property bool qualityMain: false
    readonly property bool effectiveMain: qualityMain
    onForceMainChanged: qualityMain = forceMain
    readonly property bool hasSource: deviceRow >= 0
    // What this pane is currently streaming: an RTSP URL, or "bc:<row>" for native
    // Baichuan HD live. HD (main) live goes over Baichuan because the NVR's RTSP
    // output for 8K duo main streams is itself corrupt (verified against raw
    // captures) — the official clients use Baichuan for live too. The grid's sub
    // streams stay RTSP (clean, and the NVR only allows ~1 Baichuan session).
    property string streamKey: ""
    property bool bcFallback: false // Baichuan slot busy → RTSP main for this round

    // These carry the DEVICE ROW, not the pane index: the page tracks which
    // camera is maximized/selected, so both survive a rearrange.
    signal toggleMaximize(int index)
    signal clicked(int index)
    signal popOut(int deviceRow, string label)
    // A camera was dropped onto this pane — from the sidebar or another pane.
    signal cameraDropped(int pane, int row)

    // True while a drag hovers this pane, so it can show where it would land.
    property bool dropTarget: false

    // A pane hidden by a layout change holds its stream this long before
    // dropping it, so coming straight back costs nothing.
    property int keepAliveMs: 5000

    // Drop the stream for good. Separate from updateSource() so the deferred
    // path and the immediate one can't disagree about what "stopped" means.
    function releaseStream() {
        keepAlive.stop();
        if (streamKey === "")
            return;
        streamKey = "";
        player.stop();
    }

    Timer {
        id: keepAlive
        interval: root.keepAliveMs
        onTriggered: root.releaseStream()
    }

    function updateSource() {
        // Hidden by a layout change — maximizing another pane, or switching to a
        // preset this slot falls outside of. Hold the stream briefly rather than
        // tearing it down: restoring, or flipping 6 -> 4 -> 6, then reuses the
        // running session instead of reconnecting every pane (each reconnect
        // waits for the next keyframe, so it costs seconds per camera).
        //
        // Leaving Live View entirely is deliberately NOT covered: Playback needs
        // the NVR's session slots, and hidden panes holding them starve it.
        if (deviceRow >= 0 && pageActive && !visible) {
            if (streamKey !== "")
                keepAlive.restart();
            return;
        }
        keepAlive.stop();   // visible again (or shutting down): cancel the drop

        // Only stream when the pane is laid out AND its page is actually on screen
        // — otherwise hidden Live View panes keep streaming and exhaust the NVR's
        // limited session slots (starving Playback and other cameras).
        var want = deviceRow >= 0 && visible && pageActive;
        var key = "";
        if (want) {
            if (effectiveMain && !bcFallback)
                key = "bc:" + deviceRow;
            else
                key = Devices.liveUrl(deviceRow, effectiveMain);
        }
        if (key === streamKey)
            return;
        streamKey = key;
        if (key === "") {
            player.stop();
        } else if (key.substring(0, 3) === "bc:") {
            player.loop = false;
            Devices.startBaichuanLive(root.deviceRow, player, true);
        } else {
            // Set loop before start() so the worker sees the right value from
            // frame one (the declarative `loop:` binding can lag the handler).
            player.loop = !key.startsWith("rtsp://") && !key.startsWith("rtmp://")
                       && !key.startsWith("tcp://") && !key.startsWith("udp://");
            // Declared size for the stream we're opening, so a transmitted-rotated
            // main stream (e.g. Duo 3) is presented upright.
            player.expectedSize = Devices.declaredSize(root.deviceRow, root.effectiveMain);
            player.source = key;
            player.start();
        }
    }
    // A pane pointed at a different camera must let the old one go now — its
    // session is wanted elsewhere (a swap hands it straight to another pane).
    onDeviceRowChanged: { bcFallback = false; releaseStream(); updateSource(); }
    onVisibleChanged: updateSource()
    onPageActiveChanged: updateSource()
    onEffectiveMainChanged: { bcFallback = false; updateSource(); }
    Component.onCompleted: updateSource()

    // The NVR grants only ~1 Baichuan session. If HD live can't get the slot,
    // fall back to RTSP main once (artifact-prone on 8K duos, but beats a dead
    // pane); the next SD/HD flip or camera change retries Baichuan.
    Connections {
        target: player
        function onStateChanged() {
            if (player.state === StreamPlayer.Error && !root.bcFallback
                && root.streamKey.substring(0, 3) === "bc:") {
                root.bcFallback = true;
                root.updateSource();
            }
        }
    }

    // Recompute when the backing device finishes priming / changes.
    Connections {
        target: Devices
        function onDataChanged(topLeft, bottomRight) {
            if (root.deviceRow >= topLeft.row && root.deviceRow <= bottomRight.row)
                root.updateSource();
        }
    }

    StreamPlayer {
        id: player
        videoSink: video.videoSink
    }

    Component.onDestruction: player.stop()

    // ---- Drag and drop -----------------------------------------------------
    // Dropping a camera here re-points this cell at it. Nothing needs to stop
    // the outgoing stream by hand: changing deviceRow (or dropping out of the
    // visible preset) runs updateSource(), and both StreamPlayer.setSource and
    // setPacketSource stop the running session first — which for an HD pane also
    // fires the Baichuan client's stop callback and frees the NVR's single slot.
    DropArea {
        anchors.fill: parent
        keys: ["reolink/camera"]
        onEntered: (drag) => {
            // Dragging a pane onto itself is a no-op; don't advertise a drop.
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

    // The thing that actually gets dragged when this pane is picked up. It lives
    // in the window overlay so it isn't clipped by the pane it came from.
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

    // ---- Video with digital zoom ------------------------------------------
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
            visible: player.state === StreamPlayer.Streaming
            orientation: root.viewRotation
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

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            drag.target: undefined
            property real lastX: 0
            property real lastY: 0

            // Keep the zoomed footage pinned to the viewport edges — panning must
            // not drag it off into empty space, and zooming out reels it back in.
            function clampPan() {
                var mx = Math.max(0, (video.contentRect.width * root.zoom - video.width) / 2);
                var my = Math.max(0, (video.contentRect.height * root.zoom - video.height) / 2);
                root.panX = Math.max(-mx, Math.min(mx, root.panX));
                root.panY = Math.max(-my, Math.min(my, root.panY));
            }
            // Drag state. A pane is picked up only when it isn't zoomed in —
            // while zoomed, dragging pans the image, which is the older and more
            // frequent gesture.
            property real pressX: 0
            property real pressY: 0
            property bool dragging: false
            readonly property bool canDrag: root.hasSource && root.zoom <= 1.0

            onClicked: {
                if (dragging)      // the release that ended a drag isn't a click
                    return;
                root.clicked(root.deviceRow);
            }
            onDoubleClicked: if (root.hasSource) root.toggleMaximize(root.deviceRow)
            onPressed: (m) => {
                lastX = m.x; lastY = m.y;
                pressX = m.x; pressY = m.y;
                dragging = false;
            }
            onPositionChanged: (m) => {
                if (root.zoom > 1.0 && (m.buttons & Qt.LeftButton)) {
                    root.panX += (m.x - lastX);
                    root.panY += (m.y - lastY);
                    lastX = m.x; lastY = m.y;
                    clampPan();
                    return;
                }
                if (!(m.buttons & Qt.LeftButton) || !canDrag)
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
                if (!root.capZoom && !root.hasSource) return;
                var z = root.zoom * (w.angleDelta.y > 0 ? 1.15 : 0.87);
                root.zoom = Math.max(1.0, Math.min(8.0, z));
                if (root.zoom <= 1.0) { root.panX = 0; root.panY = 0; }
                else clampPan();
            }
        }
    }

    // ---- Name + zoom badge -------------------------------------------------
    Rectangle {
        visible: root.hasSource && player.state === StreamPlayer.Streaming
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

    // ---- Connection-state overlay -----------------------------------------
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing
        visible: player.state !== StreamPlayer.Streaming

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: player.state === StreamPlayer.Connecting
            visible: running
            width: 32
            height: 32
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
                switch (player.state) {
                case StreamPlayer.Connecting: return qsTr("Connecting…");
                case StreamPlayer.Error: return player.errorString;
                case StreamPlayer.Stopped: return qsTr("Stopped");
                default: return "";
                }
            }
        }
    }

    // ---- PTZ joystick overlay ---------------------------------------------
    property bool ptzOpen: false
    Loader {
        active: root.ptzOpen && root.capPtz
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 10
        sourceComponent: PtzPad {
            deviceRow: root.deviceRow
            showZoom: root.capZoom
        }
    }

    // ---- Floating toolbar (hover) -----------------------------------------
    HoverHandler { id: paneHover }

    Rectangle {
        id: toolbar
        visible: root.hasSource && (paneHover.hovered || root.ptzOpen)
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 8
        radius: Theme.radius
        color: "#cc0d141b"
        border.color: Theme.border
        height: 34
        width: toolRow.implicitWidth + 16

        Row {
            id: toolRow
            anchors.centerIn: parent
            spacing: 2

            component ToolButton: Rectangle {
                property string glyph: ""
                property string tip: ""
                property bool active: false
                property bool enabledTool: true
                signal activated()
                width: 28; height: 28; radius: 4
                visible: enabledTool
                color: active ? Theme.accentDim : (hover.hovered ? Theme.surfaceAlt : "transparent")
                Text {
                    anchors.centerIn: parent
                    text: parent.glyph
                    color: parent.active ? Theme.text : Theme.textMuted
                    font.pixelSize: 14
                }
                HoverHandler { id: hover }
                ToolTip {
                    visible: hover.hovered && tip !== ""
                    delay: 500
                    x: (parent.width - width) / 2
                    y: -height - 8
                    contentItem: Text { text: tip; color: Theme.text; font.pixelSize: 11 }
                    background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: 4 }
                }
                // ReleaseWithinBounds takes an exclusive grab on press so the
                // full-pane MouseArea (click-to-select / pan) behind the video
                // can't steal the tap.
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: parent.activated()
                }
            }

            // Quality: Fluent / (Balanced) / Clear. Balanced omitted until wired.
            ToolButton {
                glyph: root.qualityMain ? "HD" : "SD"
                tip: root.qualityMain ? qsTr("Switch to Fluent (SD)") : qsTr("Switch to Clear (HD)")
                active: root.qualityMain
                onActivated: root.qualityMain = !root.qualityMain
            }
            ToolButton {
                glyph: "◉"; enabledTool: true
                tip: qsTr("Save snapshot")
                onActivated: Devices.snapshot(root.deviceRow)
            }
            // Manual record: taps the displayed stream, no second connection.
            ToolButton {
                glyph: "⏺"; active: player.recording
                tip: player.recording ? qsTr("Stop recording") : qsTr("Record video")
                onActivated: player.recording ? player.stopRecording() : player.startRecording()
            }
            ToolButton {
                glyph: "⊕"; active: root.zoom > 1.01
                tip: root.zoom > 1.01 ? qsTr("Reset zoom") : qsTr("Digital zoom (scroll to adjust)")
                onActivated: { if (root.zoom > 1.01) { root.zoom = 1; root.panX = 0; root.panY = 0; }
                               else root.zoom = 2; }
            }
            ToolButton {
                glyph: "⇅"; active: root.ptzOpen; enabledTool: root.capPtz
                tip: qsTr("PTZ controls")
                onActivated: root.ptzOpen = !root.ptzOpen
            }
            // Two-way talk (push-to-hold). Baichuan talk path is the primary
            // transport (DESIGN §5.4) and wires in with the M12 protocol work;
            // this toggles the UI state and mic intent today.
            ToolButton {
                glyph: "🎙"; active: root.talkActive; enabledTool: root.capTalk
                tip: qsTr("Two-way talk (coming soon)")
                onActivated: root.talkActive = !root.talkActive
            }
            ToolButton {
                glyph: "📢"; enabledTool: root.capSiren
                tip: qsTr("Sound siren")
                // AudioAlarmPlay takes its fields FLAT in param (verified on real
                // firmware — a Set*-style wrapped object gets rspCode -4 "param
                // error"). alarm_mode "times"/1 sounds a single blast.
                onActivated: Devices.applySetting(root.deviceRow, "AudioAlarmPlay",
                    { "alarm_mode": "times", "times": 1, "channel": Devices.channelOf(root.deviceRow) })
            }
            ToolButton {
                glyph: "💡"; active: root.floodOn; enabledTool: root.capFloodlight
                tip: qsTr("Toggle floodlight")
                onActivated: {
                    root.floodOn = !root.floodOn;       // optimistic; toast confirms
                    Devices.toggleFloodlight(root.deviceRow);
                }
            }
            // Pop out into a detached window (drag to another monitor).
            ToolButton {
                glyph: "⧉"
                tip: qsTr("Pop out to a window")
                onActivated: if (root.hasSource) root.popOut(root.deviceRow, root.label)
            }
            ToolButton {
                glyph: "⛶"
                tip: root.forceMain ? qsTr("Restore grid") : qsTr("Maximize")
                onActivated: if (root.hasSource) root.toggleMaximize(root.deviceRow)
            }
        }
    }
}
