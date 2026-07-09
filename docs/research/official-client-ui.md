# Research Dossier: Official Reolink desktop Client (Windows/macOS) — exhaustive UI screen, control, and settings inventory for a 1:1 Linux clone

> Produced by the design workflow on 2026-07-09. Facts marked for verification were adversarially checked; see [fact-check.md](fact-check.md).

## Summary

The Reolink Client (aka "Reolink Client (New Client)" / "Reolink for PC") is a desktop app for Windows 7+ and macOS 10.9+ organized around three primary pages: Live View, Playback, and Device Settings, plus a left-hand device list, an add-device ("+") flow, a top-right gear for Client (local/system) Settings, and a download/clip capability. Devices are added by auto-discovery, LAN scan, manual IP/domain, or UID (cloud/WAN), with login using username (default "admin") and password. Live View supports single and multi-camera grid modes with layout splitting, scrollview auto-cycling, PTZ (pan/tilt/zoom, presets, patrol, guard point), digital/optical zoom, stream mode (Clear=main / Fluent=sub), snapshot/clip, manual record, two-way audio, mute/volume, fullscreen, double-click to maximize a pane, a floating toolbar, and a lock-screen. Playback has a calendar date picker (blue dots on days with recordings), a color-coded timeline (grey=timer/continuous, blue=alarm, black=none) with mouse-wheel zoom, alarm-type filters (Any Motion, Person, Vehicle, Animal/Pet, Visitor), Fluent/Clear stream toggle, speed and frame-by-frame controls, and download with a scissors clip-cut tool. Device Settings is grouped into Device, Surveillance, Network, Storage, and System, covering stream/encoding, Display, Smart/Motion detection, and alarm actions. Client Settings covers app-level preferences like startup, auto-update, hardware decoding, language, and lock-screen password.

## Findings

### Top-level app structure and navigation

Three main operational pages: Live View, Playback, Device Settings. Persistent chrome: device list on the LEFT (cameras, NVRs, Home Hubs); a 'collapse' button to minimize the device list and expand the viewing area; a gear icon TOP-RIGHT opening Client Settings (local/system settings); an 'add_device'/'+' icon (top-left) to add devices; a feedback/suggestions option; a 'lock screen' icon that blocks further settings changes; and (Home Hub series) a video decryption tool. Windows 7+ / macOS 10.9+. Version 8.1.20+ removed the prior 32-device cap.

### Add Device wizard (4 methods)

Add via '+' icon by: (1) Add Device Automatically / auto-discovery on LAN; (2) LAN scan; (3) manual IP or Domain then blue 'Add' button; (4) by UID (cloud ID) for WAN/remote. Flow: enter IP or UID, 'Access Device'/'Add' then 'Confirm', enter Username (default 'admin') and Password (default blank), click 'Login'. Same network (LAN) use IP; different network (WAN) use UID. UID access uses Reolink P2P cloud relay.

### Live View — layout and navigation controls

Mode switch between 'one screen mode' and 'multiple screen mode' (grid). Split/layout button selects grid layouts (single, and multi-camera grids in 4/9/16 style). 'previous'/'next' icons page between camera views. 'scrollview' auto-cycles cameras (interval set by 'Scrollview Time' in Client Settings). Double-click a pane to maximize/restore. Fullscreen enlargement button. Collapse button hides device list to enlarge video area.

### Live View — per-camera / floating toolbar controls

Per-pane/floating-toolbar controls: stream/quality toggle (Clear=main stream vs Fluent=sub stream), snapshot, manual record/clip, two-way audio/talk (push-to-talk), volume/mute icon (audio-capable cameras), digital zoom, optical zoom, manual focus, PTZ pan/tilt/zoom, fullscreen. PTZ: presets and patrol, patrol speed and duration, Guard Point.

### Playback — calendar, timeline, and colors

Calendar/date picker: click to choose date; dates with recordings marked with a blue dot. Timeline recording bar: left-click to jump to that time; color code grey=timer/continuous, blue=alarm, black=none. Zoom: scroll mouse wheel up/down to change time interval; drag slider to choose time. Double-click to play back multiple cameras simultaneously.

### Playback — filters, stream, speed, download

Alarm-type filter buttons (turn blue when active, icons beside calendar): Any Motion (moving-ball), Person, Vehicle, Animal/Pet, Visitor. Filters only appear for supported alarm types. Stream mode button picks Fluent or Clear for playback. Speed icon changes playback speed. 'Frame by Frame' icon steps frames. Volume + fullscreen during playback. Download button saves the playing clip; 'scissors' icon cuts a segment before downloading; right-click can save as MP4. Cloud recordings via Cloud Library with 'Select files' to download/delete.

### Device Settings — top-level grouping

Five categories: (1) Device — basic settings, naming, stream quality/encoding; (2) Surveillance — recording schedules, FTP, email, push notifications, detection/alarm actions; (3) Network — status check, ports, UPnP; (4) Storage — SD/HDD management and formatting; (5) System — firmware upgrade, passwords, user management, time/date settings.

### Encoding / Stream settings (Device > Stream)

