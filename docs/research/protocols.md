# Research Dossier: Reolink NVR/camera network protocols and APIs for building a native Linux client

> Produced by the design workflow on 2026-07-09. Facts marked for verification were adversarially checked; see [fact-check.md](fact-check.md).

## Summary

Reolink devices expose four independent network interfaces that a Linux client can drive: (1) an HTTP/HTTPS JSON "CGI" API at /cgi-bin/api.cgi that carries nearly all configuration, control, snapshot, search and playback commands; (2) RTSP on port 554 for H.264/H.265 live main/sub streams with AAC/G.711 audio; (3) the proprietary "Baichuan" binary protocol on TCP/UDP port 9000 (XOR-obfuscated XML + AES-CFB, header magic f0debc0a) used for events, battery cameras and models lacking RTSP/ONVIF; and (4) ONVIF Profile S/T on port 8000 for discovery, streaming profiles and events. The definitive open-source references are reolink_aio (MIT, Python, semi-official) and Home Assistant's Reolink integration for the HTTP+Baichuan API, and neolink / QuantumEntangledAndy fork (AGPLv3, Rust) plus camera_proxy for the reverse-engineered Baichuan/P2P protocol. Remote access uses a UDP P2P scheme via p2p.reolink.com with UID registration and optional relay; it has been reverse-engineered (neolink, Nozomi Networks, Kaspars Dambis blog). The HTTP API is fully documented in Reolink's own "Camera API User Guide v8" PDF, and all commands, request/response JSON shapes and stream URL formats can be read directly from reolink_aio's source.

## Findings

### HTTP CGI API base URL and transport

Base URL is http://{host}:{port}/cgi-bin/api.cgi (or https://). Confirmed in reolink_aio api.py: `f"https://{host}:{port}/cgi-bin/api.cgi"` / `f"http://..."`. Default port 80 (or 443 https). Requests are HTTP POST with a JSON body that is ALWAYS an array of objects, even for a single command. Each element: {"cmd":"<Name>","action":0|1,"param":{...}}. action=0 typically returns current values, action=1 returns the value plus its range/capabilities. The command name is ALSO passed as a URL query parameter (?cmd=<Name>) in addition to being in the body. The response is always a JSON array of {"cmd":...,"code":0,"value":{...}} (code 0 = success; non-zero + "error":{"rspCode":...} on failure). Multiple commands can be batched in one POST array. Snap and image responses come back as image/jpeg; playback/download as application/octet-stream.

### Login and token flow

Login is POST /cgi-bin/api.cgi?cmd=Login&token=null with body: [{"cmd":"Login","action":0,"param":{"User":{"userName":"<user>","password":"<pass>"}}}]. Response value: {"Token":{"name":"<token-string>","leaseTime":3600}}. leaseTime is seconds (typically 3600 = 1 hour). The returned token name is then sent as the `token` URL query parameter on every subsequent request (?cmd=X&token=<token>). Before login token is the literal string "null". reolink_aio refreshes the token ~5 min before lease expiry. Logout: [{"cmd":"Logout","action":0,"param":{}}] with token. NOTE a documented firmware bug: issuing Logout without a token can break subsequent logins on some models. There is also a shorter "per-request" auth mode where user/password are appended to the URL instead of using a token. Password is sent in cleartext JSON, so HTTPS is recommended; newer firmware ships with RTSP/RTMP/ONVIF disabled by default and may require HTTPS.

### Full command inventory (from reolink_aio api.py)

