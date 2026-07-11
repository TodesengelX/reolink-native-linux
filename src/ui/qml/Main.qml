import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

ApplicationWindow {
    id: window
    width: 1400
    height: 860
    visible: true
    title: qsTr("Reolink Client")
    color: Theme.window

    // Video fullscreen (official client: chrome disappears, grid fills the
    // screen). F11 or the ⛶ buttons toggle; Esc always exits. The pre-fullscreen
    // visibility is saved and restored so leaving fullscreen doesn't clobber a
    // maximized window.
    property bool videoFullscreen: false
    property int _preFsVisibility: Window.AutomaticVisibility
    onVideoFullscreenChanged: {
        if (videoFullscreen) {
            if (visibility !== Window.FullScreen)
                _preFsVisibility = visibility;
            visibility = Window.FullScreen;
        } else {
            visibility = _preFsVisibility === Window.FullScreen
                       ? Window.Windowed : _preFsVisibility;
        }
    }
    // Keep the flag in sync if the compositor changes visibility out from under us.
    onVisibilityChanged: () => {
        if (window.visibility !== Window.FullScreen && videoFullscreen)
            videoFullscreen = false;
    }

    Shortcut {
        sequence: "F11"
        onActivated: window.videoFullscreen = !window.videoFullscreen
    }
    Shortcut {
        sequence: "Escape"
        onActivated: if (window.videoFullscreen) window.videoFullscreen = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        NavBar {
            id: nav
            Layout.fillWidth: true
            visible: !window.videoFullscreen
            onFullscreenRequested: window.videoFullscreen = true
        }

        Rectangle { // divider
            Layout.fillWidth: true
            height: 1
            color: Theme.border
            visible: !window.videoFullscreen
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SideBar {
                Layout.fillHeight: true
                visible: !window.videoFullscreen
                onAddRequested: addDeviceDialog.open()
                // Clicking a camera jumps to Live View with it full-size.
                onDeviceClicked: (row) => {
                    nav.currentIndex = 0;
                    liveViewPage.showCamera(row);
                }
                onOpenSettings: (row) => {
                    nav.currentIndex = 3;
                    settingsPage.showDevice(row);
                }
                onCameraProperties: (row) => window.showCameraProperties(row)
                onNvrProperties: (host) => window.showNvrProperties(host)
            }

            Rectangle { // divider
                Layout.fillHeight: true
                width: 1
                color: Theme.border
                visible: !window.videoFullscreen
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: nav.currentIndex

                LiveViewPage {
                    id: liveViewPage
                    active: nav.currentIndex === 0
                    fullscreen: window.videoFullscreen
                    onFullscreenToggled: window.videoFullscreen = !window.videoFullscreen
                    onPopOut: (row, lbl) => window.openPopout(row, lbl)
                }
                PlaybackPage { id: playbackPage; active: nav.currentIndex === 1 }
                EventsPage {
                    onJumpToPlayback: (hostId, channel, timestamp) => {
                        nav.currentIndex = 1;
                        playbackPage.openAt(hostId, channel, timestamp);
                    }
                }
                DeviceSettingsPage { id: settingsPage }
            }
        }
    }

    AddDeviceDialog {
        id: addDeviceDialog
        anchors.centerIn: parent
    }

    // Doorbell visitor-press surface (raised on a "visitor" detection).
    DoorbellOverlay {
        id: doorbell
        anchors.centerIn: parent
        z: 100
        onAnswered: {
            // Answering opens the doorbell's live view; talk wires in with M12.
            active = false;
            nav.currentIndex = 0;
        }
    }
    Connections {
        target: Devices
        function onDetectionEvent(hostId, channel, type, camera) {
            if (type === "visitor") {
                doorbell.deviceRow = Devices.rowOfHost(hostId);
                doorbell.camera = camera;
                doorbell.active = true;
            }
        }
    }
    Component.onCompleted: {
        if (typeof mockDoorbell !== "undefined" && mockDoorbell) {
            doorbell.camera = "Front Door";
            doorbell.active = true;
        }
        // First run: no devices yet → open Add Device and scan the network, like
        // the official client on install. Decide here (the DB count is stable at
        // startup); don't re-check in the timer, where an NVR mid-fan-out briefly
        // reports 0 as its placeholder row is swapped for channel rows.
        if (Devices.count === 0)
            firstRunTimer.start();
    }
    // A short delay so the window is mapped before the modal + scan appear.
    Timer {
        id: firstRunTimer
        interval: 400
        onTriggered: addDeviceDialog.openAndScan()
    }

    // Device / camera "Properties" sheet, populated from the model.
    PropertiesDialog {
        id: propsDialog
        onOpenSettings: (row) => { nav.currentIndex = 3; settingsPage.showDevice(row); }
    }
    function showCameraProperties(row) {
        var c = Devices.cameraInfo(row);
        if (!c || c.name === undefined)
            return;
        var caps = [];
        if (c.capPtz) caps.push(qsTr("PTZ"));
        if (c.capZoom) caps.push(qsTr("Zoom"));
        if (c.capAudio) caps.push(qsTr("Audio"));
        if (c.capTalk) caps.push(qsTr("Two-way talk"));
        if (c.capSiren) caps.push(qsTr("Siren"));
        if (c.capFloodlight) caps.push(qsTr("Floodlight"));
        if (c.capBattery) caps.push(qsTr("Battery"));
        propsDialog.heading = c.name;
        propsDialog.subheading = c.kind === "nvr" ? qsTr("Camera on %1").arg(c.hostName)
                                                  : qsTr("Camera");
        propsDialog.rows = [
            { label: qsTr("Status"), value: c.online ? qsTr("Online") : qsTr("Offline") },
            { label: qsTr("Channel"), value: String(c.channel) },
            { label: qsTr("Main stream"),
              value: String(c.codec).toUpperCase() + (c.mainSize ? " · " + c.mainSize : "") },
            { label: qsTr("Sub stream"), value: c.subSize || "—" },
            { label: qsTr("Features"), value: caps.length ? caps.join(", ") : "—" },
            { label: qsTr("UID"), value: c.uid || "—" }
        ];
        propsDialog.targetRow = row;
        propsDialog.isAdmin = c.isAdmin;
        propsDialog.showReboot = false;
        propsDialog.showRemove = c.kind !== "nvr"; // NVR channels are removed as a whole
        propsDialog.removeLabel = qsTr("Remove device");
        propsDialog.open();
    }
    function showNvrProperties(host) {
        if (!host || host.name === undefined)
            return;
        propsDialog.heading = host.name;
        propsDialog.subheading = host.kind === "nvr" ? qsTr("Reolink NVR") : qsTr("Camera");
        propsDialog.rows = [
            { label: qsTr("Model"), value: host.model || "—" },
            { label: qsTr("Address"),
              value: host.addr + ":" + host.port + (host.https ? qsTr(" (HTTPS)") : "") },
            { label: qsTr("Account"), value: host.username + (host.isAdmin ? qsTr(" · admin") : "") },
            { label: qsTr("Cameras"),
              value: host.onlineCount + " / " + host.channelCount + qsTr(" online") }
        ];
        propsDialog.targetRow = host.firstRow;
        propsDialog.isAdmin = host.isAdmin;
        propsDialog.showReboot = true;
        propsDialog.showRemove = true;
        propsDialog.removeLabel = host.kind === "nvr" ? qsTr("Remove NVR") : qsTr("Remove device");
        propsDialog.open();
    }

    // Detached camera windows for multi-monitor viewing.
    Component { id: popoutComponent; PoppedWindow {} }
    function openPopout(row, label) {
        popoutComponent.createObject(window, { deviceRow: row, label: label });
    }

    // Transient status toast for one-shot camera actions (snapshot, siren,
    // floodlight). These fire on the device but gave no on-screen confirmation,
    // so they felt like dead buttons; this surfaces success/failure.
    Rectangle {
        id: toast
        z: 300
        property bool isError: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        radius: Theme.radius
        color: isError ? Theme.danger : Theme.surfaceAlt
        border.color: isError ? Theme.danger : Theme.border
        width: Math.min(toastLabel.implicitWidth + 28, window.width - 80)
        height: toastLabel.implicitHeight + 18
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Text {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, window.width - 108)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: 13
        }
        Timer { id: toastTimer; interval: 2600; onTriggered: toast.opacity = 0 }
        function show(msg, err) {
            toastLabel.text = msg;
            toast.isError = err === true;
            toast.opacity = 1.0;
            toastTimer.restart();
        }
    }
    Connections {
        target: Devices
        function onSnapshotSaved(row, path) { toast.show(qsTr("Snapshot saved"), false); }
        function onSnapshotFailed(row, error) { toast.show(qsTr("Snapshot failed: %1").arg(error), true); }
        function onSettingApplied(row, command, ok, error) {
            if (command === "AudioAlarmPlay")
                toast.show(ok ? qsTr("Siren triggered") : qsTr("Siren failed: %1").arg(error), !ok);
            else if (command === "SetWhiteLed")
                toast.show(ok ? qsTr("Floodlight toggled") : qsTr("Floodlight failed: %1").arg(error), !ok);
            else if (!ok)
                toast.show(qsTr("%1 failed: %2").arg(command).arg(error), true);
        }
    }
}
