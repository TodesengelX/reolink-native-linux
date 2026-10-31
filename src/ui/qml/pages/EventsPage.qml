import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// Event / Notification Center: chronological detection inbox with AI-type
// filters. Content is a centered, max-width column so cards stay readable on
// wide windows. Clicking an event jumps to Playback at that camera/time.
Item {
    id: page
    signal jumpToPlayback(var hostId, var channel, var timestamp)

    readonly property int columnWidth: Math.min(width - Theme.spacing * 4, 820)

    function iconFor(type) {
        switch (type) {
        case "person": return "🚶";
        case "vehicle": return "🚗";
        case "pet": return "🐾";
        case "visitor": return "🔔";
        case "offline": return "⚠";
        case "online": return "✔";
        default: return "👁";
        }
    }
    function labelFor(type) {
        switch (type) {
        case "person": return qsTr("Person");
        case "vehicle": return qsTr("Vehicle");
        case "pet": return qsTr("Pet");
        case "visitor": return qsTr("Visitor");
        case "offline": return qsTr("Went offline");
        case "online": return qsTr("Back online");
        default: return qsTr("Motion");
        }
    }
    function colorFor(type) {
        switch (type) {
        case "person": return Theme.accent;
        case "vehicle": return "#e08a3c";
        case "pet": return Theme.online;
        case "visitor": return Theme.danger;
        case "offline": return Theme.danger;
        case "online": return Theme.online;
        default: return Theme.textMuted;
        }
    }

    onVisibleChanged: if (visible) Events.markAllRead()

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.spacing * 2
        anchors.bottomMargin: Theme.spacing
        width: page.columnWidth
        spacing: Theme.spacing

        // ---- Header ----
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Events")
                color: Theme.text
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                text: Events.count > 0 ? Events.count : ""
                color: Theme.textMuted
                font.pixelSize: 13
                Layout.alignment: Qt.AlignBottom
                bottomPadding: 2
            }
            Item { Layout.fillWidth: true }
            // Inbox cleanup settings (merge repeats, motion, auto-delete).
            Rectangle {
                id: cleanupBtn
                implicitWidth: cleanupText.implicitWidth + 20
                height: 30
                radius: Theme.radius
                color: cleanupPopup.opened || cleanupHover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border
                Text {
                    id: cleanupText
                    anchors.centerIn: parent
                    text: qsTr("Cleanup \u25BE")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                HoverHandler { id: cleanupHover }
                TapHandler { onTapped: cleanupPopup.opened ? cleanupPopup.close() : cleanupPopup.open() }

                Popup {
                    id: cleanupPopup
                    y: parent.height + 6
                    x: parent.width - width
                    padding: 14
                    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }

                    // Choices as [label, value] pairs; value 0 means off / never.
                    readonly property var mergeChoices: [[qsTr("Off"), 0], [qsTr("30 seconds"), 30],
                        [qsTr("1 minute"), 60], [qsTr("2 minutes"), 120], [qsTr("5 minutes"), 300],
                        [qsTr("10 minutes"), 600]]
                    readonly property var keepChoices: [[qsTr("1 day"), 1], [qsTr("3 days"), 3],
                        [qsTr("7 days"), 7], [qsTr("14 days"), 14], [qsTr("30 days"), 30],
                        [qsTr("Never delete"), 0]]
                    function indexOf(choices, value) {
                        for (var i = 0; i < choices.length; ++i)
                            if (choices[i][1] === value) return i;
                        return -1;
                    }

                    contentItem: ColumnLayout {
                        spacing: 10
                        Text {
                            text: qsTr("Event cleanup")
                            color: Theme.text; font.pixelSize: 13; font.bold: true
                        }
                        RowLayout {
                            spacing: 10
                            ColumnLayout {
                                spacing: 1
                                Layout.preferredWidth: 190
                                Text { text: qsTr("Combine repeats within"); color: Theme.text; font.pixelSize: 12 }
                                Text {
                                    text: qsTr("Ongoing activity on a camera stays one event until it's been quiet this long.")
                                    color: Theme.textMuted; font.pixelSize: 10
                                    wrapMode: Text.WordWrap; Layout.maximumWidth: 190
                                }
                            }
                            ComboBox {
                                Layout.preferredWidth: 150
                                model: cleanupPopup.mergeChoices.map(c => c[0])
                                currentIndex: Math.max(0, cleanupPopup.indexOf(cleanupPopup.mergeChoices, Events.mergeSeconds))
                                onActivated: (i) => Events.mergeSeconds = cleanupPopup.mergeChoices[i][1]
                            }
                        }
                        RowLayout {
                            spacing: 10
                            ColumnLayout {
                                spacing: 1
                                Layout.preferredWidth: 190
                                Text { text: qsTr("Motion-only events"); color: Theme.text; font.pixelSize: 12 }
                                Text {
                                    text: qsTr("Off keeps only people, vehicles, animals and visitors.")
                                    color: Theme.textMuted; font.pixelSize: 10
                                    wrapMode: Text.WordWrap; Layout.maximumWidth: 190
                                }
                            }
                            Switch {
                                checked: Events.motionEvents
                                onToggled: Events.motionEvents = checked
                            }
                        }
                        RowLayout {
                            spacing: 10
                            ColumnLayout {
                                spacing: 1
                                Layout.preferredWidth: 190
                                Text { text: qsTr("Delete events after"); color: Theme.text; font.pixelSize: 12 }
                                Text {
                                    text: qsTr("Older events and their snapshots are removed automatically.")
                                    color: Theme.textMuted; font.pixelSize: 10
                                    wrapMode: Text.WordWrap; Layout.maximumWidth: 190
                                }
                            }
                            ComboBox {
                                Layout.preferredWidth: 150
                                model: cleanupPopup.keepChoices.map(c => c[0])
                                currentIndex: Math.max(0, cleanupPopup.indexOf(cleanupPopup.keepChoices, Events.retentionDays))
                                onActivated: (i) => Events.retentionDays = cleanupPopup.keepChoices[i][1]
                            }
                        }
                    }
                }
            }
            // Desktop pop-ups on/off (events are still recorded here either way).
            Rectangle {
                implicitWidth: popupsText.implicitWidth + 20
                height: 30
                radius: Theme.radius
                color: Events.popupsEnabled ? Theme.accentDim
                     : popupsHover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Events.popupsEnabled ? Theme.accent : Theme.border
                Text {
                    id: popupsText
                    anchors.centerIn: parent
                    text: Events.popupsEnabled ? qsTr("✓ Pop-ups on") : qsTr("Pop-ups off")
                    color: Events.popupsEnabled ? Theme.text : Theme.textMuted
                    font.pixelSize: 12
                }
                HoverHandler { id: popupsHover }
                TapHandler { onTapped: Events.popupsEnabled = !Events.popupsEnabled }
                ToolTip.visible: popupsHover.hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Show a desktop notification for each new event")
            }
            Rectangle {
                visible: Events.count > 0
                implicitWidth: clearText.implicitWidth + 20
                height: 30
                radius: Theme.radius
                color: clearHover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border
                Text {
                    id: clearText
                    anchors.centerIn: parent
                    text: qsTr("Clear all")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                HoverHandler { id: clearHover }
                TapHandler { onTapped: Events.clear() }
            }
        }

        // ---- Filter chips ----
        Flow {
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
                    implicitWidth: chipText.implicitWidth + 24
                    height: 30
                    radius: 15
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

            // Camera filter: a chip-styled dropdown listing the cameras.
            Rectangle {
                id: camChip
                property bool active: Events.cameraFilter !== ""
                implicitWidth: camChipText.implicitWidth + 34
                height: 30
                radius: 15
                color: active ? Theme.accentDim : (camChipHover.hovered ? Theme.surfaceAlt : Theme.surface)
                border.color: active ? Theme.accent : Theme.border
                Row {
                    anchors.centerIn: parent
                    spacing: 5
                    Text {
                        id: camChipText
                        text: camChip.active ? Events.cameraFilter : qsTr("All cameras")
                        color: camChip.active ? Theme.text : Theme.textMuted
                        font.pixelSize: 12
                    }
                    Text { text: "\u25be"; color: Theme.textMuted; font.pixelSize: 10
                           anchors.verticalCenter: parent.verticalCenter }
                }
                HoverHandler { id: camChipHover }
                TapHandler { onTapped: camMenu.popup() }
                ThemedMenu {
                    id: camMenu
                    ThemedMenuItem { text: qsTr("All cameras")
                                     onTriggered: Events.cameraFilter = "" }
                    ThemedMenuSeparator {}
                    Instantiator {
                        model: Devices
                        delegate: ThemedMenuItem {
                            required property string name
                            text: name
                            onTriggered: Events.cameraFilter = name
                        }
                        onObjectAdded: (i, o) => camMenu.insertItem(i + 2, o)
                        onObjectRemoved: (i, o) => camMenu.removeItem(o)
                    }
                }
            }
        }

        // ---- Event list ----
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Events
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                required property int index
                required property string type
                required property string camera
                required property string timeText
                required property string thumbnail
                required property var hostId
                required property var channel
                required property var timestamp

                width: ListView.view.width
                height: 72
                radius: Theme.radius
                color: rowHover.hovered ? Theme.surfaceAlt : Theme.surface
                border.color: rowHover.hovered ? Theme.border : Qt.rgba(1, 1, 1, 0.04)

                // Left accent bar in the event's type color.
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    radius: Theme.radius
                    color: page.colorFor(type)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 14
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 12

                    // Thumbnail (16:9) or type icon
                    Rectangle {
                        Layout.preferredWidth: 96
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
                            font.pixelSize: 24
                            opacity: 0.85
                        }
                    }

                    // Type + camera
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: page.labelFor(type)
                            color: page.colorFor(type)
                            font.pixelSize: 14
                            font.bold: true
                        }
                        Text {
                            text: camera
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    // Time + affordance
                    ColumnLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 3
                        Text {
                            text: timeText
                            color: Theme.textMuted
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignRight
                        }
                        Text {
                            text: qsTr("View ›")
                            color: rowHover.hovered ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignRight
                        }
                    }
                }

                HoverHandler { id: rowHover }
                TapHandler { onTapped: page.jumpToPlayback(hostId, channel, timestamp) }
            }

            // Empty state
            Column {
                anchors.centerIn: parent
                visible: Events.count === 0
                spacing: 8
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "🔔"; font.pixelSize: 40; opacity: 0.7
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Events.filter.length > 0 ? qsTr("No events of this type")
                                                   : qsTr("No events yet")
                    color: Theme.text
                    font.pixelSize: 14
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Detections from your cameras appear here")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }
        }
    }
}
