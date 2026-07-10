import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp

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
                    fullscreen: window.videoFullscreen
                    onFullscreenToggled: window.videoFullscreen = !window.videoFullscreen
                }
                PlaybackPage { id: playbackPage }
                EventsPage {
                    onJumpToPlayback: (hostId, timestamp) => {
                        nav.currentIndex = 1;
                        playbackPage.openAt(hostId, timestamp);
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
}
