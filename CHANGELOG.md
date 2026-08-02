# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the project is pre-1.0, minor versions may still change behaviour.

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

[0.1.5]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/TodesengelX/reolink-native-linux/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/TodesengelX/reolink-native-linux/releases/tag/v0.1.0
