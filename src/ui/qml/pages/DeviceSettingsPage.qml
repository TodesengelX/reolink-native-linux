import QtQuick
import ReolinkApp

// Placeholder — the settings surface lands in M9 per DESIGN.md §6.7/§9.
Item {
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Device Settings")
            color: Theme.text
            font.pixelSize: 22
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Device configuration pages arrive in milestone M9.")
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }
}
