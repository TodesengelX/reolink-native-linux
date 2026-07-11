# Reolink Native Linux Client

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A fully **native Linux desktop client** for Reolink cameras and NVRs — built to work like the official Reolink Client (Windows/macOS/Android): the 1/4/9/16 live grid, double-click maximize, fullscreen, PTZ, a color-coded playback timeline, and a full device-settings surface.

**No Wine, no Electron, no web wrappers, no bundled third-party NVR software.** Compiled C++/Qt6 talking directly to the devices over their own protocols — including the native **Baichuan** protocol (TCP 9000) the official apps use, which is why live HD, recorded playback, and settings work reliably where third-party RTSP/HTTP tools struggle.

> **Status: alpha.** Actively developed and validated against a real RLN8-410 NVR. Core live/playback/settings work well; some areas (weekly recording-schedule grid, two-way talk audio, remote/P2P) are still in progress.

## Install

### AppImage — download and run (no dependencies)

Grab the latest `.AppImage` from the [**Releases**](../../releases) page, then:

```sh
chmod +x Reolink_Native_Linux-*.AppImage
./Reolink_Native_Linux-*.AppImage
```

### Flatpak

Download the `.flatpak` bundle from [Releases](../../releases) and:

```sh
flatpak install --user ./reolink-native-linux.flatpak
flatpak run io.github.todesengelx.ReolinkLinux
```

*(A Flathub listing — `flatpak install flathub io.github.todesengelx.ReolinkLinux` — is planned.)*

### Arch Linux (AUR)

```sh
yay -S reolink-native-linux        # or: paru -S reolink-native-linux
```

*(PKGBUILD lives in [`packaging/aur/`](packaging/aur/).)*

### Build from source

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/reolink-client
```

Requires Qt 6.5+ (base / declarative / multimedia / shadertools / tools), FFmpeg dev headers, libcurl, libsecret, and sqlite. Run the tests with `ctest --test-dir build`.

## Features

- **Live view** — 1/4/9/16 grid; hardware-accelerated H.264/H.265 decode (VAAPI/NVDEC) with software fallback; double-click a camera (or single-click it in the sidebar) to maximize; F11 fullscreen; per-pane floating toolbar (SD/HD, snapshot, record, digital zoom with edge-clamped pan, PTZ, talk, siren, floodlight, pop-out); auto-reconnect. Maximized/pop-out HD streams use Baichuan, which avoids the corruption some Reolink NVRs emit on their RTSP main stream.
- **Playback** — month calendar, two-tone timeline (timer/alarm), recording search, in-place seek and a live playhead, pan/zoom the timeline, HD playback over Baichuan.
- **Events** — notification inbox with AI-type filters (person/vehicle/pet/visitor/motion), detection polling, jump-to-playback, unread badge.
- **Device tree** — NVR shown as an expandable parent with its cameras nested; right-click for Properties / Settings / Reboot / Remove.
- **Settings** — Image (brightness/contrast/saturation/sharpness, mirror/flip, day/night), Detection (motion + per-AI-type sensitivity, alerts), Recording, Encoding, Display/OSD, Network, Storage, Users, Time, System. Camera settings run over the native Baichuan protocol, so they don't hit the NVR web server's under-load 502s.
- **Extras** — battery/solar status, doorbell answer surface, multi-monitor pop-out, fisheye dewarp shader, manual MP4 recording (stream-copy).

## How it talks to your devices

| Transport | Used for |
|---|---|
| **Baichuan** (TCP 9000) | The native app protocol: HD live + recorded playback, and reading/writing device settings. Reliable under NVR load. |
| RTSP / HTTP-FLV | Live sub-stream grid and light playback scrubbing. |
| HTTP-CGI JSON API (:443) | Device discovery/validation, host-level settings (network/storage/users/time), and detection polling. |

## Documentation

- **[docs/DESIGN.md](docs/DESIGN.md)** — the definitive design document (goals, stack, architecture, protocols, media pipeline, UI inventory, data model, packaging, roadmap, risks).
- [docs/research/](docs/research/) — the research dossiers behind the design (official-client UI, protocols, video pipeline, GUI stack, prior art, fact-checks).

## Licensing

MIT — see [LICENSE](LICENSE). All runtime dependencies are LGPL/MIT/Apache and dynamically linked. The protocol layer is implemented from the MIT-licensed `reolink_aio` reference and Reolink's published HTTP API and documented wire facts — **never** from AGPL-licensed code.

## Disclaimer

Independent, unofficial project — not affiliated with, endorsed by, or supported by Reolink. "Reolink" is a trademark of its respective owner. Use with your own devices at your own risk.
