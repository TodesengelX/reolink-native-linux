# Native Linux Reolink NVR Client — Definitive Design Document

**Codename:** `reonative` (working title)
**Status:** Architecture baseline, v1.1 (revised)
**Author:** Lead architect
**Date:** 2026-07-09
**Goal in one line:** A fully native Linux desktop application that replicates the official Reolink Client (Windows/macOS) 1:1 — device list, live grid, PTZ, playback/timeline, events, downloads, and the full settings surface — with hardware-accelerated video for up to 16 simultaneous panes.

---

## 1. Overview, Goals, and the "Native" Constraint

### 1.1 What we are building

A single-binary desktop application that a user installs on Linux and uses exactly as they use the official Reolink Client today: add cameras/NVRs by IP or UID, watch a 1/4/9/16 live grid, drive PTZ cameras, answer the doorbell, scrub recorded footage on a color-coded timeline, browse an event inbox, download clips, and configure every device setting the official client exposes. The application talks directly to Reolink hardware over the device's own network protocols — no Reolink cloud account is required, and no vendor server sits between the app and a LAN camera. (Reolink's Cloud Library — which *does* require a cloud account — is explicitly out of v1; see §11.)

### 1.2 Goals

- **1:1 UI/UX fidelity** with the official desktop Client: same page structure (Live View / Playback / Events / Device Settings), same controls, same stream-quality vocabulary (Clear / Balanced / Fluent), same PTZ/timeline behaviors, including multi-monitor live view.
- **16-pane live grid** at usable latency using GPU hardware decode, with a zero-copy render path where the GPU/driver allows. Latency target: **<500 ms glass-to-glass on LAN over the HTTP-FLV low-latency path** (the primary live transport where firmware supports it); RTSP/TCP — the fallback — typically lands at **~0.5–1.5 s** due to TCP delivery and reorder buffering.
- **Full protocol coverage**: HTTP-CGI JSON API (config/control/search/download/snapshot), RTSP/HTTP-FLV (live/playback video), Baichuan (events + talk + battery-cam control), ONVIF (discovery + event/PTZ fallback).
- **Redistribution-clean licensing**: every third-party dependency is LGPL/MIT/Apache and dynamically linked, so the app can ship as a proprietary, freely-redistributable Flatpak/AppImage/deb.
- **Long-term maintainability**: one primary language for UI + media + protocol, mature tooling, large hiring pool.

### 1.3 Non-goals (v1)

- No re-encoding/transcoding pipeline (record = stream-copy remux only).
- **No continuous multi-camera 24/7 recording in the client.** Around-the-clock recording is the NVR's (or camera SD card's) job; the client searches, plays back, and downloads those recordings. Client-side recording is manual, per-pane, of the currently displayed stream (§5.5). This also keeps per-device concurrent-session budgets intact (§5.7).
- No Reolink cloud account features: no cloud login, no Cloud Library, no subscription management, no firmware hosting. (Cloud Library is a post-1.0 candidate — §11.)
- **Home Hub support is deferred to post-1.0.** Home Hub recordings are encrypted and require a recording-encryption key exchange/decryption flow that is undesigned; rather than ship a broken tile, the Hub is excluded from the v1 device model and chrome (§11).
- No mobile/embedded targets; desktop Linux (X11 + Wayland) only.
- No web/browser UI, no Electron, no WebView.
- Not a general-purpose NVR/VMS for non-Reolink cameras (though ONVIF cameras will largely work incidentally).

### 1.4 Interpreting "native, no third-party solutions"

The constraint means: **native compiled desktop code rendering with the platform's real GPU/windowing stack — not a web wrapper (Electron/Tauri/Flutter-Skia) and not a bundled generic VMS (Frigate/ZoneMinder/go2rtc-as-the-app).** It does **not** mean "write a video codec from scratch."

Video decode is explicitly in scope for FFmpeg (libavcodec/libavformat) and, on the audio side, PipeWire. This is still "native" for three concrete reasons:

1. **These are the OS-native media substrate, not a "solution".** libavcodec is the same library the kernel-adjacent userspace, GStreamer, mpv, VLC, Chromium, and every Linux media app link against. Using it is equivalent to using `libc` or OpenGL — it is the platform, not a third-party product bolted on top.
2. **H.264/H.265 decoding is fixed-function silicon.** The actual decode happens in the GPU's VAAPI/NVDEC block. FFmpeg is a thin dispatch layer to `VAContext`/`CUVID`; there is no meaningful "native" alternative that isn't just a reimplementation of the same VA-API/NVDEC calls. Writing an HEVC decoder by hand would be *less* native (a bespoke software codec) and vastly slower.
3. **We own the pipeline, we don't embed a black box.** The distinction we hold to: we call libavcodec/libavformat as libraries and control every packet, thread, and surface ourselves. We do **not** shell out to a separate daemon (go2rtc/MediaMTX/neolink) as the product's engine. One bounded exception exists and must be stated honestly: **if battery/wire-free cameras are in scope, a local bridge process is *required*, not optional** — their video transport (Baichuan-over-UDP/P2P) has no complete permissively-licensed implementation to port (§4.6). We bundle **go2rtc (MIT)** as that bridge; it is a clearly-bounded helper for one camera class, never the app's engine, and everything the user perceives as "the app" — window, grid, controls, decode orchestration, compositing — is our compiled code.

---

## 2. Tech-Stack Decision

### 2.1 Primary stack (chosen): C++20 + Qt 6 + owned FFmpeg pipeline

| Concern | Choice |
|---|---|
| Language | C++20 |
| UI toolkit | Qt 6 (LGPLv3, **dynamically linked**) — Qt Quick/QML for grid, PTZ, timeline; Qt Widgets for dense settings dialogs (embedded via `QQuickWidget`/`createWindowContainer`) |
| Media ingest/decode | FFmpeg `libavformat`/`libavcodec`/`libswscale`/`libswresample`, **LGPL-only build** (no `--enable-gpl`, no x264/x265) |
| HW decode | VAAPI (Intel/AMD), NVDEC/CUVID (NVIDIA), threaded software fallback |
| Render | Qt RHI / `QRhiWidget` / `QOpenGLWidget` tiles; zero-copy VAAPI→DMA-BUF→EGLImage→GL on Intel/AMD, CUDA-GL interop on NVIDIA; YUV→RGB + 10-bit tone-map + fisheye dewarp in fragment shaders |
| Audio | PipeWire (`pw_stream`), Pulse-compat fallback |
| HTTP-CGI client | libcurl + nlohmann/json |
| Crypto/TLS | OpenSSL 3 (HTTPS + Baichuan AES-CFB) |
| XML/SOAP | pugixml + hand-rolled minimal SOAP/WS-Discovery over libcurl (avoids GPL gSOAP) |
| Protocol source-of-truth | Reimplemented in C++ from **MIT** `reolink_aio` + the official HTTP API PDF — **never** from AGPL `neolink` |
| Battery-cam video bridge | **go2rtc (MIT)**, bundled as a bounded child process (§4.6) |
| State/storage | SQLite (device tree, layouts, recording index, event log) |
| Credentials | libsecret (Secret Service / OS keyring) |
| Build | CMake |
| Packaging | Flatpak (primary, `org.kde` runtime) + AppImage (`linuxdeployqt`) + `.deb` |

### 2.2 Justification

The weighted goals are **UI fidelity, 16-stream video performance, maintainability, and clean licensing.** Qt 6 + an owned FFmpeg pipeline dominates on all four:

- **UI fidelity.** Reolink's client is a heavily custom-skinned dark UI: 16-pane grid, floating per-pane toolbars, a color-coded scrubbable timeline, a PTZ joystick, and the Clear/Balanced/Fluent stream toggle. QML's GPU-composited scene graph gives pixel-perfect, HIG-unconstrained theming and cheap animated custom controls. The GTK4/libadwaita path actively resists non-GNOME skinning, turning a 1:1 clone into a fight with the toolkit. (Whether the official client itself uses Qt is unverified trivia and carries no weight in this decision.)
- **Video performance / control at 16×.** The binding constraints for a 16-tile grid are GPU decode sessions and render copies, not the toolkit. Owning `libavformat`/`libavcodec` directly — one demux+decode worker per stream, present-latest frame mailboxes, and a *single render thread with one GL context per window drawing all tiles* — gives per-packet control over RTSP transport, low-latency flags, and per-stream hardware/software fallback, plus remux-to-MP4 without re-encode. GStreamer's element negotiation hides exactly this control and makes it harder to reason about at 16×.
- **Licensing.** Every dependency is LGPL (Qt, FFmpeg LGPL build), MIT (nlohmann/json, pugixml, curl, go2rtc), or Apache-2.0 (OpenSSL 3), all dynamically linked or run out-of-process. The protocol layer is reimplemented from MIT `reolink_aio`, never AGPL `neolink`. Nothing forces source disclosure; the binary ships cleanly.
- **Maintainability.** One language spans GUI, FFmpeg pipeline, and GL/CUDA interop under one memory/threading model; Qt has the largest desktop ecosystem, mature tooling, and a large hiring pool.

### 2.3 Runner-up (and why not): Python 3.12 + PySide6 + `reolink-aio` verbatim + libmpv-per-pane

