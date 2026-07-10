import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

Dialog {
    id: dialog
    title: qsTr("Add Device")
    modal: true
    width: 460
    standardButtons: Dialog.Ok | Dialog.Cancel

    // Opens the dialog and immediately scans the network (first-run flow).
    function openAndScan() {
        tabs.currentIndex = 0;
        open();
        Discovery.scan();
    }

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    ListModel { id: discoveredModel }
    Connections {
        target: Discovery
        function onScanningChanged() {
            if (Discovery.scanning)
                discoveredModel.clear();
        }
        function onDeviceFound(ip, info) {
            for (var i = 0; i < discoveredModel.count; i++)
                if (discoveredModel.get(i).ip === ip) return;
            discoveredModel.append({ ip: ip, info: info });
        }
    }

    component ThemedField: TextField {
        Layout.fillWidth: true
        color: Theme.text
        placeholderTextColor: Theme.textMuted
        background: Rectangle {
            color: Theme.surfaceAlt
            border.color: parent.activeFocus ? Theme.accent : Theme.border
            radius: 4
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }

            component ThemedTab: TabButton {
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? Theme.text : Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 13
                }
                background: Rectangle {
                    color: parent.checked ? Theme.surfaceAlt : "transparent"
                    radius: Theme.radius
                }
            }
            ThemedTab { text: qsTr("Camera / NVR") }
            ThemedTab { text: qsTr("Stream URL") }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: Theme.spacing

                // ---- Auto-discovery ----
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        implicitWidth: scanText.implicitWidth + 24
                        height: 30
                        radius: Theme.radius
                        color: scanArea.containsMouse && !Discovery.scanning
                             ? Theme.accentDim : Theme.surfaceAlt
                        border.color: Theme.border
                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 6
                            BusyIndicator {
                                running: Discovery.scanning
                                visible: running
                                implicitWidth: 16; implicitHeight: 16
                            }
                            Text {
                                id: scanText
                                text: Discovery.scanning ? qsTr("Scanning…") : qsTr("Scan network")
                                color: Theme.text
                                font.pixelSize: 12
                            }
                        }
                        MouseArea {
                            id: scanArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: !Discovery.scanning
                            onClicked: Discovery.scan()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: discoveredModel.count > 0
                            ? qsTr("%1 found — click to select").arg(discoveredModel.count)
                            : (Discovery.scanning ? qsTr("Looking for Reolink devices…")
                                                  : qsTr("or enter an address below"))
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                // ---- Discovered devices ----
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: discoveredModel.count > 0 ? 90 : 0
                    visible: discoveredModel.count > 0
                    color: Theme.paneBackground
                    border.color: Theme.border
                    radius: 4
                    ListView {
                        anchors.fill: parent
                        anchors.margins: 2
                        clip: true
                        model: discoveredModel
                        delegate: Rectangle {
                            required property string ip
                            required property int index
                            width: ListView.view.width
                            height: 28
                            color: addrField.text === ip ? Theme.accentDim
                                 : dHover.hovered ? Theme.surfaceAlt : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                Text { text: "📷"; font.pixelSize: 12 }
                                Text {
                                    text: parent.parent.ip
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: qsTr("Reolink")
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    rightPadding: 8
                                }
                            }
                            HoverHandler { id: dHover }
                            TapHandler { onTapped: addrField.text = parent.ip }
                        }
                    }
                }

                ThemedField {
                    id: addrField
                    placeholderText: qsTr("IP address or hostname")
                }
                ThemedField {
                    id: userField
                    placeholderText: qsTr("Username")
                    text: "admin"
                }
                ThemedField {
                    id: passwordField
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                }
                RowLayout {
                    CheckBox {
                        id: httpsCheck
                        checked: true
                        contentItem: Text {
                            text: qsTr("Use HTTPS")
                            color: Theme.text
                            font.pixelSize: 12
                            leftPadding: parent.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    ThemedField {
                        id: portField
                        placeholderText: qsTr("Port (default)")
                        validator: IntValidator { bottom: 1; top: 65535 }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("An NVR adds all its cameras automatically.")
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
            }

            ColumnLayout {
                spacing: Theme.spacing
                ThemedField {
                    id: streamNameField
                    placeholderText: qsTr("Display name")
                }
                ThemedField {
                    id: streamUrlField
                    placeholderText: qsTr("rtsp:// or file path")
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("For testing and generic RTSP sources.")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    onAccepted: {
        if (tabs.currentIndex === 0 && addrField.text.length > 0) {
            Devices.addDevice(addrField.text, userField.text, passwordField.text,
                              httpsCheck.checked,
                              portField.text.length > 0 ? parseInt(portField.text) : 0);
        } else if (tabs.currentIndex === 1 && streamUrlField.text.length > 0) {
            Devices.addStreamUrl(streamNameField.text, streamUrlField.text);
        }
    }

    onClosed: {
        addrField.text = "";
        passwordField.text = "";
        portField.text = "";
        streamNameField.text = "";
        streamUrlField.text = "";
        discoveredModel.clear();
    }
}
