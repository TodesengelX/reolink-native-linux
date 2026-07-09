# Reolink Linux Client

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

**M0 + M1 core complete** (2026-07-09): the app builds and runs natively — dark-theme shell (Live View / Playback / Events / Device Settings navigation), device sidebar with add/remove, SQLite store, keyring credentials, the HTTP-CGI protocol client with login/token lifecycle, and the FFmpeg media pipeline rendering a multi-pane live grid (verified with 4 simultaneous streams including a network source). Double-click pane maximize/restore and F11/Esc fullscreen are wired.

### Building

```
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/reolink-client
```

Requires: Qt 6.5+ (base/declarative/multimedia/tools), FFmpeg dev headers, libcurl, libsecret, sqlite. Tests: `ctest --test-dir build`.

Next per the [roadmap](docs/DESIGN.md): M2 feasibility spikes (hardware decode bench, P2P/UID, battery bridge), M3 live-view completion (PTZ, snapshot, stream-quality toggle), M4 manual recording.

## Licensing posture

All runtime dependencies are LGPL/MIT/Apache and dynamically linked; the protocol layer is implemented from the MIT-licensed `reolink_aio` knowledge base and Reolink's published HTTP API — never from AGPL code. See DESIGN.md §10 for the full legal notes (HEVC, Baichuan reverse-engineering, bridge policy).