Get*: GetAbility (per-user capabilities/permissions), GetDevInfo, GetChannelstatus (NVR per-channel online/model/name), GetChnTypeInfo, GetLocalLink (MAC/IP), GetNetPort (ports), GetEnc (stream/codec config), GetRtspUrl (returns exact RTSP URLs), GetIsp, GetImage, GetOsd, GetMask, GetTime, GetNtp, GetUser, GetEmail, GetFtp, GetPush/GetPushCfg, GetWebHook, GetRec/GetManualRec, GetHddInfo, GetPerformance, GetIrLights, GetPowerLed, GetWhiteLed (floodlight), GetStateLight, GetPtzPreset, GetPtzPatrol, GetPtzGuard, GetPtzCurPos, GetPtzTraceSection, GetZoomFocus, GetAutoFocus, GetAiState (AI object detection state), GetAiCfg, GetAiAlarm, GetMdState (motion bool), GetMdAlarm, GetAlarm, GetAudioAlarm, GetAudioFileList, GetDeviceAudioCfg, GetAutoReply, GetBatteryInfo, GetPirInfo, GetWifiSignal, GetEvents, GetDingDongList/GetPirInfo (doorbell/chime), GetWhiteLed. Set* mirror the getters: SetEnc, SetIsp, SetImage, SetOsd, SetMask, SetTime, SetNtp, SetIrLights, SetPowerLed, SetWhiteLed, SetStateLight, SetPtzGuard, SetPtzTraceSection, SetAutoFocus, SetRec, SetManualRec, SetAlarm, SetMdAlarm, SetAiAlarm, SetAiCfg, SetAudioAlarm, SetAudioCfg, SetDeviceAudioCfg, SetAutoReply, SetEmail, SetFtp, SetPush, SetWebHook, SetNetPort, SetPirInfo, SetDingDongCfg. Action: PtzCtrl, PtzCheck, StartZoomFocus, Snap, Search, Download, NvrDownload, Reboot, QuickReplyPlay, DingDongOpt, TestWebHook, and firmware: CheckFirmware, Upgrade/UpgradePrepare/UpgradeStatus/UpgradeOnline.

### Channel addressing on NVRs and multi-lens cameras

Channels are addressed by a zero-based integer `channel` inside each command's param, e.g. [{"cmd":"GetEnc","action":0,"param":{"channel":0}}]. A single camera is channel 0; an NVR exposes channels 0..N-1. GetChannelstatus enumerates channels (online status, typeInfo/model, name). GetAbility with User can be queried per host. In RTSP/RTMP URLs the channel is 1-based and zero-padded to 2 digits: internal channel index i maps to `f"{i+1:02d}"` (index 0 -> "01", index 1 -> "02").

### RTSP URL formats (port 554)

Default RTSP port 554. reolink_aio builds: rtsp://{user}:{pass}@{host}:554/{encoding}Preview_{CC}_{stream} where encoding is `h264` or `h265`, CC is 2-digit 1-based channel, stream is `main` or `sub` (also `autotrack` on some PTZ). Examples: rtsp://admin:pass@host:554/h264Preview_01_main, rtsp://admin:pass@host:554/h264Preview_01_sub, rtsp://admin:pass@host:554/h265Preview_01_main (4K/8MP main). NVR channel 2 sub: rtsp://admin:pass@host:554/h264Preview_02_sub. A legacy/codec-agnostic form also exists: rtsp://.../Preview_01_main. Best practice is to call the HTTP command GetRtspUrl (param channel) which returns the device's own exact main/sub URLs (rtsp apiVersion >=3). Password is URL-encoded in the userinfo.

### RTMP and HTTP-FLV streaming

Default RTMP port 1935. Live RTMP: rtmp://{host}:1935/bcs/channel{ch}_{stream}.bcs?channel={ch}&stream={streamType}&token={token} (streamType 0=main,1=sub). HTTP-FLV live (served on the HTTP port): {http|https}://{host}:{port}/flv?port={rtmp_port}&app=bcs&stream=channel{ch}_{stream}.bcs&user={user}&password={pass}. Playback via RTMP-VOD: rtmp://{host}:1935/vod/{filename}?channel={ch}&stream={streamType}. Playback via HTTP-FLV: {http|https}://{host}:{port}/flv?port={rtmp_port}&app=bcs&stream=playback.bcs&channel={ch}&type={streamType}&start={filename}&seek=0. FLV/RTMP are lower-latency alternatives to RTSP and are what the mobile app/web UI use.

### Snapshot, recording search, and playback/download

Snapshot: HTTP GET/POST ?cmd=Snap&channel={ch}&token={token} (extra param snapType/rs), returns image/jpeg bytes directly. Search recordings: cmd=Search with param {"Search":{"channel":ch,"onlyStatus":0,"streamType":"main","StartTime":{Y,M,d,h,m,s},"EndTime":{...}}}; response lists VOD files with name, size, StartTime/EndTime, plus a per-day status calendar. Download a found file: cmd=Download (camera) or cmd=NvrDownload (NVR) with the file name/source, returned as application/octet-stream. Playback also available via the FLV/RTMP VOD URLs above.

### PTZ control

