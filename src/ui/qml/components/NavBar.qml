import QtQuick
import QtQuick.Layouts
import ReolinkApp

Rectangle {
    id: root
    height: Theme.navHeight
    color: Theme.surface

    property int currentIndex: 0
    signal fullscreenRequested()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing * 2
        anchors.rightMargin: Theme.spacing * 2
        spacing: Theme.spacing

        Text {
            text: qsTr("Reolink Client")
            color: Theme.accent
            font.pixelSize: 16
            font.bold: true
        }

        Item { width: Theme.spacing * 2; height: 1 }

        Repeater {
            model: [qsTr("Live View"), qsTr("Playback"), qsTr("Events"), qsTr("Device Settings")]

            Rectangle {
                required property int index
                required property string modelData
                Layout.fillHeight: true
                implicitWidth: tabLabel.implicitWidth + Theme.spacing * 4
                color: "transparent"

                Rectangle { // active-tab underline
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: Theme.accent
                    visible: root.currentIndex === parent.index
                }
                Text {
                    id: tabLabel
                    anchors.centerIn: parent
                    text: parent.modelData
                    color: root.currentIndex === parent.index ? Theme.text : Theme.textMuted
                    font.pixelSize: 14
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.currentIndex = parent.index
                }
            }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            width: 32
            height: 32
            radius: Theme.radius
            color: fullscreenArea.containsMouse ? Theme.surfaceAlt : "transparent"
            Text {
                anchors.centerIn: parent
                text: "⛶"
                color: Theme.textMuted
                font.pixelSize: 16
            }
            MouseArea {
                id: fullscreenArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.fullscreenRequested()
            }
        }
    }
}
