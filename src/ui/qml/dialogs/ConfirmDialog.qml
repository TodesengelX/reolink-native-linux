import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// A themed confirmation sheet for destructive actions. The caller sets the text
// and a `payload`, opens it, and handles `confirmed(payload)`.
Dialog {
    id: dialog
    modal: true
    width: 420
    anchors.centerIn: Overlay.overlay

    property string message: ""
    property string detail: ""
    property string confirmLabel: qsTr("Confirm")
    property var payload: null

    signal confirmed(var payload)

    title: qsTr("Are you sure?")
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing
        Text {
            Layout.fillWidth: true
            text: dialog.message
            color: Theme.text; font.pixelSize: 13; wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: dialog.detail.length > 0
            text: dialog.detail
            color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: cxTxt.implicitWidth + 24; implicitHeight: 32; radius: Theme.radius
                color: cxHover.hovered ? Theme.surfaceAlt : "transparent"
                border.color: Theme.border
                Text { id: cxTxt; anchors.centerIn: parent; text: qsTr("Cancel"); color: Theme.text; font.pixelSize: 12 }
                HoverHandler { id: cxHover }
                TapHandler { onTapped: dialog.close() }
            }
            Rectangle {
                implicitWidth: okTxt.implicitWidth + 24; implicitHeight: 32; radius: Theme.radius
                color: okHover.hovered ? Theme.danger : Theme.surfaceAlt
                border.color: Theme.danger
                Text { id: okTxt; anchors.centerIn: parent; text: dialog.confirmLabel; color: Theme.text; font.pixelSize: 12 }
                HoverHandler { id: okHover }
                TapHandler { onTapped: { dialog.confirmed(dialog.payload); dialog.close(); } }
            }
        }
    }
}
