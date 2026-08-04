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

    // The device is probed BEFORE anything is persisted, so a typo'd address
    // or rejected password is an inline message here — not a phantom row stuck
    // on "connecting…" in the sidebar.
    property bool testing: false
    property string errorText: ""

    function tryAdd() {
        if (testing)
            return;
        errorText = "";
        if (tabs.currentIndex === 1) {
            if (streamUrlField.text.trim().length === 0) {
                errorText = qsTr("Enter a stream URL or file path.");
                return;
            }
            Devices.addStreamUrl(streamNameField.text, streamUrlField.text);
            dialog.close();
            return;
        }
        if (addrField.text.trim().length === 0) {
            errorText = qsTr("Enter the device's IP address or hostname.");
            return;
        }
        if (userField.text.trim().length === 0) {
            errorText = qsTr("Enter the username.");
            return;
        }
        testing = true;
        Devices.testDevice(addrField.text.trim(), userField.text, passwordField.text,
                           httpsCheck.checked,
                           portField.text.length > 0 ? parseInt(portField.text) : 0);
    }

    Connections {
        target: Devices
        function onTestDeviceResult(ok, message, name, model, problem) {
            if (!dialog.visible || !dialog.testing)
                return;
            dialog.testing = false;
            if (ok) {
                Devices.addDevice(addrField.text.trim(), userField.text,
                                  passwordField.text, httpsCheck.checked,
                                  portField.text.length > 0 ? parseInt(portField.text) : 0);
                dialog.close();
                return;
            }
            if (problem === "transport") {
                dialog.errorText = qsTr("Can't reach %1 — check the address and port, and that the device is powered on and on your network.")
                    .arg(addrField.text.trim()) + "\n" + message;
            } else if (problem === "auth") {
                dialog.errorText = qsTr("The device rejected the sign-in: %1").arg(message);
            } else if (problem === "locked") {
                dialog.errorText = qsTr("The account is locked after too many failed sign-ins. Wait a few minutes (or reboot the device) before trying again. %1").arg(message);
            } else {
                dialog.errorText = qsTr("Unexpected reply from %1 — is this a Reolink device? %2")
                    .arg(addrField.text.trim()).arg(message);
            }
        }
    }

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

    header: Item {
        implicitHeight: 46
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing * 2
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            color: Theme.text
            font.pixelSize: 15
            font.bold: true
        }
        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right
                      leftMargin: Theme.spacing; rightMargin: Theme.spacing }
            height: 1; color: Theme.border
        }
    }

    footer: Item {
        implicitHeight: 56
        Rectangle {
            anchors { top: parent.top; left: parent.left; right: parent.right
                      leftMargin: Theme.spacing; rightMargin: Theme.spacing }
            height: 1; color: Theme.border
        }
        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing * 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing
            Rectangle {
                implicitWidth: cancelLbl.implicitWidth + 28; implicitHeight: 34; radius: Theme.radius
                color: cancelHov.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border
                Text { id: cancelLbl; anchors.centerIn: parent; text: qsTr("Cancel"); color: Theme.text; font.pixelSize: 13 }
                HoverHandler { id: cancelHov }
                TapHandler { onTapped: dialog.reject() }
            }
            Rectangle {
                implicitWidth: addRow.implicitWidth + 28; implicitHeight: 34; radius: Theme.radius
                color: dialog.testing ? Theme.surfaceAlt
                     : addHov.hovered ? Theme.accent : Theme.accentDim
                border.color: dialog.testing ? Theme.border : Theme.accent
                Row {
                    id: addRow
                    anchors.centerIn: parent
                    spacing: 6
                    BusyIndicator {
                        visible: dialog.testing; running: visible
                        width: 14; height: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: dialog.testing ? qsTr("Checking…") : qsTr("Add")
                        color: Theme.text; font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                HoverHandler { id: addHov }
                TapHandler { onTapped: dialog.tryAdd() }
            }
        }
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
                                Rectangle { width: 7; height: 7; radius: 3.5
                                            color: Theme.accent; Layout.alignment: Qt.AlignVCenter }
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
                        Layout.alignment: Qt.AlignVCenter
                        indicator: Rectangle {
                            implicitWidth: 18; implicitHeight: 18
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 4
                            color: httpsCheck.checked ? Theme.accent : Theme.surfaceAlt
                            border.color: httpsCheck.checked ? Theme.accent : Theme.border
                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"; visible: httpsCheck.checked
                                color: Theme.window; font.pixelSize: 12; font.bold: true
                            }
                        }
                        contentItem: Text {
                            text: qsTr("Use HTTPS")
                            color: Theme.text
                            font.pixelSize: 12
                            leftPadding: httpsCheck.indicator.width + 8
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

        // Why the device couldn't be added, in place — the dialog stays open
        // so the field at fault can be corrected and retried.
        Text {
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            text: dialog.errorText
            color: Theme.danger
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    onClosed: {
        addrField.text = "";
        passwordField.text = "";
        portField.text = "";
        streamNameField.text = "";
        streamUrlField.text = "";
        discoveredModel.clear();
        testing = false;
        errorText = "";
    }
}
