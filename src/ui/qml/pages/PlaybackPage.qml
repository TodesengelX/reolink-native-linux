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

    function refresh() {
        if (page.deviceRow >= 0)
            Devices.searchRecordings(page.deviceRow, page.selYear, page.selMonth, page.selDay);
    }

    Connections {
        target: Devices
        function onRecordingsFound(row, segments) {
            if (row === page.deviceRow) {
                timeline.segments = segments;
                statusText.text = segments.length + qsTr(" recordings");
            }
        }
        function onRecordingsFailed(row, error) {
            if (row === page.deviceRow) {
                timeline.segments = [];
                statusText.text = error;
            }
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
                ComboBox {
                    id: deviceCombo
                    Layout.preferredWidth: 220
                    model: Devices
                    textRole: "name"
                    onCurrentIndexChanged: { page.deviceRow = currentIndex; page.refresh(); }
                    Component.onCompleted: if (Devices.count > 0) page.deviceRow = 0;
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
                StreamPlayer { id: player; videoSink: video.videoSink }

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
                    timeline.position = 32400;
                    statusText.text = "5 recordings (mock)";
                }
            }

            Timeline {
                id: timeline
                Layout.fillWidth: true
                position: 0
                onSegmentActivated: (name, seconds) => {
                    var url = Devices.playbackUrl(page.deviceRow, name);
                    if (url.length > 0) {
                        player.source = url;
                        player.start();
                    }
                }
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
                onDateSelected: (y, m, d) => {
                    page.selYear = y; page.selMonth = m; page.selDay = d;
                    page.refresh();
                }
            }
        }
    }
}
