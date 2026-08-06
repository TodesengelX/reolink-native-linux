# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the project is pre-1.0, minor versions may still change behaviour.

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

[0.1.7]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/TodesengelX/reolink-native-linux/releases/tag/v0.1.0
