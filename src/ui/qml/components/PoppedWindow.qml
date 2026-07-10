import QtQuick
import QtQuick.Window
import QtMultimedia
import ReolinkApp
import ReolinkApp.Core

// A detached live-view window for one camera — drag it to a second monitor for
// multi-screen viewing (DESIGN §6.2). Plays the main ("Clear") stream. Closing
// stops the player; Esc closes.
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

    VideoOutput {
        id: video
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        visible: player.state === StreamPlayer.Streaming
    }
    StreamPlayer {
        id: player
        videoSink: video.videoSink
    }
    Component.onCompleted: {
        var url = Devices.liveUrl(win.deviceRow, true); // main stream in a big window
        if (url.length > 0) {
            player.source = url;
            player.start();
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
        width: nm.implicitWidth + 12; height: nm.implicitHeight + 6
        Text { id: nm; anchors.centerIn: parent; text: win.label; color: "white"; font.pixelSize: 12 }
    }

    Shortcut { sequence: "Escape"; onActivated: win.close() }
}
