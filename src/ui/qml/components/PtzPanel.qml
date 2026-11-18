import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// PTZ side panel for one camera, docked inside its live pane. A control deck
// that never moves (round pad, speed, zoom, focus, auto focus) sits above
// tabs for presets, patrol, home position and AI auto-tracking. Every part is
// gated on what the camera reports, and state is read back from the device
// (fetchPtzState) rather than assumed.
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
    property var guard: null   // PtzGuard: the camera's home ("guard") position
    property var ai: null      // AiCfg (with _range)
    property string status: ""
    property int thumbStamp: 0 // bumps to reload thumbnails after a save
    property int confirmDeleteId: -1
    property bool confirmSetHome: false
    property bool savingPreset: false
    property bool focusing: false
    property bool savingHome: false // "Set current view as home" sent, no answer yet
    property bool homeSaved: false  // shows the confirmation for a few seconds
    readonly property bool stickMode: ptzStore.padStyle === "stick"
    readonly property bool hasHome: guard !== null && parseInt(guard.bexistPos) === 1

    width: 304
    // Preset cards: two 16:9 pictures across the content width (24 = margins, 8 = gap).
    readonly property int cardHeight: Math.round((width - 24 - 8) / 2 * 9 / 16)
    radius: 12
    color: "#f00f151c"
    border.color: Theme.border

    // Speed and the open tab are remembered; speed per camera.
    Settings {
        id: ptzStore
        category: "ptz"
        property string tab: "presets"
        property string padStyle: "arrows" // "arrows" | "stick"
    }
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
    Timer { id: homeSavedTimer; interval: 5000; onTriggered: panel.homeSaved = false }
    function lensMoved() { lensRefresh.restart(); patrolRunning = -1; }

    Connections {
        target: Devices
        function onPtzStateLoaded(row, values) { if (row === panel.deviceRow) panel.apply(values); }
        function onPtzCommandDone(row, command, ok, error) {
            if (row !== panel.deviceRow)
                return;
            if (command === "AutoFocus")
                panel.focusing = false;
            if (command === "SetPtzGuard" && panel.savingHome) {
                panel.savingHome = false;
                panel.homeSaved = ok;
                if (ok)
                    homeSavedTimer.restart();
            }
            panel.status = ok ? "" : qsTr("%1 failed: %2").arg(command).arg(error);
            if (!ok)
                return;
            if (command === "StartPatrol" || command === "StopPatrol")
                return;
            if (command === "Set3DPos" || command === "StartZoomFocus" || command === "AutoFocus")
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
    function goHome() {
        Devices.ptzGuard(deviceRow, "toPos");
        lensMoved();
    }

    // Tabs this camera has something for, in order; falls back to the first.
    readonly property var tabs: {
        var t = [];
        if (cam.capPtzPreset === true) t.push({ key: "presets", label: qsTr("Presets") });
        if (cam.capPtzPatrol === true || patrols.length > 0) t.push({ key: "patrol", label: qsTr("Patrol") });
        if (guard !== null) t.push({ key: "home", label: qsTr("Home") });
        if (cam.capAiTrack === true && ai !== null) t.push({ key: "tracking", label: qsTr("Tracking") });
        return t;
    }
    readonly property string tab: {
        for (var i = 0; i < tabs.length; i++)
            if (tabs[i].key === ptzStore.tab) return ptzStore.tab;
        return tabs.length > 0 ? tabs[0].key : "";
    }

    // ---- themed building blocks ---------------------------------------------
    component Pill: Rectangle {
        id: pill
        property string label: ""
        property string icon: ""
        property bool active: false
        property bool primary: false
        property bool danger: false
        property bool enabledPill: true
        signal activated()
        implicitWidth: pillRow.implicitWidth + 24
        implicitHeight: 30
        radius: height / 2
        opacity: enabledPill ? 1 : 0.45
        color: (active || primary) ? (pillHover.hovered ? Theme.accent : Theme.accentDim)
                                   : (pillHover.hovered ? Theme.border : Theme.surfaceAlt)
        border.color: danger ? Theme.danger : ((active || primary) ? Theme.accent : Theme.border)
        Behavior on color { ColorAnimation { duration: 100 } }
        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 6
            Text {
                visible: pill.icon !== ""
                text: pill.icon
                color: pill.danger ? Theme.danger : Theme.text
                font.pixelSize: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: pill.label
                color: pill.danger ? Theme.danger : Theme.text
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        HoverHandler { id: pillHover; cursorShape: pill.enabledPill ? Qt.PointingHandCursor : Qt.ArrowCursor }
        TapHandler {
            enabled: pill.enabledPill
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: pill.activated()
        }
    }
    // Round press-and-hold lens button: continuous PtzCtrl until release.
    component HoldButton: Rectangle {
        id: hold
        property string op: ""
        property string glyph: ""
        implicitWidth: 30
        implicitHeight: 30
        radius: width / 2
        color: holdArea.pressed ? Theme.accentDim : (holdArea.containsMouse ? Theme.border : Theme.surfaceAlt)
        border.color: holdArea.containsMouse ? Theme.accent : Theme.border
        Text { anchors.centerIn: parent; text: hold.glyph; color: Theme.text; font.pixelSize: 16 }
        MouseArea {
            id: holdArea
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.PointingHandCursor
            onPressed: Devices.ptzMove(panel.deviceRow, hold.op, panel.speed)
            onReleased: { Devices.ptzStop(panel.deviceRow); panel.lensMoved(); }
            onCanceled: { Devices.ptzStop(panel.deviceRow); panel.lensMoved(); }
        }
    }
    component ThemedSlider: Slider {
        id: ts
        implicitHeight: 24
        background: Item {
            x: ts.leftPadding
            y: ts.topPadding + ts.availableHeight / 2 - height / 2
            width: ts.availableWidth
            height: 4
            Rectangle { anchors.fill: parent; radius: 2; color: Theme.border }
            Rectangle { width: ts.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
        }
        handle: Rectangle {
            x: ts.leftPadding + ts.visualPosition * (ts.availableWidth - width)
            y: ts.topPadding + ts.availableHeight / 2 - height / 2
            width: ts.pressed || ts.hovered ? 16 : 14
            height: width
            radius: width / 2
            color: ts.pressed ? Theme.text : Theme.accent
            border.color: "#0f151c"
            border.width: 2
            Behavior on width { NumberAnimation { duration: 80 } }
        }
    }
    component ThemedSwitch: Switch {
        id: sw
        indicator: Rectangle {
            implicitWidth: 38
            implicitHeight: 20
            x: sw.leftPadding
            y: sw.height / 2 - height / 2
            radius: height / 2
            color: sw.checked ? Theme.accentDim : Theme.surfaceAlt
            border.color: sw.checked ? Theme.accent : Theme.border
            Rectangle {
                width: 14; height: 14; radius: 7
                anchors.verticalCenter: parent.verticalCenter
                x: sw.checked ? parent.width - width - 3 : 3
                color: sw.checked ? Theme.text : Theme.textMuted
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Text {
            leftPadding: sw.indicator.width + 10
            text: sw.text
            color: Theme.text
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }
    }
    // A small choice of values as chips (timeouts and the like).
    component Chips: Flow {
        id: chips
        property var options: []   // [{ value, label }]
        property var value
        signal picked(var value)
        spacing: 4
        Repeater {
            model: chips.options
            delegate: Pill {
                required property var modelData
                label: modelData.label
                active: modelData.value === chips.value
                implicitHeight: 26
                onActivated: chips.picked(modelData.value)
            }
        }
    }
    component Caption: Text {
        color: Theme.textMuted
        font.pixelSize: 11
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }
    component LensBlock: ColumnLayout {
        id: lens
        property string title: ""
        property string what: "zoom"    // "zoom" | "focus"
        property var lensState: null    // { pos, min, max }
        property string decOp: ""
        property string incOp: ""
        Layout.fillWidth: true
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Text { text: lens.title; color: Theme.text; font.pixelSize: 12; font.bold: true }
            Item { Layout.fillWidth: true }
            Text {
                text: lens.lensState ? Math.round(lensSlider.value) + " / " + lens.lensState.max : "—"
                color: Theme.textMuted
                font.pixelSize: 11
                font.family: "monospace"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            HoldButton { op: lens.decOp; glyph: "−" }
            ThemedSlider {
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
        }
    }

    // ---- layout ---------------------------------------------------------------
    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 10

            // Header: title, camera, 3D, close.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Text { text: qsTr("PTZ"); color: Theme.text; font.pixelSize: 15; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: panel.cam.name || ""
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                Pill {
                    visible: panel.cam.capPtz3D === true
                    icon: "⌖"
                    label: "3D"
                    active: panel.threeD
                    enabledPill: panel.threeDAvailable
                    onActivated: panel.toggleThreeD()
                    ToolTip.visible: tdHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text: panel.threeDAvailable
                                  ? qsTr("3D positioning: click the video to centre that spot, drag a box to zoom into it, double-click to zoom in. Esc leaves.")
                                  : qsTr("3D positioning isn't available while the view is rotated")
                    HoverHandler { id: tdHover }
                }
                Rectangle {
                    width: 30; height: 30; radius: 15
                    color: closeArea.containsMouse ? Theme.border : "transparent"
                    Text { anchors.centerIn: parent; text: "✕"; color: Theme.textMuted; font.pixelSize: 13 }
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.closeRequested()
                    }
                }
            }

            // ---- control deck ----
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: deck.implicitHeight + 24
                radius: 10
                color: Theme.surface
                border.color: Theme.border

                ColumnLayout {
                    id: deck
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    // Arrow pad or joystick, with the style switch in the corner. The
                    // joystick's centre is its knob, so its home button sits opposite.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 156
                        Loader {
                            anchors.centerIn: parent
                            sourceComponent: panel.stickMode ? stickComponent : padComponent
                        }
                        Component {
                            id: padComponent
                            PtzPad {
                                deviceRow: panel.deviceRow
                                speed: panel.speed
                                showHome: panel.hasHome
                                onMoved: panel.patrolRunning = -1
                                onHomeRequested: panel.goHome()
                            }
                        }
                        Component {
                            id: stickComponent
                            PtzStick {
                                deviceRow: panel.deviceRow
                                maxSpeed: panel.speed
                                onMoved: panel.patrolRunning = -1
                            }
                        }
                        Rectangle { // style switch
                            anchors.top: parent.top
                            anchors.right: parent.right
                            width: 30
                            height: styleColumn.implicitHeight + 4
                            radius: width / 2
                            color: Theme.surfaceAlt
                            border.color: Theme.border
                            Column {
                                id: styleColumn
                                anchors.centerIn: parent
                                spacing: 2
                                Repeater {
                                    model: [{ key: "arrows", glyph: "✛", tip: qsTr("Arrows: hold one to move at the set speed") },
                                            { key: "stick", glyph: "◉", tip: qsTr("Joystick: push further to move faster") }]
                                    delegate: Rectangle {
                                        id: styleButton
                                        required property var modelData
                                        readonly property bool current: ptzStore.padStyle === modelData.key
                                        width: 26
                                        height: 26
                                        radius: 13
                                        color: current ? Theme.accentDim : (styleHover.hovered ? Theme.border : "transparent")
                                        Text {
                                            anchors.centerIn: parent
                                            text: styleButton.modelData.glyph
                                            color: styleButton.current ? Theme.text : Theme.textMuted
                                            font.pixelSize: 13
                                        }
                                        HoverHandler { id: styleHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: ptzStore.padStyle = styleButton.modelData.key }
                                        ToolTip.visible: styleHover.hovered
                                        ToolTip.delay: 400
                                        ToolTip.text: modelData.tip
                                    }
                                }
                            }
                        }
                        Rectangle { // home, for the joystick
                            visible: panel.stickMode && panel.hasHome
                            anchors.top: parent.top
                            anchors.left: parent.left
                            width: 30
                            height: 30
                            radius: 15
                            color: stickHomeHover.hovered ? Theme.border : Theme.surfaceAlt
                            border.color: stickHomeHover.hovered ? Theme.accent : Theme.border
                            Text { anchors.centerIn: parent; text: "⌂"; color: Theme.text; font.pixelSize: 16 }
                            HoverHandler { id: stickHomeHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: panel.goHome() }
                            ToolTip.visible: stickHomeHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Return to home")
                        }
                    }

                    RowLayout {
                        visible: panel.cam.capPtzSpeed === true
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: panel.stickMode ? qsTr("Max speed") : qsTr("Speed"); color: Theme.textMuted; font.pixelSize: 12 }
                        ThemedSlider {
                            Layout.fillWidth: true
                            from: 1; to: 64; stepSize: 1
                            value: panel.speed
                            onMoved: {
                                panel.speed = Math.round(value);
                                ptzStore.setValue(panel.speedKey(), panel.speed);
                            }
                        }
                        Text {
                            text: panel.speed
                            color: Theme.textMuted
                            font.pixelSize: 11
                            font.family: "monospace"
                            Layout.preferredWidth: 20
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    Rectangle {
                        visible: panel.cam.capZoom === true || panel.cam.capFocus === true
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Theme.border
                    }

                    LensBlock {
                        visible: panel.cam.capZoom === true
                        title: qsTr("Zoom")
                        what: "zoom"; lensState: panel.zoom; decOp: "ZoomDec"; incOp: "ZoomInc"
                    }
                    LensBlock {
                        visible: panel.cam.capFocus === true
                        title: qsTr("Focus")
                        what: "focus"; lensState: panel.focusPos; decOp: "FocusDec"; incOp: "FocusInc"
                    }
                    Pill {
                        visible: panel.cam.capFocus === true && panel.cam.capZoom === true
                        Layout.alignment: Qt.AlignHCenter
                        icon: "◎"
                        label: panel.focusing ? qsTr("Focusing…") : qsTr("Auto focus")
                        enabledPill: !panel.focusing
                        onActivated: {
                            panel.focusing = true;
                            Devices.ptzAutoFocus(panel.deviceRow);
                        }
                        ToolTip.visible: afHover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Refocus now. The camera also refocuses by itself after every move.")
                        HoverHandler { id: afHover }
                    }
                }
            }

            // ---- tabs ----
            Rectangle {
                visible: panel.tabs.length > 0
                Layout.fillWidth: true
                implicitHeight: 34
                radius: 17
                color: Theme.surface
                border.color: Theme.border
                Row {
                    anchors.fill: parent
                    anchors.margins: 3
                    Repeater {
                        model: panel.tabs
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width / panel.tabs.length
                            height: parent.height
                            radius: height / 2
                            color: panel.tab === modelData.key ? Theme.accentDim
                                 : (tabHover.hovered ? Theme.surfaceAlt : "transparent")
                            Behavior on color { ColorAnimation { duration: 100 } }
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData.label
                                color: panel.tab === parent.modelData.key ? Theme.text : Theme.textMuted
                                font.pixelSize: 12
                                font.bold: panel.tab === parent.modelData.key
                            }
                            HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: ptzStore.tab = parent.modelData.key }
                        }
                    }
                }
            }

            // Presets: a grid of picture cards, plus one to save the current view.
            GridLayout {
                visible: panel.tab === "presets"
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: panel.presets
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property bool confirming: panel.confirmDeleteId === modelData.id
                        Layout.fillWidth: true
                        Layout.preferredHeight: panel.cardHeight
                        radius: 8
                        clip: true
                        color: Theme.paneBackground
                        border.color: cardHover.hovered ? Theme.accent : Theme.border
                        border.width: cardHover.hovered ? 2 : 1
                        Image {
                            id: thumb
                            anchors.fill: parent
                            anchors.margins: 1
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                            asynchronous: true
                            source: "file://" + Devices.ptzPresetImagePath(panel.deviceRow, card.modelData.id)
                                    + "?" + panel.thumbStamp
                        }
                        readonly property string label: modelData.name || qsTr("Preset %1").arg(modelData.id)
                        Text { // no picture saved yet: just the name
                            anchors.fill: parent
                            anchors.margins: 8
                            visible: thumb.status !== Image.Ready
                            text: card.label
                            color: Theme.textMuted
                            font.pixelSize: 15
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        Rectangle { // name over a dark strip at the bottom of the picture
                            visible: thumb.status === Image.Ready
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 22
                            gradient: Gradient {
                                GradientStop { position: 0; color: "#000d141b" }
                                GradientStop { position: 1; color: "#e60d141b" }
                            }
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                verticalAlignment: Text.AlignVCenter
                                text: card.label
                                color: Theme.text
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            enabled: !card.confirming
                            onTapped: { Devices.ptzPreset(panel.deviceRow, card.modelData.id); panel.lensMoved(); }
                        }
                        Rectangle { // delete, on hover
                            visible: cardHover.hovered && !card.confirming
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 4
                            width: 22; height: 22; radius: 11
                            color: delHover.hovered ? Theme.danger : "#cc0d141b"
                            Text { anchors.centerIn: parent; text: "✕"; color: Theme.text; font.pixelSize: 10 }
                            HoverHandler { id: delHover }
                            TapHandler {
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: panel.confirmDeleteId = card.modelData.id
                            }
                        }
                        Rectangle { // confirm delete
                            visible: card.confirming
                            anchors.fill: parent
                            color: "#e60d141b"
                            Column {
                                anchors.centerIn: parent
                                spacing: 4
                                Pill {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    label: qsTr("Delete")
                                    danger: true
                                    implicitHeight: 24
                                    onActivated: {
                                        Devices.ptzPresetDelete(panel.deviceRow, card.modelData.id);
                                        panel.confirmDeleteId = -1;
                                    }
                                }
                                Pill {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    label: qsTr("Keep")
                                    implicitHeight: 24
                                    onActivated: panel.confirmDeleteId = -1
                                }
                            }
                        }
                    }
                }
                Rectangle { // save the current view
                    visible: panel.nextPresetId >= 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: panel.cardHeight
                    radius: 8
                    color: addHover.hovered ? Theme.surfaceAlt : "transparent"
                    border.color: addHover.hovered || panel.savingPreset ? Theme.accent : Theme.border
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "＋"; color: Theme.accent; font.pixelSize: 22 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Save view"); color: Theme.textMuted; font.pixelSize: 11 }
                    }
                    HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { panel.savingPreset = true; presetName.forceActiveFocus(); } }
                }
            }
            RowLayout {
                visible: panel.tab === "presets" && panel.savingPreset
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: presetName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name this view")
                    maximumLength: 31
                    color: Theme.text
                    placeholderTextColor: Theme.textMuted
                    font.pixelSize: 12
                    background: Rectangle {
                        implicitHeight: 30
                        radius: 15
                        color: Theme.surface
                        border.color: presetName.activeFocus ? Theme.accent : Theme.border
                    }
                    leftPadding: 12
                    onAccepted: savePreset.activated()
                    Keys.onEscapePressed: panel.savingPreset = false
                }
                Pill {
                    id: savePreset
                    label: qsTr("Save")
                    primary: true
                    onActivated: {
                        var id = panel.nextPresetId;
                        var name = presetName.text.trim() || qsTr("Preset %1").arg(id);
                        Devices.ptzPresetSave(panel.deviceRow, id, name);
                        panel.captureThumbnail(Devices.ptzPresetImagePath(panel.deviceRow, id));
                        presetName.text = "";
                        panel.savingPreset = false;
                    }
                }
                Pill { label: "✕"; onActivated: { presetName.text = ""; panel.savingPreset = false; } }
            }
            Caption {
                visible: panel.tab === "presets" && panel.presets.length === 0
                text: qsTr("No presets yet. Frame a view and save it.")
            }

            // Patrol.
            ColumnLayout {
                visible: panel.tab === "patrol"
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: panel.patrols
                    delegate: Rectangle {
                        id: patrolRow
                        required property var modelData
                        readonly property bool running: panel.patrolRunning === modelData.id
                        Layout.fillWidth: true
                        implicitHeight: 44
                        radius: 10
                        color: Theme.surface
                        border.color: running ? Theme.accent : Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text {
                                    Layout.fillWidth: true
                                    text: patrolRow.modelData.name ? qsTr("Patrol “%1”").arg(patrolRow.modelData.name)
                                                                    : qsTr("Patrol %1").arg(patrolRow.modelData.id)
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: patrolRow.running ? qsTr("Running")
                                                            : qsTr("%n stop(s)", "", (patrolRow.modelData.preset || []).length)
                                    color: patrolRow.running ? Theme.accent : Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }
                            Pill {
                                icon: patrolRow.running ? "■" : "▶"
                                label: patrolRow.running ? qsTr("Stop") : qsTr("Start")
                                active: patrolRow.running
                                onActivated: {
                                    Devices.ptzPatrol(panel.deviceRow, patrolRow.modelData.id, !patrolRow.running);
                                    panel.patrolRunning = patrolRow.running ? -1 : patrolRow.modelData.id;
                                }
                            }
                        }
                    }
                }
                Caption {
                    visible: panel.patrols.length === 0
                    text: qsTr("No patrol is set up on this camera. Patrols are made in the camera's own settings.")
                }
            }

            // Home position (the camera's "guard" position).
            ColumnLayout {
                visible: panel.tab === "home"
                Layout.fillWidth: true
                spacing: 8
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Pill {
                        visible: panel.hasHome
                        primary: true
                        icon: "⌂"
                        label: qsTr("Return to home")
                        onActivated: panel.goHome()
                    }
                    Pill {
                        visible: !panel.confirmSetHome
                        label: qsTr("Set current view as home")
                        onActivated: panel.confirmSetHome = true
                    }
                }
                ColumnLayout {
                    visible: panel.confirmSetHome
                    Layout.fillWidth: true
                    spacing: 6
                    Caption { text: qsTr("Replace the home position with what the camera sees now?") }
                    Row {
                        spacing: 6
                        Pill {
                            label: qsTr("Replace home")
                            danger: true
                            onActivated: {
                                panel.homeSaved = false;
                                panel.savingHome = true;
                                Devices.ptzGuard(panel.deviceRow, "setPos");
                                panel.confirmSetHome = false;
                            }
                        }
                        Pill { label: qsTr("Cancel"); onActivated: panel.confirmSetHome = false }
                    }
                }
                Row { // outcome of "Set current view as home"
                    visible: panel.savingHome || panel.homeSaved
                    spacing: 6
                    Text {
                        text: panel.savingHome ? "…" : "✓"
                        color: panel.savingHome ? Theme.textMuted : Theme.online
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        text: panel.savingHome ? qsTr("Saving the home position…")
                                               : qsTr("Home position set to the current view")
                        color: panel.savingHome ? Theme.textMuted : Theme.online
                        font.pixelSize: 12
                    }
                }
                ThemedSwitch {
                    text: qsTr("Return home when idle")
                    checked: panel.guard !== null && parseInt(panel.guard.benable) === 1
                    onClicked: Devices.ptzGuardOptions(panel.deviceRow, checked, parseInt(panel.guard.timeout))
                }
                Caption {
                    visible: panel.guard !== null && parseInt(panel.guard.benable) === 1
                    text: qsTr("After")
                }
                Chips {
                    visible: panel.guard !== null && parseInt(panel.guard.benable) === 1
                    Layout.fillWidth: true
                    options: [10, 20, 30, 60, 120, 300, 600].map(function (s) {
                        return { value: s, label: s < 60 ? s + " s" : (s / 60) + " min" };
                    })
                    value: panel.guard ? parseInt(panel.guard.timeout) : 60
                    onPicked: (v) => Devices.ptzGuardOptions(panel.deviceRow, true, v)
                }
            }

            // AI auto-tracking.
            ColumnLayout {
                visible: panel.tab === "tracking"
                Layout.fillWidth: true
                spacing: 8
                ThemedSwitch {
                    text: qsTr("Follow what it detects")
                    checked: panel.ai !== null && parseInt(panel.ai.bSmartTrack) === 1
                    onClicked: Devices.setAiTracking(panel.deviceRow, { bSmartTrack: checked ? 1 : 0 })
                }
                Caption { text: qsTr("Follow") }
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
                        delegate: Pill {
                            required property string modelData
                            label: ({ people: qsTr("People"), vehicle: qsTr("Vehicles"), dog_cat: qsTr("Pets"),
                                      face: qsTr("Faces"), package: qsTr("Packages") })[modelData] || modelData
                            icon: active ? "✓" : ""
                            active: parseInt(panel.ai.trackType[modelData]) === 1
                            onActivated: {
                                var t = Object.assign({}, panel.ai.trackType);
                                t[modelData] = active ? 0 : 1;
                                Devices.setAiTracking(panel.deviceRow, { trackType: t });
                            }
                        }
                    }
                }
                Caption { text: qsTr("Back to home after the target stops for") }
                Chips {
                    Layout.fillWidth: true
                    options: (panel.ai && panel.ai._range && panel.ai._range.aiStopBackTime
                              ? panel.ai._range.aiStopBackTime : [10, 20, 30, 60]).map(function (s) {
                        return { value: s, label: s + " s" };
                    })
                    value: panel.ai ? parseInt(panel.ai.aiStopBackTime) : 30
                    onPicked: (v) => Devices.setAiTracking(panel.deviceRow, { aiStopBackTime: v })
                }
            }

            Rectangle {
                visible: panel.status !== ""
                Layout.fillWidth: true
                implicitHeight: statusText.implicitHeight + 14
                radius: 8
                color: "#22e5484d"
                border.color: Theme.danger
                Text {
                    id: statusText
                    anchors.fill: parent
                    anchors.margins: 7
                    text: panel.status
                    color: Theme.text
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                TapHandler { onTapped: panel.status = "" }
            }
        }
    }
}
