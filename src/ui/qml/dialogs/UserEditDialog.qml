import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// Add a user, or change an existing user's password, on a host (NVR/camera).
// Shapes verified against reolink_aio: AddUser {User:{userName,password,level}},
// ModifyUser {User:{userName,newPassword,oldPassword}}. Admin-only (the caller
// gates the entry points); the write itself is also rejected server-side if not.
Dialog {
    id: dialog
    modal: true
    width: 420
    anchors.centerIn: Overlay.overlay

    property int deviceRow: -1
    property string mode: "add"        // "add" | "password"
    property string userName: ""       // pre-filled + locked in password mode

    title: mode === "add" ? qsTr("Add user") : qsTr("Change password")
    standardButtons: Dialog.Ok | Dialog.Cancel
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }

    component Field: TextField {
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

        Field {
            id: nameField
            placeholderText: qsTr("Username")
            text: dialog.userName
            enabled: dialog.mode === "add"
        }
        Field {
            id: oldPassField
            visible: dialog.mode === "password"
            placeholderText: qsTr("Current password")
            echoMode: TextInput.Password
        }
        Field {
            id: passField
            placeholderText: dialog.mode === "add" ? qsTr("Password") : qsTr("New password")
            echoMode: TextInput.Password
        }
        RowLayout {
            visible: dialog.mode === "add"
            Layout.fillWidth: true
            Text { text: qsTr("Privilege"); color: Theme.textMuted; font.pixelSize: 12 }
            ComboBox {
                id: levelBox
                Layout.preferredWidth: 160
                model: ["guest", "admin"]
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.mode === "add"
            text: qsTr("Guests can view live/playback; admins can also change settings.")
            color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
    }

    onAccepted: {
        if (dialog.deviceRow < 0)
            return;
        if (dialog.mode === "add") {
            if (nameField.text.length === 0 || passField.text.length === 0)
                return;
            Devices.applySetting(dialog.deviceRow, "AddUser",
                { "User": { "userName": nameField.text, "password": passField.text,
                            "level": levelBox.currentText } });
        } else {
            if (passField.text.length === 0)
                return;
            Devices.applySetting(dialog.deviceRow, "ModifyUser",
                { "User": { "userName": dialog.userName, "newPassword": passField.text,
                            "oldPassword": oldPassField.text } });
        }
    }

    onClosed: { passField.text = ""; oldPassField.text = ""; nameField.text = ""; }
}
