# Reolink Native Linux Client

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/TodesengelX/reolink-native-linux)](../../releases/latest)

A fully **native Linux desktop client** for Reolink cameras and NVRs — built to work like the official Reolink Client (Windows/macOS/Android): the 1/4/6/9/12/16 live grid, synced multi-camera playback on one timeline, double-click maximize, fullscreen, PTZ, and a full device-settings surface.

**No Wine, no Electron, no web wrappers, no bundled third-party NVR software.** Compiled C++/Qt6 talking directly to the devices over their own protocols — including the native **Baichuan** protocol (TCP 9000) the official apps use, which is why live HD, recorded playback, and settings work reliably where third-party RTSP/HTTP tools struggle.

> **Status: alpha.** Actively developed against a real RLN8-410 NVR with a PTZ camera, with community-confirmed setups on an RLC-823S1 standalone camera, a Home Hub Pro and Baichuan-only cameras. Live view with audio, playback, events, notifications, PTZ and device settings all work; two-way talk and remote/P2P access are still in progress.

Grab the [latest release](../../releases/latest), and see the [changelog](CHANGELOG.md) for what's new. Found a bug? [Issues](../../issues) are read and acted on — recent reports were fixed and released within a day.

## Install

### AppImage — download and run (no dependencies)

Grab the latest `.AppImage` from the [**Releases**](../../releases) page, then:

```sh
chmod +x reolink-native-linux-x86_64.AppImage
./reolink-native-linux-x86_64.AppImage
```

### Flatpak

Download the `.flatpak` bundle from [Releases](../../releases) and:

```sh
flatpak install --user ./reolink-native-linux.flatpak
flatpak run io.github.todesengelx.ReolinkLinux
```

*(A Flathub listing — `flatpak install flathub io.github.todesengelx.ReolinkLinux` — is planned.)*

### Arch Linux

Not on the AUR yet — build from the repo's PKGBUILD:

```sh
git clone https://github.com/TodesengelX/reolink-native-linux.git
cd reolink-native-linux/packaging/aur && makepkg -si
```

