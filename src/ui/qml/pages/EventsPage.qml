import QtQuick
import ReolinkApp

// Placeholder — Events / Notification Center lands in M8 per DESIGN.md §6.4/§9.
Item {
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Events")
            color: Theme.text
            font.pixelSize: 22
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("The event inbox arrives in milestone M8.")
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }
}
