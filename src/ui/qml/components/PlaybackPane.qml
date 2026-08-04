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

    VideoOutput {
        id: video
        anchors.fill: parent
        anchors.margins: 1
        fillMode: VideoOutput.PreserveAspectFit
        visible: root.streaming
    }

    // Name badge (matches the live grid's).
    Rectangle {
        visible: root.hasSource
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        radius: 3
        color: "#80000000"
        width: nameText.implicitWidth + 12
        height: nameText.implicitHeight + 6
        Text { id: nameText; anchors.centerIn: parent; text: root.label
               color: "white"; font.pixelSize: 11 }
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

    HoverHandler { id: paneHover }
    MouseArea {
        anchors.fill: parent
        // Let the combo above receive its own clicks.
        z: -1

        property real pressX: 0
        property real pressY: 0
        property bool dragging: false

        onClicked: {
            if (dragging)      // the release that ended a drag isn't a click
                return;
            root.clicked();
        }
        onPressed: (m) => { pressX = m.x; pressY = m.y; dragging = false; }
        onPositionChanged: (m) => {
            if (!(m.buttons & Qt.LeftButton) || !root.hasSource)
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