### Build from source

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/reolink-client
```

Requires Qt 6.5+ (base / declarative / multimedia / shadertools / tools), FFmpeg dev headers, libcurl, libsecret, and sqlite. Run the tests with `ctest --test-dir build`.

## Features

- **Live view** — 1/4/6/9/12/16 grid; drag cameras in from the sidebar or between cells to arrange them (the layout persists); hardware-accelerated H.264/H.265 decode (VAAPI/NVDEC) with software fallback; double-click a camera (or single-click it in the sidebar) to maximize; F11 fullscreen; per-pane floating toolbar (SD/HD remembered per camera, **audio**, snapshot, record, digital zoom with edge-clamped pan, PTZ, talk, siren, **spotlight/floodlight**, pop-out); auto-reconnect; streaming pauses while the window is hidden in the tray. Light controls distinguish Spotlight from Floodlight using device capabilities and expose brightness in Device Settings when the camera reports it. Maximized/pop-out HD streams use Baichuan, which avoids the corruption some Reolink NVRs emit on their RTSP main stream.
- **PTZ** — a side panel on PTZ cameras: an eight-way pad or a joystick (push further to move faster), **3D positioning** like the phone app (click to centre, drag a box to zoom into it, double-click to zoom in, scroll to zoom), zoom and focus sliders with the lens's real position and an **Auto focus** button, presets as picture cards, patrol, home position with return-when-idle, and AI auto-tracking settings. Arrow keys, `+`/`−` and `[`/`]` drive it from the keyboard. Works through an NVR: where the NVR ignores or refuses the HTTP command, the app uses the native Baichuan request the official app sends.
- **Playback** — month calendar, a timeline with **detection markers read from the device's own event index** (person, vehicle, animal and motion in their own colours, each switchable, with the day's count), recording search, in-place seek and a live playhead, pan/zoom the timeline (clicks snap to nearby recordings), clip export, HD playback over Baichuan, recorded audio. **Synced multi-camera playback**: a 4-pane grid driven by one timeline showing every camera's coverage as per-camera lanes, with one playhead driving all panes to the same moment — drag cameras in, swap panes without interrupting their streams, double-click to maximize with an SD/HD toggle, digital zoom in every pane.
- **Honest failure handling** — unreachable devices show why and retry themselves with backoff; sign-in failures surface the device's attempts-left lockout warning and are never auto-retried; right-click a device to Reconnect or fix its credentials in place; adding a device validates it first, so a typo'd address or wrong password is an inline message instead of a stuck row. Per-camera **Rotate view** override for devices whose firmware misreports orientation.
- **Events** — notification inbox with thumbnails and real timestamps, AI-type filters (person/vehicle/pet/visitor/motion) and per-camera filtering, detection polling, unread badge. Repeated events merge, plain motion can be hidden, and events older than 7 days are deleted. Opening one jumps to playback zoomed in on the event (or shows its snapshot when nothing was recorded).
- **Background monitoring** — lives in the system tray with the window closed, so alerts keep arriving; optional start-on-login. Desktop notifications fire for detections (gated on each camera's Push setting, and can be turned off) and for cameras or the NVR going offline; clicking one opens the app and plays that event back.
- **Devices** — NVR shown as an expandable parent with its cameras nested, each with its IP address; right-click for Properties / Settings / Reboot / Remove. Adding scans the network and can add every device it finds with one set of credentials. Cameras that only speak Baichuan (no HTTP or RTSP) are supported.
- **Settings** — Image (brightness/contrast/saturation/sharpness, mirror/flip, day/night), Detection (motion + per-AI-type sensitivity, alerts, and a paint-on-image detection-zone editor), Recording (including a weekly 7×24 schedule grid per recording type), Encoding, Display/OSD, Network, Storage, Users, Time, System. Camera settings run over the native Baichuan protocol, so they don't hit the NVR web server's under-load 502s.
- **Extras** — battery/solar status, doorbell answer surface, multi-monitor pop-out, fisheye dewarp shader, manual MP4 recording (stream-copy), a built-in update checker with one-click self-update for the AppImage, and a German translation (follows the system language).

## How it talks to your devices

| Transport | Used for |
|---|---|
| **Baichuan** (TCP 9000) | The native app protocol: HD live + recorded playback, reading/writing device settings, and the PTZ requests an NVR won't take over HTTP (3D positioning, home-position options). The only transport for Baichuan-only cameras. Reliable under NVR load. |
| RTSP / HTTP-FLV | Live sub-stream grid and light playback scrubbing. |
| HTTP-CGI JSON API (:443) | Device discovery/validation, host-level settings (network/storage/users/time), detection polling, and PTZ moves, presets, patrol and tracking. |

## Documentation

- **[CHANGELOG.md](CHANGELOG.md)** — what changed in each release.
- **[docs/DESIGN.md](docs/DESIGN.md)** — the definitive design document (goals, stack, architecture, protocols, media pipeline, UI inventory, data model, packaging, roadmap, risks).
- [docs/research/](docs/research/) — the research dossiers behind the design (official-client UI, protocols, video pipeline, GUI stack, prior art, fact-checks).

## Licensing

MIT — see [LICENSE](LICENSE). All runtime dependencies are LGPL/MIT/Apache and dynamically linked; the Flatpak bundles its own LGPL build of FFmpeg. The protocol layer is implemented from the MIT-licensed `reolink_aio` reference and Reolink's published HTTP API, documented wire facts and our own captures of the official app's traffic — **never** from AGPL-licensed code.

## Disclaimer

Independent, unofficial project — not affiliated with, endorsed by, or supported by Reolink. "Reolink" is a trademark of its respective owner. Use with your own devices at your own risk.
