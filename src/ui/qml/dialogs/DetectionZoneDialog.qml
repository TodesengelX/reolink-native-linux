import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// Motion-detection zone editor. The device stores the zone as a base64 grid
// mask in its MdAlarm config (cmd 46/47): one bit per cell (1 = watch),
// row-major over columns x rows. This decodes it onto a snapshot of the camera,
// lets the user drag to toggle regions, and writes the re-encoded mask back.
Dialog {
    id: dlg
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(900, (Overlay.overlay ? Overlay.overlay.width : 900) - 80)
    padding: Theme.spacing * 2

    property int deviceRow: -1
    property int cols: 120
    property int rows: 33
    property var cells: []
    property string snapshotPath: ""
    property real imgAspect: 16 / 9

    title: qsTr("Detection Zone")
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radius }

    function openFor(row, md) {
        deviceRow = row;
        cols = parseInt(md.columns || md.width || "120");
        rows = parseInt(md.rows || md.height || "33");
        var bits = Devices.mdZoneBits(md.valueTable || "", cols * rows);
        var arr = new Array(bits.length);
        for (var i = 0; i < bits.length; ++i) arr[i] = bits.charAt(i) === '1';
        cells = arr;
        snapshotPath = "";
        imgAspect = 16 / 9;
        open();
        Devices.snapshot(row);   // fetch a backdrop frame
    }
    function fillAll(v) { for (var i = 0; i < cells.length; ++i) cells[i] = v; grid.requestPaint(); }
    function invertAll() { for (var i = 0; i < cells.length; ++i) cells[i] = !cells[i]; grid.requestPaint(); }
    function bitsString() { var s = ""; for (var i = 0; i < cells.length; ++i) s += cells[i] ? '1' : '0'; return s; }

    Connections {
        target: Devices
        function onSnapshotSaved(row, path) { if (row === dlg.deviceRow) dlg.snapshotPath = path; }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Drag over the image to toggle areas. Red areas are ignored — motion there won't trigger alarms.")
            color: Theme.textMuted; font.pixelSize: 12
        }

        Rectangle {
            id: stage
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(width / dlg.imgAspect)
            color: Theme.paneBackground
            border.color: Theme.border
            clip: true

            Image {
                id: snap
                anchors.fill: parent
                fillMode: Image.Stretch
                cache: false
                source: dlg.snapshotPath ? ("file://" + dlg.snapshotPath) : ""
                onStatusChanged: if (status === Image.Ready && sourceSize.width > 0)
                                     dlg.imgAspect = sourceSize.width / sourceSize.height
            }
            Text {
                anchors.centerIn: parent
                visible: dlg.snapshotPath === ""
                text: qsTr("Loading camera image…")
                color: Theme.textMuted; font.pixelSize: 12
            }

            Canvas {
                id: grid
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    var cw = width / dlg.cols, chh = height / dlg.rows;
                    ctx.fillStyle = "rgba(229,72,77,0.42)";
                    for (var r = 0; r < dlg.rows; ++r)
                        for (var c = 0; c < dlg.cols; ++c)
                            if (!dlg.cells[r * dlg.cols + c])
                                ctx.fillRect(c * cw, r * chh, cw + 0.6, chh + 0.6);
                    ctx.strokeStyle = "rgba(255,255,255,0.07)"; ctx.lineWidth = 1;
                    ctx.beginPath();
                    for (var x = 0; x <= dlg.cols; x += 5) { var px = Math.round(x * cw) + 0.5; ctx.moveTo(px, 0); ctx.lineTo(px, height); }
                    for (var y = 0; y <= dlg.rows; y += 3) { var py = Math.round(y * chh) + 0.5; ctx.moveTo(0, py); ctx.lineTo(width, py); }
                    ctx.stroke();
                }
                MouseArea {
                    anchors.fill: parent
                    property bool paintVal: false
                    function idx(x, y) {
                        var c = Math.floor(x / (width / dlg.cols)), r = Math.floor(y / (height / dlg.rows));
                        return (c < 0 || c >= dlg.cols || r < 0 || r >= dlg.rows) ? -1 : r * dlg.cols + c;
                    }
                    onPressed: (m) => { var i = idx(m.x, m.y); if (i < 0) return;
                        paintVal = !dlg.cells[i]; dlg.cells[i] = paintVal; grid.requestPaint(); }
                    onPositionChanged: (m) => { if (!(m.buttons & Qt.LeftButton)) return;
                        var i = idx(m.x, m.y); if (i < 0 || dlg.cells[i] === paintVal) return;
                        dlg.cells[i] = paintVal; grid.requestPaint(); }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            component TxtBtn: Rectangle {
                property string text: ""
                property color edge: Theme.border
                signal clicked()
                implicitWidth: lbl.implicitWidth + 24; implicitHeight: 32; radius: Theme.radius
                color: h.hovered ? Theme.surfaceAlt : "transparent"
                border.color: edge
                Text { id: lbl; anchors.centerIn: parent; text: parent.text; color: Theme.text; font.pixelSize: 12 }
                HoverHandler { id: h }
                TapHandler { onTapped: parent.clicked() }
            }

            TxtBtn { text: qsTr("Watch all"); onClicked: dlg.fillAll(true) }
            TxtBtn { text: qsTr("Ignore all"); onClicked: dlg.fillAll(false) }
            TxtBtn { text: qsTr("Invert"); onClicked: dlg.invertAll() }
            Item { Layout.fillWidth: true }
            TxtBtn { text: qsTr("Cancel"); onClicked: dlg.close() }
            TxtBtn {
                text: qsTr("Save"); edge: Theme.accent
                onClicked: {
                    Devices.writeBcConfig(dlg.deviceRow, 46, 47,
                        { "valueTable": Devices.mdZoneTable(dlg.bitsString()) });
                    dlg.close();
                }
            }
        }
    }
}
