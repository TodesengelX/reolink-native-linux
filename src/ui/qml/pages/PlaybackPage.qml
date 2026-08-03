import QtCore
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

    // ---- Synced multi-camera playback ---------------------------------------
    // Like the official client: up to 4 panes, one calendar and one timeline
    // driving all of them, forced to the sub stream (the device's published
    // budget is 10 sub + 2 main, and its spec caps synchronous playback at 4).
    // Sync is one shared playhead asking every camera for the same wall-clock
    // moment — the protocol has no cross-stream sync mechanism.
    property int paneCount: 1
    property var gridRows: [-1, -1, -1, -1]   // pane -> device row
    property var gridSegs: ({})               // device row -> that day's segments
    property int streamingPanes: 0            // recount kept by the panes

    // Stored as hostId:channel — rows shift when devices are added or removed.
    Settings {
        id: pbStore
        category: "playback"
        property string gridArrangement: ""
    }

    function gridPane(i) { return paneRepeater.itemAt(i); }
    function stopAllPanes() {
        for (var i = 0; i < 4; i++) {
            var p = gridPane(i);
            if (p) p.stopPlayback();
        }
    }
    function countStreaming() {
        var n = 0;
        for (var i = 0; i < 4; i++) {
            var p = gridPane(i);
            if (p && p.streaming) n++;
        }
        page.streamingPanes = n;
    }

    // The union track lets you scrub to anything any pane has; each lane shows
    // who actually has footage where. Overlapping segments simply overdraw on
    // the union track, so no interval merging is needed.
    function recomputeGrid() {
        var lanes = [];
        var union = [];
        for (var i = 0; i < 4; i++) {
            var r = page.gridRows[i];
            if (r === undefined || r < 0)
                continue;
            var segs = page.gridSegs[r] || [];
            var info = Devices.cameraInfo(r);
            lanes.push({ name: info && info.name ? info.name : qsTr("Camera %1").arg(i + 1),
                         segments: segs });
            union = union.concat(segs);
        }
        timeline.lanes = lanes;
        timeline.segments = union;
        statusText.text = qsTr("Synced playback — %n camera(s)", "", lanes.length);
    }

    function saveGridLayout() {
        var out = [];
        for (var i = 0; i < 4; i++) {
            var r = page.gridRows[i];
            if (r === undefined || r < 0) { out.push("-"); continue; }
            var c = Devices.cameraInfo(r);
            out.push(c && c.hostId !== undefined ? c.hostId + ":" + c.channel : "-");
        }
        pbStore.gridArrangement = out.join(",");
    }

    // Fill the panes: the saved arrangement where it still matches a device,
    // else the first cameras in sidebar order.
    function initGridRows() {
        var byKey = ({});
        for (var r = 0; r < Devices.count; ++r) {
            var c = Devices.cameraInfo(r);
            if (c && c.hostId !== undefined)
                byKey[c.hostId + ":" + c.channel] = r;
        }
        var parts = pbStore.gridArrangement.split(",");
        var g = [];
        var used = ({});
        for (var i = 0; i < 4; i++) {
            var key = i < parts.length ? parts[i] : "-";
            var row = (key !== "-" && byKey[key] !== undefined) ? byKey[key] : -1;
            if (row >= 0 && used[row]) row = -1;
            if (row >= 0) used[row] = true;
            g.push(row);
        }
        var free = [];
        for (r = 0; r < Devices.count; ++r)
            if (!used[r]) free.push(r);
        for (i = 0; i < 4 && free.length > 0; i++)
            if (g[i] < 0) { g[i] = free.shift(); }
        page.gridRows = g;
    }

    // Pane combo picked a camera. If it's already on the grid the panes swap,
    // so a choice never silently drops a camera from the layout.
    function assignGridPane(pane, row) {
        var g = page.gridRows.slice();
        var prev = g.indexOf(row);
        if (prev >= 0 && prev !== pane)
            g[prev] = g[pane];
        g[pane] = row;
        page.gridRows = g;
        saveGridLayout();
        page.refresh();
    }

    function setPaneCount(n) {
        if (n === page.paneCount)
            return;
        if (n === 4) {
            player.stop();
            page.paneCount = 4;
            page.initGridRows();
            // Mock hook: seed staggered segments per pane so the grid + lanes
            // render without an NVR (RL_MOCK_RECORDINGS + RL_PLAYBACK_GRID).
            if (typeof mockRecordings !== "undefined" && mockRecordings) {
                var base = timeline.segments.length > 0 ? timeline.segments : [];
                var s = ({});
                for (var i = 0; i < 4; i++) {
                    var r = page.gridRows[i];
                    if (r === undefined || r < 0) continue;
                    s[r] = base.map(function (seg) {
                        return { start: Math.min(86399, seg.start + i * 1800),
                                 end: Math.min(86399, seg.end + i * 1800),
                                 type: seg.type };
                    });
                }
                page.gridSegs = s;
                page.recomputeGrid();
            } else {
                page.refresh();
            }
        } else {
            stopAllPanes();
            page.paneCount = 1;
            timeline.lanes = [];
            timeline.segments = [];
            page.refresh();
        }
    }

    // False when the Playback page isn't on screen — stop streaming so a Baichuan
    // session (or FLV stream) isn't left running on the connection-limited NVR.
    property bool active: true
    onActiveChanged: if (!active) { player.stop(); stopAllPanes(); }

    // Advance the playhead in realtime while streaming, so the timeline cursor
    // tracks the current position (and play/pause resumes from where you are).
    Timer {
        interval: 1000; repeat: true
        running: player.state === StreamPlayer.Streaming || page.streamingPanes > 0
        onTriggered: if (page.playheadSecs < 86399) page.playheadSecs += 1
    }

    function refresh() {
        if (page.paneCount === 4) {
            // One Search per distinct grid camera. These queue behind the
            // per-device request lock in ReolinkHttpClient rather than racing —
            // concurrent api.cgi commands are what 502 the NVR's web server.
            var done = ({});
            for (var i = 0; i < 4; i++) {
                var r = page.gridRows[i];
                if (r === undefined || r < 0 || done[r]) continue;
                done[r] = true;
                Devices.searchRecordings(r, page.selYear, page.selMonth, page.selDay);
            }
            return;
        }
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
        if (page.paneCount === 4) {
            // One commit fans out to every pane at the same wall-clock moment;
            // each pane answers from its own footage (play, or "no footage").
            var epoch = epochAt(sec);
            for (var i = 0; i < 4; i++) {
                var p = gridPane(i);
                if (p) p.playAtSecs(sec, epoch);
            }
            return;
        }
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
    // Export a clip starting at the playhead (NVR downloads run below
    // realtime, so longer clips take a while — the button shows progress).
    function exportClip(secs) {
        var epoch = new Date(page.selYear, page.selMonth - 1, page.selDay).getTime() / 1000
                    + Math.floor(page.playheadSecs);
        exportBtn.busy = true;
        statusText.text = qsTr("Exporting clip…");
        Devices.exportClip(page.deviceRow, epoch, secs);
    }

    function openAt(hostId, channel, timestamp) {
        // Event jump targets one specific camera and moment — single-pane flow.
        setPaneCount(1);
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
        function onClipExported(row, path) {
            if (row !== page.deviceRow) return;
            exportBtn.busy = false;
            statusText.text = qsTr("Clip saved: %1").arg(path);
        }
        function onClipExportFailed(row, error) {
            if (row !== page.deviceRow) return;
            exportBtn.busy = false;
            statusText.text = qsTr("Export failed: %1").arg(error);
        }
        function onRecordingsFound(row, segments) {
            if (page.paneCount === 4) {
                if (page.gridRows.indexOf(row) < 0)
                    return;
                var s = Object.assign({}, page.gridSegs);
                s[row] = segments;
                page.gridSegs = s;   // wholesale reassign so pane bindings re-evaluate
                page.recomputeGrid();
                return;
            }
            if (row === page.deviceRow) {
                timeline.segments = segments;
                statusText.text = segments.length + qsTr(" recordings");
                // Overlay this day's detection events as timeline markers.
                var info = Devices.cameraInfo(page.deviceRow);
                if (info && info.hostId !== undefined) {
                    var dayStart = new Date(page.selYear, page.selMonth - 1, page.selDay).getTime() / 1000;
                    timeline.alarmTicks = Events.eventTimesFor(info.hostId, info.channel, dayStart);
                }
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
            // In grid mode the calendar follows the first pane's camera.
            var calRow = page.paneCount === 4 ? page.gridRows[0] : page.deviceRow;
            if (row === calRow && year === page.selYear && month === page.selMonth)
                page.recordingDays = days;
        }
        function onRecordingsFailed(row, error) {
            if (page.paneCount === 4) {
                if (page.gridRows.indexOf(row) < 0)
                    return;
                // An empty lane is the honest render of a failed search; the
                // status line carries the reason.
                var s = Object.assign({}, page.gridSegs);
                s[row] = [];
                page.gridSegs = s;
                page.recomputeGrid();
                statusText.text = error;
                return;
            }
            if (row === page.deviceRow) {
                timeline.segments = [];
                statusText.text = error;
            }
        }
        // Re-fetch once the selected device finishes connecting (its client
        // isn't primed at page-load time, so the initial fetch returns empty).
        function onDataChanged(topLeft, bottomRight) {
            if (page.paneCount === 4) {
                for (var i = 0; i < 4; i++) {
                    var r = page.gridRows[i];
                    if (r >= topLeft.row && r <= bottomRight.row
                        && page.gridSegs[r] === undefined) {
                        page.refresh();
                        return;
                    }
                }
                return;
            }
            if (page.deviceRow >= topLeft.row && page.deviceRow <= bottomRight.row
                && timeline.segments.length === 0)
                page.refresh();
        }
        // Rows shift when devices come or go; the arrangement tracks cameras,
        // not indices, so rebuild it from the store rather than patching.
        function onRowsRemoved() {
            if (page.paneCount === 4) { page.gridSegs = ({}); page.initGridRows(); page.refresh(); }
        }
        function onRowsInserted() {
            if (page.paneCount === 4) { page.gridSegs = ({}); page.initGridRows(); page.refresh(); }
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
                Text { visible: page.paneCount === 1
                       text: qsTr("Camera:"); color: Theme.textMuted; font.pixelSize: 12 }
                CameraComboBox {
                    id: deviceCombo
                    visible: page.paneCount === 1   // each grid pane has its own
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
                // 1 / 4 pane layout — like the official client's split playback.
                Repeater {
                    model: [1, 4]
                    Rectangle {
                        required property int modelData
                        width: 34; height: 26; radius: Theme.radius
                        color: page.paneCount === modelData ? Theme.accentDim
                             : pcHover.hovered ? Theme.surfaceAlt : Theme.surface
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData
                            color: page.paneCount === parent.modelData ? Theme.text : Theme.textMuted
                            font.pixelSize: 12
                        }
                        HoverHandler { id: pcHover }
                        TapHandler { onTapped: page.setPaneCount(parent.modelData) }
                        ToolTip {
                            visible: pcHover.hovered
                            delay: 500
                            x: (parent.width - width) / 2
                            y: parent.height + 6
                            contentItem: Text {
                                text: modelData === 1 ? qsTr("Single camera")
                                    : qsTr("Synced 4-camera grid")
                                color: Theme.text; font.pixelSize: 11
                            }
                            background: Rectangle { color: Theme.surfaceAlt
                                                    border.color: Theme.border; radius: 4 }
                        }
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
                visible: page.paneCount === 1
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
                        // Pin the zoomed footage to the viewport edges (no panning
                        // it off into empty space; zooming out reels it back in).
                        function clampPan() {
                            var mx = Math.max(0, (video.contentRect.width * videoBox.zoom - video.width) / 2);
                            var my = Math.max(0, (video.contentRect.height * videoBox.zoom - video.height) / 2);
                            videoBox.panX = Math.max(-mx, Math.min(mx, videoBox.panX));
                            videoBox.panY = Math.max(-my, Math.min(my, videoBox.panY));
                        }
                        onPressed: (m) => { lastX = m.x; lastY = m.y; }
                        onPositionChanged: (m) => {
                            if (videoBox.zoom > 1.0 && (m.buttons & Qt.LeftButton)) {
                                videoBox.panX += (m.x - lastX);
                                videoBox.panY += (m.y - lastY);
                                lastX = m.x; lastY = m.y;
                                clampPan();
                            }
                        }
                        onWheel: (w) => {
                            var z = videoBox.zoom * (w.angleDelta.y > 0 ? 1.15 : 0.87);
                            videoBox.zoom = Math.max(1.0, Math.min(8.0, z));
                            if (videoBox.zoom <= 1.0) { videoBox.panX = 0; videoBox.panY = 0; }
                            else clampPan();
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

            // ---- Synced 4-camera grid (paneCount === 4) --------------------
            Item {
                id: gridBox
                visible: page.paneCount === 4
                Layout.fillWidth: true
                Layout.fillHeight: true

                Grid {
                    anchors.fill: parent
                    columns: 2
                    spacing: 4
                    Repeater {
                        id: paneRepeater
                        model: 4
                        PlaybackPane {
                            required property int index
                            width: (gridBox.width - 4) / 2
                            height: (gridBox.height - 4) / 2
                            deviceRow: page.gridRows[index] !== undefined ? page.gridRows[index] : -1
                            label: {
                                if (deviceRow < 0) return "";
                                var c = Devices.cameraInfo(deviceRow);
                                return c && c.name ? c.name : "";
                            }
                            segments: page.gridSegs[deviceRow] || []
                            playheadSecs: page.playheadSecs
                            onStreamingChanged: page.countStreaming()
                            onCameraRequested: (row) => page.assignGridPane(index, row)
                        }
                    }
                }
            }

            // Test hook: RL_MOCK_RECORDINGS seeds the timeline so its two-tone
            // rendering can be verified without a real NVR.
            Component.onCompleted: {
                if (typeof playbackGrid !== "undefined" && playbackGrid)
                    Qt.callLater(function () { page.setPaneCount(4); });
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
                // In grid mode the one control drives every pane.
                Ctl {
                    readonly property bool playing: page.paneCount === 4
                        ? page.streamingPanes > 0
                        : player.state === StreamPlayer.Streaming
                    glyph: playing ? "⏸" : "▶"
                    tip: playing ? qsTr("Pause") : qsTr("Play")
                    onActivated: {
                        if (playing) {
                            if (page.paneCount === 4) page.stopAllPanes();
                            else player.stop();
                        } else {
                            page.playAt(page.playheadSecs);
                        }
                    }
                }
                Ctl { glyph: "⏹"; tip: qsTr("Stop")
                      onActivated: { player.stop(); page.stopAllPanes(); } }
                // Export: save a main-stream MP4 of the moment at the playhead.
                // Single-pane only — it targets THE camera, and grid mode has four.
                Rectangle {
                    id: exportBtn
                    visible: page.paneCount === 1
                    property bool busy: false
                    width: expRow.implicitWidth + 18; height: 30; radius: Theme.radius
                    color: expHover.hovered && !busy ? Theme.surfaceAlt : Theme.surface
                    border.color: Theme.border
                    Row {
                        id: expRow
                        anchors.centerIn: parent
                        spacing: 5
                        BusyIndicator { visible: exportBtn.busy; running: visible
                                        width: 14; height: 14
                                        anchors.verticalCenter: parent.verticalCenter }
                        Text { text: exportBtn.busy ? qsTr("Exporting…") : qsTr("Export clip")
                               color: Theme.text; font.pixelSize: 12
                               anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: !exportBtn.busy; text: "\u25be"; color: Theme.textMuted
                               font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                    }
                    HoverHandler { id: expHover }
                    TapHandler { onTapped: if (!exportBtn.busy) exportMenu.popup() }
                    ThemedMenu {
                        id: exportMenu
                        ThemedMenuItem { text: qsTr("15 seconds"); onTriggered: page.exportClip(15) }
                        ThemedMenuItem { text: qsTr("30 seconds"); onTriggered: page.exportClip(30) }
                        ThemedMenuItem { text: qsTr("1 minute");  onTriggered: page.exportClip(60) }
                        ThemedMenuItem { text: qsTr("2 minutes"); onTriggered: page.exportClip(120) }
                    }
                }
                Item { Layout.fillWidth: true }
                // Quality toggle: SD = light sub-stream (FLV) scrubbing; HD = full-res
                // main stream over native Baichuan. Grid mode is forced to the sub
                // stream (as in the official client): the device budget allows only
                // 2 concurrent main streams, and a 4-pane grid would blow past it.
                Rectangle {
                    visible: page.paneCount === 1
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
