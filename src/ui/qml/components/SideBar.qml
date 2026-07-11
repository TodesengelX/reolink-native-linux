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
    signal deviceClicked(int row)

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

            delegate: Rectangle {
                required property int index
                required property string name
                required property string status
                required property string model
                required property bool online
                required property int batteryPercent
                required property bool batteryCharging

                width: ListView.view.width
                height: 52
                color: delegateArea.containsMouse ? Theme.surfaceAlt : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
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
                            text: model.length > 0 ? model + " · " + status : status
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
                    // right-click keeps the management menu.
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton)
                            contextMenu.popup();
                        else
                            root.deviceClicked(index);
                    }
                }

                Menu {
                    id: contextMenu
                    MenuItem {
                        text: qsTr("Remove device")
                        onTriggered: Devices.removeDevice(index)
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
