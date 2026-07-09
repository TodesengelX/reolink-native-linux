# Research Dossier: Native GUI toolkit selection for a Linux desktop NVR client (Reolink clone): multi-camera grid, PTZ, timelines, rich settings

> Produced by the design workflow on 2026-07-09. Facts marked for verification were adversarially checked; see [fact-check.md](fact-check.md).

## Summary

For a native, non-Electron Linux NVR client that must render a dense multi-camera video grid with zero-copy hardware decode, replicate Reolink's heavily custom-skinned dark UI, and provide rich custom controls (timeline scrubbers, PTZ joystick) plus deep settings dialogs, Qt 6 is the strongest fit and my primary recommendation, using Qt Quick/QML for the video grid and custom controls (with Qt Widgets acceptable for dense settings dialogs). Qt 6 Multimedia's rewritten FFmpeg-based pipeline supports zero-copy hardware-decoded video in both QVideoWidget (Widgets) and the VideoOutput element (QML); QML's scene graph makes pixel-perfect custom theming, animated joysticks and timelines straightforward; and LGPLv3 dynamic linking is compliant for a proprietary, distributable app via AppImage/Flatpak/deb. The runner-up is GTK4 + libadwaita (via C or Rust/gtk4-rs) with GStreamer's gtk4paintablesink, which offers excellent zero-copy dmabuf video on Wayland and the cleanest LGPL story, but fights custom non-GNOME theming, has an NVIDIA dmabuf gap, and is less turnkey for a pixel-perfect Reolink look and rich custom controls. Slint (Rust) is an honorable mention for the cleanest proprietary/royalty-free license, but its video path is example-grade rather than a built-in feature. Flutter/Linux (Skia over a GTK embedder, not native widgets), Dear ImGui (developer-tools-oriented, not consumer polish), egui/Iced, wxWidgets, and Tauri (web-wrapper, explicitly disallowed) are not the right fit against the stated constraint.

## Findings

### PRIMARY RECOMMENDATION: Qt 6 (C++), QML-first hybrid

Recommend Qt 6 with Qt Quick/QML as the primary UI layer for the video grid, timeline and PTZ controls, and Qt Widgets acceptable for the dense, form-heavy settings dialogs (a hybrid is well supported via QQuickWidget / QWidget::createWindowContainer). Rationale against the six weighted criteria: (1) Video/zero-copy: Qt 6 Multimedia was rewritten on an FFmpeg backend that supports full hardware-accelerated decode presented zero-copy; both QVideoWidget and the QML VideoOutput element are output surfaces for hardware-decoded QVideoFrames. (2) Reolink UI replication: QML gives complete pixel control and is the industry norm for custom-skinned media/surveillance control panels; theming is not constrained by any platform HIG. (3) Custom-control richness: QML's declarative scene graph plus Canvas/QQuickPaintedItem and GLSL shaders make animated PTZ joysticks and scrubbable multi-track timelines natural; Qt also has GraphicsView for complex 2D timeline scenes. (4) Packaging: mature AppImage (linuxdeployqt) and Flatpak (org.kde runtime) and .deb paths. (5) Licensing: LGPLv3 dynamic linking keeps app source proprietary. (6) Maintainability/velocity: largest ecosystem, Qt Creator/Designer tooling, huge hiring pool, long-term commercial backing. Note the real Reolink desktop client is widely believed to be a Qt application, which lowers replication risk.

### Qt 6: Widgets vs Quick/QML tradeoff

Widgets excel at dense native settings dialogs and are lower-effort for standard form UI, but their custom-skinning story is stylesheet-based (QSS) and comparatively rigid for a heavily branded dark theme with custom animated controls. QML is the right layer for the video grid, maximize/fullscreen pane transitions, timeline, and PTZ joystick because animations, layouts (GridLayout/Repeater), and GPU-composited custom items are first-class. Both consume hardware-decoded frames zero-copy (QVideoWidget for Widgets; VideoOutput for QML). Practical architecture: QML shell for camera grid + overlays + controls, embed Qt Widgets settings dialogs where form density wins.

### Qt 6 licensing implications for a distributable proprietary app

Qt is offered under LGPLv3 (and GPL) or commercial. Under LGPLv3 you may keep application source proprietary if you DYNAMICALLY link Qt, ship the Qt shared libraries, allow the user to replace/relink them, and include the LGPL license text and attribution. STATIC linking under LGPL additionally requires providing object files so a user can relink against a modified Qt — usually undesirable, so keep Qt dynamically linked. AppImage and Flatpak both ship Qt as separate .so files and are LGPL-compliant. A commercial Qt license is only needed if you must statically link, must avoid the relink obligation, or want indemnification/support. Some Qt modules and tooling have differing terms (e.g., certain mobile/embedded and some add-ons) but the core Widgets/Quick/Multimedia stack is LGPLv3-usable for desktop.

### RUNNER-UP: GTK4 + libadwaita with GStreamer gtk4paintablesink

Strong runner-up, especially if the stakeholder prioritizes LGPL cleanliness and a GNOME-native Wayland experience. GStreamer's gtk4paintablesink exposes a GdkPaintable that renders decoded frames, and since GTK 4.14 it can import DMABufs directly for true zero-copy on Wayland with VA-API decoders (large CPU/power savings). Bindings: C (gtkmm for C++), or Rust via gtk4-rs (mature, actively maintained, MSRV 1.83). Tradeoffs vs Qt: (a) libadwaita is opinionated toward the GNOME HIG and actively resists custom non-Adwaita theming — replicating Reolink's bespoke dark skin means fighting the toolkit or dropping libadwaita and hand-rolling CSS. (b) NVIDIA GPUs do NOT support the dmabuf zero-copy path, so the marquee efficiency win is lost on a large chunk of desktop NVR users (falls back to GL texture upload). (c) Custom controls (timeline, joystick) require GtkDrawingArea/Snapshot/Cairo hand-drawing — workable but more manual than QML. (d) gtk4-rs subclassing (needed for custom widgets) is notably verbose. Best when the deployment target is Intel/AMD on Wayland and a GNOME look is acceptable.

