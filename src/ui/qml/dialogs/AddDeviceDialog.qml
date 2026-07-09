import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

Dialog {
    id: dialog
    title: qsTr("Add Device")
    modal: true
    width: 420
    standardButtons: Dialog.Ok | Dialog.Cancel

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    // Basic-style controls take explicit colors; this keeps fields readable
    // on the dark surface without a full custom style.
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

    // Clear every field after close (fires on both accept and cancel/Escape), so a
    // typed password never lingers in this reused dialog instance.
    onClosed: {
        addrField.text = "";
        passwordField.text = "";
        portField.text = "";
        streamNameField.text = "";
        streamUrlField.text = "";
    }
}
