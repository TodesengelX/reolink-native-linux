# Research Dossier: Open-source reference projects for building a Linux NVR/client that talks to Reolink devices

> Produced by the design workflow on 2026-07-09. Facts marked for verification were adversarially checked; see [fact-check.md](fact-check.md).

## Summary

There is no official Reolink Linux client and essentially no serious open-source Qt/GTK desktop GUI client for Reolink specifically; Linux users rely on generic NVR platforms plus a small cluster of protocol libraries. The two canonical protocol references are reolink_aio (Python, MIT, Reolink-authorized — implements the HTTP/CGI JSON API plus the Baichuan TCP push protocol) and neolink (Rust, AGPLv3 — the reverse-engineered reference for the proprietary Baichuan "port 9000" protocol used by battery/wifi cams that lack RTSP/ONVIF). For streaming plumbing, go2rtc (MIT) and MediaMTX (MIT) are permissively licensed and have Reolink-specific handling including a native HTTP-FLV producer for H265 from newer cameras. Full NVR platforms (Frigate MIT, ZoneMinder GPLv2, Shinobi CE GPLv3/AGPLv3, motionEye GPLv3) show end-to-end architectures but only Frigate is permissively licensed. The critical license split: reolink_aio, go2rtc, MediaMTX, Frigate, and Home Assistant core are permissive (copyable); neolink and the traditional NVRs are GPL/AGPL (study only). Hard-won lessons cluster around H265 breaking RTSP/WebRTC, unusable RTSP implementations on some models, 1-hour token expiry, 31-char password limits, NVR channel addressing, and 30s battery-cam idle disconnects.

## Findings

### reolink_aio (Python) — the primary API reference, MIT licensed

github.com/starkillerOG/reolink_aio, PyPI 'reolink-aio' (~v0.19+). Async Python (>=3.11) library, MIT license, and notably officially authorized by Reolink. This is the single most valuable permissive reference. Implements the HTTP/CGI JSON API (Login, GetEnc, Snap, PTZ, spotlight/siren/IR, GetAiState, recordings/playback) AND the Baichuan TCP protocol for real-time 'TCP push' events with callback registration. Supports NVRs with channel-indexed operations (e.g. ir_enabled(channel=0)), battery cameras, ONVIF SWN webhook event subscription (with a renewal/keep-alive timer). Its Baichuan implementation is explicitly derived from neolink's reverse-engineering work. This is the Home Assistant integration's underlying library. Because it is MIT, its code and protocol handling can be studied and reused directly. A TypeScript port exists: verheesj/reolink-aio-ts (Baichuan API).

### Home Assistant 'reolink' integration — Apache-2.0, feature map for what's achievable

home-assistant.io/integrations/reolink, source in home-assistant/core (Apache-2.0, permissive). Built on top of reolink_aio. Demonstrates the full practical feature set: live view (via stream/go2rtc), PTZ move/continuous/stop and presets, playback of up to ~1 month of local recordings through the media browser, person/vehicle/pet AI binary sensors, two-way audio (TalkAbility via Baichuan), spotlight/siren, and battery cameras (which require a Home Assistant Home Hub or NVR to relay). Reolink formally joined 'Works with Home Assistant' (2025). Good source of documented quirks via its issue tracker (token reuse/expiry bugs, PTZ preset entity regressions).

### neolink (Rust) — THE Baichuan protocol reference, but AGPLv3 (study, don't copy)

Original github.com/thirtythreeforty/neolink; the actively maintained fork is github.com/QuantumEntangledAndy/neolink. Rust, binds to GStreamer to expose an RTSP server. License is AGPLv3 — copyleft/network-copyleft, so you can LEARN the protocol from it but cannot copy code into a permissively-licensed product. It is the definitive reverse-engineered implementation of the proprietary 'Baichuan' protocol (a.k.a. 'port 9000') used by cameras like the B800/battery/wifi cams that do NOT implement ONVIF or RTSP. Demonstrates: PTZ + presets, two-way talk (mic or ADPCM files), motion detection with MQTT publishing, battery level reporting, LED/IR control, floodlight control, reboot. Ships a Wireshark Lua dissector (dissector/baichuan.lua) for the protocol. Key lessons: modern Baichuan uses obfuscated XML commands wrapping ordinary H.265/H.264 video in a custom header; connection can be local, remote, UID-mapping, or Reolink relay; battery cams idle-disconnect after ~30s and need selective feature disabling to conserve power. C++ port exists (weltmeyer/neolink_cpplib) and a C#/.NET port (borexola/neolink.net, 'no GStreamer, no transcoding').

