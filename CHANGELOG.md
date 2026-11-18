# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the project is pre-1.0, minor versions may still change behaviour.

## [Unreleased]

A large release: twenty-four community pull requests, a full PTZ panel, and
audio. Thanks to everyone who sent code: gsoros, thelabcorner, iibaranov-IG,
atripathy86, aalap, greenfanDev, MathiasDeWeerdt and 815rx1.

### Added

- **PTZ panel.** Open it from a PTZ camera's toolbar (⇅). Everything in it is
  read back from the camera and shown only when the camera supports it.
  - **Pad or joystick.** A round eight-way pad (hold to move) or a joystick
    where a small push moves slowly and a full push moves at the chosen
    maximum. Switch between them in the pad's corner; the choice and the
    speed are remembered.
  - **3D positioning**, like the phone app: click the video to centre that
    spot, drag a box to zoom into it, double-click to zoom in, scroll to zoom.
    Works through an NVR.
  - **Zoom and focus** sliders with the lens's real position, hold-to-move
    buttons, and an **Auto focus** button.
  - **Presets** as picture cards: save the current view with a snapshot, go
    to one, delete one.
  - **Patrol** start/stop; **Home position**: return to it, set it to the
    current view, and choose whether (and after how long) the camera returns
    there when idle; **AI auto-tracking**: on/off, which targets to follow,
    and how soon to return home once the target stops.
  - Keyboard on a maximized PTZ camera: arrows pan and tilt, `+`/`−` zoom,
    `[`/`]` focus, `Esc` leaves 3D mode.