cmd=PtzCtrl with param {"channel":ch,"op":"<Left|Right|Up|Down|LeftUp|.../ZoomInc|ZoomDec|FocusInc|FocusDec|ToPos|Stop|Auto>","speed":1-64,"id":<presetId for ToPos>}. Preset management via GetPtzPreset/SetPtzGuard/GetPtzPatrol. Absolute position via GetPtzCurPos; StartZoomFocus for zoom/focus; PtzCheck to calibrate. Reolink notably does NOT implement PTZ over ONVIF, so PTZ must go through this HTTP command (or Baichuan).

### Baichuan proprietary protocol (port 9000) - purpose and role

TCP (mains-powered) / UDP (battery) on default port 9000 (DEFAULT_BC_PORT=9000). It is Reolink's native app/NVR protocol and predates their HTTP API. Roles vs HTTP: (a) real-time push events (motion/AI/visitor/day-night) via a persistent connection - reolink_aio subscribes with cmd_id 31 on ch_id 251; (b) mandatory for cameras that implement NEITHER RTSP nor ONVIF (battery cams B800/D800, Argus, E1, Lumus, some doorbells) - these are "baichuan_only"; (c) a fallback for HTTP commands that a given firmware exposes only over Baichuan; (d) fetching video/snapshots on battery cameras. neolink/neolink.net act as RTSP bridges by speaking Baichuan and re-emitting standard RTSP so normal players can view battery cams.

### Baichuan wire format, obfuscation and AES encryption (reverse-engineered)

Header magic = 0x0abcdef0 (little-endian bytes f0 de bc 0a; HEADER_MAGIC="f0debc0a"). Messages are identified by numeric cmd_id (1=login/get-nonce, 2=logout, 26/78 channel-scoped ISP, 31=subscribe events, 33=motion/AI/visitor event, 234=UDP heartbeat) and carry XML payloads. Two payload obfuscation schemes: (1) legacy XOR - each byte ^= XML_KEY[(offset+idx)%8] ^ offset, with XML_KEY=[0x1F,0x2D,0x3C,0x4B,0x5A,0x69,0x78,0xFF]; (2) modern AES - AES-CFB, segment_size=128, fixed IV=b"0123456789abcdef", key = first 16 chars of md5_str_modern(f"{nonce}-{password}"). md5_str_modern = uppercase hex of MD5, truncated to 31 chars. UDP transport uses a separate cyclic 32-bit-shift XOR key (UDP_KEY) with offset. CRC is CRC-32 poly 0xEDB88320. A Wireshark dissector (dissector/baichuan.lua in neolink) decodes the header and deobfuscated XML for the XOR scheme but cannot decrypt AES messages.

### Baichuan modern login (nonce/AES handshake)

Modern login: client sends a header-only message cmd_id=1, message_class="1465", enc_type=BC to request a nonce; device replies with <nonce> in XML. Client derives aes_key=md5_str_modern(nonce+"-"+password)[0:16] and password hash via md5_str_modern, then sends the login XML AES-encrypted. Subsequent commands are AES-CFB encrypted. A legacy login (no nonce, XOR only) also exists for older firmware. This is the scheme neolink's QuantumEntangledAndy fork implements as AES/FullAes and is required for modern camera firmware.

### ONVIF support (port 8000)

Default ONVIF port 8000. Reolink wired cameras/NVRs conform to Profile S (H.264 streaming, audio, basic PTZ) and Profile T (H.265, analytics). Used by third-party VMS (Frigate, Blue Iris) for device discovery, GetProfiles/stream URIs, and event subscription (PullPoint/base subscription for motion). Important limitations: Reolink does NOT support PTZ over ONVIF (use HTTP PtzCtrl instead); modern firmware ships with ONVIF, RTSP and RTMP DISABLED by default and they must be enabled in the camera web UI. reolink_aio uses ONVIF push (WS-BaseNotification/SubscriptionManager) for motion/AI events as an alternative to Baichuan TCP push.

### Cloud / UID P2P remote access

Each device has a UID; remote access uses a UDP P2P scheme. Client queries a dispatcher (p2p.reolink.com family) which returns register/log/relay server IPs; NAT-traversal packets carry the peer IP inside an obfuscated payload so NAT rewrites don't break it, establishing a direct client<->camera UDP path, with a Reolink relay server as fallback (effectively MITM-capable). reolink_aio's UDP protocol uses local connect port 2015 for LAN discovery. The full P2P/UID protocol HAS been reverse-engineered: neolink (QuantumEntangledAndy fork) connects to battery cams by UID over the internet using this, the camera_proxy project and Kaspars Dambis's blog document the Argus P2P packets, and Nozomi Networks published a security analysis of the Reolink P2P protocol. So remote UID access is reproducible in a native client, though it depends on Reolink's relay infrastructure.

