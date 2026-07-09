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
    // screen). F11 or the ⛶ buttons toggle; Esc always exits.
    property bool videoFullscreen: false
    onVideoFullscreenChanged: visibility = videoFullscreen ? Window.FullScreen : Window.Windowed

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
                PlaybackPage {}
                EventsPage {}
                DeviceSettingsPage {}
            }
        }
    }

    AddDeviceDialog {
        id: addDeviceDialog
        anchors.centerIn: parent
    }
}
