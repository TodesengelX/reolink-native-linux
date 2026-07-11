import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ReolinkApp
import ReolinkApp.Core

// Device Settings: device selector + category sidebar + panel. Panels fetch their
// Get* commands on show and render editable forms; writes are gated on the
// device's admin flag (disabled with a tooltip otherwise). Mirrors the official
// client's settings structure (DESIGN §6.7).
Item {
    id: page

    property int deviceRow: -1
    property string category: "system"
    property var settings: ({})      // cmd -> value map from the last fetch
    property bool isAdmin: false
    property string status: ""

    // Called from the sidebar's Settings action: focus this device row.
    function showDevice(row) {
        if (row >= 0 && row < Devices.count)
            deviceCombo.currentIndex = row;
    }

    readonly property var categories: [
        { key: "image",     label: qsTr("Image"),           cmds: ["GetImage", "GetIsp", "GetIrLights"] },
        { key: "display",   label: qsTr("Display / OSD"),    cmds: ["GetOsd"] },
        { key: "encoding",  label: qsTr("Encoding"),        cmds: ["GetEnc"] },
        { key: "recording", label: qsTr("Recording"),       cmds: ["GetRec"] },
        { key: "detection", label: qsTr("Detection / Alerts"), cmds: ["GetMdAlarm", "GetAiCfg"] },
        { key: "network",   label: qsTr("Network"),         cmds: ["GetLocalLink", "GetNetPort"] },
        { key: "storage",   label: qsTr("Storage"),         cmds: ["GetHddInfo"] },
        { key: "users",     label: qsTr("Users"),           cmds: ["GetUser"] },
        { key: "time",      label: qsTr("Time"),            cmds: ["GetTime", "GetNtp"] },
        { key: "system",    label: qsTr("System"),          cmds: ["GetDevInfo", "GetTime"] }
    ]

    function cmdsFor(key) {
        for (var i = 0; i < categories.length; i++)
            if (categories[i].key === key) return categories[i].cmds;
        return [];
    }
    property bool fetched: false     // a fetch has completed (values may be partial)
    function has(cmd) { return page.settings.hasOwnProperty(cmd); }

    // Alert enables (push/email/ftp) come over native Baichuan, not the flaky
    // HTTP-CGI — keys ok/push/email/ftp, each 0/1 or -1. Empty until loaded.
    property var alerts: ({})

    function fetch() {
        page.settings = ({});
        page.fetched = false;
        if (page.deviceRow >= 0) {
            page.status = qsTr("Loading…");
            Devices.fetchSettings(page.deviceRow, cmdsFor(page.category));
            if (page.category === "detection") {
                page.alerts = ({});
                Devices.fetchAlerts(page.deviceRow);
            }
        } else {
            page.status = qsTr("Select a device");
        }
    }
    // Nested lookup helper: val("GetEnc","Enc","mainStream","size").
    function val() {
        var o = page.settings;
        for (var i = 0; i < arguments.length; i++) {
            if (o === undefined || o === null) return undefined;
            o = o[arguments[i]];
        }
        return o;
    }

    Connections {
        target: Devices
        function onSettingsLoaded(row, values) {
            if (row === page.deviceRow) {
                page.settings = values;
                page.fetched = true;
                var f = values["_failed"] || [];
                page.status = f.length > 0 ? qsTr("%1 unavailable on this device").arg(f.join(", ")) : "";
            }
        }
        function onSettingsFailed(row, error) {
            if (row === page.deviceRow) { page.fetched = true; page.status = error; }
        }
        function onSettingApplied(row, command, ok, error) {
            if (row !== page.deviceRow)
                return;
            page.status = ok ? qsTr("%1 saved").arg(command) : (command + ": " + error);
            // Reflect user add/remove/password changes immediately.
            if (ok && (command === "AddUser" || command === "DelUser" || command === "ModifyUser"))
                page.fetch();
            // Re-read alert enables after a Baichuan toggle so the switch reflects
            // the device's confirmed state.
            if (command === "push" || command === "email" || command === "ftp")
                Devices.fetchAlerts(page.deviceRow);
        }
        function onAlertsLoaded(row, values) {
            if (row === page.deviceRow)
                page.alerts = values;
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        // ---- Category sidebar ----
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: 6

                Text { text: qsTr("Camera:"); color: Theme.textMuted; font.pixelSize: 11 }
                CameraComboBox {
                    id: deviceCombo
                    Layout.fillWidth: true
                    onCurrentIndexChanged: {
                        page.deviceRow = currentIndex;
                        page.isAdmin = currentIndex >= 0 ? Devices.isAdminAt(currentIndex) : false;
                        page.fetch();
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                Repeater {
                    model: page.categories
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 34
                        radius: 4
                        color: page.category === modelData.key ? Theme.accentDim
                             : catHover.hovered ? Theme.surfaceAlt : "transparent"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            text: modelData.label
                            color: page.category === modelData.key ? Theme.text : Theme.textMuted
                            font.pixelSize: 13
                        }
                        HoverHandler { id: catHover }
                        TapHandler { onTapped: { page.category = modelData.key; page.fetch(); } }
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    visible: !page.isAdmin && page.deviceRow >= 0
                    Layout.fillWidth: true
                    text: qsTr("Read-only (not an admin account)")
                    color: Theme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---- Panel ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing * 2
                spacing: Theme.spacing

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: {
                            for (var i = 0; i < page.categories.length; i++)
                                if (page.categories[i].key === page.category)
                                    return page.categories[i].label;
                            return "";
                        }
                        color: Theme.text
                        font.pixelSize: 18
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: page.status; color: Theme.textMuted; font.pixelSize: 12 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: {
                        switch (page.category) {
                        case "image": return imagePanel;
                        case "encoding": return encodingPanel;
                        case "display": return displayPanel;
                        case "detection": return detectionPanel;
                        case "recording": return recordingPanel;
                        case "network": return networkPanel;
                        case "storage": return storagePanel;
                        case "users": return usersPanel;
                        case "time": return timePanel;
                        case "system": return systemPanel;
                        default: return genericPanel;
                        }
                    }
                }
            }
        }
    }

    // Add-user / change-password dialog for the Users panel.
    UserEditDialog { id: userDialog; deviceRow: page.deviceRow }

    // Confirmation sheet for destructive actions (e.g. disk format).
    ConfirmDialog {
        id: confirmDialog
        onConfirmed: (payload) => {
            if (payload && payload.action === "format")
                Devices.applySetting(page.deviceRow, "Format", { "HddInfo": { "id": [payload.id] } });
        }
    }
    function confirmFormat(diskId, label) {
        confirmDialog.message = qsTr("Format %1?").arg(label);
        confirmDialog.detail = qsTr("This permanently erases ALL recordings on the disk and cannot be undone.");
        confirmDialog.confirmLabel = qsTr("Format disk");
        confirmDialog.payload = { action: "format", id: diskId };
        confirmDialog.open();
    }
    function openUserDialog(mode, name) {
        userDialog.mode = mode;
        userDialog.userName = name || "";
        userDialog.open();
    }

    // ---- Reusable form controls -------------------------------------------
    // A labeled slider that writes once, on release (so dragging doesn't spam the
    // NVR). `commit` receives the integer value.
    component SliderRow: RowLayout {
        property string label: ""
        property int from: 0
        property int to: 255
        property int value: 0
        property bool enabledCtl: true
        signal commit(int v)
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text { text: parent.label; color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 150 }
        Slider {
            id: sld
            Layout.fillWidth: true
            enabled: parent.enabledCtl
            from: parent.from; to: parent.to
            value: parent.value
            stepSize: 1
            onPressedChanged: if (!pressed) parent.commit(Math.round(value))
        }
        Text { text: Math.round(sld.value); color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 32 }
    }

    // A labeled dropdown of string options; writes the chosen option on change.
    component EnumRow: RowLayout {
        property string label: ""
        property var options: []
        property string value: ""
        property bool enabledCtl: true
        signal commit(string v)
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text { text: parent.label; color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 150 }
        ComboBox {
            id: cb
            Layout.preferredWidth: 200
            enabled: parent.enabledCtl
            model: parent.options
            currentIndex: Math.max(0, parent.options.indexOf(parent.value))
            onActivated: parent.commit(currentText)
        }
        Item { Layout.fillWidth: true }
    }

    component SwitchRow: RowLayout {
        property string label: ""
        property bool checked: false
        property bool enabledCtl: true
        signal commit(bool v)
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text { text: parent.label; color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 150 }
        Switch { checked: parent.checked; enabled: parent.enabledCtl; onToggled: parent.commit(checked) }
        Item { Layout.fillWidth: true }
    }

    // ---- Image panel (brightness/contrast/… + day-night + IR) -------------
    Component {
        id: imagePanel
        Item {
            // The NVR refused these camera commands (some proxy image/ISP poorly and
            // return HTTP 502) — say so instead of showing fake default sliders.
            ColumnLayout {
                anchors.fill: parent
                visible: page.fetched && !page.has("GetImage")
                spacing: Theme.spacing
                Text { text: qsTr("Image settings couldn't be loaded from this camera.")
                       color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Text { text: qsTr("The NVR returned an error proxying this camera's image/ISP commands (often HTTP 502). They may need to be changed on the camera directly or in the NVR's own menu.")
                       color: Theme.textMuted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                anchors.fill: parent
                visible: !(page.fetched && !page.has("GetImage"))
                spacing: Theme.spacing
                SliderRow {
                    label: qsTr("Brightness"); enabledCtl: page.isAdmin
                value: page.val("GetImage","Image","bright") || 128
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetImage",
                    { "Image": { "channel": Devices.channelOf(page.deviceRow), "bright": v } })
            }
            SliderRow {
                label: qsTr("Contrast"); enabledCtl: page.isAdmin
                value: page.val("GetImage","Image","contrast") || 128
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetImage",
                    { "Image": { "channel": Devices.channelOf(page.deviceRow), "contrast": v } })
            }
            SliderRow {
                label: qsTr("Saturation"); enabledCtl: page.isAdmin
                value: page.val("GetImage","Image","saturation") || 128
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetImage",
                    { "Image": { "channel": Devices.channelOf(page.deviceRow), "saturation": v } })
            }
            SliderRow {
                label: qsTr("Sharpness"); enabledCtl: page.isAdmin
                value: page.val("GetImage","Image","sharpen") || 128
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetImage",
                    { "Image": { "channel": Devices.channelOf(page.deviceRow), "sharpen": v } })
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            EnumRow {
                label: qsTr("Day / Night"); enabledCtl: page.isAdmin
                options: ["Auto", "Color", "Black&White"]
                value: page.val("GetIsp","Isp","dayNight") || "Auto"
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetIsp",
                    { "Isp": { "channel": Devices.channelOf(page.deviceRow), "dayNight": v } })
            }
            EnumRow {
                label: qsTr("Infrared lights"); enabledCtl: page.isAdmin
                options: ["Auto", "On", "Off"]
                value: page.val("GetIrLights","IrLights","state") || "Auto"
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetIrLights",
                    { "IrLights": { "channel": Devices.channelOf(page.deviceRow), "state": v } })
            }
            SwitchRow {
                label: qsTr("Mirror (horizontal)"); enabledCtl: page.isAdmin
                checked: page.val("GetIsp","Isp","mirror") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetIsp",
                    { "Isp": { "channel": Devices.channelOf(page.deviceRow), "mirror": v ? 1 : 0 } })
            }
            SwitchRow {
                label: qsTr("Flip (vertical)"); enabledCtl: page.isAdmin
                checked: page.val("GetIsp","Isp","flip") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetIsp",
                    { "Isp": { "channel": Devices.channelOf(page.deviceRow), "flip": v ? 1 : 0 } })
            }
            Item { Layout.fillHeight: true }
                Text {
                    text: qsTr("Changes apply on release. Options come from the camera's GetImage/GetIsp ranges.")
                    color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ---- Detection / Alerts panel (motion + AI types + push) --------------
    Component {
        id: detectionPanel
        ColumnLayout {
            spacing: Theme.spacing
            Text { text: qsTr("Motion detection"); color: Theme.text; font.pixelSize: 13; font.bold: true }
            SliderRow {
                label: qsTr("Sensitivity"); from: 1; to: 50; enabledCtl: page.isAdmin
                value: page.val("GetMdAlarm","MdAlarm","sensitivity") || 25
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetMdAlarm",
                    { "MdAlarm": { "channel": Devices.channelOf(page.deviceRow), "sensitivity": v } })
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            Text { text: qsTr("Smart (AI) detection"); color: Theme.text; font.pixelSize: 13; font.bold: true }
            SwitchRow {
                label: qsTr("People"); enabledCtl: page.isAdmin
                checked: page.val("GetAiCfg","AiDetectType","people") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetAiCfg",
                    { "channel": Devices.channelOf(page.deviceRow), "AiDetectType": { "people": v ? 1 : 0 } })
            }
            SwitchRow {
                label: qsTr("Vehicles"); enabledCtl: page.isAdmin
                checked: page.val("GetAiCfg","AiDetectType","vehicle") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetAiCfg",
                    { "channel": Devices.channelOf(page.deviceRow), "AiDetectType": { "vehicle": v ? 1 : 0 } })
            }
            SwitchRow {
                label: qsTr("Animals (pets)"); enabledCtl: page.isAdmin
                checked: page.val("GetAiCfg","AiDetectType","dog_cat") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetAiCfg",
                    { "channel": Devices.channelOf(page.deviceRow), "AiDetectType": { "dog_cat": v ? 1 : 0 } })
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            Text { text: qsTr("Alerts"); color: Theme.text; font.pixelSize: 13; font.bold: true }
            SwitchRow {
                label: qsTr("Push notifications")
                enabledCtl: page.isAdmin && page.alerts.ok === true
                checked: page.alerts.push === 1
                onCommit: (v) => Devices.setAlertEnable(page.deviceRow, "push", v)
            }
            SwitchRow {
                label: qsTr("Email on alarm")
                enabledCtl: page.isAdmin && page.alerts.ok === true
                checked: page.alerts.email === 1
                onCommit: (v) => Devices.setAlertEnable(page.deviceRow, "email", v)
            }
            SwitchRow {
                label: qsTr("FTP upload on alarm")
                enabledCtl: page.isAdmin && page.alerts.ok === true
                checked: page.alerts.ftp === 1
                onCommit: (v) => Devices.setAlertEnable(page.deviceRow, "ftp", v)
            }
            Text {
                visible: page.alerts.ok === false
                text: qsTr("Alert settings couldn't be read from the device.")
                color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
            Text {
                text: qsTr("Detection zones and per-type sensitivity/schedules are on the roadmap.")
                color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
    }

    // ---- Encoding panel ----
    Component {
        id: encodingPanel
        ColumnLayout {
            spacing: Theme.spacing
            SettingRow {
                label: qsTr("Main stream (Clear)")
                Text {
                    text: (page.val("GetEnc","Enc","mainStream","size") || "—") + "  ·  "
                        + (page.val("GetEnc","Enc","mainStream","bitRate") || "—") + " kbps  ·  "
                        + (page.val("GetEnc","Enc","mainStream","frameRate") || "—") + " fps  ·  "
                        + String(page.val("GetEnc","Enc","mainStream","vType") || "—").toUpperCase()
                    color: Theme.textMuted; font.pixelSize: 12
                }
            }
            SettingRow {
                label: qsTr("Sub stream (Fluent)")
                Text {
                    text: (page.val("GetEnc","Enc","subStream","size") || "—") + "  ·  "
                        + (page.val("GetEnc","Enc","subStream","bitRate") || "—") + " kbps  ·  "
                        + (page.val("GetEnc","Enc","subStream","frameRate") || "—") + " fps"
                    color: Theme.textMuted; font.pixelSize: 12
                }
            }
            SettingRow {
                label: qsTr("Audio")
                Switch {
                    checked: page.val("GetEnc","Enc","audio") === 1
                    enabled: page.isAdmin
                    onToggled: Devices.applySetting(page.deviceRow, "SetEnc",
                        { "Enc": { "channel": Devices.channelOf(page.deviceRow), "audio": checked ? 1 : 0 } })
                }
            }
            Item { Layout.fillHeight: true }
            Text {
                text: qsTr("Resolution/bitrate/framerate options populate from the device's GetEnc ranges.")
                color: Theme.textMuted; font.pixelSize: 11
            }
        }
    }

    // ---- Display / Image panel (OSD) ----
    Component {
        id: displayPanel
        ColumnLayout {
            spacing: Theme.spacing
            SettingRow {
                label: qsTr("Show camera name (OSD)")
                Switch {
                    checked: page.val("GetOsd","Osd","osdChannel","enable") === 1
                    enabled: page.isAdmin
                    onToggled: Devices.applySetting(page.deviceRow, "SetOsd",
                        { "Osd": { "channel": Devices.channelOf(page.deviceRow), "osdChannel": { "enable": checked ? 1 : 0 } } })
                }
            }
            SettingRow {
                label: qsTr("Show date/time (OSD)")
                Switch {
                    checked: page.val("GetOsd","Osd","osdTime","enable") === 1
                    enabled: page.isAdmin
                    onToggled: Devices.applySetting(page.deviceRow, "SetOsd",
                        { "Osd": { "channel": Devices.channelOf(page.deviceRow), "osdTime": { "enable": checked ? 1 : 0 } } })
                }
            }
            SettingRow {
                label: qsTr("Camera name")
                TextField {
                    Layout.preferredWidth: 200
                    text: page.val("GetOsd","Osd","osdChannel","name") || ""
                    enabled: page.isAdmin
                    color: Theme.text
                    background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: 4 }
                    onEditingFinished: Devices.applySetting(page.deviceRow, "SetOsd",
                        { "Osd": { "channel": Devices.channelOf(page.deviceRow), "osdChannel": { "name": text } } })
                }
            }
            Item { Layout.fillHeight: true }
            Text {
                text: qsTr("Brightness, contrast, day/night and IR settings load from GetImage/GetIsp.")
                color: Theme.textMuted; font.pixelSize: 11
            }
        }
    }

    // ---- System panel ----
    Component {
        id: systemPanel
        ColumnLayout {
            spacing: Theme.spacing
            SettingRow { label: qsTr("Name")
                Text { text: page.val("GetDevInfo","DevInfo","name") || "—"; color: Theme.textMuted; font.pixelSize: 12 } }
            SettingRow { label: qsTr("Model")
                Text { text: page.val("GetDevInfo","DevInfo","model") || "—"; color: Theme.textMuted; font.pixelSize: 12 } }
            SettingRow { label: qsTr("Firmware")
                Text { text: page.val("GetDevInfo","DevInfo","firmVer") || "—"; color: Theme.textMuted; font.pixelSize: 12 } }
            SettingRow { label: qsTr("Hardware")
                Text { text: page.val("GetDevInfo","DevInfo","hardVer") || "—"; color: Theme.textMuted; font.pixelSize: 12 } }
            SettingRow { label: qsTr("Channels")
                Text { text: String(page.val("GetDevInfo","DevInfo","channelNum") || "—"); color: Theme.textMuted; font.pixelSize: 12 } }
            Item { Layout.fillHeight: true }
            Rectangle {
                implicitWidth: rebootText.implicitWidth + 24
                height: 32
                radius: Theme.radius
                color: page.isAdmin ? (rebootHover.hovered ? Theme.danger : Theme.surfaceAlt) : Theme.surface
                border.color: page.isAdmin ? Theme.danger : Theme.border
                opacity: page.isAdmin ? 1 : 0.5
                Text {
                    id: rebootText
                    anchors.centerIn: parent
                    text: qsTr("Reboot device")
                    color: page.isAdmin ? Theme.text : Theme.textMuted
                    font.pixelSize: 12
                }
                HoverHandler { id: rebootHover; enabled: page.isAdmin }
                TapHandler { enabled: page.isAdmin; onTapped: Devices.reboot(page.deviceRow) }
            }
        }
    }

    // A read-only label:value row.
    component InfoRow: RowLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text { text: parent.label; color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 160 }
        Text { text: parent.value.length ? parent.value : "—"; color: Theme.text; font.pixelSize: 12
               Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
    }

    // ---- Recording panel (camera-level) ----
    Component {
        id: recordingPanel
        ColumnLayout {
            spacing: Theme.spacing
            SwitchRow {
                label: qsTr("Overwrite when full"); enabledCtl: page.isAdmin
                checked: page.val("GetRec","Rec","overwrite") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetRec",
                    { "Rec": { "channel": Devices.channelOf(page.deviceRow), "overwrite": v ? 1 : 0 } })
            }
            InfoRow { label: qsTr("Scheduled recording")
                value: page.val("GetRec","Rec","schedule","enable") === 1 ? qsTr("On") : qsTr("Off") }
            InfoRow { label: qsTr("Pre / post record")
                value: (page.val("GetRec","Rec","preRec") !== undefined ? page.val("GetRec","Rec","preRec") + qsTr(" min pre") : "")
                     + (page.val("GetRec","Rec","postRec") ? "  ·  " + page.val("GetRec","Rec","postRec") : "") }
            Item { Layout.fillHeight: true }
            Text { text: qsTr("Editing the weekly recording schedule grid is on the roadmap.")
                   color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

    // ---- Network panel (host-level, read-only) ----
    Component {
        id: networkPanel
        ColumnLayout {
            spacing: Theme.spacing
            InfoRow { label: qsTr("Connection"); value: page.val("GetLocalLink","LocalLink","activeLink") || "" }
            InfoRow { label: qsTr("Addressing")
                value: page.val("GetLocalLink","LocalLink","type") === "DHCP" ? qsTr("DHCP") : qsTr("Static") }
            InfoRow { label: qsTr("IP address"); value: page.val("GetLocalLink","LocalLink","static","ip") || "" }
            InfoRow { label: qsTr("Gateway"); value: page.val("GetLocalLink","LocalLink","static","gateway") || "" }
            InfoRow { label: qsTr("Subnet mask"); value: page.val("GetLocalLink","LocalLink","static","mask") || "" }
            InfoRow { label: qsTr("MAC address"); value: page.val("GetLocalLink","LocalLink","mac") || "" }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            Text { text: qsTr("Ports"); color: Theme.text; font.pixelSize: 13; font.bold: true }
            InfoRow { label: qsTr("RTSP")
                value: (page.val("GetNetPort","NetPort","rtspPort") || "—")
                     + (page.val("GetNetPort","NetPort","rtspEnable") === 1 ? "" : qsTr("  (disabled)")) }
            InfoRow { label: qsTr("ONVIF")
                value: (page.val("GetNetPort","NetPort","onvifPort") || "—")
                     + (page.val("GetNetPort","NetPort","onvifEnable") === 1 ? "" : qsTr("  (disabled)")) }
            InfoRow { label: qsTr("HTTPS")
                value: (page.val("GetNetPort","NetPort","httpsPort") || "—")
                     + (page.val("GetNetPort","NetPort","httpsEnable") === 1 ? "" : qsTr("  (disabled)")) }
            Item { Layout.fillHeight: true }
            Text { text: qsTr("Port and IP changes are read-only here — editing them from the app could lock out access.")
                   color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

    // ---- Storage panel (host-level, read-only) ----
    Component {
        id: storagePanel
        ColumnLayout {
            spacing: Theme.spacing
            Repeater {
                model: page.val("GetHddInfo","HddInfo") || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    InfoRow {
                        Layout.fillWidth: true
                        label: qsTr("Disk %1").arg((modelData.number !== undefined ? modelData.number : 0) + 1)
                        value: (modelData.storageType || qsTr("Disk"))
                             + "  ·  " + Math.round((modelData.capacity || 0) / 1024) + qsTr(" GB")
                             + "  ·  " + Math.round((modelData.size || 0) / 1024) + qsTr(" GB free")
                             + (modelData.mount ? "" : qsTr("  ·  not mounted"))
                             + (modelData.format ? "" : qsTr("  ·  unformatted"))
                    }
                    Rectangle {
                        visible: page.isAdmin
                        implicitWidth: fmtTxt.implicitWidth + 16; implicitHeight: 24; radius: 4
                        color: fmtHover.hovered ? Theme.danger : Theme.surfaceAlt
                        border.color: Theme.danger
                        Text { id: fmtTxt; anchors.centerIn: parent; text: qsTr("Format"); color: Theme.text; font.pixelSize: 11 }
                        HoverHandler { id: fmtHover }
                        TapHandler { onTapped: page.confirmFormat(modelData.number || 0,
                            qsTr("Disk %1").arg((modelData.number !== undefined ? modelData.number : 0) + 1)) }
                    }
                }
            }
            Text { visible: (page.val("GetHddInfo","HddInfo") || []).length === 0
                   text: page.status === "" ? qsTr("No storage reported.") : qsTr("Loading…")
                   color: Theme.textMuted; font.pixelSize: 12 }
            Item { Layout.fillHeight: true }
            Text { visible: !page.isAdmin
                   text: qsTr("Formatting a disk requires an administrator account.")
                   color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

    // ---- Users panel (host-level) ----
    Component {
        id: usersPanel
        ColumnLayout {
            spacing: Theme.spacing

            component MiniBtn: Rectangle {
                property string label: ""
                property bool danger: false
                signal clicked()
                implicitWidth: mbTxt.implicitWidth + 16; implicitHeight: 24; radius: 4
                color: mbHover.hovered ? (danger ? Theme.danger : Theme.accentDim) : Theme.surfaceAlt
                border.color: danger ? Theme.danger : Theme.border
                Text { id: mbTxt; anchors.centerIn: parent; text: parent.label; color: Theme.text; font.pixelSize: 11 }
                HoverHandler { id: mbHover }
                TapHandler { onTapped: parent.clicked() }
            }

            Repeater {
                model: page.val("GetUser","User") || []
                delegate: RowLayout {
                    required property var modelData
                    property bool confirming: false
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    Text { text: "👤"; font.pixelSize: 12 }
                    Text { text: modelData.userName || "—"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                    Rectangle {
                        radius: 3; color: Theme.surfaceAlt; border.color: Theme.border
                        implicitWidth: lvl.implicitWidth + 12; implicitHeight: lvl.implicitHeight + 4
                        Text { id: lvl; anchors.centerIn: parent
                               text: modelData.level || ""; color: Theme.textMuted; font.pixelSize: 11 }
                    }
                    MiniBtn {
                        visible: page.isAdmin
                        label: qsTr("Password")
                        onClicked: page.openUserDialog("password", modelData.userName)
                    }
                    MiniBtn {
                        visible: page.isAdmin
                        label: parent.confirming ? qsTr("Confirm remove") : qsTr("Remove")
                        danger: parent.confirming
                        onClicked: {
                            if (parent.confirming) {
                                Devices.applySetting(page.deviceRow, "DelUser",
                                    { "User": { "userName": modelData.userName } });
                                parent.confirming = false;
                            } else {
                                parent.confirming = true;
                            }
                        }
                    }
                }
            }
            Text { visible: (page.val("GetUser","User") || []).length === 0
                   text: page.status === "" ? qsTr("No users reported.") : qsTr("Loading…")
                   color: Theme.textMuted; font.pixelSize: 12 }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; visible: page.isAdmin }
            MiniBtn {
                visible: page.isAdmin
                label: qsTr("+ Add user")
                onClicked: page.openUserDialog("add", "")
            }
            Item { Layout.fillHeight: true }
            Text { visible: !page.isAdmin
                   text: qsTr("Managing users requires an administrator account.")
                   color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

    // ---- Time panel (host-level) ----
    Component {
        id: timePanel
        ColumnLayout {
            spacing: Theme.spacing
            InfoRow { label: qsTr("Device time")
                value: {
                    var t = page.val("GetTime","Time");
                    if (!t) return "";
                    function pad(n) { return String(n).padStart(2, "0"); }
                    return t.year + "-" + pad(t.mon) + "-" + pad(t.day) + "  "
                         + pad(t.hour) + ":" + pad(t.min) + ":" + pad(t.sec);
                } }
            InfoRow { label: qsTr("Time zone")
                value: {
                    var tz = page.val("GetTime","Time","timeZone");
                    if (tz === undefined) return "";
                    var h = -tz / 3600; // Reolink stores seconds west of UTC
                    return "UTC" + (h >= 0 ? "+" : "") + h;
                } }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
            Text { text: qsTr("Network time (NTP)"); color: Theme.text; font.pixelSize: 13; font.bold: true }
            SwitchRow {
                label: qsTr("Sync from NTP"); enabledCtl: page.isAdmin
                checked: page.val("GetNtp","Ntp","enable") === 1
                onCommit: (v) => Devices.applySetting(page.deviceRow, "SetNtp",
                    { "Ntp": { "enable": v ? 1 : 0,
                               "server": page.val("GetNtp","Ntp","server") || "pool.ntp.org",
                               "port": page.val("GetNtp","Ntp","port") || 123,
                               "interval": page.val("GetNtp","Ntp","interval") || 1440 } })
            }
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.spacing
                Text { text: qsTr("NTP server"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 160 }
                TextField {
                    id: ntpServer
                    Layout.fillWidth: true
                    enabled: page.isAdmin
                    text: page.val("GetNtp","Ntp","server") || ""
                    color: Theme.text
                    background: Rectangle { color: Theme.surfaceAlt; border.color: Theme.border; radius: 4 }
                    onEditingFinished: if (text.length > 0) Devices.applySetting(page.deviceRow, "SetNtp",
                        { "Ntp": { "enable": page.val("GetNtp","Ntp","enable") || 1, "server": text,
                                   "port": page.val("GetNtp","Ntp","port") || 123,
                                   "interval": page.val("GetNtp","Ntp","interval") || 1440 } })
                }
            }
            Item { Layout.fillHeight: true }
            Text { text: qsTr("Time zone / manual-clock editing is on the roadmap.")
                   color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

    // ---- Generic panel (fetched raw values) ----
    Component {
        id: genericPanel
        ColumnLayout {
            spacing: Theme.spacing
            Text {
                text: page.deviceRow < 0 ? qsTr("Select a camera to load settings.")
                    : Object.keys(page.settings).length === 0
                        ? qsTr("Loading settings from the device…")
                        : qsTr("This category's controls populate from the device response.")
                color: Theme.textMuted
                font.pixelSize: 13
            }
            // Raw fetched values (useful until each category has a bespoke form).
            Text {
                Layout.fillWidth: true
                visible: Object.keys(page.settings).length > 0
                text: JSON.stringify(page.settings, null, 2)
                color: Theme.textMuted
                font.pixelSize: 10
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
            }
            Item { Layout.fillHeight: true }
        }
    }
}