- **Audio.** Live audio with a speaker toggle on each pane ([#20]), and
  recorded audio in playback, over Baichuan too ([#24]). By greenfanDev.
- **Baichuan-only cameras** (no HTTP or RTSP, such as some Lumus and E1
  models) can be discovered, added and watched live over TCP 9000 ([#6]).
  By gsoros, who also made recorded playback on such firmware work.
- **Spotlight and floodlight control**, shown only on cameras that really have
  a light, with brightness in Device Settings → Light ([#10]). By thelabcorner.
- **12-pane live layout** ([#13]), and the chosen layout is remembered
  ([#12]). By atripathy86.
- **Event inbox tidying**: repeated events from one camera merge into one,
  plain motion events can be hidden, and events older than 7 days are
  deleted ([#26]). A setting turns off event pop-ups ([#21]). Events show
  their real time instead of "just now" ([#29]), and opening one plays it in
  your chosen quality or shows its snapshot when nothing was recorded ([#28]).
  By greenfanDev.
- Opening an event zooms the playback timeline to a 5-minute window with the
  event's marker centred; the time ticks follow the zoom level.
- Timeline clicks snap to a nearby recording instead of landing in a gap
  ([#25]). By greenfanDev.
- The sidebar shows each device's IP address ([#19]). By greenfanDev.
- **Add all discovered devices** at once with one set of credentials ([#36]).
  By 815rx1.
- SD/HD is remembered per camera ([#35]). By 815rx1.
- **German translation**; the app follows the system language ([#34]).
  By 815rx1.

### Changed

- **Full screen can be left without a keyboard** ([#37]): touching the screen or
  moving the pointer shows an **Exit full screen** button in the corner for a
  few seconds. Made for wall-mounted touchscreens, where Esc and F11 don't exist.
- The Add Device scan now says when no device answered, and explains that
  battery cameras (the Argus series) can't be added on their own yet; paired
  with a Reolink Home Hub, they can be added through the hub ([#9]).
- **Flatpak: streams decode on every system.** It now bundles its own LGPL
  FFmpeg 7.1.5 (with VA-API) on the KDE 6.11 runtime instead of relying on
  Cisco's separately downloaded openh264, whose absence left every stream
  blank ([#5]). The error now names the decoder that failed (from [#32], by
  815rx1).
- Video stops streaming while the window is hidden in the tray, and resumes
  when it's shown ([#23]). By greenfanDev.
- A live pane's SD stream switches to Baichuan only when the camera's RTSP is
  actually turned off ([#35]), not after any temporary RTSP error.

### Fixed

- **Detection markers were missing from the playback timeline** on most days
  ([#31]). They only came from the app's own inbox, which keeps the last 100
  events and only sees what happens while the app runs. The timeline now reads
  each day's detections from the device itself, as the official apps do, and
  shows each one as a coloured bar for its length: person, vehicle, animal or
  motion (and other types when there are any). Chips under the timeline show
  the day's count of each kind and switch it on or off; the choice is
  remembered.
- In the 4-camera playback view, the per-camera lanes under the timeline were
  cut off. They show again, each with its own camera's detection markers.
- A standalone camera listed above an NVR could have the NVR's header drawn
  over its row ([#7], fixed by [#16] from greenfanDev), and a standalone
  camera could stay classified as an NVR ([#8], by iibaranov-IG).
- Live streams stayed stopped after a reconnect, and a stalled decoder wasn't
  recovered ([#14]). By atripathy86 and aalap.
- The Baichuan connection spun at 100% CPU after a device dropped off
  ([#15]). By greenfanDev.
- Older firmware with 1024-bit TLS keys couldn't be added ([#17]). By
  greenfanDev.
- The Add Device scan list left out some discovered devices ([#18]).
  By greenfanDev.
- Streams now send RTSP TEARDOWN when they stop, and the app quits cleanly
  ([#22]). By greenfanDev.
- The sidebar keeps its intended width when the window is maximized or the
  layout is tight. Camera rows now grow with the system font instead of
  overlapping ([#30], by MathiasDeWeerdt).
- Playback on a standalone camera falls back to the native Baichuan sub
  stream when the camera's HTTPS `/flv` endpoint is unavailable, instead of
  retrying FLV forever on "Connecting…" ([#30], by MathiasDeWeerdt).
- A device that refuses a Baichuan stream request no longer leaves the player
  on "Connecting" forever; the player reports the error and falls back.
- Event cleanup only ever deletes thumbnails inside the app's own thumbnails
  folder, even when the database came from another install.

[#5]: https://github.com/TodesengelX/reolink-native-linux/issues/5
[#6]: https://github.com/TodesengelX/reolink-native-linux/pull/6
[#7]: https://github.com/TodesengelX/reolink-native-linux/issues/7
[#8]: https://github.com/TodesengelX/reolink-native-linux/pull/8
[#9]: https://github.com/TodesengelX/reolink-native-linux/issues/9
[#10]: https://github.com/TodesengelX/reolink-native-linux/pull/10
[#12]: https://github.com/TodesengelX/reolink-native-linux/pull/12
[#13]: https://github.com/TodesengelX/reolink-native-linux/pull/13
[#14]: https://github.com/TodesengelX/reolink-native-linux/pull/14
[#15]: https://github.com/TodesengelX/reolink-native-linux/pull/15
[#16]: https://github.com/TodesengelX/reolink-native-linux/pull/16
[#17]: https://github.com/TodesengelX/reolink-native-linux/pull/17
[#18]: https://github.com/TodesengelX/reolink-native-linux/pull/18
[#19]: https://github.com/TodesengelX/reolink-native-linux/pull/19
[#20]: https://github.com/TodesengelX/reolink-native-linux/pull/20
[#21]: https://github.com/TodesengelX/reolink-native-linux/pull/21
[#22]: https://github.com/TodesengelX/reolink-native-linux/pull/22
[#23]: https://github.com/TodesengelX/reolink-native-linux/pull/23
[#24]: https://github.com/TodesengelX/reolink-native-linux/pull/24
[#25]: https://github.com/TodesengelX/reolink-native-linux/pull/25
[#26]: https://github.com/TodesengelX/reolink-native-linux/pull/26
[#28]: https://github.com/TodesengelX/reolink-native-linux/pull/28
[#29]: https://github.com/TodesengelX/reolink-native-linux/pull/29
[#30]: https://github.com/TodesengelX/reolink-native-linux/pull/30
[#31]: https://github.com/TodesengelX/reolink-native-linux/issues/31
[#32]: https://github.com/TodesengelX/reolink-native-linux/pull/32
[#34]: https://github.com/TodesengelX/reolink-native-linux/pull/34
[#35]: https://github.com/TodesengelX/reolink-native-linux/pull/35
[#36]: https://github.com/TodesengelX/reolink-native-linux/pull/36
[#37]: https://github.com/TodesengelX/reolink-native-linux/issues/37

## [0.1.8] — 2026-08-12

### Fixed

- **Playback failed with a black screen that retried forever** on systems with
  FFmpeg 8 or newer. Reolink devices serve HTTPS with a self-signed
  certificate, which the app has always accepted; FFmpeg 8.0 changed its
  default to reject it. Live view was unaffected because it uses RTSP. This
  only ever hit builds from source (Arch and other current distros) — the
  AppImage and Flatpak bundle an older FFmpeg — but it would have reached
  those too as their base images move forward.
- **Playback stopped dead at every event marker.** The NVR streams one
  recording file per connection and stores motion events as separate files, so
  continuous footage ended exactly where an event began. Playback now
  continues across recording boundaries automatically.
- **Removing a device and adding it back left its header stuck** on the first
  camera's name and "connecting…" forever, and a re-add could briefly show
  "undefined" in place of the camera count.

### Changed

- Clicking an event now starts playback **5 seconds before** the moment, so the
  approach is visible rather than only the aftermath.

### Internal

- Continuous integration: every push and pull request now builds and runs the
  test suite, including a second build against the newest Qt and FFmpeg so
  upstream changes surface before they reach anyone building from source.
- New regression test covering the TLS behaviour above.

## [0.1.7] — 2026-08-05

### Added

- Per-camera view rotation: right-click a camera in the sidebar →
  **Rotate view 90°** turns its picture in 90° steps, persisted per camera
  and applied in live view and both playback modes — for devices whose
  firmware misreports orientation. ([#3])

### Fixed

- HD live view failed ("Could not find codec parameters") on cameras whose
  newer firmware doesn't report the main-stream codec the way the app
  expected — it assumed H.264 and choked on H.265 streams. The codec report
  is now normalized across firmware spellings, and when it's genuinely
  unknown the stream is probed instead of assumed. ([#4])
- Cameras attached through a Home Hub could show rotated 90° because the hub
  declares their resolution transposed while the stream arrives upright; the
  auto-rotation heuristic now also requires the decoded frame to actually be
  portrait before correcting. ([#3])

[#3]: https://github.com/TodesengelX/reolink-native-linux/issues/3
[#4]: https://github.com/TodesengelX/reolink-native-linux/issues/4

## [0.1.6] — 2026-08-05

### Added

- **Synced multi-camera playback**: a 4-pane playback grid driven by one
  calendar and one timeline, like the official client's split playback. The
  timeline shows the union of every camera's recordings with a thin coverage
  lane per camera, and one playhead drives all panes to the same wall-clock
  moment; a camera with no footage there says so instead of looking broken.
- Playback grid interactions: drag cameras in from the sidebar or between
  panes (swaps, never drops), double-click to maximize a pane and again to
  restore, digital zoom with drag-to-pan in every pane, and an SD/HD toggle
  on the maximized pane that plays the full-resolution main stream with
  in-place seeking.
- Live view: drag cameras from the sidebar into any grid cell and drag panes
  between cells to rearrange; the arrangement persists across restarts.
- Sidebar recovery actions: right-click a device for **Reconnect** and
  **Update credentials…** — fix a wrong password in place instead of
  removing and re-adding the device.

### Fixed

- A device that was unreachable when the app started stayed "connecting…"
  forever; unreachable devices now retry automatically with backoff and
  recover on their own. Sign-in failures are shown with the device's
  attempts-left lockout warning and are deliberately never auto-retried.
- Adding a device now checks it first: a wrong address or rejected password
  is an inline message in the dialog instead of a phantom row.
- Rearranging grids no longer reconnects running streams — panes carry their
  streams with them, in live view and playback both. Restoring a maximized
  view reconnects nothing.
- Switching live-view layouts briefly holds hidden panes' streams so flipping
  between presets (or maximize and back) is instant.
- Concurrent commands to a device are now queued instead of racing, which
  was the main source of the NVR web server's intermittent 502 errors.

## [0.1.5] — 2026-08-02

### Fixed

- **Launching the app again raises the running window instead of starting a
  second copy.** Because closing the window leaves the app monitoring in the
  tray, relaunching from the launcher is the normal way back in — but every
  launch used to build a whole second client, with its own tray icon, NVR login
  and decoder set. They accumulated over a day of use.
- **Hardware video decoding now works in the AppImage.** The bundled `libva`
  could not load the host's VAAPI driver, so every stream silently fell back to
  software decode. The host's own `libva` is now used, with the bundled copy
  kept as a fallback so the app still starts on systems that have none.

## [0.1.4] — 2026-08-01

### Added

- System tray with background monitoring — close to tray, start on login, and
  an unread-event badge, so detection alerts keep working with the window shut.
- Offline and online alerts for cameras and for the NVR itself.
- Real thumbnails for each event in the inbox, and filtering events by camera.
- Detection events marked as red ticks on the playback timeline.
- Clip export — save the recording around the playhead to a file.
- Weekly recording-schedule editor: a 7×24 grid per recording type.

### Fixed

- Tray **Quit** always exits the app, instead of only removing the tray icon.

## [0.1.3] — 2026-07-30

### Added

- Clicking a detection notification opens the app, switches to Playback and
  plays that event back.
- The app version is shown in the nav bar.

### Fixed

- Clicking a notification now raises the window on Wayland. The compositor
  ignores a bare activation request as focus stealing, so the notification
  daemon's activation token is forwarded instead.

## [0.1.2] — 2026-07-30

### Fixed

- The AppImage runs natively on Wayland instead of falling back to XWayland;
  it now bundles the Qt Wayland platform and EGL client-buffer plugins.

## [0.1.1] — 2026-07-30

### Added

- Detection-zone editor — paint the parts of the image a camera should ignore.
- 6-camera live grid.
- Desktop notifications for detections, gated on each camera's Push setting.
- Built-in update checker, with one-click self-update for the AppImage.
- Event retention cap so the inbox stops growing without bound.

### Changed

- Device tree nests cameras under their NVR, with drawn NVR/camera icons and
  clearer online status.
- Right-click menus and the Add Device dialog restyled to match the rest of
  the app.

## [0.1.0] — 2026-07-11

Initial development release: live view, playback, events and device settings,
published as an AppImage and a Flatpak bundle.

[0.1.8]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.7...v0.1.8
[0.1.7]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/TodesengelX/reolink-native-linux/releases/tag/v0.1.0
