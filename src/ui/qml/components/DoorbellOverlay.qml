import QtQuick
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// High-priority visitor-press surface for Reolink doorbells. Shows live view of
// the doorbell with Answer (two-way talk), Dismiss, and quick-reply buttons.
// Raised on a "visitor" event; the answer path uses the Baichuan talk channel
// (wires in with M12). Dismisses after a timeout if unanswered.
Rectangle {
    id: root
    property int deviceRow: -1
    property string camera: ""
    property bool active: false

    signal dismissed()
    signal answered()

    visible: active
    color: "#cc0a0f14"
    radius: Theme.radius
    border.color: Theme.accent
    border.width: 2
    width: 360
    height: 300

    onActiveChanged: if (active) autoDismiss.restart()
    Timer { id: autoDismiss; interval: 30000; onTriggered: { root.active = false; root.dismissed(); } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 2
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            Text { text: "🔔"; font.pixelSize: 22 }
            ColumnLayout {
                spacing: 0
                Text { text: qsTr("Someone's at the door"); color: Theme.text; font.pixelSize: 15; font.bold: true }
                Text { text: root.camera; color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
        }

        // Live view of the doorbell
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.paneBackground
            border.color: Theme.border
            radius: 4
            Text {
                anchors.centerIn: parent
                text: qsTr("Live view")
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            component ActionBtn: Rectangle {
                property string label: ""
                property color tint: Theme.surfaceAlt
                signal clicked()
                Layout.fillWidth: true
                height: 40
                radius: Theme.radius
                color: abHover.hovered ? Qt.lighter(tint, 1.2) : tint
                border.color: Theme.border
                Text { anchors.centerIn: parent; text: parent.label; color: Theme.text; font.pixelSize: 13; font.bold: true }
                HoverHandler { id: abHover }
                TapHandler { onTapped: parent.clicked() }
            }

            ActionBtn {
                label: qsTr("Answer")
                tint: Theme.online
                onClicked: { autoDismiss.stop(); root.answered(); }
            }
            ActionBtn {
                label: qsTr("Quick reply")
                onClicked: Devices.applySetting(root.deviceRow, "QuickReplyPlay",
                    { "channel": 0, "id": 0 })
            }
            ActionBtn {
                label: qsTr("Dismiss")
                tint: Theme.danger
                onClicked: { root.active = false; root.dismissed(); }
            }
        }
    }
}
