import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

Rectangle {
    id: root
    width: Theme.sidebarWidth
    color: Theme.surface

    signal addRequested()
    signal deviceClicked(int row)      // open this camera in Live View
    signal openSettings(int row)       // open Device Settings for this row
    signal cameraProperties(int row)   // show the camera properties dialog
    signal nvrProperties(var host)     // show the NVR properties dialog (hostInfo map)

    // Collapsed NVR hosts (hostId key -> true). Reassigned wholesale so the
    // bindings that read it re-evaluate (QML doesn't observe deep mutation).
    property var collapsed: ({})
    function toggleCollapse(key) {
        var c = Object.assign({}, collapsed);
        c[key] = !c[key];
        collapsed = c;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            Text {
                text: qsTr("Devices")
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 26
                height: 26
                radius: Theme.radius
                color: addArea.containsMouse ? Theme.accentDim : Theme.surfaceAlt
                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Theme.text
                    font.pixelSize: 16
                }
                MouseArea {
                    id: addArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.addRequested()
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Devices
            clip: true
            spacing: 2

            // Group channels under their host. The header renders only for NVRs
            // (a standalone camera is its own host — no redundant parent row).
            section.property: "hostId"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                id: nvrHeader
                required property string section
                property var host: Devices.hostInfo(parseInt(section))
                property bool isNvr: host && host.kind === "nvr"
                width: ListView.view.width
                height: isNvr ? 40 : 0
                visible: isNvr
                color: nvrHover.hovered ? Theme.surfaceAlt : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: Theme.spacing
                    spacing: 8
                    Text {
                        text: root.collapsed[nvrHeader.section] ? "▸" : "▾"
                        color: Theme.textMuted; font.pixelSize: 11
                        Layout.preferredWidth: 10
                    }
                    Rectangle { // online dot
                        width: 8; height: 8; radius: 4
                        color: nvrHeader.host && nvrHeader.host.online ? Theme.online : Theme.textMuted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: nvrHeader.host ? nvrHeader.host.name : ""
                            color: Theme.text; font.pixelSize: 13; font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: nvrHeader.host
                                ? (nvrHeader.host.model + " · " + nvrHeader.host.onlineCount
                                   + "/" + nvrHeader.host.channelCount + qsTr(" cameras"))
                                : ""
                            color: Theme.textMuted; font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                }
                HoverHandler { id: nvrHover }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Left-click expands/collapses the camera list; only
                    // right-click brings up the management menu.
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton)
                            nvrMenu.popup();
                        else
                            root.toggleCollapse(nvrHeader.section);
                    }
                }
                Menu {
                    id: nvrMenu
                    MenuItem { text: qsTr("Properties")
                               onTriggered: root.nvrProperties(nvrHeader.host) }
                    MenuItem { text: qsTr("Settings")
                               onTriggered: root.openSettings(nvrHeader.host.firstRow) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Reboot NVR")
                               enabled: nvrHeader.host && nvrHeader.host.isAdmin
                               onTriggered: Devices.reboot(nvrHeader.host.firstRow) }
                    MenuItem { text: qsTr("Remove NVR")
                               onTriggered: Devices.removeDevice(nvrHeader.host.firstRow) }
                }
            }

            delegate: Rectangle {
                id: camRow
                required property int index
                required property string name
                required property string status
                required property string model
                required property string kind
                required property int hostId
                required property bool online
                required property int batteryPercent
                required property bool batteryCharging

                readonly property bool underNvr: kind === "nvr"
                readonly property bool hidden: underNvr && root.collapsed[hostId] === true

                width: ListView.view.width
                height: hidden ? 0 : 48
                visible: !hidden
                clip: true
                color: delegateArea.containsMouse ? Theme.surfaceAlt : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: camRow.underNvr ? Theme.spacing * 2 + 6 : Theme.spacing
                    anchors.rightMargin: Theme.spacing
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    spacing: Theme.spacing

                    Rectangle { // online dot
                        width: 8
                        height: 8
                        radius: 4
                        color: online ? Theme.online : Theme.textMuted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: name
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: camRow.underNvr ? status
                                : (model.length > 0 ? model + " · " + status : status)
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    // Battery badge (battery/solar cameras only).
                    Row {
                        visible: batteryPercent >= 0
                        spacing: 3
                        Text {
                            text: batteryCharging ? "⚡" : ""
                            color: Theme.online
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            width: 22; height: 11; radius: 2
                            border.color: Theme.textMuted
                            color: "transparent"
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle {
                                anchors.left: parent.left
                                anchors.leftMargin: 1
                                anchors.verticalCenter: parent.verticalCenter
                                height: 7
                                width: Math.max(1, (parent.width - 2) * batteryPercent / 100)
                                radius: 1
                                color: batteryPercent > 20 ? Theme.online : Theme.danger
                            }
                            Rectangle { // terminal
                                anchors.left: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 2; height: 5; color: Theme.textMuted
                            }
                        }
                        Text {
                            text: batteryPercent + "%"
                            color: Theme.textMuted
                            font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                MouseArea {
                    id: delegateArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Left-click opens the camera full-size in Live View;
                    // right-click opens its management menu.
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton)
                            camMenu.popup();
                        else
                            root.deviceClicked(camRow.index);
                    }
                }

                Menu {
                    id: camMenu
                    MenuItem { text: qsTr("Properties")
                               onTriggered: root.cameraProperties(camRow.index) }
                    MenuItem { text: qsTr("Settings")
                               onTriggered: root.openSettings(camRow.index) }
                    MenuSeparator {}
                    // Standalone cameras are their own host and can be removed here;
                    // an NVR's channels are managed on the NVR (remove it whole).
                    MenuItem {
                        text: camRow.underNvr ? qsTr("Remove NVR…") : qsTr("Remove device")
                        onTriggered: Devices.removeDevice(camRow.index)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: Devices.count === 0
                text: qsTr("No devices yet.\nClick + to add one.")
                color: Theme.textMuted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