This is a genuinely strong, higher-velocity alternative. Its decisive lever is **reuse**: `reolink-aio` is MIT, Reolink-authorized, and already implements the **full HTTP-CGI surface plus a partial Baichuan client (event push, some battery-cam control flows)** — collapsing months of protocol work into `pip install reolink-aio`. To be precise about its limits: it does **not** provide a complete P2P/UID transport nor battery-cam *video* streaming; those remain reverse-engineering work in any stack. Video would be `libmpv` embedded per pane (`python-mpv`), which already solves Reolink's ugly-RTSP/H.265 edge cases and hwdec fallback across vendors, plus optional go2rtc restream for broken models.

**Why not primary:**
- **16-pane control.** Sixteen independent libmpv instances each own their own decode/render context; you lose the single-render-context, present-latest, per-packet control that makes a dense grid cheap and predictable. The escape hatch (drop in a C++/FFmpeg tile) is exactly the primary stack — so if we know we need it, build there.
- **Language boundary at the hot edges.** A/V sync, zero-copy interop, and two-way-audio transport live in C anyway; Python becomes orchestration glue with a qasync↔asyncio bridge, adding a second runtime and packaging surface (Python + Qt + mpv + FFmpeg + VAAPI in one Flatpak) for no performance gain.
- **Fidelity ceiling is the same** (QML either way), so Python's advantage is pure velocity, which matters most for a throwaway prototype, not a maintainable 1:1 product.

We **borrow** the runner-up's best ideas: `reolink_aio` is the authoritative behavioral reference we port from, and **go2rtc (MIT)** is our bundled bridge for battery-cam *video* (§4.6), keeping copyleft code out of our binary and out of our package.

---

## 3. Architecture

### 3.1 Layered diagram (text)

```
┌───────────────────────────────────────────────────────────────────────────┐
│                        APPLICATION & UI LAYER (QML + Qt Widgets)            │
│  Live grid · PTZ joystick · Playback timeline · Events inbox ·              │
│  Doorbell answer · Add-device wizard · Device Settings (5 groups) ·         │
│  Client Settings · Downloads manager · Pop-out windows                      │
│      ▲ view-models (QAbstractItemModel) bound to QML                        │
├──────┼────────────────────────────────────────────────────────────────────┤
│      │                    APPLICATION CORE                                  │
│  Session manager · Capability/permission gating (GetAbility + user level) · │
│  Event bus + event store · Stream-lifecycle supervisor ·                    │
│  Per-device session budget · Layout/pane→camera bindings ·                  │
│  Token refresh timers · A/V focus routing                                   │
├──────┬───────────────────────────┬────────────────────────┬───────────────┤
│      │                           │                        │               │
│  DEVICE & PROTOCOL LAYER    MEDIA PIPELINE LAYER      RENDER LAYER     AUDIO │
│  ┌────────────────────┐    ┌───────────────────┐   ┌─────────────┐  ┌──────┐│
│  │ HTTP-CGI client    │    │ RTSP/FLV ingest   │   │ Zero-copy   │  │ Pipe ││
│  │ (libcurl+json)     │    │ (libavformat)     │   │ VAAPI→      │  │ Wire ││
│  │ RTSP URL builder   │    │ 1 demux+decode    │──▶│ DMA-BUF→    │  │ play ││
│  │ Baichuan client    │    │ worker / stream   │   │ EGLImage→GL │  │ +mic ││
│  │ (OpenSSL AES-CFB,  │    │ HW decode +       │   │ (Intel/AMD) │  │ G711 ││
│  │  events + talk)    │    │ SW fallback       │   │ CUDA-GL     │  │ talk ││
│  │ ONVIF discovery/   │    │ latest-frame      │   │ interop     │  │ path ││
│  │ event sub (SOAP)   │    │ mailboxes         │   │ (NVIDIA)    │  └──────┘│
│  │ P2P/UID (UDP)      │    │ remux record +    │   │ NV12 upload │         │
│  │ go2rtc bridge mgr  │    │ snapshot + seek   │   │ fallback ·  │         │
│  │ (battery video)    │    │                   │   │ 1 GL ctx /  │         │
│  └────────────────────┘    └───────────────────┘   │ window      │         │
│                                                    └─────────────┘         │
├───────────────────────────────────────────────────────────────────────────┤
│         PERSISTENCE & CONFIG   (SQLite · libsecret keyring · event log)     │
└───────────────────────────────────────────────────────────────────────────┘
                         Hardware: VAAPI / NVDEC · GL/EGL/Vulkan · PipeWire
```

### 3.2 Modules and responsibilities

**Device & Connection layer.**
Owns the per-device lifecycle: discovery (LAN broadcast, ONVIF WS-Discovery, UID/P2P), login/token session, capability model, and connection health. Exposes a clean C++ `Device`/`Channel` model to the rest of the app. Handles NVR channel enumeration and per-channel online status, and NVR channel bind/unbind management (§6.7). Runs token-refresh timers driven by the device-reported `leaseTime` (§4.2). **Enforces a per-device concurrent-session budget** (§5.7): every stream open goes through this layer, which knows how many media sessions a given camera/NVR can serve and reuses existing sessions (e.g., record taps the display session) rather than opening duplicates.

**Protocol clients.**
- *HTTP-CGI client* (libcurl + nlohmann/json): all config/control/search/download/snapshot commands. Batches commands, appends `?cmd=X&token=Y`, always sends a JSON array body.
- *RTSP client*: FFmpeg's `libavformat` RTSP demuxer with tuned options; URLs from `GetRtspUrl` or built locally (§4.3).
- *Baichuan client* (OpenSSL 3): port-9000 nonce/AES-CFB handshake, event subscription (real-time motion/AI/visitor push), **two-way talk (the primary talk path — §5.4)**, and battery-cam control. Video for battery cams goes through the go2rtc bridge (§4.6).
- *ONVIF client* (pugixml + hand-rolled SOAP over libcurl): WS-Discovery for onboarding and WS-BaseNotification event subscription as a Baichuan alternative; ONVIF PTZ as a secondary PTZ path (§4.5); ONVIF Profile-T backchannel as the secondary talk path where advertised.
- *Bridge manager*: supervises the bundled go2rtc child process for battery-cam video — spawn/config/health-check/teardown; its RTSP/FLV output is ingested by the media pipeline like any other camera.

**Application core.**
Marries protocol events and UI models. Hosts the **stream-lifecycle supervisor** (per-stream connection state machine, backoff, watchdogs — §5.8), the **event bus and persistent event store** feeding the Events screen and timeline coloring, capability/permission gating (which controls exist *and* which are enabled for the logged-in user level), token refresh, and A/V focus routing.

**Media pipeline layer.**
RTSP (TCP default) and HTTP-FLV ingest, one demux+decode worker thread per stream owning its own `AVFormatContext`/`AVCodecContext`, hardware decode with per-stream software fallback, per-stream triple-buffered latest-frame mailboxes, stream-copy remux recording of the displayed stream, keyframe-seek playback scrubbing, and JPEG snapshots.

**Render layer.**
One render thread per window (main window + pop-outs), each with a single GL context compositing its tiles. Vendor-split zero-copy import with a universal upload fallback; YUV→RGB, range handling, 10-bit tone-map, and fisheye/equirectangular dewarp in fragment shaders. Decode threads never touch GL (§5.2).

**UI layer.**
QML shell (grid, PTZ, timeline, events inbox, doorbell answer, wizard) plus embedded Qt Widgets for form-dense settings. Owns view-model state, persists layout and pane→camera bindings. Supports detached pop-out windows and fullscreen-on-chosen-display (§6.2).

**Storage.**
SQLite for the device tree, layouts, recording index, and event log; libsecret for credentials; a local recordings folder for downloads/manual records.

---

## 4. Protocol Integration Details

### 4.1 HTTP-CGI JSON API

- **Base URL:** `http(s)://{host}:{port}/cgi-bin/api.cgi`. Default 80 / 443.
- **Transport:** HTTP POST. Body is **always a JSON array**, even for one command. The `cmd` name is **also** duplicated as a URL query parameter.
- **Request element:** `{"cmd":"<Name>","action":0|1,"param":{...}}`. `action=0` → current values; `action=1` → values **plus their ranges/capabilities** (used to build dropdowns and gate controls).
- **Response:** JSON array of `{"cmd":...,"code":0,"value":{...}}`; `code!=0` carries `error.rspCode`. Snapshots return `image/jpeg`; downloads return `application/octet-stream`.
- **Batching:** multiple commands in one POST array — used at connect time (e.g. `GetDevInfo`+`GetAbility`+`GetChannelstatus`+`GetEnc`) to cut round-trips.

**Command inventory (drive the whole settings UI):**
`GetAbility`, `GetDevInfo`, `GetChannelstatus`, `GetLocalLink`, `GetNetPort`, `GetEnc`/`SetEnc`, `GetRtspUrl`, `GetIsp`/`SetIsp`, `GetImage`/`SetImage`, `GetOsd`/`SetOsd`, `GetMask`/`SetMask`, `GetTime`/`SetTime`, `GetNtp`/`SetNtp`, `GetUser`, `GetEmail`/`SetEmail`, `GetFtp`/`SetFtp`, `GetPush`/`SetPush`, `GetWebHook`/`SetWebHook`, `GetRec`/`SetRec`, `GetManualRec`/`SetManualRec`, `GetHddInfo`, `GetPerformance`, `GetIrLights`/`SetIrLights`, `GetPowerLed`/`SetPowerLed`, `GetWhiteLed`/`SetWhiteLed` (floodlight), `GetPtzPreset`/`GetPtzPatrol`/`GetPtzGuard`/`GetPtzCurPos`, `GetZoomFocus`/`GetAutoFocus`/`SetAutoFocus`, `GetAiState`/`GetAiCfg`/`SetAiCfg`/`GetAiAlarm`/`SetAiAlarm`, `GetMdState`/`GetMdAlarm`/`SetMdAlarm`, `GetAlarm`/`SetAlarm`, `GetAudioAlarm`/`SetAudioAlarm`, `SetAudioAlarmPlay` (manual siren), `GetBatteryInfo`, `GetPirInfo`/`SetPirInfo`, `GetWifiSignal`, `GetEvents`, `GetAutoUpgrade`.
Actions: `PtzCtrl`, `PtzCheck`, `StartZoomFocus`, `Snap`, `Search`, `Download`, `NvrDownload`, `Reboot`, `CheckFirmware`, `Upgrade`/`UpgradePrepare`/`UpgradeStatus`.

