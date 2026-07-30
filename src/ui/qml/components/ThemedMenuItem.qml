import QtQuick
import QtQuick.Controls
import ReolinkApp

// A MenuItem matching the app theme; use inside ThemedMenu.
MenuItem {
    id: control
    implicitHeight: 32
    implicitWidth: 180
    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.text : Theme.textMuted
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        leftPadding: 8
        rightPadding: 8
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: control.highlighted ? Theme.accentDim : "transparent"
    }
}
