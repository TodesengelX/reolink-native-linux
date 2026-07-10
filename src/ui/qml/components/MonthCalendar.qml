import QtQuick
import QtQuick.Layouts
import ReolinkApp

// Compact month calendar. Emits dateSelected(year, month, day) — month is
// 1-based. Days that have recordings can be marked via markedDays.
Column {
    id: root
    spacing: 6

    property int year: 2026
    property int month: 7           // 1-based
    property int selDay: 9
    property var markedDays: []      // days (1..31) with recordings → blue dot

    signal dateSelected(int year, int month, int day)

    function daysInMonth(y, m) { return new Date(y, m, 0).getDate(); }
    function firstWeekday(y, m) { return new Date(y, m - 1, 1).getDay(); } // 0=Sun

    RowLayout {
        width: parent.width
        Rectangle {
            width: 24; height: 24; radius: 4
            color: prevHover.hovered ? Theme.surfaceAlt : "transparent"
            Text { anchors.centerIn: parent; text: "‹"; color: Theme.text; font.pixelSize: 16 }
            HoverHandler { id: prevHover }
            TapHandler { onTapped: {
                if (root.month === 1) { root.month = 12; root.year--; }
                else root.month--;
            } }
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: Qt.locale().standaloneMonthName(root.month - 1) + " " + root.year
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
        }
        Rectangle {
            width: 24; height: 24; radius: 4
            color: nextHover.hovered ? Theme.surfaceAlt : "transparent"
            Text { anchors.centerIn: parent; text: "›"; color: Theme.text; font.pixelSize: 16 }
            HoverHandler { id: nextHover }
            TapHandler { onTapped: {
                if (root.month === 12) { root.month = 1; root.year++; }
                else root.month++;
            } }
        }
    }

    Grid {
        columns: 7
        spacing: 2
        Repeater {
            model: ["S", "M", "T", "W", "T", "F", "S"]
            Text {
                required property string modelData
                width: 30; height: 18
                horizontalAlignment: Text.AlignHCenter
                text: modelData
                color: Theme.textMuted
                font.pixelSize: 10
            }
        }
    }

    Grid {
        columns: 7
        spacing: 2
        Repeater {
            model: 42
            Item {
                required property int index
                property int dayNum: index - root.firstWeekday(root.year, root.month) + 1
                property bool valid: dayNum >= 1 && dayNum <= root.daysInMonth(root.year, root.month)
                property bool selected: valid && dayNum === root.selDay
                width: 30; height: 28
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    visible: parent.valid
                    color: parent.selected ? Theme.accentDim
                         : dayHover.hovered ? Theme.surfaceAlt : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: parent.parent.valid ? parent.parent.dayNum : ""
                        color: parent.parent.selected ? Theme.text : Theme.textMuted
                        font.pixelSize: 11
                    }
                    Rectangle { // recording marker
                        visible: root.markedDays.indexOf(parent.parent.dayNum) >= 0
                        width: 4; height: 4; radius: 2
                        color: Theme.accent
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    HoverHandler { id: dayHover; enabled: parent.parent.valid }
                    TapHandler {
                        enabled: parent.parent.valid
                        onTapped: {
                            root.selDay = parent.parent.dayNum;
                            root.dateSelected(root.year, root.month, parent.parent.dayNum);
                        }
                    }
                }
            }
        }
    }
}