### 4.2 Auth / token flow

- **Login:** `POST ?cmd=Login&token=null` with `[{"cmd":"Login","action":0,"param":{"User":{"userName":"<u>","password":"<p>"}}}]`. Response: `value.Token = {"name":"<tok>","leaseTime":<sec>}`.
- **`leaseTime` is device-supplied.** It is **not** a hardcoded constant. It is 3600 s (1 hour) on essentially all current firmware, but a correct client **reads `value.Token.leaseTime` from each Login response** and re-logs-in before it elapses, subtracting a safety margin (~60 s). Do not assume 3600.
- **Every subsequent request** carries the token as a URL query parameter: `?cmd=X&token=<tok>`.
- **Password handling:** truncate the plaintext password to **31 characters** before sending — Reolink devices have been observed to silently truncate longer passwords on both HTTP and Baichuan, so a client that doesn't match will mis-authenticate. (Note: this 31-char limit is on the *password*, not on any hash; there is no MD5-hex truncation in the HTTP path — the CGI login transmits the password itself.)
- **Logout quirk:** issuing `Logout` **without** a valid token has been observed to break subsequent logins on some firmware — only log out with the live token, and prefer letting the lease expire on shutdown.
- **Provenance caveat:** the three quirks above (31-char truncation, dynamic `leaseTime`, Logout-without-token breakage) are **observed behaviors documented by `reolink_aio` and the Home Assistant integration, to be validated against our target firmware matrix** — treat them as defensive defaults, not immutable facts, and re-verify on each firmware wave since they gate authentication.
- **Security:** the CGI login sends the password in cleartext JSON; **require HTTPS** where the device supports it. Newer firmware ships with RTSP/RTMP/ONVIF **disabled by default** and may be HTTPS-only — detect and surface this in onboarding.

### 4.3 RTSP / FLV URLs and NVR channel addressing

- **Channel addressing is dual-based.** In HTTP `param.channel` it is **0-based** (single camera = 0; NVR = 0..N−1). In RTSP/RTMP URLs it is **1-based, zero-padded to 2 digits**: internal index `i` → `f"{i+1:02d}"` (0→`01`, 1→`02`). This mismatch is a classic bug source — centralize it in one helper.
- **RTSP (port 554):** `rtsp://{user}:{urlenc-pass}@{host}:554/{h264|h265}Preview_{CC}_{main|sub}`.
  Examples: `.../h264Preview_01_main`, `.../h264Preview_01_sub`, `.../h265Preview_01_main` (4K/8MP main), `.../h264Preview_02_sub` (NVR ch 2). Some PTZ models also expose `_autotrack`.
  **Best practice:** call `GetRtspUrl` (with `channel`) to get the device's own exact main/sub URLs (rtsp apiVersion ≥ 3) rather than hand-building.
- **HTTP-FLV (on the HTTP port):** `http(s)://{host}:{port}/flv?port={rtmp_port}&app=bcs&stream=channel{ch}_{main|sub}.bcs&user={u}&password={p}`. Lower-latency than RTSP — this is what the mobile app/web UI use, and it is **our primary live path where the firmware supports it** (RTSP is the fallback).
- **RTMP (port 1935):** `rtmp://{host}:1935/bcs/channel{ch}_{stream}.bcs?channel={ch}&stream={0|1}&token={tok}`.
- **Playback via FLV/RTMP-VOD:** `.../flv?...&stream=playback.bcs&channel={ch}&type={streamType}&start={filename}&seek=0`, or RTMP `rtmp://{host}:1935/vod/{filename}?channel={ch}&stream={streamType}`.
- **Credential exposure caveat:** FLV/RTMP URLs embed `user`/`password` (or token) in cleartext in the URL/query string — visible on the wire over plain HTTP/RTMP and in any intermediary logs. Mitigation: prefer **HTTPS-FLV** where the device supports it, never log full media URLs, and document the residual exposure for users who must run plain HTTP.

### 4.4 Codecs and audio (device reality)

- **Codec depends on model/stream/config, not purely resolution:** 8MP/4K cameras use **H.265** on the main/Clear stream and **H.264** on the sub/Fluent stream; within the main stream, HEVC is used at the top resolution and H.264 below it. **2MP/4MP/5MP** cameras use **H.264 on both streams** and never emit H.265. Many newer PoE/WiFi models expose a manual **H.264 vs H.265 (and H.265+)** toggle on the Clear stream. Read/set via `GetEnc`/`SetEnc` (`mainStream`/`subStream`, `vType`).
- **Audio:** most current models record **AAC**, but **several models (and older firmware) record G.711/PCM** — the demux/decode/remux path must handle **both AAC and G.711 (a-law/mu-law/PCM)** and must not assume AAC-only. Probe the actual audio codec per stream at open. Two-way talkback uses G.711. Snapshots are JPEG.

### 4.5 PTZ

- **Primary path — HTTP `PtzCtrl`:** `param={"channel":ch,"op":"<Left|Right|Up|Down|LeftUp|…|ZoomInc|ZoomDec|FocusInc|FocusDec|ToPos|Stop|Auto>","speed":1..64,"id":<presetId for ToPos>}`. Presets/patrol/guard via `GetPtzPreset`/`GetPtzPatrol`/`GetPtzGuard`; absolute position via `GetPtzCurPos`; `StartZoomFocus` and `PtzCheck` for zoom/focus/calibration. This is the most complete and reliable path and covers all firmware.
- **Secondary path — ONVIF PTZ:** contrary to older lore, Reolink PTZ models **do** expose PTZ via the ONVIF PTZ service (E1 Zoom, E1 Outdoor, RLC-523WA, RLC-823, TrackMix, Argus Track, etc. — this is how Blue Iris/iSpy move them). We keep ONVIF PTZ as a fallback but default to HTTP `PtzCtrl` for coverage and preset fidelity.

### 4.6 Baichuan (port 9000) — events, talk, and battery-cam control/video

- **Role:** (a) real-time push events over a persistent connection (subscribe with `cmd_id=31` on `ch_id=251`; motion/AI/visitor arrive as `cmd_id=33`); (b) **two-way talk** — the message-based talk transport used by the official clients, our primary talkback path (§5.4); (c) the *only* transport for cameras that implement **neither RTSP nor ONVIF** (battery/wire-free Argus & Go family; wired-PoE B800/D800 which are Baichuan-only despite being mains-powered); (d) a fallback for HTTP commands some firmware exposes only over Baichuan.
- **Wire format:** header magic `0x0abcdef0` (bytes `f0 de bc 0a`), numeric `cmd_id`, XML payloads, CRC-32 (`0xEDB88320`). TCP for mains-powered, UDP for battery.
- **Obfuscation/crypto:** legacy XOR (`XML_KEY=[0x1F,0x2D,0x3C,0x4B,0x5A,0x69,0x78,0xFF]`) on older firmware; **modern AES-CFB** (segment_size 128, fixed IV `0123456789abcdef`) on current firmware. The AES key derives from a nonce/password handshake:
  1. Client sends header-only `cmd_id=1` (`message_class="1465"`) to request a nonce.
  2. Device returns `<nonce>`.
  3. Client derives the key from an MD5 over `"{nonce}-{password}"` (uppercase-hex form) and sends the login XML AES-encrypted; subsequent commands are AES-CFB.
  Implementation note: the **31-char limit is on the plaintext password** fed into this derivation — not a truncation of the resulting MD5 hex (a 32-char digest is never sliced to 31). Port this handshake in C++ **from MIT `reolink_aio`'s `baichuan/` submodule and the official docs only**; do not read AGPL `neolink`.
- **Coverage honesty:** `reolink_aio`'s Baichuan implementation covers **events, talk-adjacent control, and some battery flows** — it does **not** implement battery-cam *video* streaming or a complete P2P transport. Those parts are original reverse-engineering work for us (see §4.7 and the M2 spikes).
- **Battery-cam *video* is a required, out-of-process concern.** The only complete open reference for battery-cam video is AGPL `neolink`, which we cannot copy or bundle. **Decision:** for battery-cam live video we bundle and supervise **go2rtc (MIT)** as a local bridge process that speaks the camera's transport and re-emits standard RTSP/FLV, which our media pipeline ingests exactly like any other camera. If go2rtc's coverage proves insufficient for specific models, `neolink` may be documented as a **clearly separate, user-installed optional component** (with its AGPL source offer) that the app can detect and use — it is **never bundled** in the Flatpak/AppImage/deb (§10.2). Battery-cam events/control still go through our in-app Baichuan client. This bridge is **required** for the battery-cam feature — plan, package, and test it as a first-class dependency, not an afterthought.

