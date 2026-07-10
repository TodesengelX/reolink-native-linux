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
        var url = Devices.playbackUrl(page.deviceRow, epochAt(sec), true); // type=1
        if (url.length > 0) {
            player.source = url;
            player.start();
        }
    }

    // Called when an event is clicked in the Events inbox: jump to that exact
    // camera, date, and moment, and start playing.
    function openAt(hostId, channel, timestamp) {
        var row = Devices.rowOfHostChannel(hostId, channel);
        if (row >= 0)
            deviceCombo.currentIndex = row;
        var d = new Date(timestamp * 1000);
        page.selYear = d.getFullYear();
        page.selMonth = d.getMonth() + 1;
        page.selDay = d.getDate();
        page.refresh();
        page.playheadSecs = d.getHours() * 3600 + d.getMinutes() * 60 + d.getSeconds();
        var url = Devices.playbackUrl(row, timestamp, true);
        if (url.length > 0) {
            player.source = url;
            player.start();
        }
    }

    Connections {
        target: Devices
        function onRecordingsFound(row, segments) {
            if (row === page.deviceRow) {
                timeline.segments = segments;
                statusText.text = segments.length + qsTr(" recordings");
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
                Ctl { glyph: player.state === StreamPlayer.Streaming ? "⏸" : "▶"
                      onActivated: player.state === StreamPlayer.Streaming ? player.stop() : player.start() }
                Ctl { glyph: "⏹"; onActivated: player.stop() }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("Speed and download arrive with NVR verification");
                       color: Theme.textMuted; font.pixelSize: 11 }
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