### Codecs, audio and resolutions

Video: H.264 and H.265 (HEVC). Reolink outputs H.265 only in the highest resolution (4K / 8MP+ main stream); dropping resolution to 2K/lower switches the main stream to H.264. Sub stream is typically H.264 (lower res, e.g. 640x480/896x512). Per-stream codec/resolution/bitrate/framerate is read/set via GetEnc/SetEnc (mainStream/subStream, vType field = h264/h265). Audio: RTSP main stream carries AAC (fixed, cannot be changed on most models); two-way/RTSP talkback audio uses G.711 (G711); PCM referenced on some models. Snapshot is JPEG.

### Reference libraries and licensing

reolink_aio (github.com/starkillerOG/reolink_aio) - Python asyncio, MIT license, semi-official (built with Reolink support); powers the Home Assistant Reolink integration; implements BOTH the HTTP CGI API (reolink_aio/api.py) and the Baichuan protocol (reolink_aio/baichuan/) - this is the single best implementation reference. reolinkapipy / reolink-api (ReolinkCameraAPI) - Python, HTTP API only. neolink (github.com/thirtythreeforty/neolink, now maintained at github.com/QuantumEntangledAndy/neolink) - Rust RTSP bridge, AGPLv3, the authoritative Baichuan reverse-engineering (includes a Wireshark dissector); AGPL means network use requires source disclosure. neolink.net (borexola) - C#/.NET reimplementation. camera_proxy - Baichuan/P2P dissector. Reolink's own "Reolink Camera API User Guide v8" (April 2023) PDF, distributed via the Reolink community forum, documents the HTTP JSON commands officially. Home Assistant Reolink integration (Apache-2.0) is another reference consumer of reolink_aio.

## Open Questions

- Exact full XML schema of each Baichuan cmd_id (message classes) - reolink_aio implements many but a complete cmd_id catalog would need reading baichuan.py + neolink's protocol docs in full
- Precise P2P/UID handshake packet layout and the current set of Reolink dispatcher/relay server hostnames and ports (subject to change; needs live capture or camera_proxy source)
- Whether the newest firmware enforces HTTPS-only or an additional encrypted-login wrapper on the HTTP CGI API beyond cleartext-JSON-over-TLS
- Two-way audio (talkback) send path details over Baichuan/RTSP for a native client
- Per-model differences in which commands are HTTP-only vs Baichuan-only (reolink_aio maintains runtime baichuan_cmds/broken_cmds lists rather than a static table)

## Sources

- https://github.com/starkillerOG/reolink_aio (api.py, baichuan/baichuan.py, baichuan/util.py, baichuan/udp_protocol.py) - MIT, primary implementation reference
- https://github.com/thirtythreeforty/neolink and https://github.com/QuantumEntangledAndy/neolink - AGPLv3 Baichuan RE + Wireshark dissector
- https://github.com/thirtythreeforty/neolink/blob/master/README.md
- https://www.thirtythreeforty.net/posts/2020/05/hacking-reolink-cameras-for-fun-and-profit/
- https://community.reolink.com/topic/4196/reolink-camera-api-user-guide_v8-updated-in-april-2023 (official HTTP API PDF)
- https://mosleyit.github.io/reolink_api_wrapper/redoc.html (Reolink API Redoc)
- https://community.reolink.com/topic/1182/rtsp-urls and https://support.reolink.com/articles/900000630706-Introduction-to-RTSP/
- https://support.reolink.com/hc/en-us/articles/900000638523-What-s-the-Format-of-the-RTSP-Video-Audio-that-Reolink-Cameras-Use/
- https://support.reolink.com/hc/en-us/articles/360008718893-Introduction-to-ONVIF-Protocol/
- https://kaspars.net/blog/reolink-battery-camera-remote-protocol (P2P/UID RE)
- https://www.nozominetworks.com/blog/new-reolink-p2p-vulnerabilities-show-iot-security-camera-risks
- https://support.reolink.com/hc/en-us/articles/900000618443-Introduction-to-P2P-or-UID/
- https://github.com/ReolinkCameraAPI/reolinkapipy
- https://gist.github.com/jasonk/4772d1cd5154069cfc9eed07acb2057a (bash API examples)
