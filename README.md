# Reolink Linux Client

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A fully **native Linux desktop client** for Reolink cameras and NVRs — a 1:1 replica of the official Reolink Client (Windows/macOS): same screens, controls, settings, and behaviors, including the 1/4/9/16 live grid, double-click pane maximize/restore, fullscreen, PTZ, color-coded playback timeline, downloads, and the full device-settings surface.

**No Wine, no Electron, no web wrappers, no bundled third-party NVR software.** Compiled native code talking directly to the devices over their own protocols.

## Chosen stack (see the design doc for full rationale)

| Concern | Choice |
|---|---|
| Language / UI | C++20 + Qt 6 (QML for grid/PTZ/timeline, Widgets for settings), dynamically linked (LGPL-clean) |
| Media | FFmpeg (LGPL-only build) — RTSP/FLV ingest, H.264/H.265 hw decode (VAAPI / NVDEC) with software fallback |
| Render | Zero-copy DMA-BUF→EGLImage→GL where supported; universal NV12 upload fallback |
| Audio | PipeWire (AAC + G.711/PCM) |
| Protocols | Reolink HTTP-CGI JSON API + RTSP/HTTP-FLV + Baichuan (port 9000: events, talk, battery) + ONVIF fallback |
| Storage | SQLite (devices, layouts, event log) + libsecret keyring (credentials) |
| Build / packaging | CMake; Flatpak (primary), AppImage, deb |

## Documentation

- **[docs/DESIGN.md](docs/DESIGN.md)** — the definitive design document: goals, tech-stack decision, architecture, protocol integration, media pipeline, complete UI screen inventory, data model, packaging, roadmap (M0–M15), and risks.
- [docs/research/](docs/research/) — research dossiers behind the design:
  - [official-client-ui.md](docs/research/official-client-ui.md) — screen/control/settings inventory of the official client
  - [protocols.md](docs/research/protocols.md) — HTTP-CGI API, RTSP/FLV URLs, Baichuan, ONVIF, auth
  - [video-pipeline.md](docs/research/video-pipeline.md) — decode/render/audio architecture options on Linux
  - [gui-stack.md](docs/research/gui-stack.md) — native toolkit comparison (Qt vs GTK4 vs Rust options)
  - [prior-art.md](docs/research/prior-art.md) — reference projects and their licenses
  - [fact-check.md](docs/research/fact-check.md) — 24 adversarially verified claims (with corrections)
- [docs/proposals/](docs/proposals/) — the two candidate architectures considered, and the design-review critique that shaped the final doc.

## Status

Milestones **M0–M14** implemented (2026-07-09). The app builds, runs natively, and mirrors the official client:

- **Live View** — 1/4/9/16 grid, hardware-accelerated H.264/H.265 decode (VAAPI/NVDEC) with software fallback, double-click maximize, F11 fullscreen, per-pane floating toolbar (stream quality, snapshot, record, digital zoom, PTZ joystick, talk, siren, pop-out), auto-reconnect
- **Playback** — month calendar, color-coded two-tone timeline (grey timer / blue alarm), recording search, segment playback
- **Events** — notification inbox with AI-type filters (person/vehicle/pet/visitor/motion), detection polling, jump-to-playback, unread badge
- **Device Settings** — Display/Encoding/Recording/Detection/Network/Storage/System pages with `Get*`/`Set*` wiring, admin-gated writes
- **Extras** — battery/solar status, doorbell answer surface, multi-monitor pop-out windows, fisheye dewarp shader, manual MP4 recording (stream-copy)

Backend-complete but pending validation against real Reolink hardware: PTZ/snapshot/settings `Set` operations, NVR playback streams, two-way talk and doorbell answer (need the Baichuan port-9000 protocol — the M12 spike), and UID/P2P remote access.

### Building

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/reolink-client
```

Requires: Qt 6.5+ (base/declarative/multimedia/shadertools/tools), FFmpeg dev headers, libcurl, libsecret, sqlite. Run tests with `ctest --test-dir build`. Compare hardware vs software decode with `tools/bench.sh`.

### Packaging

- **Flatpak:** `flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.todesengelx.ReolinkLinux.yml`
- **AppImage:** `packaging/appimage/build-appimage.sh` (needs `linuxdeploy` + the Qt plugin)
- **System install:** `cmake --install build --prefix /usr/local` (installs the binary, `.desktop`, icon, and AppStream metadata)

A GitHub Actions CI config (build + test) is provided at [packaging/ci/github-actions-ci.yml](packaging/ci/github-actions-ci.yml) — copy it to `.github/workflows/ci.yml` to activate (adding a workflow requires a token with the `workflow` scope).

## Licensing

This project is licensed under the [MIT License](LICENSE).

All runtime dependencies are LGPL/MIT/Apache and dynamically linked; the protocol layer is implemented from the MIT-licensed `reolink_aio` knowledge base and Reolink's published HTTP API — never from AGPL code. See DESIGN.md §10 for the full legal notes (HEVC, Baichuan reverse-engineering, bridge policy).

## Disclaimer

This is an independent, unofficial project and is not affiliated with, endorsed by, or supported by Reolink. "Reolink" is a trademark of its respective owner. Use with your own devices at your own risk.
