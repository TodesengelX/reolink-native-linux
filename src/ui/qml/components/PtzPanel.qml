import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// PTZ side panel for one camera, docked inside its live pane: 3D positioning
// toggle, pad + speed, zoom and focus (absolute sliders and hold buttons),
// presets with thumbnails, patrol, guard position and AI auto-tracking. Every
// section is gated on what the camera reports, and the state is read back from
// the device (fetchPtzState) rather than assumed.
Rectangle {
    id: panel
    property int deviceRow: -1
    property bool threeD: false        // 3D positioning armed; owned by the pane
    property bool threeDAvailable: true // false on a rotated view (coordinates wouldn't map)
    signal toggleThreeD()
    signal closeRequested()
    signal captureThumbnail(string path) // ask the pane to save its current frame there

    readonly property var cam: deviceRow >= 0 ? Devices.cameraInfo(deviceRow) : ({})
    property int speed: 32
    property var zoom: null    // { pos, min, max }
    property var focusPos: null
    property var presets: []   // enabled presets, by id
    property int nextPresetId: -1
    property var patrols: []   // enabled patrols
    // Which patrol this panel started. The NVR's GetPtzPatrol keeps `running` at
    // 0 even mid-patrol, so the state is tracked here; any manual move ends it,
    // as it does on the camera.
    property int patrolRunning: -1
    property var guard: null   // PtzGuard
    property var ai: null      // AiCfg (with _range)
    property string status: ""
    property int thumbStamp: 0 // bumps to reload thumbnails after a save
    property int confirmDeleteId: -1
    property bool confirmSetGuard: false

    width: 264
    radius: Theme.radius
    color: "#ee0d141b"
    border.color: Theme.border

    // Speed is remembered per camera, keyed like the other per-camera settings.
    Settings { id: ptzStore; category: "ptz" }
    function speedKey() { return "speed_h" + cam.hostId + "c" + cam.channel; }

    function load() {
        if (deviceRow < 0)
            return;
        speed = parseInt(ptzStore.value(speedKey(), 32));
        Devices.fetchPtzState(deviceRow);
    }
    // Creation sets deviceRow too; load once then, and again only on a real change.
    property bool ready: false
    Component.onCompleted: { ready = true; load(); }
    onDeviceRowChanged: if (ready) load()

    function rangeOf(v, key) {
        var r = v._range ? v._range.ZoomFocus : null;
        return r && r[key] && r[key].pos ? r[key].pos : null;
    }
    function apply(v) {
        if (v.GetZoomFocus) {
            var zf = v.GetZoomFocus.ZoomFocus;
            var zr = rangeOf(v.GetZoomFocus, "zoom"), fr = rangeOf(v.GetZoomFocus, "focus");
            zoom = { pos: zf.zoom.pos, min: zr ? zr.min : (zoom ? zoom.min : 0),
                     max: zr ? zr.max : (zoom ? zoom.max : 0) };
            focusPos = { pos: zf.focus.pos, min: fr ? fr.min : (focusPos ? focusPos.min : 0),
                         max: fr ? fr.max : (focusPos ? focusPos.max : 0) };
        }
        if (v.GetPtzPreset) {
            var all = v.GetPtzPreset.PtzPreset || [];
            var used = {}, on = [];
            for (var i = 0; i < all.length; i++)
                if (parseInt(all[i].enable) === 1) {
                    on.push(all[i]);
                    used[all[i].id] = true;
                }
            on.sort(function (a, b) { return a.id - b.id; });
            presets = on;
            nextPresetId = -1;
            for (var id = 0; id < Math.max(64, all.length); id++)
                if (!used[id]) { nextPresetId = id; break; }
        }
        if (v.GetPtzPatrol) {
            var ps = v.GetPtzPatrol.PtzPatrol || [];
            patrols = ps.filter(function (p) { return parseInt(p.enable) === 1; });
        }
        if (v.GetPtzGuard)
            guard = v.GetPtzGuard.PtzGuard;
        if (v.GetAiCfg)
            ai = v.GetAiCfg;
    }

    // Lens positions settle a moment after a move; the camera refocuses itself.
    Timer { id: lensRefresh; interval: 2000; onTriggered: { Devices.fetchZoomFocus(panel.deviceRow); lensRefreshLate.restart(); } }
    Timer { id: lensRefreshLate; interval: 3500; onTriggered: Devices.fetchZoomFocus(panel.deviceRow) }
    Timer { id: stateRefresh; interval: 800; onTriggered: Devices.fetchPtzState(panel.deviceRow) }
    function lensMoved() { lensRefresh.restart(); patrolRunning = -1; }

    Connections {
        target: Devices
        function onPtzStateLoaded(row, values) { if (row === panel.deviceRow) panel.apply(values); }
        function onPtzCommandDone(row, command, ok, error) {
            if (row !== panel.deviceRow)
                return;
            panel.status = ok ? "" : qsTr("%1 failed: %2").arg(command).arg(error);
            if (!ok)
                return;
            if (command === "StartPatrol" || command === "StopPatrol")
                return;
            if (command === "Set3DPos" || command === "StartZoomFocus")
                panel.lensMoved();
            else
                stateRefresh.restart();
        }
    }

    // Used by the pane: mouse wheel over the video in 3D mode zooms the lens.
    function nudgeZoom(steps) {
        if (!zoom || zoom.max <= zoom.min)
            return;
        var target = Math.max(zoom.min, Math.min(zoom.max, zoom.pos + steps));
        if (target === zoom.pos)
            return;
        zoom = { pos: target, min: zoom.min, max: zoom.max };
        Devices.ptzZoomFocusTo(deviceRow, "zoom", target);
    }

    // ---- small building blocks --------------------------------------------
    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        color: Theme.textMuted
        font.pixelSize: 11
        font.bold: true
        font.capitalization: Font.AllUppercase
    }
    component PanelButton: Rectangle {
        property string label: ""
        property bool active: false
        property bool danger: false
        signal activated()
        implicitWidth: btnText.implicitWidth + 18
        implicitHeight: 26
        radius: 4
        color: active ? Theme.accentDim : (btnHover.hovered ? Theme.surfaceAlt : Theme.surface)
        border.color: danger ? Theme.danger : (active ? Theme.accent : Theme.border)
        Text {
            id: btnText
            anchors.centerIn: parent
            text: parent.label
            color: parent.danger ? Theme.danger : Theme.text
            font.pixelSize: 12
        }
        HoverHandler { id: btnHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.activated() }
    }
    // Press-and-hold lens button: continuous PtzCtrl until release.
    component HoldButton: Rectangle {
        property string op: ""
        property string glyph: ""
        implicitWidth: 30
        implicitHeight: 26
        radius: 4
        color: holdArea.pressed ? Theme.accentDim : Theme.surface
        border.color: Theme.border
        Text { anchors.centerIn: parent; text: parent.glyph; color: Theme.text; font.pixelSize: 14 }
        MouseArea {
            id: holdArea
            anchors.fill: parent
            preventStealing: true
            onPressed: Devices.ptzMove(panel.deviceRow, parent.op, panel.speed)
            onReleased: { Devices.ptzStop(panel.deviceRow); panel.lensMoved(); }
            onCanceled: { Devices.ptzStop(panel.deviceRow); panel.lensMoved(); }
        }
    }
    component LensRow: RowLayout {
        id: lens
        property string what: "zoom"   // "zoom" | "focus"
        property var lensState: null   // { pos, min, max }
        property string decOp: ""
        property string incOp: ""
        Layout.fillWidth: true
        spacing: 4
        HoldButton { op: lens.decOp; glyph: "−" }
        Slider {
            id: lensSlider
            Layout.fillWidth: true
            enabled: !!lens.lensState && lens.lensState.max > lens.lensState.min
            from: lens.lensState ? lens.lensState.min : 0
            to: lens.lensState ? lens.lensState.max : 1
            stepSize: 1
            value: lens.lensState ? lens.lensState.pos : 0
            onPressedChanged: if (!pressed) {
                Devices.ptzZoomFocusTo(panel.deviceRow, lens.what, Math.round(value));
                panel.lensMoved();
            }
        }
        HoldButton { op: lens.incOp; glyph: "+" }
        Text {
            Layout.preferredWidth: 58
            horizontalAlignment: Text.AlignRight
            text: lens.lensState ? Math.round(lensSlider.value) + " / " + lens.lensState.max : "—"
            color: Theme.textMuted
            font.pixelSize: 11
        }
    }

    // ---- content ------------------------------------------------------------
    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("PTZ"); color: Theme.text; font.pixelSize: 14; font.bold: true }
                Item { Layout.fillWidth: true }
                PanelButton {
                    visible: panel.cam.capPtz3D === true
                    label: "3D"
                    active: panel.threeD
                    opacity: panel.threeDAvailable ? 1 : 0.4
                    onActivated: if (panel.threeDAvailable) panel.toggleThreeD()
                    ToolTip.visible: tdHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text: panel.threeDAvailable
                                  ? qsTr("3D positioning: click the video to centre that spot, drag a box to zoom into it, double-click to zoom in. Esc leaves.")
                                  : qsTr("3D positioning isn't available while the view is rotated")
                    HoverHandler { id: tdHover }
                }
                PanelButton { label: "✕"; onActivated: panel.closeRequested() }
            }

            PtzPad {
                Layout.alignment: Qt.AlignHCenter
                deviceRow: panel.deviceRow
                showZoom: false
                speed: panel.speed
                onMoved: panel.patrolRunning = -1
                color: "transparent"
                border.color: "transparent"
            }

            RowLayout {
                visible: panel.cam.capPtzSpeed === true
                Layout.fillWidth: true
                Text { text: qsTr("Speed"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 44 }
                Slider {
                    Layout.fillWidth: true
                    from: 1; to: 64; stepSize: 1
                    value: panel.speed
                    onMoved: {
                        panel.speed = Math.round(value);
                        ptzStore.setValue(panel.speedKey(), panel.speed);
                    }
                }
                Text { text: panel.speed; color: Theme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 22 }
            }

            SectionTitle { text: qsTr("Zoom"); visible: panel.cam.capZoom === true }
            LensRow {
                visible: panel.cam.capZoom === true
                what: "zoom"; lensState: panel.zoom; decOp: "ZoomDec"; incOp: "ZoomInc"
            }
            SectionTitle { text: qsTr("Focus"); visible: panel.cam.capFocus === true }
            LensRow {
                visible: panel.cam.capFocus === true
                what: "focus"; lensState: panel.focusPos; decOp: "FocusDec"; incOp: "FocusInc"
            }
            Text {
                visible: panel.cam.capFocus === true
                Layout.fillWidth: true
                text: qsTr("The camera refocuses on its own after every move.")
                color: Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            // ---- presets ----
            SectionTitle { text: qsTr("Presets"); visible: panel.cam.capPtzPreset === true }
            Repeater {
                model: panel.cam.capPtzPreset === true ? panel.presets : []
                delegate: Rectangle {
                    id: presetRow
                    required property var modelData
                    readonly property string thumbPath: Devices.ptzPresetImagePath(panel.deviceRow, modelData.id)
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 4
                    color: presetHover.hovered ? Theme.surfaceAlt : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        spacing: 8
                        RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        // Going to the preset is a tap on its picture or name; the
                        // buttons beside them must not also trigger it.
                        TapHandler {
                            onTapped: if (panel.confirmDeleteId !== presetRow.modelData.id) {
                                Devices.ptzPreset(panel.deviceRow, presetRow.modelData.id);
                                panel.lensMoved();
                            }
                        }
                        Rectangle {
                            implicitWidth: 56; implicitHeight: 32
                            color: Theme.paneBackground
                            border.color: Theme.border
                            Image {
                                id: thumb
                                anchors.fill: parent
                                anchors.margins: 1
                                fillMode: Image.PreserveAspectCrop
                                cache: false
                                asynchronous: true
                                source: "file://" + presetRow.thumbPath + "?" + panel.thumbStamp
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: thumb.status !== Image.Ready
                                text: presetRow.modelData.id
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: presetRow.modelData.name || qsTr("Preset %1").arg(presetRow.modelData.id)
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        }
                        PanelButton {
                            visible: panel.confirmDeleteId !== presetRow.modelData.id && presetHover.hovered
                            label: "✕"
                            onActivated: panel.confirmDeleteId = presetRow.modelData.id
                        }
                        PanelButton {
                            visible: panel.confirmDeleteId === presetRow.modelData.id
                            label: qsTr("Delete")
                            danger: true
                            onActivated: {
                                Devices.ptzPresetDelete(panel.deviceRow, presetRow.modelData.id);
                                panel.confirmDeleteId = -1;
                            }
                        }
                        PanelButton {
                            visible: panel.confirmDeleteId === presetRow.modelData.id
                            label: qsTr("Keep")
                            onActivated: panel.confirmDeleteId = -1
                        }
                    }
                    HoverHandler { id: presetHover; cursorShape: Qt.PointingHandCursor }
                }
            }
            RowLayout {
                visible: panel.cam.capPtzPreset === true && panel.nextPresetId >= 0
                Layout.fillWidth: true
                spacing: 4
                TextField {
                    id: presetName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name for this view")
                    maximumLength: 31
                    color: Theme.text
                    placeholderTextColor: Theme.textMuted
                    font.pixelSize: 12
                    background: Rectangle { color: Theme.surface; border.color: presetName.activeFocus ? Theme.accent : Theme.border; radius: 4 }
                    onAccepted: savePreset.activated()
                }
                PanelButton {
                    id: savePreset
                    label: qsTr("Save")
                    onActivated: {
                        var id = panel.nextPresetId;
                        var name = presetName.text.trim() || qsTr("Preset %1").arg(id);
                        Devices.ptzPresetSave(panel.deviceRow, id, name);
                        panel.captureThumbnail(Devices.ptzPresetImagePath(panel.deviceRow, id));
                        presetName.text = "";
                    }
                }
            }

            // ---- patrol ----
            SectionTitle { text: qsTr("Patrol"); visible: panel.patrols.length > 0 }
            Repeater {
                model: panel.patrols
                delegate: RowLayout {
                    required property var modelData
                    readonly property bool running: panel.patrolRunning === modelData.id
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name ? qsTr("Patrol “%1”").arg(modelData.name) : qsTr("Patrol %1").arg(modelData.id)
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    PanelButton {
                        label: parent.running ? qsTr("Stop") : qsTr("Start")
                        active: parent.running
                        onActivated: {
                            Devices.ptzPatrol(panel.deviceRow, parent.modelData.id, !parent.running);
                            panel.patrolRunning = parent.running ? -1 : parent.modelData.id;
                        }
                    }
                }
            }

            // ---- guard position ----
            SectionTitle { text: qsTr("Home position"); visible: panel.guard !== null }
            Flow {
                visible: panel.guard !== null
                Layout.fillWidth: true
                spacing: 4
                PanelButton {
                    visible: panel.guard !== null && parseInt(panel.guard.bexistPos) === 1
                    label: qsTr("Return to home")
                    onActivated: { Devices.ptzGuard(panel.deviceRow, "toPos"); panel.lensMoved(); }
                }
                PanelButton {
                    visible: !panel.confirmSetGuard
                    label: qsTr("Set current view")
                    onActivated: panel.confirmSetGuard = true
                }
                PanelButton {
                    visible: panel.confirmSetGuard
                    label: qsTr("Replace home")
                    danger: true
                    onActivated: { Devices.ptzGuard(panel.deviceRow, "setPos"); panel.confirmSetGuard = false; }
                }
                PanelButton {
                    visible: panel.confirmSetGuard
                    label: qsTr("Cancel")
                    onActivated: panel.confirmSetGuard = false
                }
            }
            RowLayout {
                visible: panel.guard !== null
                Layout.fillWidth: true
                Switch {
                    id: guardReturn
                    checked: panel.guard !== null && parseInt(panel.guard.benable) === 1
                    onClicked: Devices.ptzGuardOptions(panel.deviceRow, checked, parseInt(panel.guard.timeout))
                }
                Text { text: qsTr("Return after"); color: Theme.textMuted; font.pixelSize: 12 }
                ComboBox {
                    id: guardTimeout
                    Layout.preferredWidth: 78
                    enabled: guardReturn.checked
                    readonly property var secs: [10, 20, 30, 60, 120, 300, 600]
                    model: secs.map(function (s) { return s < 60 ? s + " s" : (s / 60) + " min"; })
                    currentIndex: panel.guard ? Math.max(0, secs.indexOf(parseInt(panel.guard.timeout))) : 3
                    onActivated: (i) => Devices.ptzGuardOptions(panel.deviceRow, true, secs[i])
                }
            }

            // ---- AI auto-tracking ----
            SectionTitle { text: qsTr("Auto-tracking"); visible: panel.cam.capAiTrack === true && panel.ai !== null }
            ColumnLayout {
                visible: panel.cam.capAiTrack === true && panel.ai !== null
                Layout.fillWidth: true
                spacing: 4
                readonly property bool trackingOn: panel.ai !== null && parseInt(panel.ai.bSmartTrack) === 1
                RowLayout {
                    Switch {
                        checked: parent.parent.trackingOn
                        onClicked: Devices.setAiTracking(panel.deviceRow, { bSmartTrack: checked ? 1 : 0 })
                    }
                    Text { text: qsTr("Follow what it detects"); color: Theme.text; font.pixelSize: 12 }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        // Only the types this camera can detect.
                        model: {
                            var a = panel.ai, keys = [];
                            if (!a || !a.trackType)
                                return keys;
                            for (var k in a.trackType)
                                if (!a.AiDetectType || parseInt(a.AiDetectType[k]) === 1)
                                    keys.push(k);
                            return keys;
                        }
                        delegate: PanelButton {
                            required property string modelData
                            label: ({ people: qsTr("People"), vehicle: qsTr("Vehicles"), dog_cat: qsTr("Pets"),
                                      face: qsTr("Faces"), package: qsTr("Packages") })[modelData] || modelData
                            active: parseInt(panel.ai.trackType[modelData]) === 1
                            onActivated: {
                                var t = Object.assign({}, panel.ai.trackType);
                                t[modelData] = active ? 0 : 1;
                                Devices.setAiTracking(panel.deviceRow, { trackType: t });
                            }
                        }
                    }
                }
                RowLayout {
                    Text { text: qsTr("Back to home"); color: Theme.textMuted; font.pixelSize: 12 }
                    ComboBox {
                        Layout.preferredWidth: 78
                        readonly property var secs: panel.ai && panel.ai._range && panel.ai._range.aiStopBackTime
                                                    ? panel.ai._range.aiStopBackTime : [10, 20, 30, 60]
                        model: secs.map(function (s) { return s + " s"; })
                        currentIndex: panel.ai ? Math.max(0, secs.indexOf(parseInt(panel.ai.aiStopBackTime))) : 0
                        onActivated: (i) => Devices.setAiTracking(panel.deviceRow, { aiStopBackTime: secs[i] })
                    }
                    Text { text: qsTr("after it stops"); color: Theme.textMuted; font.pixelSize: 12 }
                }
            }

            Text {
                visible: panel.status !== ""
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: panel.status
                color: Theme.danger
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
