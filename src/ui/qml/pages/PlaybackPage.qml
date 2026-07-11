import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import ReolinkApp
import ReolinkApp.Core

// Playback: pick a device + date, search the day's recordings, scrub the
// two-tone timeline, and play a segment. Frame-accurate in-file seeking is a
// follow-up (needs seek support in StreamPlayer); clicking a segment plays it.
Item {
    id: page

    property int deviceRow: -1
    property int selYear: 2026
    property int selMonth: 7
    property int selDay: 9

    // Default the date to today.
    Component.onCompleted: {
        var now = new Date();
        selYear = now.getFullYear();
        selMonth = now.getMonth() + 1;
        selDay = now.getDate();
    }

    property var recordingDays: []
    property real playheadSecs: 0    // playhead position (seconds into the day)
    property bool _autoplayed: false // test hook guard
    // HD mode streams the full-resolution main stream over native Baichuan; off =
    // light sub-stream (FLV) scrubbing.
    property bool hdMode: false

    // False when the Playback page isn't on screen — stop streaming so a Baichuan
    // session (or FLV stream) isn't left running on the connection-limited NVR.
    property bool active: true
    onActiveChanged: if (!active) player.stop()

    // Advance the playhead in realtime while streaming, so the timeline cursor
    // tracks the current position (and play/pause resumes from where you are).
    Timer {
        interval: 1000; repeat: true
        running: player.state === StreamPlayer.Streaming
        onTriggered: if (page.playheadSecs < 86399) page.playheadSecs += 1
    }

    function refresh() {
        if (page.deviceRow >= 0)
            Devices.searchRecordings(page.deviceRow, page.selYear, page.selMonth, page.selDay);
    }

    // Unix epoch (local time) at a given second into the selected day.
    function epochAt(sec) {
        var d = new Date(page.selYear, page.selMonth - 1, page.selDay, 0, 0, 0);
        return Math.floor(d.getTime() / 1000) + Math.floor(sec);
    }
    function inRecording(sec) {
        for (var i = 0; i < timeline.segments.length; i++) {
            var s = timeline.segments[i];
            if (sec >= s.start && sec <= s.end) return true;
        }
        return false;
    }
    // Move the playhead and, if a recording covers that moment, play from it.
    function playAt(sec) {
        page.playheadSecs = sec;
        if (!inRecording(sec)) {
            player.stop();
            return;
        }
        if (page.hdMode) {
            page.playHd(sec);
            return;
        }
        // Scrub against the sub stream: it is light and always landscape, so the
        // timeline stays responsive. (HD mode below plays the full-res main.)
        player.loop = false;
        var url = Devices.playbackUrl(page.deviceRow, epochAt(sec), false);
        if (url.length > 0) {
            player.expectedSize = Devices.declaredSize(page.deviceRow, false); // sub
            player.source = url;
            player.start();
        }
    }

    // HD: stream the full-resolution main stream over native Baichuan (TCP 9000)
    // from this exact moment — realtime and frame-accurate, the way the official
    // apps do it (HTTP-FLV can't carry the HEVC main stream, cmd=Download is slow).
    function playHd(sec) {
        if (page.deviceRow < 0)
            return;
        statusText.text = qsTr("HD");
        // Seek the running session in place (no reconnect) when possible; else open
        // a fresh Baichuan session.
        if (player.state === StreamPlayer.Streaming
            && Devices.seekBaichuanPlayback(page.deviceRow, epochAt(sec)))
            return;
        player.loop = false;
        Devices.startBaichuanPlayback(page.deviceRow, epochAt(sec), player, true);
    }

    property real _pendingPlayEpoch: 0  // play this once the day's search returns
    property real _pendingPlaySecs: -1  // resume here after a camera switch

    // Called when an event is clicked in the Events inbox: jump to that exact
    // camera, date, and moment. Runs ONE search, then plays when it returns —
    // firing search+search+playback at once overwhelms a connection-limited NVR.
    function openAt(hostId, channel, timestamp) {
        var d = new Date(timestamp * 1000);
        page.selYear = d.getFullYear();
        page.selMonth = d.getMonth() + 1;
        page.selDay = d.getDate();
        page.playheadSecs = d.getHours() * 3600 + d.getMinutes() * 60 + d.getSeconds();
        page._pendingPlayEpoch = timestamp;
        var row = Devices.rowOfHostChannel(hostId, channel);
        if (row < 0)
            return;
        if (deviceCombo.currentIndex !== row)
            deviceCombo.currentIndex = row; // triggers the (single) refresh
        else
            page.refresh();
    }

    Connections {
        target: Devices
        function onRecordingsFound(row, segments) {
            if (row === page.deviceRow) {
                timeline.segments = segments;
                statusText.text = segments.length + qsTr(" recordings");
                // Event jump: play the requested moment now that recordings loaded.
                if (page._pendingPlayEpoch > 0) {
                    var ep = page._pendingPlayEpoch;
                    page._pendingPlayEpoch = 0;
                    page._pendingPlaySecs = -1;
                    var url = Devices.playbackUrl(page.deviceRow, ep, false); // sub stream
                    if (url.length > 0) {
                        player.expectedSize = Devices.declaredSize(page.deviceRow, false);
                        player.source = url;
                        player.start();
                    }
                } else if (page._pendingPlaySecs >= 0) {
                    // Camera switch: resume at the same playhead moment (playAt
                    // respects SD/HD mode and stops if no recording covers it).
                    var sec = page._pendingPlaySecs;
                    page._pendingPlaySecs = -1;
                    page.playAt(sec);
                }
                // Test hook: auto-play the first recording ONCE to verify the video path.
                if (typeof playbackAutoplay !== "undefined" && playbackAutoplay
                    && segments.length > 0 && !page._autoplayed) {
                    page._autoplayed = true;
                    page.playAt(segments[0].start); // exact segment start
                }
            }
        }
        function onRecordingDaysFound(row, year, month, days) {
            if (row === page.deviceRow && year === page.selYear && month === page.selMonth)
                page.recordingDays = days;
        }
        function onRecordingsFailed(row, error) {
            if (row === page.deviceRow) {
                timeline.segments = [];
                statusText.text = error;
            }
        }
        // Re-fetch once the selected device finishes connecting (its client
        // isn't primed at page-load time, so the initial fetch returns empty).
        function onDataChanged(topLeft, bottomRight) {
            if (page.deviceRow >= topLeft.row && page.deviceRow <= bottomRight.row
                && timeline.segments.length === 0)
                page.refresh();
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        // ---- Main: device bar + video + timeline + controls ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Camera:"); color: Theme.textMuted; font.pixelSize: 12 }
                CameraComboBox {
                    id: deviceCombo
                    Layout.preferredWidth: 240
                    onCurrentIndexChanged: {
                        // Switching cameras keeps the date and playhead: once the new
                        // camera's recordings load, resume playing at this same moment
                        // (the event-jump flow drives its own epoch instead).
                        var resume = page.deviceRow >= 0 && page.playheadSecs > 0
                                     && page._pendingPlayEpoch <= 0;
                        page.deviceRow = currentIndex;
                        if (resume)
                            page._pendingPlaySecs = page.playheadSecs;
                        page.refresh();
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    id: statusText
                    text: qsTr("Select a date")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }

            Rectangle {
                id: videoBox
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.paneBackground
                border.color: Theme.border

                // Digital zoom on the footage: wheel to zoom, drag to pan when
                // zoomed — the same interaction as a live pane's video.
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
                        transform: [
                            Scale {
                                origin.x: video.width / 2
                                origin.y: video.height / 2
                                xScale: videoBox.zoom
                                yScale: videoBox.zoom
                            },
                            Translate { x: videoBox.panX; y: videoBox.panY }
                        ]
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        property real lastX: 0
                        property real lastY: 0
                        cursorShape: videoBox.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor
                        onPressed: (m) => { lastX = m.x; lastY = m.y; }
                        onPositionChanged: (m) => {
                            if (videoBox.zoom > 1.0 && (m.buttons & Qt.LeftButton)) {
                                videoBox.panX += (m.x - lastX);
                                videoBox.panY += (m.y - lastY);
                                lastX = m.x; lastY = m.y;
                            }
                        }
                        onWheel: (w) => {
                            var z = videoBox.zoom * (w.angleDelta.y > 0 ? 1.15 : 0.87);
                            videoBox.zoom = Math.max(1.0, Math.min(8.0, z));
                            if (videoBox.zoom <= 1.0) { videoBox.panX = 0; videoBox.panY = 0; }
                        }
                    }
                }

                // Zoom badge
                Rectangle {
                    visible: videoBox.zoom > 1.01 && player.state === StreamPlayer.Streaming
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 6
                    radius: 3; color: "#80000000"
                    width: zoomBadge.implicitWidth + 12; height: zoomBadge.implicitHeight + 6
                    Text {
                        id: zoomBadge; anchors.centerIn: parent
                        text: videoBox.zoom.toFixed(1) + "×"
                        color: Theme.accent; font.pixelSize: 11
                    }
                }

                // Retry on connection error: NVRs are connection-limited and may
                // momentarily refuse the playback stream.
                StreamPlayer { id: player; videoSink: video.videoSink; retryOnError: true }

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacing
                    visible: player.state !== StreamPlayer.Streaming
                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        running: player.state === StreamPlayer.Connecting
                        visible: running; width: 32; height: 32
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: player.state === StreamPlayer.Error ? Theme.danger : Theme.textMuted
                        font.pixelSize: 12
                        text: player.state === StreamPlayer.Error ? player.errorString
                            : qsTr("Click a recording on the timeline to play")
                    }
                }
            }

            // Test hook: RL_MOCK_RECORDINGS seeds the timeline so its two-tone
            // rendering can be verified without a real NVR.
            Component.onCompleted: {
                if (typeof mockRecordings !== "undefined" && mockRecordings) {
                    timeline.segments = [
                        { start: 3600, end: 12600, type: "timer", name: "a" },
                        { start: 14400, end: 15000, type: "alarm", name: "b" },
                        { start: 28800, end: 43200, type: "timer", name: "c" },
                        { start: 45000, end: 45600, type: "alarm", name: "d" },
                        { start: 61200, end: 79200, type: "timer", name: "e" }
                    ];
                    page.playheadSecs = 32400;
                    statusText.text = "5 recordings (mock)";
                }
            }

            Timeline {
                id: timeline
                Layout.fillWidth: true
                position: page.playheadSecs
                onSeek: (seconds) => page.playheadSecs = seconds  // move playhead only
                onCommit: (seconds) => page.playAt(seconds)       // start playback on release
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing
                component Ctl: Rectangle {
                    property string glyph: ""
                    property string tip: ""
                    signal activated()
                    width: 34; height: 30; radius: Theme.radius
                    color: cHover.hovered ? Theme.surfaceAlt : Theme.surface
                    border.color: Theme.border
                    Text { anchors.centerIn: parent; text: parent.glyph; color: Theme.text; font.pixelSize: 14 }
                    HoverHandler { id: cHover }
                    ToolTip {
                        visible: cHover.hovered && tip !== ""
                        delay: 500
                        x: (parent.width - width) / 2
                        y: -height - 8
                        contentItem: Text { text: tip; color: Theme.text; font.pixelSize: 11 }
                        background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: 4 }
                    }
                    TapHandler { onTapped: parent.activated() }
                }
                // Play/pause resumes at the current playhead in the active quality
                // (a StreamPlayer session is one-shot, so "play" re-opens the stream).
                Ctl { glyph: player.state === StreamPlayer.Streaming ? "⏸" : "▶"
                      tip: player.state === StreamPlayer.Streaming ? qsTr("Pause") : qsTr("Play")
                      onActivated: player.state === StreamPlayer.Streaming
                                   ? player.stop() : page.playAt(page.playheadSecs) }
                Ctl { glyph: "⏹"; tip: qsTr("Stop"); onActivated: player.stop() }
                Item { Layout.fillWidth: true }
                // Quality toggle: SD = light sub-stream (FLV) scrubbing; HD = full-res
                // main stream over native Baichuan.
                Rectangle {
                    width: 52; height: 30; radius: Theme.radius
                    color: page.hdMode ? Theme.accent : (hdHover.hovered ? Theme.surfaceAlt : Theme.surface)
                    border.color: page.hdMode ? Theme.accent : Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: page.hdMode ? qsTr("HD") : qsTr("SD")
                        color: page.hdMode ? Theme.window : Theme.text
                        font.pixelSize: 12; font.bold: true
                    }
                    HoverHandler { id: hdHover }
                    ToolTip {
                        visible: hdHover.hovered
                        delay: 500
                        x: (parent.width - width) / 2
                        y: -height - 8
                        contentItem: Text {
                            text: page.hdMode ? qsTr("Switch to SD (light scrubbing)")
                                              : qsTr("Switch to HD (full resolution)")
                            color: Theme.text; font.pixelSize: 11
                        }
                        background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: 4 }
                    }
                    TapHandler {
                        onTapped: {
                            page.hdMode = !page.hdMode;
                            // Re-play the current moment in the newly selected quality.
                            if (player.state === StreamPlayer.Streaming
                                || player.state === StreamPlayer.Connecting)
                                page.playAt(page.playheadSecs);
                        }
                    }
                }
            }
        }

        // ---- Right: calendar ----
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius

            MonthCalendar {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: Theme.spacing
                year: page.selYear
                month: page.selMonth
                selDay: page.selDay
                markedDays: page.recordingDays
                onDateSelected: (y, m, d) => {
                    page.selYear = y; page.selMonth = m; page.selDay = d;
                    page.refresh();
                }
            }
        }
    }
}
