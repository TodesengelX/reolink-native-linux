import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// A read-only "Properties" sheet for an NVR or a camera: a heading, a table of
// facts, and quick actions (open settings, reboot, remove). Populated by Main
// from Devices.hostInfo()/cameraInfo(). Actions that mutate the device are gated
// on the admin flag.
Dialog {
    id: dialog
    modal: true
    width: 460
    anchors.centerIn: Overlay.overlay

    property string heading: ""
    property string subheading: ""
    property var rows: []           // [{ label, value }]
    property int targetRow: -1      // camera row, or an NVR's first channel row
    property bool isAdmin: false
    property bool showReboot: false
    property bool showRemove: false
    property string removeLabel: qsTr("Remove")

    signal openSettings(int row)

    standardButtons: Dialog.Close
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }

    header: Item {
        implicitHeight: hdr.implicitHeight + 22
        ColumnLayout {
            id: hdr
            x: Theme.spacing; width: parent.width - Theme.spacing * 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: dialog.heading; color: Theme.text
                font.pixelSize: 16; font.bold: true; elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.subheading.length > 0
                text: dialog.subheading; color: Theme.textMuted; font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    component ActionButton: Rectangle {
        property string label: ""
        property bool danger: false
        property bool enabledBtn: true
        signal clicked()
        implicitWidth: abTxt.implicitWidth + 24
        implicitHeight: 30
        radius: Theme.radius
        opacity: enabledBtn ? 1 : 0.5
        color: !enabledBtn ? Theme.surface
             : abHover.hovered ? (danger ? Theme.danger : Theme.accentDim) : Theme.surfaceAlt
        border.color: danger ? Theme.danger : Theme.border
        Text { id: abTxt; anchors.centerIn: parent; text: parent.label; color: Theme.text; font.pixelSize: 12 }
        HoverHandler { id: abHover; enabled: parent.enabledBtn }
        TapHandler { enabled: parent.enabledBtn; onTapped: parent.clicked() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: dialog.rows
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    Text {
                        text: modelData.label
                        color: Theme.textMuted; font.pixelSize: 12
                        Layout.preferredWidth: 140
                    }
                    Text {
                        text: modelData.value
                        color: Theme.text; font.pixelSize: 12
                        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }

        Item { Layout.fillHeight: true; Layout.minimumHeight: Theme.spacing }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            ActionButton {
                label: qsTr("Open settings")
                onClicked: { dialog.openSettings(dialog.targetRow); dialog.close(); }
            }
            ActionButton {
                visible: dialog.showReboot
                label: qsTr("Reboot")
                danger: true
                enabledBtn: dialog.isAdmin
                onClicked: { Devices.reboot(dialog.targetRow); dialog.close(); }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                visible: dialog.showRemove
                label: dialog.removeLabel
                danger: true
                onClicked: { Devices.removeDevice(dialog.targetRow); dialog.close(); }
            }
        }
    }
}