### go2rtc (Go) — MIT streaming Swiss-army-knife with native Reolink handling

github.com/AlexxIT/go2rtc, MIT (permissive, copyable). Go backend, web UI. Central to modern Reolink live-view because it bridges camera streams to WebRTC/MSE/RTSP/HLS. Has Reolink-specific sources: ONVIF, and critically a native HTTP-FLV producer that as of v1.9.13 supports H265 from newer Reolink cameras (avoiding the broken RTSP path). Documented hard-won lessons: several Reolink models have an 'awful, unusable' RTSP implementation; H265 breaks WebRTC (browsers can't do H265 over WebRTC) so H265 falls back to MSE; H264 substreams are far more reliable for WebRTC than H265 main streams; HTTP-FLV+H265 can be less stable than RTSP on some models; a common pattern is HTTP-FLV for video stability plus a secondary RTSP stream used only for two-way audio.

### MediaMTX / rtsp-simple-server (Go) — MIT media server/proxy

github.com/bluenviron/mediamtx (formerly aler9/rtsp-simple-server), MIT (permissive). Zero-dependency Go media server supporting RTSP/RTMP/WebRTC/SRT/LL-HLS/MPEG-TS and MoQ, with publish/read/proxy/record/playback. Not Reolink-specific but the standard building block for re-streaming a camera to many clients and for recording. Good reference for RTSP/WebRTC server internals and for a recording+playback backend. Permissive means code is reusable.

### Frigate (Python + TypeScript) — MIT, full local NVR architecture reference

github.com/blakeblackshear/frigate. License is MIT (the source is MIT; only the 'Frigate' name/logo are trademarked — NOT AGPL as sometimes assumed). Backend Python (~45%), frontend TypeScript (~53%), uses OpenCV + TensorFlow for realtime local object detection. Embeds/uses go2rtc for restreaming and provides WebRTC & MSE low-latency live view plus RTSP re-streaming to cut camera connection count. Excellent permissively-licensed reference for a full architecture: detect stream (low-res substream) vs record stream (high-res main), connection reduction via restream, and event-driven recording. Reolink lessons from its discussions: prefer HTTP video streams over RTSP for reliability; feed the H264 substream to the detector, main stream to record.

### ZoneMinder / motionEye / Shinobi — mature NVR platforms, all GPL-family (study only)

ZoneMinder (github.com/ZoneMinder/zoneminder): C++/Perl/PHP, GPLv2 — oldest Linux NVR, works with Reolink via RTSP/ONVIF. motionEye (github.com/motioneye-project/motioneye): Python web frontend over the 'motion' daemon, GPLv3 — lightweight motion-detection recorder. Shinobi: Node.js + ffmpeg; the Community Edition (moeiscool/Shinobi, gitlab Shinobi-Systems/ShinobiCE) is GPLv3/AGPLv3, while 'Shinobi Pro' is proprietary/commercial. All three are copyleft, so they are useful as architecture/feature references (multi-camera management, recording, motion zones, timeline playback) but their code cannot be copied into a permissive project. None implements Baichuan; they consume RTSP/ONVIF.

### Blue Iris & Reolink's own client — proprietary, Windows/Mac only (no Linux)

Blue Iris is a proprietary, Windows-only paid NVR — often paired with neolink to ingest battery/wifi Reolink cams that lack RTSP. Reolink's own official Client and app are proprietary and ship only for Windows and macOS (plus a browser-based login/control UI); there is no official Linux client, and no known official or serious community Qt/GTK Linux desktop GUI client dedicated to Reolink. Linux desktop viewing today = VLC/mpv on an RTSP/HTTP-FLV URL, or a full NVR platform, or neolink-as-bridge. This confirms a genuine gap for a native Linux GUI client.

### VLC / mpv / libVLC / ffmpeg as decoders — mixed licenses, pick libVLC (LGPL) for linking

For actually decoding/rendering Reolink H264/H265 streams in a GUI: libVLC is LGPLv2.1 (can be dynamically linked from a permissive/closed app — best choice for embedding a player widget, has Qt bindings). The VLC application itself and mpv are GPLv2+ (mpv can be built in an LGPL configuration). ffmpeg/libav is LGPLv2.1+ core with some GPL components depending on build flags. Practical decoder lesson echoed across projects: H265 (HEVC) is where things break — many browser/WebRTC paths can't handle it, and some hardware/software decoders need explicit HEVC support; a GUI client should plan for H265 main + H264 sub and possibly transcode.

### Reolink HTTP/CGI API quirks — token expiry, password limits, channel addressing

Hard-won lessons for the HTTP JSON API (used by fwestenberg/reolink and reolink_aio): token from Login expires after 3600s (1 hour) and must be refreshed — a well-known bug class is reusing an expired token and getting 'please login first' / http-login errors (see HA core issue #173535). Commands must be passed BOTH as a URL query param (?cmd=X&token=Y) and inside the JSON body, and the body is always an array of objects (response likewise). Baichuan/HTTP passwords are limited to 31 characters or fewer — longer passwords fail on both transports. NVR operations are channel-indexed (channel 0..N), distinct from single-camera hosts. GetEnc reveals per-channel/substream encoder config (resolution/codec/bitrate). The predecessor library fwestenberg/reolink is what reolink_aio grew out of.

## Open Questions

- Does any abandoned/niche Qt or GTK Reolink desktop client exist on GitHub/GitLab that didn't surface in search (e.g. hobby projects, non-English repos)? Worth a deeper GitHub topic/code search before concluding the gap is total.
- What exactly does reolink_aio's Baichuan implementation cover vs. neolink (e.g. does it support battery-cam wake, two-way talk, or only event push)? Needs a code-level read of the baichuan submodule.
- For a native Linux GUI, is the best decode path libVLC (LGPL, embeddable) vs. GStreamer (LGPL, what neolink uses) vs. direct ffmpeg — and which handles Reolink H265 + the custom Baichuan framing most cleanly?
- How do battery/wifi-only Reolink cameras (Argus/Go series) behave — do they require Baichuan exclusively, or is there any RTSP/ONVIF path at all? Affects whether a client must implement Baichuan.
- What is the current license of Frigate at HEAD — MIT confirmed via repo fetch, but reconcile with the widespread 'AGPL' claim to be certain which applies to reusable code.

## Sources

- https://github.com/starkillerOG/reolink_aio
- https://pypi.org/project/reolink-aio/
- https://github.com/verheesj/reolink-aio-ts
- https://github.com/fwestenberg/reolink
- https://github.com/thirtythreeforty/neolink
- https://github.com/QuantumEntangledAndy/neolink
- https://github.com/thirtythreeforty/neolink/blob/master/README.md
- https://www.thirtythreeforty.net/posts/2020/05/hacking-reolink-cameras-for-fun-and-profit/
- https://github.com/borexola/neolink.net
- https://github.com/weltmeyer/neolink_cpplib
- https://www.home-assistant.io/integrations/reolink
- https://www.home-assistant.io/blog/2025/04/17/reolink-joins-works-with-home-assistant/
- https://github.com/home-assistant/core/issues/173535
- https://github.com/AlexxIT/go2rtc
- https://github.com/AlexxIT/go2rtc/issues/1938
- https://github.com/AlexxIT/go2rtc/issues/1711
- https://github.com/blakeblackshear/frigate
- https://docs.frigate.video/guides/configuring_go2rtc/
- https://docs.frigate.video/configuration/camera_specific/
- https://github.com/bluenviron/mediamtx
- https://github.com/moeiscool/Shinobi
- https://gitlab.com/Shinobi-Systems/ShinobiCE
- https://github.com/ZoneMinder/zoneminder
- https://github.com/motioneye-project/motioneye
- https://community.reolink.com/topic/737/linux-client-support
- https://reolink.com/software-and-manual/
- https://community.home-assistant.io/t/solved-reolink-h-265-h-264-transcoding-in-go2rtc-complete-working-guide/966420