### Slint (Rust): honorable mention, cleanest proprietary license

Slint has the friendliest licensing for a closed-source product: a Royalty-Free license permits proprietary DESKTOP/mobile/web apps at no cost (requires showing Slint attribution), alongside GPLv3 and a paid commercial option (commercial mainly needed for embedded or to drop attribution). Its declarative .slint DSL is pleasant for custom controls and theming, giving good Reolink-look potential. The disqualifier for this project is video maturity: GStreamer integration exists only as an example in the repo (generates GL textures imported into slint::Image, with an extra YUV→RGB render pass, no dmabuf import yet), tested only on Ubuntu/Windows — not a built-in, hardened multi-stream video element. Viable for a small grid or a bet on the ecosystem, but higher integration risk than Qt/GTK for a many-camera NVR.

### Dear ImGui (C++): not recommended for this

ImGui can render video efficiently (it batches into vertex buffers + few draw calls; it is immediate-mode GUI, not inefficient immediate-mode rendering) and drawing a PTZ joystick/timeline is trivial. But by design it targets developer tools, debug/visualization overlays, and game-engine tooling — not polished consumer end-user apps. It lacks native-feeling text input, dialog/accessibility conventions, DPI/theming polish, and OS integration expected of a 'polished native app.' Good as an internal debug overlay, wrong as the primary shell.

### Flutter/Linux: fails the native constraint

Flutter on Linux uses a GTK/GDK + X11 embedder but renders its entire UI with Skia (its own 2D engine), not native platform widgets — so it is 'native-ish binary, non-native rendering.' Against a stakeholder constraint of NATIVE code with no web-wrapper/third-party 'solutions,' Flutter is in the same philosophical bucket as the disallowed options (custom-drawn everything, large bundled engine). Note the google/flutter-desktop-embedding reference repo was archived in Dec 2025. Video is via platform-view/texture plugins, adding integration surface. Exclude.

### Other options surveyed and why they lose

wxWidgets (C++): thin wrappers over native widgets, but weak custom-theming and no first-class hardware video pipeline — poor fit for a branded, video-dense UI. egui (Rust): immediate-mode like ImGui, same consumer-polish limitations. Iced (Rust): Elm-style retained GUI, improving but younger, no hardened multi-stream video story. Tauri: explicitly a web/WebView wrapper — disallowed by the stakeholder constraint. gtkmm (C++ GTK4): same engine/tradeoffs as the GTK4 runner-up, choose it only if the team prefers C++ over Rust/C for GTK.

### Cross-cutting NVR caveat independent of toolkit

For a many-camera grid, the binding constraint is often the GPU's simultaneous hardware-decode session limit, not the GUI toolkit. Regardless of Qt vs GTK, plan to decode camera sub-streams (lower res) for the grid and switch to the main stream only on a maximized/fullscreen pane, and/or mix hardware and software decode. This is why Qt's flexible QVideoSink / per-view player model and GStreamer's pipeline-per-tile model both matter — the recommendation holds either way, but validate decode-session limits on target GPUs early.

## Open Questions

- What are the exact target GPUs/drivers (Intel/AMD/NVIDIA, X11 vs Wayland)? This decides whether GTK4's dmabuf zero-copy advantage is real (NVIDIA loses it) and how many HW decode sessions the grid can sustain.
- How many cameras must render simultaneously at what resolution — does the grid rely on camera sub-streams, and is main-stream decode only needed on a maximized pane?
- Is closed-source/proprietary distribution a hard requirement? If yes and licensing cleanliness is paramount, Slint's royalty-free license or Qt commercial may be preferred over Qt LGPL dynamic-linking obligations.
- Does the team have stronger C++ or Rust expertise? Qt (C++/QML) vs GTK4 (Rust gtk4-rs) developer velocity depends heavily on this.
- Must the app strictly match Reolink's exact skin, or just feel polished and native? Strict replication strongly favors Qt/QML over libadwaita's opinionated GNOME styling.
- Which packaging format is primary (AppImage vs Flatpak vs deb)? Flatpak favors GTK4/GNOME runtime slightly; AppImage is well-trodden for Qt.

## Sources

- https://www.qt.io/blog/qt-multimedia-in-qt-6
- https://doc.qt.io/qt-6/videooverview.html
- https://doc.qt.io/qt-6/qml-qtmultimedia-videooutput.html
- https://www.qt.io/development/open-source-lgpl-obligations
- https://www.smallstepsystems.com/using-qt-5-15-and-qt-6-under-lgplv3/
- https://gstreamer.freedesktop.org/documentation/gtk4/
- https://centricular.com/devlog/2024-04/gtk4-dmabuf-import/
- https://lib.rs/crates/gst-plugin-gtk4
- https://github.com/gtk-rs/gtk4-rs
- https://gtk-rs.org/gtk4-rs/stable/latest/book/libadwaita.html
- https://slint.dev/pricing
- https://slint.dev/blog/slint-1.1-released
- https://github.com/slint-ui/slint/blob/master/examples/gstreamer-player/README.md
- https://github.com/ocornut/imgui
- https://medium.com/@ronakofficial/flutter-skia-driving-high-performance-ui-rendering-fcbc6d8ac9ec
