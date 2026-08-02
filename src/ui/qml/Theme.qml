pragma Singleton
import QtQuick

// Dark palette approximating the official Reolink Client. Single source of
// truth for colors/metrics — no literal colors anywhere else in the UI.
QtObject {
    readonly property color window: "#10161d"
    readonly property color surface: "#1a222b"
    readonly property color surfaceAlt: "#222d38"
    readonly property color border: "#2c3945"
    readonly property color accent: "#2aa7ff"
    readonly property color accentDim: "#1d6faa"
    readonly property color text: "#e8eef4"
    readonly property color textMuted: "#8b98a5"
    readonly property color paneBackground: "#05080b"
    readonly property color danger: "#e5484d"
    readonly property color online: "#46a758"

    readonly property int radius: 6
    readonly property int spacing: 8
    // How far the pointer must move before a press becomes a drag rather than
    // a click, for dragging cameras between grid cells.
    readonly property int dragThreshold: 8
    readonly property int sidebarWidth: 240
    readonly property int navHeight: 48
}
