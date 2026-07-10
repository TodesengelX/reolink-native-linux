import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// Event / Notification Center: chronological detection inbox with AI-type
// filters. Clicking an event jumps to Playback at that camera/time.
Item {
    id: page
    signal jumpToPlayback(var hostId, var timestamp)

    function iconFor(type) {
        switch (type) {
        case "person": return "🚶";
        case "vehicle": return "🚗";
        case "pet": return "🐾";
        case "visitor": return "🔔";
        default: return "👁";
        }
    }
    function labelFor(type) {
        switch (type) {
        case "person": return qsTr("Person");
        case "vehicle": return qsTr("Vehicle");
        case "pet": return qsTr("Pet");
        case "visitor": return qsTr("Visitor");
        default: return qsTr("Motion");
        }
    }

    // Clear the unread badge whenever the user is looking at this page.
    onVisibleChanged: if (visible) Events.markAllRead()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        // Filter chips
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: [
                    { key: "", label: qsTr("All") },
                    { key: "person", label: qsTr("Person") },
                    { key: "vehicle", label: qsTr("Vehicle") },
                    { key: "pet", label: qsTr("Pet") },
                    { key: "visitor", label: qsTr("Visitor") },
                    { key: "motion", label: qsTr("Motion") }
                ]
                Rectangle {
                    required property var modelData
                    property bool sel: Events.filter === modelData.key
                    implicitWidth: chipText.implicitWidth + 20
                    height: 28
                    radius: 14
                    color: sel ? Theme.accentDim : (chipHover.hovered ? Theme.surfaceAlt : Theme.surface)
                    border.color: sel ? Theme.accent : Theme.border
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: parent.modelData.label
                        color: parent.sel ? Theme.text : Theme.textMuted
                        font.pixelSize: 12
                    }
                    HoverHandler { id: chipHover }
                    TapHandler { onTapped: Events.filter = parent.modelData.key }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                implicitWidth: clearText.implicitWidth + 16
                height: 28
                radius: Theme.radius
                color: clearHover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border
                Text {
                    id: clearText
                    anchors.centerIn: parent
                    text: qsTr("Clear")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                HoverHandler { id: clearHover }
                TapHandler { onTapped: Events.clear() }
            }
        }

        // Event list
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Events
            clip: true
            spacing: 4
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                required property int index
                required property string type
                required property string camera
                required property string timeText
                required property string thumbnail
                required property var hostId
                required property var timestamp

                width: ListView.view.width
                height: 64
                radius: Theme.radius
                color: rowHover.hovered ? Theme.surfaceAlt : Theme.surface
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    // Thumbnail or type icon
                    Rectangle {
                        Layout.preferredWidth: 80
                        Layout.fillHeight: true
                        radius: 4
                        color: Theme.paneBackground
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: thumbnail.length > 0 ? "file://" + thumbnail : ""
                            fillMode: Image.PreserveAspectCrop
                            visible: thumbnail.length > 0
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: thumbnail.length === 0
                            text: page.iconFor(type)
                            font.pixelSize: 22
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Text { text: page.iconFor(type); font.pixelSize: 13 }
                            Text {
                                text: page.labelFor(type)
                                color: Theme.text
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }
                        Text { text: camera; color: Theme.textMuted; font.pixelSize: 12 }
                    }

                    Text {
                        text: timeText
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                HoverHandler { id: rowHover }
                TapHandler { onTapped: page.jumpToPlayback(hostId, timestamp) }
            }

            // Empty state
            Column {
                anchors.centerIn: parent
                visible: Events.count === 0
                spacing: 6
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "🔔"; font.pixelSize: 32
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Events.filter.length > 0 ? qsTr("No events of this type")
                                                   : qsTr("No events yet")
                    color: Theme.textMuted
                    font.pixelSize: 13
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Detections from your cameras appear here")
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
            }
        }
    }
}