### 4.7 Remote / UID / P2P — how it works and its limits

- **Mechanism:** each device has a **UID**. The client queries a dispatcher family `p2p0–p2p15.reolink.com` (over UDP, discovery port 9999) which returns **register, log, and relay** server addresses. NAT-traversal packets carry the peer IP inside an obfuscated payload so NAT rewrites don't break hole-punching, establishing a direct client↔camera UDP path; a Reolink **relay** server is the fallback when hole-punching fails.
- **In `reolink_aio`:** UDP port **2015** is the **remote/destination** connect port on the device side (with 2018 for broadcast/discovery); the client's **local** port is OS-assigned (random), *not* fixed to 2015. Note that `reolink_aio` implements only fragments of this — **there is no complete, ready-to-port P2P transport in any permissive codebase.**
- **Scope honestly:** P2P/UID is **its own high-risk, multi-milestone reverse-engineering effort**, not a feature bolted onto another milestone. We de-risk it with an **early feasibility spike (M2)** against a real UID: dispatcher query, hole-punch, relay fallback, and an encrypted media session. Until that spike proves otherwise, the working assumption is that **battery-cam video over P2P will go through the go2rtc bridge even at 1.0**, with in-process P2P media as a post-spike stretch goal.
- **Limits (call these out to users):** remote UID access depends on Reolink's dispatcher/relay infrastructure and reverse-engineered packet formats that **can change with firmware**; the relay is effectively **MITM-capable** (traffic can pass through Reolink servers). Remote-viewing reliability is outside the app's control and needs periodic maintenance. LAN (direct IP) is always preferred; UID is the WAN fallback and a stated best-effort feature.

---

## 5. Media Pipeline

### 5.1 Decode

- **Per stream:** one worker thread owns an `AVFormatContext` (RTSP/FLV demux) and an `AVCodecContext`. Core loop: `av_read_frame` → `avcodec_send_packet` → `avcodec_receive_frame`.
- **Hardware decode:** `av_hwdevice_ctx_create(&ctx, AV_HWDEVICE_TYPE_VAAPI|_CUDA, …)`, assign `avctx->hw_device_ctx`, and return the HW pixfmt (`AV_PIX_FMT_VAAPI`/`AV_PIX_FMT_CUDA`) from the `get_format` callback. Enumerate support with `avcodec_get_hw_config`. Decoded `AVFrame->data[3]` holds a `VASurfaceID` (VAAPI) or `CUdeviceptr` (CUDA). On NVIDIA use `hevc_cuvid`/`h264_cuvid` with `hwaccel_output_format=cuda` to keep frames on-GPU.
- **Software fallback is per-stream and silent.** If HW-device creation fails, `get_format` is never offered the HW pixfmt, or the first frames error (exotic HEVC Main10 / 4:2:2 / unusual GOP that a given VAAPI driver rejects), tear down and reopen that one stream on the threaded software H.264/HEVC decoder. One bad camera profile must never kill the grid. Software H.264/HEVC is the universal floor.
- **NVDEC concurrency:** consumer GeForce **no longer enforces a concurrent decode-session cap** (the historical driver cap applied to NVENC *encode*) — but validate this on the min-spec driver rather than assuming it; Intel/AMD fixed-function decode blocks handle many low-res sub-streams comfortably.

### 5.2 Render — threading, sharing, and fencing model (concrete)

The rule that makes 16 streams tractable: **decode threads never touch GL.** The handoff is data, not GL objects.

