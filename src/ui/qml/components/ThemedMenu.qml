import QtQuick
import QtQuick.Controls
import ReolinkApp

// A Menu styled to the app palette. The default (Basic) Menu renders light/grey
// and clashes with the dark UI, so every context menu uses this instead.
Menu {
    id: control
    padding: 5
    background: Rectangle {
        implicitWidth: 190
        color: Theme.surfaceAlt
        border.color: Theme.border
        radius: Theme.radius
    }
}
