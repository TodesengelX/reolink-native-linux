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
        player.loop = false;
        statusText.text = qsTr("HD");
        Devices.startBaichuanPlayback(page.deviceRow, epochAt(sec), player, true);
    }

    property real _pendingPlayEpoch: 0  // play this once the day's search returns

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
                    var url = Devices.playbackUrl(page.deviceRow, ep, false); // sub stream
                    if (url.length > 0) {
                        player.expectedSize = Devices.declaredSize(page.deviceRow, false);
                        player.source = url;
                        player.start();
                    }
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
                    onCurrentIndexChanged: { page.deviceRow = currentIndex; page.refresh(); }
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
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.paneBackground
                border.color: Theme.border

                VideoOutput {
                    id: video
                    anchors.fill: parent
                    anchors.margins: 1
                    fillMode: VideoOutput.PreserveAspectFit
                    visible: player.state === StreamPlayer.Streaming
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
                    signal activated()
                    width: 34; height: 30; radius: Theme.radius
                    color: cHover.hovered ? Theme.surfaceAlt : Theme.surface
                    border.color: Theme.border
                    Text { anchors.centerIn: parent; text: parent.glyph; color: Theme.text; font.pixelSize: 14 }
                    HoverHandler { id: cHover }
                    TapHandler { onTapped: parent.activated() }
                }
                // Play/pause resumes at the current playhead in the active quality
                // (a StreamPlayer session is one-shot, so "play" re-opens the stream).
                Ctl { glyph: player.state === StreamPlayer.Streaming ? "⏸" : "▶"
                      onActivated: player.state === StreamPlayer.Streaming
                                   ? player.stop() : page.playAt(page.playheadSecs) }
                Ctl { glyph: "⏹"; onActivated: player.stop() }
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
