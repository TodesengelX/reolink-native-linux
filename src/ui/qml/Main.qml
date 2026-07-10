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
                    active: nav.currentIndex === 0
                    fullscreen: window.videoFullscreen
                    onFullscreenToggled: window.videoFullscreen = !window.videoFullscreen
                    onPopOut: (row, lbl) => window.openPopout(row, lbl)
                }
                PlaybackPage { id: playbackPage }
                EventsPage {
                    onJumpToPlayback: (hostId, channel, timestamp) => {
                        nav.currentIndex = 1;
                        playbackPage.openAt(hostId, channel, timestamp);
                    }
                }
                DeviceSettingsPage {}
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
        // First run: no devices yet → open Add Device and scan the network,
        // just like the official client does on install.
        if (Devices.count === 0)
            firstRunTimer.start();
    }
    // A short delay so the window is mapped before the modal + scan appear.
    Timer {
        id: firstRunTimer
        interval: 400
        onTriggered: if (Devices.count === 0) addDeviceDialog.openAndScan()
    }

    // Detached camera windows for multi-monitor viewing.
    Component { id: popoutComponent; PoppedWindow {} }
    function openPopout(row, label) {
        popoutComponent.createObject(window, { deviceRow: row, label: label });
    }
}
