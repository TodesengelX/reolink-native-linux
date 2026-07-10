import QtQuick
import QtMultimedia
import ReolinkApp

// Wraps a video sink and applies the fisheye dewarp shader. The VideoOutput is
// captured off-screen into a ShaderEffectSource and re-rendered through the
// dewarp fragment shader. Pan/tilt/fov are interactive; strength 0 shows the raw
// fisheye. Used for 360/panoramic cameras (DESIGN §5.2).
Item {
    id: root
    property alias videoSink: video.videoSink
    property real pan: 0.0
    property real tilt: 0.0
    property real fov: 1.4
    property real strength: 1.0

    VideoOutput {
        id: video
        anchors.fill: parent
        fillMode: VideoOutput.Stretch
        visible: false
    }

    ShaderEffectSource {
        id: capture
        sourceItem: video
        hideSource: true
        live: true
        anchors.fill: parent
        visible: false
    }

    ShaderEffect {
        anchors.fill: parent
        property variant source: capture
        property real pan: root.pan
        property real tilt: root.tilt
        property real fov: root.fov
        property real strength: root.strength
        fragmentShader: "qrc:/qt/qml/ReolinkApp/shaders/dewarp.frag.qsb"
    }

    // Drag to pan/tilt the dewarped view.
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        property real lastY: 0
        onPressed: (m) => { lastX = m.x; lastY = m.y; }
        onPositionChanged: (m) => {
            root.pan += (m.x - lastX) * 0.005;
            root.tilt = Math.max(-1.2, Math.min(1.2, root.tilt + (m.y - lastY) * 0.005));
            lastX = m.x; lastY = m.y;
        }
        onWheel: (w) => {
            root.fov = Math.max(0.5, Math.min(2.6, root.fov * (w.angleDelta.y > 0 ? 0.92 : 1.08)));
        }
    }
}
