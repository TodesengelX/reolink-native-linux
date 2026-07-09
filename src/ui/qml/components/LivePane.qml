import QtQuick
import QtQuick.Controls
import QtMultimedia
import ReolinkApp
import ReolinkApp.Core

// One cell of the live grid: video + name overlay + connection-state overlay.
// Double-click toggles maximize (handled by the page).
Rectangle {
    id: root
    color: Theme.paneBackground
    border.color: selected ? Theme.accent : Theme.border
    border.width: 1

    property int paneIndex: -1
    property string sourceUrl: ""
    property string label: ""
    property bool selected: false
    readonly property bool hasSource: sourceUrl.length > 0

    signal toggleMaximize(int index)
    signal clicked(int index)

    StreamPlayer {
        id: player
        videoSink: video.videoSink
        // Loop non-live sources so file-based test streams keep playing.
        loop: !root.sourceUrl.startsWith("rtsp://") && !root.sourceUrl.startsWith("rtmp://")
    }

    // The onSourceUrlChanged handler fires BEFORE any declarative `source:` binding
    // would re-evaluate, so assign imperatively then start — otherwise an
    // empty→URL transition (restore, preset grow, tab return) would start with a
    // still-empty source and the pane would stay dead.
    onSourceUrlChanged: {
        if (root.hasSource) {
            player.source = root.sourceUrl;
            player.start();
        } else {
            player.stop();
        }
    }
    Component.onCompleted: if (root.hasSource) { player.source = root.sourceUrl; player.start(); }
    Component.onDestruction: player.stop()

    VideoOutput {
        id: video
        anchors.fill: parent
        anchors.margins: 1
        fillMode: VideoOutput.PreserveAspectFit
        visible: player.state === StreamPlayer.Streaming
    }

    Rectangle { // name overlay
        visible: root.hasSource && player.state === StreamPlayer.Streaming
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 6
        radius: 3
        color: "#80000000"
        width: nameText.implicitWidth + 12
        height: nameText.implicitHeight + 6
        Text {
            id: nameText
            anchors.centerIn: parent
            text: root.label
            color: "white"
            font.pixelSize: 11
        }
    }

    Column { // state overlay
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
                if (!root.hasSource)
                    return qsTr("No camera");
                switch (player.state) {
                case StreamPlayer.Connecting: return qsTr("Connecting…");
                case StreamPlayer.Error: return player.errorString;
                case StreamPlayer.Stopped: return qsTr("Stopped");
                default: return "";
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked(root.paneIndex)
        onDoubleClicked: if (root.hasSource) root.toggleMaximize(root.paneIndex)
    }
}