Streams split into Clear (Main Stream) and Fluent (Sub Stream). Fields per stream: Resolution, Frame Rate, Max Bitrate, I-frame Interval (some models), Frame Rate Mode (some models), Bitrate Mode (some models). Encoding H.264 vs H.265 (HEVC) and H.265+; 8MP+ cameras use H.265 on Clear/main; H.265+ cuts bitrate ~50-60% vs H.265. Exact numeric ranges are model-dependent.

### Display / Image settings

Options (vary by model): Brightness; Anti-Flicker; HDR; 'Flip Vertical'/'Flip Horizontal' (rotate, 90° not universal); Day/Night 'Switch mode' (Auto/Day/Night); 'Adjust Highlight/Shadow' (Manual/Auto); IR/Infrared lights; exposure/backlight; 3D NR. OSD: 'Camera Name' position (corners/center/off), 'Date' position (corners/center/off), 'Logo Watermark' toggle, 'Motion Mark'. Also Privacy Mask and Image Stitching (dual-lens).

### Detection / AI alarm settings

Path: Device > Alarm Settings / Smart Detection > Sensitivity. Motion Detection sensitivity; smart detection sensitivity per Person, Vehicle, Pet. Up to four time periods with different sensitivities via 'Add Time Period'. Also Detection Zone (grid mask), Object Size (min/max), Alarm Delay (e.g. 2s dwell to suppress false alarms), auto-tracking (PTZ). Filters match camera capability.

### Alarm actions / notifications (Surveillance)

Action linkages: Camera Recording, Push Notifications (sending interval + schedule, per detection type), Email Alerts, FTP upload (server address + port, default 21, schedule, motion-only vs continuous interval, per detection type), Siren, Linked Devices. Recording schedule set per camera/NVR from device list.

### Client (app-level) Settings — top-right gear

Toggles: 'Run at Startup', 'Automatic Client Update', 'Add Device Automatically', 'Auto Live View', 'Stretch Mode' (4:3 to 16:9), 'Scrollview Time', 'Date Format' (affects playback calendar), 'Alarm Beep', 'Lockscreen Password', 'Hardware Decoding first', 'Language' (English/German/Spanish/Chinese etc.), 'System Status' (diagnostics). Local recordings accessed here and need an external media player. Apply across all devices.

### Storage, Network, System sub-settings

Storage: SD/HDD management, capacity display, format. Network: connection status/diagnostics, ports, UPnP. System: firmware upgrade, password change, user/account management (Admin vs restricted User roles), time/date and timezone settings.

## Open Questions

- What are the exact grid layout presets (tile counts/arrangement) and does the client persist last-used layout and pane-to-camera assignments across restarts?
- Does the desktop Client expose a global Reolink-account sign-in (for cloud/UID sync) separate from per-device login, and how is multi-account handled?
- What is the exact download-manager UI (queue, progress, destination folder, batch download) and does it differ between local NVR/SD vs Cloud Library recordings?
- Precise floating-toolbar icon set/ordering in the current build, and which controls are gated by device capability (PTZ vs fixed, audio, AI vs basic motion).
- Exact enumerations for Resolution/Frame Rate/Bitrate dropdowns per common models (4MP/5MP/8MP-4K) to replicate value lists.
- Whether 'Balanced' stream quality and separate main/sub bandwidth presets exist in the PC client or only in the mobile app.

## Sources

- https://support.reolink.com/articles/900003769906-Introduction-to-Reolink-Client-New-Client/
- https://support.reolink.com/articles/16976201933593-About-Reolink-Client/
- https://support.reolink.com/articles/900005264383-How-to-Playback-Recordings-via-Reolink-Client-New-Client/
- https://support.reolink.com/articles/15785632026009-How-to-Filter-out-Motion-Alarm-triggered-Videos-from-Continuous-Recordings-When-Playing-back-via-Reolink-App-or-Client/
- https://support.reolink.com/hc/en-us/articles/4403939265561-How-to-Set-up-System-Settings-via-Reolink-Client/
- https://support.reolink.com/sections/12785515862041-Live-View/
- https://support.reolink.com/articles/360006937654-Reolink-Stream-Settings-Guide-Resolution-Frame-Rate-Bitrate-More/
- https://support.reolink.com/articles/32832044594329-How-to-Set-Up-Reolink-Encoding-Format-for-Optimal-Video-Quality-and-Efficiency/
- https://support.reolink.com/hc/en-us/articles/360005066414-How-to-Set-up-Display-via-Reolink-App/
- https://support.reolink.com/articles/360006992493-How-to-Configure-Motion-Detection-Sensitivity-for-Reolink-Cameras/
- https://support.reolink.com/articles/8121678212889-How-to-Set-up-Smart-Detection-Settings-via-Reolink-App/
- https://support.reolink.com/articles/900003664686-How-to-Add-Reolink-Device-by-UID-via-Reolink-Client-New-Client/
- https://support.reolink.com/articles/900003586646-How-to-Add-Reolink-Device-to-Reolink-Client-by-Manually-Entering-IP-New-Client/
- https://reolink.com/software-and-manual/
- https://home-cdn.reolink.us/wp-content/assets/multiple-languages/manual/Reolink_Client_User_Manual.pdf
