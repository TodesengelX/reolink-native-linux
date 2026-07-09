> **Design-review record.** This critique was produced against the v1.0 draft of DESIGN.md. Every item below was resolved and integrated into the final [DESIGN.md](../DESIGN.md) — kept here as the review history.

## (a) Missing screens / features vs. the official app

- **Cloud Library is scoped in but its auth is undesigned and contradicts a non-goal.** §6.3/6.4 list Cloud Library download/delete, but §1.3 says "no cloud subscription management" and §1.1 says no Reolink account needed. Cloud Library *requires* a Reolink account login + cloud REST API — none of which appears in the protocol layer (§4), data model (§7), or roadmap. Either cut Cloud Library or add a full "Reolink Account / cloud session" subsystem (login, 2FA, cloud REST endpoints, token store).
- **No Events / Notification Center screen.** `GetEvents` and Baichuan push are ingested, but there is no UI inbox for the event history with AI-type thumbnails — a first-class screen in the official client/app. Add it.
- **Home Hub video-decryption tool is listed in chrome (§6) but never designed.** Home Hub recordings are encrypted; add the decryption key flow and a playback path, or explicitly defer Home Hub.
- **Doorbell flows are absent.** Visitor call/answer UI, chime/quick-reply, doorbell press events. Add a doorbell interaction surface (ties into two-way talk).
- **No manual siren/spotlight trigger in Live View**, despite `SetAudioAlarm`/`GetWhiteLed` being listed. Add live-view momentary siren + floodlight toggle controls.
- **No battery/solar dashboard.** `GetBatteryInfo` is listed but there is no UI for battery %, charging, or low-power state — core to Argus/Go models being targeted in M8.
- **No camera network-setup / WiFi-provisioning wizard.** Add-device (§6.1) only covers already-networked devices; onboarding a fresh WiFi camera (and QR-code pairing) is missing.
- **No multi-monitor / pop-out pane / detached window**, a standard desktop-NVR feature.
- **Fisheye / 360 dewarp missing.** "Image Stitching (dual-lens)" is mentioned but panoramic/fisheye dewarp for 360 models is not.
- **No NVR camera-management UI** (bind/unbind a camera to an NVR channel, per-channel add), only channel *enumeration*.
- **Playback digital zoom, timeline hover-thumbnail preview, and time-lapse** are not listed.
- **Sub-user/restricted-account UX is thin** — admin vs. user mentioned, but no UI for how gated controls behave under a restricted login.

## (b) Technical gaps / hand-waving

