import QtQuick
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// The live grid: 1/4/9/16 presets, double-click a pane to maximize/restore,
// fullscreen hides all chrome (handled by Main).
Item {
    id: page

    property bool fullscreen: false
    signal fullscreenToggled()
    signal popOut(int deviceRow, string label)

    property int preset: 4
    property int maximizedIndex: -1
    property int selectedIndex: -1
    readonly property int cols: preset === 1 ? 1 : preset === 4 ? 2 : preset === 9 ? 3 : 4

    // Removing a device shifts row indices; keep maximized/selected pointing at the
    // right pane (or clear them) so the grid never blanks or maximizes the wrong cam.
    Connections {
        target: Devices
        function onRowsRemoved(parent, first, last) {
            const removed = last - first + 1;
            if (page.maximizedIndex > last)
                page.maximizedIndex -= removed;
            else if (page.maximizedIndex >= first)
                page.maximizedIndex = -1;
            if (page.selectedIndex > last)
                page.selectedIndex -= removed;
            else if (page.selectedIndex >= first)
                page.selectedIndex = -1;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: page.fullscreen ? 0 : Theme.spacing
        spacing: Theme.spacing

        RowLayout { // grid toolbar
            Layout.fillWidth: true
            spacing: Theme.spacing / 2
            visible: !page.fullscreen

            Repeater {
                model: [1, 4, 9, 16]
                Rectangle {
                    required property int modelData
                    width: 34
                    height: 26
                    radius: Theme.radius
                    color: page.preset === modelData ? Theme.accentDim
                         : presetArea.containsMouse ? Theme.surfaceAlt : Theme.surface
                    border.color: Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData
                        color: page.preset === parent.modelData ? Theme.text : Theme.textMuted
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: presetArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            page.preset = parent.modelData;
                            page.maximizedIndex = -1;
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 34
                height: 26
                radius: Theme.radius
                color: fsArea.containsMouse ? Theme.surfaceAlt : Theme.surface
                border.color: Theme.border
                Text {
                    anchors.centerIn: parent
                    text: "⛶"
                    color: Theme.textMuted
                    font.pixelSize: 13
                }
                MouseArea {
                    id: fsArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.fullscreenToggled()
                }
            }
        }

        Item {
            id: gridArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property real cellWidth:
                (width - grid.spacing * (page.cols - 1)) / page.cols
            readonly property real cellHeight:
                (height - grid.spacing * (page.cols - 1)) / page.cols

            Grid {
                id: grid
                anchors.fill: parent
                columns: page.maximizedIndex >= 0 ? 1 : page.cols
                spacing: 4

                // Device-backed panes (model gives live name/status/caps updates).
                Repeater {
                    model: Devices
                    LivePane {
                        required property int index
                        required property string name
                        required property bool hasPtz
                        required property bool hasZoom
                        required property bool hasAudio
                        required property bool hasSiren
                        required property bool hasFloodlight
                        required property bool hasTalk

                        visible: index < page.preset &&
                                 (page.maximizedIndex === -1 || page.maximizedIndex === index)
                        width: page.maximizedIndex === index ? gridArea.width : gridArea.cellWidth
                        height: page.maximizedIndex === index ? gridArea.height
                                                              : gridArea.cellHeight
                        paneIndex: index
                        deviceRow: index
                        label: name
                        selected: page.selectedIndex === index
                        // Sub-stream in the grid, main stream when maximized (DESIGN §5.7).
                        forceMain: page.maximizedIndex === index
                        capPtz: hasPtz
                        capZoom: hasZoom
                        capAudio: hasAudio
                        capSiren: hasSiren
                        capFloodlight: hasFloodlight
                        capTalk: hasTalk
                        onToggleMaximize: (idx) => {
                            page.maximizedIndex = page.maximizedIndex === idx ? -1 : idx;
                        }
                        onClicked: (idx) => page.selectedIndex = idx
                        onPopOut: (row, lbl) => page.popOut(row, lbl)
                    }
                }

                // Empty slots up to the preset size.
                Repeater {
                    model: Math.max(0, page.preset - Devices.count)
                    LivePane {
                        visible: page.maximizedIndex === -1
                        width: gridArea.cellWidth
                        height: gridArea.cellHeight
                    }
                }
            }
        }
    }
}
