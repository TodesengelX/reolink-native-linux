import QtQuick
import QtQuick.Layouts
import ReolinkApp

// One labelled settings row. Put the control(s) as children; they lay out on the
// right. Disabled rows dim and show a tooltip (used for non-admin gating).
RowLayout {
    id: root
    property string label: ""
    property string hint: ""
    Layout.fillWidth: true
    spacing: Theme.spacing

    ColumnLayout {
        Layout.preferredWidth: 200
        spacing: 1
        Text {
            text: root.label
            color: Theme.text
            font.pixelSize: 13
        }
        Text {
            visible: root.hint.length > 0
            text: root.hint
            color: Theme.textMuted
            font.pixelSize: 10
            Layout.maximumWidth: 200
            wrapMode: Text.WordWrap
        }
    }
    Item { Layout.fillWidth: true }
}