- **Recording all 16 main streams while displaying 16 sub streams doubles the session count to ~32 concurrent streams (§5.5 vs §5.7).** Reolink cameras and especially a single NVR have hard limits on concurrent RTSP/FLV sessions and uplink bandwidth. This is the biggest unaddressed feasibility gap — "zero decode cost" ignores network + demux + camera session caps. Specify per-device concurrent-stream budgets and validate against real NVR uplink.
- **P2P/UID is drastically underspecified for its difficulty.** "Reverse-engineered packet formats" + relay + hole-punching is compressed into part of M8. `reolink_aio` does *not* provide a complete, ready-to-port P2P video transport. Call out that battery-cam-over-P2P likely still needs the bridge, and scope P2P as its own multi-milestone effort.
- **`reolink_aio` coverage is overstated.** §2.3 claims it "already implements the entire protocol surface." Its Baichuan/battery/P2P-video support is partial; the doc itself later admits battery video isn't covered. Reconcile the claim.
- **Two-way audio assumes ONVIF Profile-T backchannel, which many Reolink models don't expose.** The official app uses the Baichuan talk path for most cameras. Make Baichuan talk the primary path (not just the battery-cam fallback), with ONVIF as secondary.
- **Cross-thread GL/EGL sharing is glossed.** 16 decode threads each importing VAAPI surfaces as DMA-BUF→EGLImage textures, consumed by "a single shared GL context," requires shared-context setup and surface lifetime/fence synchronization across threads. Specify the context-sharing and fencing model.
- **NVIDIA Wayland zero-copy is hand-waved.** "EGLStream vs GBM nuances" hides a genuinely hard, driver-version-dependent problem (and PRIME/hybrid-GPU laptops aren't mentioned at all). Add a hybrid-GPU/offload plan.
- **HEVC remux details are thin.** Annex-B→HVCC, `hvc1` vs `hev1`, and in-band-only parameter sets (no `extradata` until first IDR) are real failure modes for stream-copy MP4. Specify parameter-set extraction/insertion.
- **Timeline color-coding data source is unspecified.** `Search` returns file lists; mapping grey/blue/black + per-type (Person/Vehicle/Pet/Visitor) segments requires event/status data that likely comes from Baichuan/`GetEvents` — define the exact source and how it aligns to segments.
- **No stream reconnect/backoff strategy** for loss, credential rotation, or camera reboot.
- **Cleartext password in HTTP-FLV/RTMP URLs** (§4.3) undercuts the "require HTTPS" stance for the video path; note the residual exposure.
- **<500 ms glass-to-glass over RTSP/TCP is optimistic;** TCP + reorder buffering typically exceeds this. State that the FLV low-latency path is likely required to hit the target, or relax the number.

## (c) Underestimated risks

- **AGPL bridge in a shipped Flatpak is riskier than "mere aggregation" implies.** Bundling `neolink` (AGPLv3) in the same Flatpak/installer, auto-launched by the app, weakens the aggregation defense and triggers AGPL's network-use source-offer obligation for that binary. Prefer **go2rtc (MIT)** where it can cover battery video, and if `neolink` is used, ship it as a clearly separate, user-installed component with source offer — get counsel sign-off.
- **HEVC patent royalties** (flagged, good) — but note the app *distributes* an HEVC decoder path; the "distros already ship VAAPI HEVC" argument doesn't cover the software-fallback decoder you bundle. Keep for counsel.
- **Hardware-decode portability matrix is larger than acknowledged:** old `iHD`/`i965` split, Mesa version floors for DMA-BUF modifiers, NVIDIA Wayland, and hybrid-GPU laptops. The 3-vendor × 2-display-server matrix is really 3×2×(driver-age) — expand the test matrix and CI hardware plan.
- **Adversarial-vendor / ToS risk unmentioned.** Reolink can (and does) change P2P/Baichuan formats and could treat an unofficial client as hostile. Add a protocol-drift monitoring/maintenance commitment and a ToS-exposure note.
- **Resourcing.** A 46-week single-track roadmap to clone a mature commercial client *plus* reverse-engineer 4 protocols is optimistic for anything short of a multi-engineer team. State team size/assumptions.

## (d) Claims that look unverified or wrong

- **"The official Reolink client is itself widely believed to be Qt"** — speculative, and used as a risk-lowering justification. Verify or remove; it shouldn't be load-bearing.
- **"`reolink_aio`… already implements the entire protocol surface"** — overstated (see above).
- **Battery video bridge described as "optional, clearly-bounded" (§1.4) but is actually required** for the M8 battery-cam feature — it's not optional if battery cams are in scope. Fix the framing.
- **Non-goal "no Reolink cloud account required" vs. Cloud Library feature** — internally contradictory (see a).
- **Audio "recorded audio is AAC (not user-changeable on most models)"** — several Reolink models record PCM/G.711; verify per-model before hard-coding AAC-only demux assumptions.
- **"NVDEC consumer GeForce has no concurrent decode session cap"** — broadly true (the cap is on *encode*), but verify against current driver EULA/behavior for your min-spec GPUs rather than asserting it.
- **31-char password / dynamic leaseTime / Logout-without-token quirks** — plausibly from `reolink_aio`, but marked as facts; label them as behaviors to *validate against firmware*, since they gate authentication.

## (e) Roadmap sequencing problems

- **Continuous local recording (record-all-16-main, §5.5) has no milestone.** It's a headline capability with the biggest feasibility risk (session doubling) yet appears nowhere in M0–M9. Add an explicit recording milestone *before* M9, and front-load the concurrent-session validation.
- **M5 alarm-type timeline filters depend on event data delivered in M7.** Playback color-coding + Person/Vehicle/Pet/Visitor filters need Baichuan/`GetEvents`, but the event subsystem lands two milestones later. Pull minimal event ingestion earlier or descope M5's alarm filters until M7.
- **M3 Live View shows a two-way-talk button and Balanced gating before the backends exist** (talkback = M7, full `GetAbility` parsing = M6). Sequence the UI control with a stub/disabled state or move the backend earlier.
- **UID/P2P and battery cams (M8) are the highest-RE-risk items but are left near the end.** A schedule-risk failure here is discovered too late. Spike P2P and the battery bridge in an early feasibility milestone.
- **Localization/i18n is deferred to M9, but string externalization must begin at M0** or it becomes a massive retrofit. Move i18n scaffolding to M0.
- **No dedicated QA / real-hardware-matrix / security-hardening milestone;** "recording remux hardening" is crammed into packaging (M9). Add a hardening/soak milestone before 1.0.
- **Cloud account/library work (if kept) has no milestone at all** — add one or cut the feature.