- **Producer side:** each decode worker publishes a **ref-counted `AVFrame`** (a VAAPI surface, CUDA frame, or software NV12 buffer) into a **per-stream triple-buffered "latest frame" mailbox** (present-latest, not queue-drain — a slow camera never stalls the grid; a fast camera's stale frames are dropped by ref-release).
- **Consumer side:** a **single render thread per window, owning that window's one GL context**, walks the visible panes each vsync and imports each pane's latest frame *at draw time*:
  - **Intel/AMD zero-copy:** `vaExportSurfaceHandle` (or `av_hwframe_map` to a DRM descriptor) → DMA-BUF fds + planes + modifiers → `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, …)` via `EGL_EXT_image_dma_buf_import[_modifiers]` → `glEGLImageTargetTexture2DOES` into a `GL_TEXTURE_EXTERNAL_OES` sampler. No pixel copy.
  - **Frame lifetime/fencing:** the render thread holds the `AVFrame` reference until the draw that sampled it has **signaled its fence** — an `EGL_KHR_fence_sync`/`EGL_ANDROID_native_fence_sync` fence inserted after the tile draw (implicit sync on Mesa covers the common path, but the explicit fence is kept so frame refs are never released while the GPU may still read the DMA-BUF). Only then is the ref released back to the decoder's frame pool.
  - **NVIDIA:** the proprietary stack does not export VA-DMA-BUF in a GL-importable form. The render thread instead uses **CUDA-GL interop**: `cuGraphicsGLRegisterImage` on a persistent GL texture per pane, map, `cuMemcpy2D` from the NVDEC output (`CUdeviceptr`) into the registered image, unmap, draw. This is one on-GPU copy — not zero-copy, but no PCIe round-trip.
  - **Universal fallback (always available):** `av_hwframe_transfer_data`/`sws_scale` to NV12 in system memory on the decode thread, `glTexSubImage2D` upload (R8 Y-plane + RG8 UV-plane) on the render thread. This path is used whenever zero-copy is unsupported — NVIDIA-Wayland driver gaps, PRIME hybrid-GPU laptops, old Mesa without modifier support — and is the software-decode path's natural output.
- **Zero-copy is a per-stream progressive enhancement**, decided at stream start (probe: HW device creation, export capability, EGL extension presence, modifier compatibility) — **never a requirement**. Any stream can run the fallback while its neighbors run zero-copy.
- **Fragment shaders** do NV12/P010 YUV→RGB with BT.601/709 + limited/full-range awareness and 10-bit tone-mapping.
- **Fisheye/panoramic dewarp (360° models):** an additional fragment-shader stage on the same GL path — equirectangular projection and single/dual-panel dewarp with user-selectable view modes (panorama, quad, virtual-PTZ), driven by per-model lens parameters. No extra copies: it is a different sampling function over the same imported texture. Scheduled late in the roadmap, before 1.0 (M13).
- **Hybrid GPU / PRIME laptops:** decode on the **render GPU** when it has a decode block (enumerate VAAPI/DRM render nodes — `/dev/dri/renderD*` — and match the EGL device in use); when decode must happen on the *other* GPU (e.g., Intel iGPU decode, NVIDIA dGPU render, or vice versa), cross-device DMA-BUF sharing is driver-roulette — take the deterministic route instead: decode, copy to system memory, and upload (the universal fallback). Detection is automatic via VAAPI/DRM device enumeration; the Client Settings diagnostics panel reports which GPU decodes and which renders.
- **Multi-window:** each pop-out window gets its own render thread + GL context consuming the same mailboxes (a mailbox may have multiple consumers; each holds its own frame ref). No GL object sharing across windows is required — sharing happens at the AVFrame/DMA-BUF level.

### 5.3 Audio

Decode **AAC or G.711/PCM** (probed per stream — §4.4) from the focused pane's `AVFormatContext`, resample with `libswresample` to the device rate, and play out via PipeWire `pw_stream` (Pulse-compat fallback). Only the **selected pane** is unmuted; audio drives A/V sync for that one stream. Mixing 16 audios is never wanted.

### 5.4 Two-way audio (talkback) and doorbell

- **Primary path — Baichuan talk (all models).** The official clients speak talkback over the Baichuan connection as message-framed G.711 audio (talk-config exchange, then audio payload messages on the established port-9000 session). This is the path that works across the whole lineup — including doorbells and battery cameras — so it is our default. Capture mic via PipeWire, encode G.711 (`pcm_mulaw`/`pcm_alaw`), frame per the Baichuan talk exchange ported from `reolink_aio`'s talk flows and validated with Wireshark.
- **Secondary path — ONVIF Profile-T backchannel**, only where the device advertises it: `DESCRIBE` with `Require: www.onvif.org/ver20/backchannel`, `SETUP` the advertised `sendonly` media line, `PLAY`, then write G.711 RTP after the `200 OK`. FFmpeg's RTSP muxer backchannel support is insufficient, so this is a small bespoke RTP sender, verified bidirectionally with Wireshark.
- **Doorbell flows (lands with the talk milestone, M10):** the Reolink Video Doorbell surfaces a **visitor press** as a Baichuan/`GetEvents` visitor event. The app raises an **answer surface** (prominent notification + one-click open of the doorbell's pane) with **Answer** (starts two-way talk), **Dismiss**, and **quick-reply audio clips** — the doorbell's stock replies enumerated and triggered via its quick-reply API (`GetAudioFileList`/`AudioAlarmPlay`-family, per `reolink_aio`'s quick-reply support), plus user-recorded clips uploaded to the device where firmware allows.

### 5.5 Recording and snapshots (no re-encode)

- **Record = remux of the displayed stream.** v1 manual recording captures **exactly what the pane is showing** — the sub-stream in grid view, the main stream when maximized — matching the official client's behavior. The recorder **taps the packets of the existing display session** (a packet-tee before the decoder); it **never opens a second stream** for recording. This keeps the per-device session budget flat (§5.7). Continuous multi-camera 24/7 recording is a stated non-goal (§1.3): that is the NVR's job, and the client's job is to search/play/download the NVR's recordings.
- **Muxing:** open an output `AVFormatContext` (fragmented MP4 for crash-safety), `avformat_new_stream` copying `codecpar`, and `av_interleaved_write_frame` the demuxed packets after `av_packet_rescale_ts`. Audio track carries AAC as-is or G.711 (PCM in MP4 where compliant; transparently repacked when required).
- **H.264/HEVC bitstream handling (load-bearing detail):**
  - Annex-B → length-prefixed conversion for MP4 (AVCC for H.264; **hvcC for HEVC**).
  - **Parameter sets:** live RTSP/FLV streams often carry VPS/SPS/PPS only in-band (no `extradata` until the first IDR). Run the **`extract_extradata` bitstream filter** on the packet path to populate `codecpar->extradata` before writing the header; if the header must be written before the first IDR arrives, buffer packets until parameter sets are extracted.
  - **Sample-entry brand:** write **`hvc1`** (parameter sets out-of-band in the `hvcC` box) rather than `hev1` — `hvc1` is what QuickTime/Apple players require and every other player accepts; it also makes downstream seeking/thumbnailing cheaper.
- **Snapshot:** for a displayed stream, reuse a decoded frame; for a hidden one, `Snap` over HTTP-CGI (returns JPEG directly) or decode-on-demand. If only a HW surface exists, `av_hwframe_transfer_data` to system memory, then encode with the `mjpeg` encoder.

### 5.6 Playback seeking

- **Local files:** `avformat_seek_file(…, AVSEEK_FLAG_BACKWARD)` to the nearest preceding keyframe, then decode-and-discard to the target PTS for frame-accurate scrubbing (GOP-length latency). Frame-by-frame steps decode forward one frame.
- **Device-side playback:** use the FLV/RTMP-VOD URLs (§4.3) with a `start` filename + `seek`, or ONVIF Profile-G replay with an RTSP `Range` header. While dragging the timeline, request keyframe-granular positions; resolve to exact frame on release.

### 5.7 Scaling to 16 streams and the per-device session budget

- **Grid subscribes to each camera's SUB-stream** (H.264, ~640×360–720p, low bitrate). **Main/Clear stream opens only on the maximized/fullscreen pane** (and where the user picks Clear). This cuts decode + upload + shader cost by roughly **5–10×** vs 16 main streams and is how commercial NVRs operate.
- **Session budget (hard constraint, enforced centrally).** Reolink cameras typically serve only **~2–3 concurrent RTSP/FLV sessions**, and an NVR's total client uplink bandwidth and session count are similarly limited. The connection manager therefore maintains a **per-device budget**: every stream open is brokered, existing sessions are reused wherever possible (display + record share one session via the packet tee — §5.5; snapshot prefers `Snap` over opening a stream), and requests beyond budget are queued or degraded (e.g., a second view of the same channel shares the first's frames). This is why "record everything while displaying everything" is out of scope: it would double the session count to ~32 and exceed both camera limits and NVR uplink.
- **Threading model:** 16 demux+decode workers → per-stream latest-frame mailboxes → 1 render thread per window, N tiles per vsync (§5.2).
- **Transport tuning:** FLV where supported (lowest latency); otherwise `rtsp_transport=tcp` (firewall-friendly), `fflags=nobuffer`, `flags=low_delay`, small `max_delay`/`reorder_queue_size`, low `probesize`/`analyzeduration`, socket `timeout`; app-side frame queue depth 1–2. Request short camera GOP (1–2 s) where configurable for fast paint after open/loss (at a bitrate cost).
- **Early validation:** confirm every camera in the target lineup exposes a *usable* sub-stream and short GOP, and measure its real concurrent-session ceiling; models that force main-stream-only tiles change the decode budget. (This is part of the M2 bench spike.)

### 5.8 Stream lifecycle, reconnect, and supervision

Every stream is owned by a **supervised connection state machine** in the application core:

```
idle → connecting → streaming → degraded → backoff → connecting → …
```

- **connecting:** URL resolution (`GetRtspUrl`/FLV build), open with timeouts; failure → backoff.
- **streaming:** healthy; a **watchdog** requires at least one decoded frame every **10 s** — on violation the stream enters *degraded*.
- **degraded:** attempt in-place recovery first — RTSP `TEARDOWN` + re-`SETUP`/`PLAY` (or FLV reconnect) without tearing down the decoder; on repeated failure → backoff.
- **backoff:** **exponential with jitter, 1 s → 30 s cap**; each retry re-resolves the URL (the device may have rebooted onto a different codec/port).
- **Auth integration:** any 401/expired-token error triggers a **token refresh** (re-Login) via the device session before retrying — never counted as a network failure. **Credential rotation** (password changed on the device) is detected as repeated auth failures post-refresh and surfaces a re-authentication prompt on the device entry rather than hammering the device into lockout.
- **UI surfacing:** each pane binds to its stream's state — spinner while *connecting*, live video while *streaming*, a stale-frame overlay + reconnect note in *degraded*, an offline badge with countdown in *backoff*. The device list mirrors aggregate health.
- Recording taps survive reconnects: the remuxer tolerates discontinuities by starting a new fragment on timestamp jumps.

---

## 6. UI Screen Inventory (mapped 1:1 to the official Client)

**Persistent chrome.** Left **device list** (cameras and NVRs; battery models show a **battery %/charging badge** — §6.8) with a **collapse** button to widen the viewing area; top-left **`+` add-device**; top-right **gear** → Client Settings; a **lock-screen** icon; feedback option. Four main pages: **Live View**, **Playback**, **Events**, **Device Settings**. (Home Hub — including its video-decryption tool — is deferred to post-1.0; see §11.)

### 6.1 Add Device wizard (4 methods)

Controls: **Add Automatically** (LAN auto-discovery), **LAN Scan** (list + select), **Manual IP/Domain** (field + **Add**), **By UID** (field + **Access Device**/**Add**). Then **Username** (default `admin`), **Password**, **Login/Confirm**. Copy: "same network → IP, different network → UID."

**WiFi provisioning / QR pairing:** onboarding a factory-fresh WiFi camera (QR-code pairing, sound pairing) is primarily a **mobile-app flow** and is not designed for v1. Open task: **verify what the current official desktop client offers here; if it has a desktop provisioning flow, replicate it in the battery/WiFi milestone (M11)** — otherwise document "set up via mobile app, then add here by IP/UID."

### 6.2 Live View

- **Layout controls:** one-screen vs multi-screen toggle; **split/layout presets 1 / 4 / 9 / 16**; **previous/next** paging; **scrollview** auto-cycle (interval from Client Settings → *Scrollview Time*); **double-click a pane to maximize/restore**; **fullscreen**; device-list **collapse**.
- **Multi-monitor:** any pane or a whole grid can be **popped out into a detached secondary window** (own render thread/GL context — §5.2), and fullscreen can target a **chosen display** — matching the official client's multi-screen live view. Pop-out layouts persist with the main layout.
- **Per-pane floating toolbar:** **stream quality Clear / Balanced / Fluent** (Balanced shown only when the camera advertises a third stream — §6.10), **snapshot**, **manual record/clip** (records the displayed stream — §5.5), **two-way talk** (push-to-talk), **volume/mute**, **digital zoom**, **optical zoom + manual focus** (capable models), **PTZ**, **manual siren** (momentary trigger via `SetAudioAlarmPlay`, capability-gated), **spotlight/floodlight toggle** (`SetWhiteLed`, capability-gated), **fullscreen/pop-out**.
- **Staged enablement:** controls whose backend lands in a later milestone (the talk button before M10; Balanced gating before full `GetAbility` parsing in M9) ship **disabled with an explanatory tooltip** rather than hidden or broken — the layout is final from M5, the wiring arrives per the roadmap.
- **PTZ overlay:** pan/tilt/zoom **joystick**, **presets**, **patrol** (speed + duration), **Guard Point**, **auto-track** toggle (capable models).
- **Pane state:** connection state machine surfaced per pane (spinner / degraded overlay / offline badge — §5.8).

### 6.3 Playback

- **Calendar/date picker:** dates with recordings marked by a **blue dot** (respects Client *Date Format*).
- **Timeline bar:** left-click to jump; color-coded segments; **mouse-wheel zoom** on the time interval; drag to scrub; **hover shows a thumbnail preview**; **double-click for multi-camera synchronized playback**.
- **Timeline data sources (defined exactly):**
  - The **base two-tone coloring** — **grey = timer/continuous, blue = alarm, black/empty = none** — comes from **`Search` (cmd=Search)** file lists: each returned file carries a type flag distinguishing scheduled/timer recordings from alarm-triggered ones, plus start/end timestamps that become segments.
  - **Per-AI-type coloring** (Person/Vehicle/Pet/Visitor sub-colors within alarm segments) requires the **event log** — Baichuan push history accumulated in the local event store plus **`GetEvents`** backfill where the firmware supports it. Alignment is by **timestamp intersection**: an event interval that overlaps a file segment colors that overlap with the event's AI type.
  - **Fallback:** when event data is unavailable (unsupported firmware, event subsystem not yet connected), the timeline **degrades gracefully to the two-tone display** — never blank, never wrong colors.
- **Thumbnails:** hover previews use **`Snap`-derived thumbnails for v1** (per-file preview JPEGs fetched lazily and cached in SQLite); decoding downloaded keyframes is a post-1.0 refinement.
- **Alarm-type filters** (turn blue when active, only shown for supported types): **Any Motion, Person, Vehicle, Animal/Pet, Visitor** — powered by the same event-log alignment above (land with the event subsystem, M8).
- **Playback controls:** **stream toggle** Fluent/Clear; **speed** control; **frame-by-frame**; **digital zoom** on the playback video (same shader zoom as live); volume; fullscreen.
- **Time-lapse:** cameras/NVRs that record time-lapse expose those files through search — the playback page gets a **time-lapse browsing tab** (per-day time-lapse files, play/scrub/download).
- **Download:** **download current clip**, **scissors clip-cut** to trim a segment first, right-click **Save as MP4** (`Download`/`NvrDownload`).

### 6.4 Events / Notification Center

A first-class page (mirroring the official client/app's notification center):

- **Chronological event inbox** fed by **Baichuan push** (live) and **`GetEvents` polling/backfill** (where supported), persisted in the local event store (§7.1).
- Each entry: timestamp, camera/channel, **AI-type icon** (motion / person / vehicle / animal-pet / visitor-doorbell), and a snapshot thumbnail where available (`Snap` at event time for live events).
- **Filters** by device, time range, and AI type; unread badge on the page tab; optional **Alarm Beep** per Client Settings.
- **Jump-to-playback:** clicking an event opens Playback at that channel/timestamp (timeline centered on the event interval).
- Doorbell visitor-press events additionally raise the answer surface (§5.4) in real time.

### 6.5 Doorbell answer surface

For Reolink Video Doorbell devices: a **visitor press** raises an immediate, high-priority overlay/notification with live view of the doorbell, **Answer** (two-way talk), **Dismiss**, and **quick-reply** buttons (device-stored audio clips — §5.4). Also reachable from the Events inbox entry. Lands with the talk milestone (M10).

### 6.6 Downloads manager

Queue with per-item **progress**, **destination folder**, **batch download**, cancel/retry; sources are NVR/SD searches and clip-cuts. (Cloud Library sources return with the post-1.0 cloud subsystem — §11.)

### 6.7 Device Settings (five groups)

- **Device / Stream:** per-stream **Clear (Main)** and **Fluent (Sub)** (and Balanced where present): **Resolution, Frame Rate, Max Bitrate, I-frame Interval, Frame Rate Mode, Bitrate Mode** (model-dependent), **Encoding H.264 / H.265 / H.265+** toggle where supported. **Display/Image:** Brightness, Anti-Flicker, HDR, Flip V/H (rotate), Day/Night switch mode (Auto/Day/Night), Highlight/Shadow, IR lights, exposure/backlight, 3D-NR; **OSD** (Camera Name position, Date position, Logo Watermark, Motion Mark); **Privacy Mask**; **Image Stitching** (dual-lens); **fisheye view-mode defaults** (360° models — §5.2).
- **Surveillance:** recording schedule (per camera/channel); **Push Notifications** (interval + schedule, per detection type); **Email Alerts**; **FTP** (server + port default 21, schedule, motion-only vs continuous interval, per type); **Siren** (schedule/auto config; the *manual* trigger lives on the live-view toolbar); **Linked Devices**; **Motion/Smart Detection** (sensitivity per Person/Vehicle/Pet, up to 4 time periods, Detection Zone grid, Object Size min/max, Alarm Delay/dwell, PTZ auto-track).
- **Network:** connection status/diagnostics, ports, UPnP.
- **Storage:** SD/HDD list, capacity, **format**.
- **System:** firmware upgrade (Check/Upgrade), password change, user/account management (Admin vs restricted User), time/date + timezone.
- **NVR channel management (NVR hosts only):** a channel table showing each channel slot's bound camera (model, IP, firmware, online state) with **bind/unbind/re-pair** actions and per-channel add (for cameras reachable on the NVR's PoE/LAN segment) — the desktop equivalent of the NVR's own channel management screen.

### 6.8 Battery / solar status surface

For battery-powered models (Argus/Go class):

- **Device-list badge:** battery percentage + charging/solar indicator on the device entry (from `GetBatteryInfo`).
- **Detail panel** (in Device Settings → Device, and on the pane's info overlay): battery %, charge state (charging / discharging / solar input), voltage/temperature where reported, PIR wake configuration (`GetPirInfo`/`SetPirInfo`), and low-battery alerts routed through the Events inbox.
- Polling is battery-respectful: status refreshes piggyback on user-initiated wakes rather than keeping the camera awake.

### 6.9 Client (app-level) Settings — top-right gear

Run at Startup, Automatic Client Update, Add Device Automatically, Auto Live View, **Stretch Mode** (4:3↔16:9), **Scrollview Time**, **Date Format**, **Alarm Beep**, **Lockscreen Password**, **Hardware Decoding first**, **Language**, **System Status** (diagnostics: per-stream decode/render backend, GPU enumeration, session budget usage), local-recordings folder.

### 6.10 Capability and permission gating (drives which controls exist — and which are enabled)

Query `GetAbility` (per-user) + `GetChnTypeInfo`/`GetChannelstatus` + `GetEnc` ranges (`action=1`) at connect and show/hide accordingly: **PTZ vs fixed**, **AI (person/vehicle/pet) vs basic motion**, **audio present**, **spotlight/siren**, **battery vs mains**, **doorbell**, **fisheye**, and **Balanced** (present only when a third stream is advertised — otherwise only Clear + Fluent, matching the official client's per-camera behavior).

**Sub-user / restricted accounts:** every privileged control binds to a unified **capability/permission model** resolved from `GetAbility` *for the logged-in user* combined with the account's user level (Admin vs User). A restricted login sees the same layout as an admin, but privileged controls (settings writes, PTZ where denied, format, firmware, user management, reboot) render **disabled with a tooltip** ("requires administrator account on this device") rather than hidden — matching the official client and keeping the UI stable across account types. The permission model is the single source of truth; no control ships with an ad-hoc `isAdmin` check.

---

## 7. Data Model & State Management

### 7.1 Core entities

```
Host        { id, kind(Camera|NVR), addr(ip|uid), port, https,
              credentialsRef, deviceInfo, ability, userLevel, leaseTime, lastSeen }
Channel     { hostId, index(0-based), name, model, online,
              capabilities{ptz,ai,audio,battery,doorbell,fisheye,siren,spotlight,
                           streams[main,balanced?,sub]},
              batteryState{percent,charging,solar}? }
StreamProfile { channelId, kind(main|balanced|sub), codec, res, fps, bitrate }
Layout      { id, preset(1|4|9|16), panes[ {slot, channelId, streamKind} ],
              popoutWindows[ {display, geometry, panes[]} ] }
RecordingIndex { channelId, day, segments[{name, start, end, recType(timer|alarm),
                 size, source}] }
Event       { id, channelId, ts, endTs?, type(md|person|vehicle|pet|visitor),
              source(baichuanPush|getEvents|onvif), thumbRef?, read }
Preset/Patrol/GuardPoint { channelId, ... }   // PTZ
EventSubscription { hostId, transport(baichuan|onvif), renewAt }
SessionBudget { hostId, maxSessions, openSessions[] }   // enforced by connection mgr
```

### 7.2 State management

- **View-models** are `QAbstractItemModel` subclasses bound into QML (device tree, grid panes, timeline segments, event inbox, download queue).
- **Application core** owns an **event bus**: protocol-layer events (token refreshed, channel online/offline, motion/AI/visitor push, stream state transitions, download progress) fan out to view-models on the UI thread via queued signals. Media threads never touch QML directly.
- **Event store:** pushed/polled events are appended to the SQLite `Event` table (ring-buffered by age/size); the Events page and the timeline's AI-type coloring both read from it (§6.3, §6.4).
- **Capability/permission model** is resolved once per connect (per user) and cached; every gated control binds to it (§6.10).
- **Persistence:** last-used layout (including pop-out windows), pane→camera→stream bindings, device tree, recording index, and event log in **SQLite**; restored on launch.

### 7.3 Config & credential storage

- **Credentials in the OS keyring** via **libsecret** (Secret Service; GNOME Keyring / KWallet backends). SQLite stores only a `credentialsRef`, never a password.
- Non-secret prefs (layout, Client Settings) in SQLite (or a small JSON) under `$XDG_CONFIG_HOME/reonative/`.
- Recordings/snapshots under `$XDG_VIDEOS_DIR` (configurable in Client Settings).

---

## 8. Build, Packaging, Distribution

### 8.1 Build system

- **CMake** (Ninja), C++20. Presets for `debug`/`release`/`asan`.
- **FFmpeg build discipline (load-bearing):** pin an **LGPL-only** FFmpeg — **no `--enable-gpl`, no x264/x265, no GPL filters.** We need only decoders + muxers + bitstream filters (decode + stream-copy remux + `extract_extradata` + mjpeg encode). CI audits the FFmpeg configure flags on every build; accidental GPL linkage would taint the whole binary.
- **Qt dynamically linked** (LGPLv3 compliance — §10). No static Qt.
- **i18n from the first commit:** all user-visible strings go through Qt `tr()`; `lupdate`/`lrelease` wired into the build from M0 so localization is a translation task, not a retrofit.

### 8.2 Packaging & distribution

- **Flatpak (primary):** `org.kde` runtime (ships Qt 6), bundling FFmpeg/OpenSSL/pugixml/curl/SQLite and the **go2rtc (MIT) bridge binary** as needed; `--device=dri` for VAAPI, PipeWire portal for audio/mic. License texts included. **AGPL components (`neolink`) are never bundled** — if supported at all, they are user-installed and merely detected (§10.2).
- **AppImage (secondary):** `linuxdeployqt`, glibc-conservative base for broad distro reach.
- **`.deb`:** for Debian/Ubuntu users who prefer system integration; may depend on the system FFmpeg where distro policy makes bundling an HEVC decoder problematic (§10.2).
- All third-party libs LGPL/MIT/Apache and **dynamically linked** (or out-of-process); license texts and relink capability shipped so the (proprietary) app remains freely redistributable.

### 8.3 Target distros

Ubuntu 22.04+/24.04, Fedora 39+, Debian 12+, Arch/CachyOS, openSUSE — validated on both **X11 and Wayland**.

### 8.4 GPU / driver considerations and the hardware test matrix

- **Intel:** VAAPI via `iHD` (media-driver) on Gen8+ — cleanest DMA-BUF zero-copy path, best on Wayland. **Legacy `i965`** driver (pre-Broadwell and some distro defaults) has different export behavior — it falls back to the upload path; test at least one `i965` box.
- **AMD:** VAAPI via Mesa `radeonsi`/RADV — DMA-BUF with modifiers works on recent Mesa; set **Mesa ≥ 21 as the recommended floor** for `EGL_EXT_image_dma_buf_import_modifiers`; older Mesa silently falls back to upload.
- **NVIDIA:** proprietary driver; **no VA-DMA-BUF export** → CUDA-GL interop backend (§5.2); validate across the supported proprietary driver branches (min-spec = oldest driver supporting the min CUDA toolkit), on **both X11 and Wayland** (the NVIDIA-Wayland GL interop path has driver-version-dependent gaps — the upload fallback covers them).
- **Hybrid GPU / PRIME laptops** (Intel+NVIDIA, AMD+NVIDIA): render GPU vs decode GPU selection per §5.2; at least one PRIME laptop in the matrix.
- **Commitment:** a **physical test matrix + CI** with at least one machine per vendor (Intel iHD, Intel i965-era, AMD/Mesa-floor, AMD-recent, NVIDIA proprietary, one PRIME laptop), each exercised on X11 and Wayland. The M2 bench spike runs on this matrix; the M14 hardening milestone signs it off.
- Ship a **software-decode fallback** and a Client Settings toggle ("Hardware Decoding first") mirroring the official client. Detect driver at runtime; log the selected decode/render backend per stream for support.

---

## 9. Phased Implementation Roadmap

**Resourcing assumption (explicit):** this roadmap assumes **2–3 experienced C++/Qt engineers** working in parallel (one media/GL-focused, one protocol-focused, one UI-focused at peak). A **single-developer timeline roughly doubles** these figures. Week ranges below overlap where tracks are parallel.

**M0 — Scaffolding (weeks 1–2).** CMake project, Qt6 QML shell, dark theme baseline, empty four-page navigation, **SQLite + libsecret + settings skeleton**, logging, **i18n string externalization (`tr()`/`lupdate`) wired from the first commit**. LGPL-only FFmpeg build in CI with flag audit.

**M1 — Protocol core + first single live stream (weeks 3–6).** HTTP-CGI client (login/token with dynamic `leaseTime` refresh, 31-char password rule, batched connect — quirks validated against real firmware), `GetDevInfo`/`GetAbility`/`GetChannelstatus`/`GetEnc`, `GetRtspUrl`. FFmpeg RTSP/FLV ingest + **software decode** rendered to one `QRhiWidget` tile. Add-device by IP. **Exit:** one camera live in a single pane.

**M2 — Feasibility spikes (weeks 3–8, parallel track).** Three de-risking spikes run alongside M1/M3, before any UI buildout depends on their outcomes:
  (a) **P2P/UID handshake spike** against a real UID: dispatcher query (`p2p*.reolink.com`), hole-punch, relay fallback, encrypted session — establishes whether in-process P2P media is 1.0-feasible or bridge-only (§4.7).
  (b) **Battery-cam-via-bridge spike**: go2rtc against Argus/Go hardware — coverage, wake latency, battery impact.
  (c) **16-stream decode/render bench** on the 3-vendor GPU matrix (§8.4): sub-stream grid, zero-copy vs upload, session-ceiling measurement per camera/NVR.
  **Exit:** written go/no-go + scope decisions feeding M11/M12.

**M3 — Hardware decode + zero-copy render (weeks 7–10).** VAAPI decode + DMA-BUF→EGLImage→GL on Intel/AMD; NVDEC + CUDA-GL interop on NVIDIA; the mailbox/fencing model of §5.2; per-stream silent software fallback; NV12/P010 shaders; PRIME detection. **Exit:** hardware-decoded single tile on all three vendors, X11 + Wayland.

**M4 — Manual per-pane recording (weeks 11–13).** Packet-tee remux of the displayed stream (§5.5): fragmented MP4, Annex-B→AVCC/hvcC, `extract_extradata` parameter-set handling, `hvc1` branding, AAC + G.711 audio tracks, discontinuity tolerance, snapshot. Session-budget enforcement in the connection manager. **Exit:** record button produces valid MP4s (H.264 and HEVC, both audio codecs) with no extra device session.

**M5 — Live grid (weeks 12–16).** 1/4/9/16 layout presets, sub-stream-for-grid / main-on-maximize, double-click maximize, fullscreen, floating per-pane toolbar (snapshot, record, mute, digital zoom; **talk and Balanced controls present but disabled-with-tooltip** until M10/M9), **stream-lifecycle supervisor + reconnect/backoff state machine (§5.8)** with per-pane state UI, present-latest mailboxes, layout persistence. **Exit:** stable 16-pane grid on sub-streams surviving camera reboots and network drops.

**M6 — PTZ (weeks 17–19).** HTTP `PtzCtrl` joystick, presets, patrol, guard point, zoom/focus; ONVIF PTZ fallback; capability gating for PTZ surfaces. **Exit:** full PTZ on a PTZ camera.

**M7 — Playback + timeline + downloads (weeks 18–23).** `Search` → recording index + calendar dots; **base two-tone timeline** (timer/alarm from `Search` type flags — §6.3) with wheel-zoom; seek (keyframe + decode-to-target), frame-step, speed, playback digital zoom; **hover thumbnails via `Snap`**; time-lapse browsing tab; multi-camera sync playback; `Download`/`NvrDownload` with scissors clip-cut and a download queue. **Exit:** scrub + download recordings; timeline correct in two-tone mode.

**M8 — Event subsystem + Notification Center (weeks 24–27).** Baichuan port-9000 AES-CFB client for real-time push (ONVIF WS-BaseNotification fallback), `GetEvents` polling/backfill, persistent event store; **Events inbox page** (§6.4) with AI-type icons/filters and jump-to-playback; **AI-type timeline coloring + alarm-type filters land here**, upgrading M7's two-tone timeline via timestamp intersection (§6.3). **Exit:** live alerts in the inbox; timeline shows per-type coloring where firmware supports it.

**M9 — Settings (weeks 26–31).** All five Device Settings groups (Stream/Display/OSD/Mask, Surveillance/detection/alarm/FTP/email/push, Network, Storage, System) built from `GetEnc/GetIsp/GetOsd/…` with `action=1` ranges driving dropdowns; **NVR channel management UI** (§6.7); Client Settings; full capability/permission gating including restricted-user behavior (§6.10) — Balanced gating goes live. **Exit:** feature-parity settings for admin and restricted users.

**M10 — Audio, two-way talk, doorbell, siren/spotlight (weeks 32–36).** PipeWire playback on focused pane (AAC + G.711); **Baichuan talk as the primary talkback path**, ONVIF Profile-T backchannel secondary (§5.4) — the M5 talk button goes live; **doorbell answer surface + quick-reply clips**; **manual siren trigger + spotlight toggle** on the pane toolbar (`SetAudioAlarmPlay`/`SetWhiteLed`). **Exit:** two-way conversation with a doorbell, including quick replies and visitor-press answer flow.

**M11 — Battery cameras + bridge (weeks 35–39).** go2rtc bridge manager (spawn/config/supervise) for battery-cam video per the M2(b) spike; in-app Baichuan control/events for battery models; **battery/solar status surfaces** (device-list badge + detail panel, `GetBatteryInfo`/`GetPirInfo` — §6.8); battery-respectful polling; resolve the WiFi-provisioning parity question from §6.1 (replicate if the official desktop client has it). **Exit:** Argus-class camera live, recorded, and monitored.

**M12 — Remote / UID / P2P (weeks 37–44, high-risk track).** Scoped by the M2(a) spike: UID onboarding, dispatcher/hole-punch/relay transport, remote HTTP + media where feasible in-process; where not, remote battery video stays on the bridge (stated openly — §4.7). Protocol-regression tests against recorded handshakes. **Exit:** remote viewing via UID on the validated model set, with best-effort labeling.

**M13 — Late feature completion (weeks 43–46).** **Fisheye/panoramic dewarp** shaders + view modes for 360° models (§5.2); **multi-monitor pop-out windows + fullscreen-on-chosen-display** (§6.2); scrollview auto-cycle; lock-screen. **Exit:** feature-complete against the v1 screen inventory.

**M14 — Hardening (weeks 45–50).** **72-hour 16-stream soak tests** (leak/fd/VRAM growth budgets); **fuzzing the protocol parsers** (Baichuan framing/XML, HTTP-CGI JSON, FLV demux edge); **security review** (credential storage, TLS validation, URL credential handling, bridge process isolation); **remux edge-case corpus** (mid-GOP starts, missing parameter sets, timestamp jumps, codec switches, G.711 files); memory-leak passes (ASan/LSan/Valgrind); **hardware-matrix validation sign-off** (§8.4). **Exit:** release-blocking checklist green.

**M15 — Packaging + 1.0 polish (weeks 49–52).** Flatpak/AppImage/deb, driver detection + diagnostics page, translations (strings already externalized since M0), per-model fidelity passes against the current official client build, docs (including the credential-exposure and best-effort-P2P notes). **Exit:** 1.0 release.

---

## 10. Risks, Unknowns, and Legal/Licensing Notes

### 10.1 Technical risks

- **NVIDIA render fork.** No VA-DMA-BUF export forces a second CUDA-GL interop backend, ~doubling render-path dev/test; the universal upload fallback (§5.2) bounds the damage but not the perf. Validate on real Intel/AMD/NVIDIA × X11/Wayland before committing (M2c, M3).
- **Hardware-decode portability matrix is large.** Not just 3 vendors × 2 display servers: **Intel iHD vs legacy i965**, **Mesa version floor for DMA-BUF modifiers (Mesa ≥ 21 recommended)**, **NVIDIA proprietary driver branches**, **Wayland vs X11**, and **PRIME hybrid laptops** multiply the matrix. Mitigation: the committed physical test matrix + per-vendor CI boxes (§8.4), zero-copy as per-stream progressive enhancement, and the always-available upload fallback.
- **HEVC edge cases.** Main10 / 4:2:2 / unusual GOP rejected by some VAAPI drivers; per-stream silent software fall-through is mandatory. Remux edge cases (in-band-only parameter sets, mid-GOP joins) are covered by the `extract_extradata` handling (§5.5) and the M14 corpus.
- **Per-device session limits.** Cameras serve ~2–3 concurrent media sessions; NVR uplink is finite. The session-budget broker (§5.7) is the control point; measure real ceilings per model in M2(c). This is also why continuous client-side multi-cam recording is a non-goal.
- **16× main-stream is infeasible** on practical hardware; the sub-stream-for-grid strategy is required. Cameras without a usable sub-stream or with long GOP degrade the grid — verify the lineup early (M2c).
- **Firmware/auth quirks:** dynamic `leaseTime`, 31-char password truncation, Logout-without-token breakage, HTTPS-only newer firmware with RTSP/ONVIF disabled by default — all handled defensively, and all **treated as observed behaviors to re-validate per firmware wave** (§4.2), since they gate authentication.
- **Two-way audio** rides Baichuan message framing (primary) and a bespoke ONVIF backchannel RTP sender (secondary) — both custom transports verified with Wireshark; neither is off-the-shelf in FFmpeg.
- **P2P/UID is the highest-RE-risk feature.** It depends on Reolink relay/dispatcher infra and reverse-engineered packet formats that change with firmware; no permissive codebase implements it completely. Mitigations: the M2(a) spike gates its scope, M12 isolates it, the bridge remains the battery-video fallback, and it ships labeled best-effort.
- **Protocol drift / adversarial vendor.** Reolink can change Baichuan/P2P formats at any firmware release, and could view an unofficial client adversarially (dispatcher blocking, format churn). Commitment: a **protocol-regression test rig** run against real firmware on the device lab (recorded handshake corpus + live smoke tests), a **maintenance cadence** tied to Reolink firmware waves, and release notes flagging firmware-compatibility status. See also the ToS note in §10.2.
- **UI fidelity is a moving target.** Reolink updates its client; grid presets, floating-toolbar icon order, timeline color mapping, and capability-gated controls must be re-checked per build (M15 fidelity passes).
- **Resourcing.** The 52-week plan assumes **2–3 experienced C++/Qt engineers**; a single developer should expect roughly **double** the calendar time, and should front-load M2's spikes even more aggressively since serial discovery of a blocker is costlier.

### 10.2 Legal / licensing notes

- **Baichuan reimplementation firewall.** The port-9000/AES-CFB/P2P protocol must be reimplemented in C++ **strictly from MIT `reolink_aio` + the official API PDF**. **`neolink` is AGPLv3** — off-limits for study-to-copy; copying it taints the product's redistribution. Enforce this in code review.
- **Battery-cam bridge licensing.** The bundled bridge is **go2rtc (MIT)** — bundling and auto-launching it is unambiguously safe. **`neolink` (AGPLv3) is never bundled** in the Flatpak/AppImage/deb: bundling + auto-launching an AGPL binary in our installer would weaken any "mere aggregation" position and, because AGPL's §13 network-source obligation attaches to that binary, creates a source-offer duty we don't want coupled to our releases. If specific models force it, `neolink` is a **clearly separate, user-installed optional component** — the app may detect and use an existing install, with documentation pointing at upstream source. **Counsel sign-off required** on the final bridge packaging before 1.0.
- **FFmpeg / HEVC redistribution.** Ship an **LGPL-only** FFmpeg (decode + remux + mjpeg); never `--enable-gpl`/x264/x265. HEVC/H.264 **patent licensing** (MPEG-LA / Access Advance pools) is separate from copyright: **our Flatpak/AppImage bundles a software HEVC decoder path via FFmpeg — the "distros already ship VAAPI HEVC" argument does not cover a decoder we ourselves distribute.** Flag for counsel before release. Fallback option if bundling is problematic in some channels: build the `.deb`/distro packages to **link the system FFmpeg** (decode-only), shifting the codec question to the distro, and keep the bundled decoder only where the channel's policy permits.
- **Qt LGPLv3.** Keep Qt **dynamically linked**, ship the Qt shared libs with relink capability and LGPL license text; then the app source may remain proprietary. Any static-link shortcut triggers object-file/relink obligations (or requires a commercial Qt license) — avoid.
- **Reolink cloud/P2P and ToS exposure.** UID/P2P uses Reolink's dispatcher/relay **infrastructure** with reverse-engineered handshakes; the relay is MITM-capable, and using Reolink's relay servers from an unofficial client may sit outside Reolink's terms of service — Reolink could rate-limit, block, or change formats at will. Disclose to users, prefer LAN, treat as best-effort, and have counsel review the ToS exposure of relay usage before enabling it by default.
- **Trademark.** "Reolink" is a trademark; ship as an independent compatible client, not as an official Reolink product, and avoid the Reolink name/logo in branding.

### 10.3 Open unknowns to resolve early

- Exact per-model sub-stream availability, GOP config, and **real concurrent-session ceilings** across the target lineup (drives decode + session budgets — M2c).
- P2P feasibility envelope: which parts of the UID transport can be done in-process at acceptable maintenance cost (M2a).
- go2rtc's actual battery-model coverage and wake-latency behavior (M2b).
- Whether the newest firmware adds an encrypted-login wrapper over HTTPS beyond cleartext-JSON-over-TLS.
- Whether the current official desktop client offers WiFi provisioning/QR pairing (replicate in M11 if so — §6.1).
- Per-model split of HTTP-only vs Baichuan-only commands (`reolink_aio` maintains this as a *runtime* list, not a static table — plan to carry a small compatibility map and be ready to update it).
- Precise floating-toolbar icon set/order and grid-preset persistence in the *current* official build (reverse-engineer against a live install).

---

## 11. Post-1.0 / v2 candidates

Deliberately excluded from v1; listed so the cuts are visible decisions, not omissions:

- **Cloud Library.** Browsing/downloading/deleting Reolink Cloud recordings requires a **full Reolink-account subsystem**: cloud login with 2FA, the cloud REST API surface, a separate cloud-token store and refresh lifecycle, subscription/entitlement awareness, and cloud-specific download plumbing — none of which belongs in a client whose stated v1 guarantee is "no cloud account required" (§1.1). If demand justifies it, it lands post-1.0 as an optional, clearly-separated account feature with its own milestone.
- **Home Hub.** Blocked on the **recording-encryption key flow**: Home Hub recordings are encrypted, and playback requires the key-exchange/decryption path the official client implements via its video-decryption tool. Deferring the Hub entirely (device model, chrome, playback) is cleaner than shipping a device that can stream but not play back.
- **In-process P2P media** beyond whatever M2(a)/M12 proves out; keyframe-decoded timeline thumbnails (v1 uses `Snap` — §6.3); desktop WiFi provisioning if the official client turns out not to have it either.

---

### Appendix A — One-line stack summary

**C++20 · Qt 6 (QML grid/PTZ/timeline/events + Widgets settings, LGPLv3 dynamic) · FFmpeg LGPL-only (VAAPI/NVDEC + software fallback) · zero-copy DMA-BUF/EGL (Intel/AMD) + CUDA-GL interop (NVIDIA) + universal NV12-upload fallback · PipeWire audio + Baichuan talk (ONVIF backchannel secondary) · libcurl/nlohmann-json HTTP-CGI · OpenSSL Baichuan AES-CFB · pugixml ONVIF · SQLite + libsecret · CMake · Flatpak/AppImage/deb.** Protocol ported from MIT `reolink_aio`, never AGPL `neolink`; battery-cam video via a bundled **go2rtc (MIT)** bridge process; Cloud Library and Home Hub deferred post-1.0.
