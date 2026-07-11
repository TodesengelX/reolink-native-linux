import QtQuick
import QtQuick.Window
import QtMultimedia
import ReolinkApp
import ReolinkApp.Core

// A detached live-view window for one camera — drag it to a second monitor for
// multi-screen viewing (DESIGN §6.2). Plays the main ("Clear") stream over
// native Baichuan — the NVR's RTSP output for 8K duo mains is corrupt (see
// LivePane) — falling back to RTSP main once if the Baichuan slot is busy.
// Closing stops the player; Esc closes.
Window {
    id: win
    property int deviceRow: -1
    property string label: ""

    width: 960
    height: 540
    minimumWidth: 320
    minimumHeight: 180
    visible: true
    color: Theme.paneBackground
    title: label + " — Reolink Client"

    // Digital zoom: wheel to zoom, drag to pan when zoomed (as in a live pane).
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0

    Item {
        anchors.fill: parent
        clip: true

        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            visible: player.state === StreamPlayer.Streaming
            transform: [
                Scale {
                    origin.x: video.width / 2
                    origin.y: video.height / 2
                    xScale: win.zoom
                    yScale: win.zoom
                },
                Translate { x: win.panX; y: win.panY }
            ]
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property real lastX: 0
            property real lastY: 0
            cursorShape: win.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor
            // Pin the zoomed footage to the window edges (no panning it off into
            // empty space; zooming out reels it back in).
            function clampPan() {
                var mx = Math.max(0, (video.contentRect.width * win.zoom - video.width) / 2);
                var my = Math.max(0, (video.contentRect.height * win.zoom - video.height) / 2);
                win.panX = Math.max(-mx, Math.min(mx, win.panX));
                win.panY = Math.max(-my, Math.min(my, win.panY));
            }
            onPressed: (m) => { lastX = m.x; lastY = m.y; }
            onPositionChanged: (m) => {
                if (win.zoom > 1.0 && (m.buttons & Qt.LeftButton)) {
                    win.panX += (m.x - lastX);
                    win.panY += (m.y - lastY);
                    lastX = m.x; lastY = m.y;
                    clampPan();
                }
            }
            onWheel: (w) => {
                var z = win.zoom * (w.angleDelta.y > 0 ? 1.15 : 0.87);
                win.zoom = Math.max(1.0, Math.min(8.0, z));
                if (win.zoom <= 1.0) { win.panX = 0; win.panY = 0; }
                else clampPan();
            }
        }
    }
    StreamPlayer {
        id: player
        videoSink: video.videoSink
    }
    property bool bcFallback: false // Baichuan slot busy -> RTSP main this round
    function startStream() {
        if (win.deviceRow < 0)
            return;
        if (!bcFallback) {
            player.loop = false;
            Devices.startBaichuanLive(win.deviceRow, player, true);
            return;
        }
        var url = Devices.liveUrl(win.deviceRow, true); // degraded: RTSP main
        if (url.length > 0) {
            player.expectedSize = Devices.declaredSize(win.deviceRow, true); // main
            player.source = url;
            player.start();
        }
    }
    Component.onCompleted: startStream()
    Connections {
        target: player
        function onStateChanged() {
            if (player.state === StreamPlayer.Error && !win.bcFallback) {
                win.bcFallback = true;
                win.startStream();
            }
        }
    }
    onClosing: player.stop()

    Text {
        anchors.centerIn: parent
        visible: player.state !== StreamPlayer.Streaming
        text: player.state === StreamPlayer.Error ? player.errorString : qsTr("Connecting…")
        color: Theme.textMuted
        font.pixelSize: 13
    }

    Rectangle { // name overlay
        visible: player.state === StreamPlayer.Streaming
        anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
        radius: 3; color: "#80000000"
        width: nmRow.implicitWidth + 12; height: nmRow.implicitHeight + 6
        Row {
            id: nmRow; anchors.centerIn: parent; spacing: 6
            Text { text: win.label; color: "white"; font.pixelSize: 12 }
            Text {
                visible: win.zoom > 1.01
                text: win.zoom.toFixed(1) + "×"
                color: Theme.accent; font.pixelSize: 12
            }
        }
    }

    Shortcut { sequence: "Escape"; onActivated: win.close() }
}
