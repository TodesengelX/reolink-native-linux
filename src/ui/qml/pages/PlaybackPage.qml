import QtQuick
import ReolinkApp

// Placeholder — lands in M7 (two-tone timeline) / M8 (AI filters) per DESIGN.md §9.
Item {
    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Playback")
            color: Theme.text
            font.pixelSize: 22
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Recorded-footage timeline arrives in milestone M7.")
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }
}
