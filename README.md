# Reolink Native Linux Client

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/TodesengelX/reolink-native-linux)](../../releases/latest)

A fully **native Linux desktop client** for Reolink cameras and NVRs — built to work like the official Reolink Client (Windows/macOS/Android): the 1/4/6/9/16 live grid, synced multi-camera playback on one timeline, double-click maximize, fullscreen, PTZ, and a full device-settings surface.

**No Wine, no Electron, no web wrappers, no bundled third-party NVR software.** Compiled C++/Qt6 talking directly to the devices over their own protocols — including the native **Baichuan** protocol (TCP 9000) the official apps use, which is why live HD, recorded playback, and settings work reliably where third-party RTSP/HTTP tools struggle.

> **Status: alpha.** Actively developed against a real RLN8-410 NVR, with community-confirmed setups on an RLC-823S1 standalone camera and a Home Hub Pro. Live view, playback, events, notifications and device settings all work; two-way talk audio and remote/P2P access are still in progress.

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

- **Live view** — 1/4/6/9/16 grid; drag cameras in from the sidebar or between cells to arrange them (the layout persists); hardware-accelerated H.264/H.265 decode (VAAPI/NVDEC) with software fallback; double-click a camera (or single-click it in the sidebar) to maximize; F11 fullscreen; per-pane floating toolbar (SD/HD, snapshot, record, digital zoom with edge-clamped pan, PTZ, talk, siren, floodlight, pop-out); auto-reconnect. Maximized/pop-out HD streams use Baichuan, which avoids the corruption some Reolink NVRs emit on their RTSP main stream.
- **Playback** — month calendar, two-tone timeline (timer/alarm) with red ticks marking detections, recording search, in-place seek and a live playhead, pan/zoom the timeline, clip export, HD playback over Baichuan. **Synced multi-camera playback**: a 4-pane grid driven by one timeline showing every camera's coverage as per-camera lanes, with one playhead driving all panes to the same moment — drag cameras in, swap panes without interrupting their streams, double-click to maximize with an SD/HD toggle, digital zoom in every pane.
- **Honest failure handling** — unreachable devices show why and retry themselves with backoff; sign-in failures surface the device's attempts-left lockout warning and are never auto-retried; right-click a device to Reconnect or fix its credentials in place; adding a device validates it first, so a typo'd address or wrong password is an inline message instead of a stuck row. Per-camera **Rotate view** override for devices whose firmware misreports orientation.
- **Events** — notification inbox with thumbnails, AI-type filters (person/vehicle/pet/visitor/motion) and per-camera filtering, detection polling, jump-to-playback, unread badge.
- **Background monitoring** — lives in the system tray with the window closed, so alerts keep arriving; optional start-on-login. Desktop notifications fire for detections (gated on each camera's Push setting) and for cameras or the NVR going offline; clicking one opens the app and plays that event back.
- **Device tree** — NVR shown as an expandable parent with its cameras nested; right-click for Properties / Settings / Reboot / Remove.
- **Settings** — Image (brightness/contrast/saturation/sharpness, mirror/flip, day/night), Detection (motion + per-AI-type sensitivity, alerts, and a paint-on-image detection-zone editor), Recording (including a weekly 7×24 schedule grid per recording type), Encoding, Display/OSD, Network, Storage, Users, Time, System. Camera settings run over the native Baichuan protocol, so they don't hit the NVR web server's under-load 502s.
- **Extras** — battery/solar status, doorbell answer surface, multi-monitor pop-out, fisheye dewarp shader, manual MP4 recording (stream-copy), and a built-in update checker with one-click self-update for the AppImage.

## How it talks to your devices

| Transport | Used for |
|---|---|
| **Baichuan** (TCP 9000) | The native app protocol: HD live + recorded playback, and reading/writing device settings. Reliable under NVR load. |
| RTSP / HTTP-FLV | Live sub-stream grid and light playback scrubbing. |
| HTTP-CGI JSON API (:443) | Device discovery/validation, host-level settings (network/storage/users/time), and detection polling. |

## Documentation

- **[CHANGELOG.md](CHANGELOG.md)** — what changed in each release.
- **[docs/DESIGN.md](docs/DESIGN.md)** — the definitive design document (goals, stack, architecture, protocols, media pipeline, UI inventory, data model, packaging, roadmap, risks).
- [docs/research/](docs/research/) — the research dossiers behind the design (official-client UI, protocols, video pipeline, GUI stack, prior art, fact-checks).

## Licensing

MIT — see [LICENSE](LICENSE). All runtime dependencies are LGPL/MIT/Apache and dynamically linked. The protocol layer is implemented from the MIT-licensed `reolink_aio` reference and Reolink's published HTTP API and documented wire facts — **never** from AGPL-licensed code.

## Disclaimer

Independent, unofficial project — not affiliated with, endorsed by, or supported by Reolink. "Reolink" is a trademark of its respective owner. Use with your own devices at your own risk.
