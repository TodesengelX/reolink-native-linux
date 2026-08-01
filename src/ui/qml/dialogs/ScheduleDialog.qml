import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// Weekly recording schedule (7 days x 24 hours) per recording type, stored on
// the device as a 168-char '0'/'1' table (day-major). Drag to paint; Save
// writes the selected type's table back over Baichuan (cmd 81/82).
Dialog {
    id: dlg
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(860, (Overlay.overlay ? Overlay.overlay.width : 860) - 80)
    padding: Theme.spacing * 2

    property int deviceRow: -1
    property var schedule: ({})     // type -> 168-char table (from the device)
    property string currentType: "Normal"
    property var cells: []          // working copy of currentType's table
    property bool dirty: false

    readonly property var typeDefs: [
        { key: "Normal",  label: qsTr("Continuous") },
        { key: "MD",      label: qsTr("Motion") },
        { key: "people",  label: qsTr("Person") },
        { key: "vehicle", label: qsTr("Vehicle") },
        { key: "dog_cat", label: qsTr("Pet") },
    ]
    readonly property var dayNames: [qsTr("Sun"), qsTr("Mon"), qsTr("Tue"), qsTr("Wed"),
                                     qsTr("Thu"), qsTr("Fri"), qsTr("Sat")]

    title: qsTr("Recording Schedule")
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }

    function openFor(row, sched) {
        deviceRow = row;
        schedule = sched;
        currentType = "Normal";
        loadType();
        open();
    }
    function loadType() {
        var t = schedule[currentType] || "0".repeat(168);
        var arr = new Array(168);
        for (var i = 0; i < 168; ++i) arr[i] = t.charAt(i) === '1';
        cells = arr;
        dirty = false;
        grid.requestPaint();
    }
    function tableString() {
        var s = "";
        for (var i = 0; i < 168; ++i) s += cells[i] ? '1' : '0';
        return s;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        // Type tabs (only types the device reports).
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: dlg.typeDefs
                Rectangle {
                    required property var modelData
                    visible: dlg.schedule[modelData.key] !== undefined
                    property bool sel: dlg.currentType === modelData.key
                    implicitWidth: tabText.implicitWidth + 22
                    height: 28; radius: 14
                    color: sel ? Theme.accentDim : (tabHover.hovered ? Theme.surfaceAlt : Theme.surface)
                    border.color: sel ? Theme.accent : Theme.border
                    Text { id: tabText; anchors.centerIn: parent; text: parent.modelData.label
                           color: parent.sel ? Theme.text : Theme.textMuted; font.pixelSize: 12 }
                    HoverHandler { id: tabHover }
                    TapHandler { onTapped: { dlg.currentType = parent.modelData.key; dlg.loadType(); } }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Drag to choose when %1 recording is active (blue = recording).")
                    .arg((dlg.typeDefs.find(t => t.key === dlg.currentType) || {label: dlg.currentType}).label)
            color: Theme.textMuted; font.pixelSize: 12; wrapMode: Text.WordWrap
        }

        // Hour scale
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Item { Layout.preferredWidth: 42 }
            Repeater {
                model: 9
                Text {
                    required property int index
                    Layout.fillWidth: true
                    text: (index * 3) + ":00"
                    color: Theme.textMuted; font.pixelSize: 9
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Column {
                Layout.preferredWidth: 36
                spacing: 0
                Repeater {
                    model: dlg.dayNames
                    Text {
                        required property string modelData
                        height: grid.height / 7
                        verticalAlignment: Text.AlignVCenter
                        text: modelData; color: Theme.textMuted; font.pixelSize: 11
                    }
                }
            }
            Canvas {
                id: grid
                Layout.fillWidth: true
                Layout.preferredHeight: 7 * 26
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    var cw = width / 24, chh = height / 7;
                    for (var d = 0; d < 7; ++d)
                        for (var h = 0; h < 24; ++h) {
                            ctx.fillStyle = dlg.cells[d * 24 + h] ? "#1d6faa" : "#151d26";
                            ctx.fillRect(h * cw + 1, d * chh + 1, cw - 2, chh - 2);
                        }
                }
                MouseArea {
                    anchors.fill: parent
                    property bool paintVal: false
                    function idx(x, y) {
                        var h = Math.floor(x / (width / 24)), d = Math.floor(y / (height / 7));
                        return (h < 0 || h > 23 || d < 0 || d > 6) ? -1 : d * 24 + h;
                    }
                    onPressed: (m) => { var i = idx(m.x, m.y); if (i < 0) return;
                        paintVal = !dlg.cells[i]; dlg.cells[i] = paintVal; dlg.dirty = true; grid.requestPaint(); }
                    onPositionChanged: (m) => { if (!(m.buttons & Qt.LeftButton)) return;
                        var i = idx(m.x, m.y); if (i < 0 || dlg.cells[i] === paintVal) return;
                        dlg.cells[i] = paintVal; dlg.dirty = true; grid.requestPaint(); }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            component SBtn: Rectangle {
                property string text: ""
                property color edge: Theme.border
                signal clicked()
                implicitWidth: sl.implicitWidth + 24; implicitHeight: 32; radius: Theme.radius
                color: sh.hovered ? Theme.surfaceAlt : "transparent"
                border.color: edge
                Text { id: sl; anchors.centerIn: parent; text: parent.text; color: Theme.text; font.pixelSize: 12 }
                HoverHandler { id: sh }
                TapHandler { onTapped: parent.clicked() }
            }
            SBtn { text: qsTr("Always"); onClicked: { for (var i = 0; i < 168; ++i) dlg.cells[i] = true; dlg.dirty = true; grid.requestPaint(); } }
            SBtn { text: qsTr("Never"); onClicked: { for (var i = 0; i < 168; ++i) dlg.cells[i] = false; dlg.dirty = true; grid.requestPaint(); } }
            Item { Layout.fillWidth: true }
            SBtn { text: qsTr("Close"); onClicked: dlg.close() }
            SBtn {
                text: qsTr("Save %1").arg((dlg.typeDefs.find(t => t.key === dlg.currentType) || {label:""}).label)
                edge: Theme.accent
                onClicked: {
                    var t = dlg.tableString();
                    var sched = dlg.schedule; sched[dlg.currentType] = t; dlg.schedule = sched;
                    Devices.writeRecSchedule(dlg.deviceRow, dlg.currentType, t);
                    dlg.dirty = false;
                }
            }
        }
    }
}
