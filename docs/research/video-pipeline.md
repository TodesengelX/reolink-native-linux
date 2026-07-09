# Research Dossier: Decode + render + audio architecture for a native Linux 16-pane NVR-grid IP-camera app (H.264/H.265 RTSP)

> Produced by the design workflow on 2026-07-09. Facts marked for verification were adversarially checked; see [fact-check.md](fact-check.md).

## Summary

For a 16-pane low-latency NVR grid on Linux, the strongest architecture is FFmpeg/libavformat+libavcodec for RTSP ingest and hardware decode, paired with a zero-copy VAAPI-DMA-BUF-to-OpenGL/Vulkan render path, and PipeWire for audio. FFmpeg gives you the tightest control over per-stream demux/decode threads, RTSP transport, remux-to-mp4 without re-encode, JPEG snapshots, and seeking; GStreamer is the pragmatic alternative when you want its RTSP client, jitterbuffer, and ready-made GTK4/Qt sinks to do the plumbing, at the cost of per-element control. The critical scaling insight is that NVDEC decode sessions are effectively unlimited on consumer NVIDIA GPUs (unlike NVENC's ~8-12 encode cap), and Intel/AMD VAAPI decode 16 sub-streams comfortably; the real win for a grid is pulling each camera's low-res sub-stream for the 16 tiles and only opening the main stream on maximize. Zero-copy rendering (VASurface -> DMA-BUF fd -> EGLImage -> GL texture, or VkImage import) is the difference between a viable and a CPU-melting grid. NVIDIA is the awkward case for the modern DMA-BUF sink paths (its VA driver doesn't export DMA-BUFs the way GTK4's importer wants), so on NVIDIA you keep frames in CUDA/NvDec memory and interop to GL/Vulkan via the CUDA-GL interop rather than DMA-BUF.

## Findings

### Decode engine: FFmpeg/libav vs GStreamer

Recommend libavformat (RTSP demux) + libavcodec (decode) for a from-scratch NVR because you get direct control of every packet, the demux/decode thread, transport flags, and reclocking. Core decode loop: av_read_frame() -> avcodec_send_packet() -> avcodec_receive_frame() (drain by sending NULL packet on EOF). GStreamer is the faster route to a working product: use uridecodebin/rtspsrc ! rtph264depay ! h264parse ! decodebin3 (or a specific hw element) ! <sink>, with appsink if you need frames in-process. GStreamer's rtspsrc bundles an RTP jitterbuffer, retransmission, and RTCP; with tuning it hits ~220 ms glass-to-glass on IP cameras. Tradeoff: FFmpeg = operational simplicity and per-frame control; GStreamer = negotiation, sinks, and jitter handling done for you but harder to reason about at 16x. A common hybrid is FFmpeg for record/snapshot/remux and GStreamer (or a custom GL sink) for the live grid.

### Hardware decode APIs on Linux (libavcodec)

Create an AVBufferRef hw device with av_hwdevice_ctx_create(&ctx, AV_HWDEVICE_TYPE_VAAPI|_CUDA|_VDPAU|_DRM, device, opts, 0). Assign avctx->hw_device_ctx = av_buffer_ref(ctx) before avcodec_open2. Pick the pixel format in the decoder's get_format callback (return AV_PIX_FMT_VAAPI / AV_PIX_FMT_CUDA), and enumerate supported configs with avcodec_get_hw_config(). Decoded AVFrame->format is then the hw pixfmt with frame->data[3] holding a VASurfaceID (VAAPI) or CUdeviceptr (CUDA); use av_hwframe_transfer_data() only when you must pull to system memory (e.g., snapshot). VAAPI covers Intel (iHD/i965) and AMD (Mesa radeonsi/RADV) for H.264 and H.265/HEVC (incl. Main10 on capable GPUs); NVDEC/CUVID (h264_cuvid, hevc_cuvid, or -hwaccel cuda with hwaccel_output_format cuda) covers NVIDIA; VDPAU is the legacy NVIDIA/older path and is effectively superseded by NVDEC. gstreamer-vaapi is now deprecated in favor of the newer GStreamer 'va' plugin (vah264dec/vah265dec).

### Hardware negotiation and software fallback

Probe at open time: for the codec, loop avcodec_get_hw_config(codec, i) and check AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX and the device type. Try to create the hw device context; if creation fails, or if get_format is never offered the hw pixfmt, or if the first few frames error out, tear down and reopen with a pure software decoder (h264/hevc). Practical robustness rule for a 16-cam app: keep a per-stream capability flag and degrade individually — one camera's exotic profile (e.g., HEVC 4:2:2, or B-frame configs a given VAAPI driver rejects) should silently fall to software (libavcodec's threaded software H.264/HEVC decoders scale across cores) without killing the grid. Always keep software H.264/HEVC as the universal floor.

### Zero-copy render path: VAAPI/DMA-BUF -> EGLImage -> GL (Intel/AMD)

This is the high-value path. Export the decoded VA surface as DMA-BUF: with FFmpeg, map the VAAPI frame to an AV_HWDEVICE_TYPE_DRM frame via av_hwframe_map(dst, src, AV_HWFRAME_MAP_READ) to obtain an AVDRMFrameDescriptor (DMA-BUF fds + planes/modifiers), or call vaExportSurfaceHandle() directly. Then import into GL with EGL: eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs) using the EGL_EXT_image_dma_buf_import extension (attribs carry fd, offset, pitch, DRM fourcc, and DRM format modifiers via EGL_EXT_image_dma_buf_import_modifiers), then glEGLImageTargetTexture2DOES() (GL_OES_EGL_image) binds it to a GL_TEXTURE_EXTERNAL_OES sampler. No pixel copy occurs. SDL2 shipped exactly this VAAPI+EGL zero-copy path; QtAV and mpv use the same technique. For Vulkan, import the DMA-BUF via VK_EXT_external_memory_dma_buf + VK_KHR_external_memory_fd into a VkImage. The surface is typically NV12/P010 — sample Y and UV planes and do YUV->RGB (and any tone-map for 10-bit) in the fragment shader.

### Upload-and-convert fallback (portable but costlier)

If you can't establish zero-copy (e.g., mixed drivers, or you already transferred to system memory), upload the NV12/YUV420 planes as GL textures (one R8 for Y, one RG8 for interleaved UV in NV12) and convert to RGB in a fragment shader (BT.709 matrix, full/limited range aware). This costs a CPU->GPU upload per frame per stream but avoids a CPU-side color conversion. At 16x sub-streams (e.g., 640x360-ish) this is tolerable; at 16x main-stream 1080p/4K it is not — which is another reason the grid should run sub-streams. Never do CPU swscale YUV->RGB for 16 live tiles.

### NVIDIA render interop caveat

NVIDIA is the odd one out for the DMA-BUF sink ecosystem: its VA-API/driver stack does not export DMA-BUFs in the form GTK4's dmabuf importer and the newer GStreamer dmabuf negotiation expect, so gtk4paintablesink's zero-copy DMABuf offload path works with Intel and newer AMD (VA) but not NVIDIA. On NVIDIA, keep decoded frames in CUDA memory (hwaccel_output_format cuda) and use CUDA-OpenGL interop (cudaGraphicsGLRegisterImage / cuGraphicsGLRegisterImage, map, cudaMemcpy2D into a GL texture surface) or CUDA-Vulkan external memory interop, rather than DMA-BUF/EGLImage. Plan two render backends: DMA-BUF/EGL for Intel/AMD, CUDA-GL/VK interop for NVIDIA.

### Qt integration (Qt 6)

Two options. (1) Qt Multimedia: on Linux the recommended backend is the GStreamer backend (set QT_MEDIA_BACKEND=gstreamer; the older ffmpeg backend also exists). Render into QVideoWidget or a QML VideoOutput fed by QMediaPlayer, or push your own frames via QVideoSink::setVideoFrame(QVideoFrame). QVideoSink exposes rhi() (the QRhi — Qt's abstraction over OpenGL/Vulkan/Metal/D3D) so frames can be delivered as GPU textures; QVideoFrame can wrap an OpenGL texture handle to keep things on-GPU. (2) Roll your own: subclass QOpenGLWidget / QRhiWidget (Qt 6.7+) or a QQuickItem and do the DMA-BUF/EGLImage or CUDA-GL import yourself, which gives the tightest 16-pane control and lets you share one GL/Vulkan context across all tiles. For a serious NVR grid, custom QRhiWidget/QOpenGLWidget tiles driven by your own FFmpeg decoders is the most controllable design.

### GTK4 integration

Use gtk4paintablesink (from the GStreamer Rust plugins, gst-plugin-gtk4): it presents decoded video as a GdkPaintable you drop into a GtkPicture. With GTK 4.14+ and the dmabuf feature it imports DMA-BUF frames directly (GTK compositor/renderer handles them on Wayland), cutting CPU and power markedly with Intel/newer-AMD hardware decoders — but not NVIDIA (no DMA-BUF export). Feature flags gtk_v4_10/gtk_v4_12/gtk_v4_14 opt into newer fast paths. GTK4's GskGLRenderer/Vulkan renderer and graphene handle the compositing of the 16 GdkPaintables. If not on GStreamer, you can push frames as GdkTexture (gdk_gl_texture_new for a GL texture, or a GdkDmabufTexture on 4.14+) into a GtkPicture per tile.

### Latency: RTSP transport and buffering

RTSP interleaved over TCP (rtsp_transport=tcp) is the reliable default for cameras and NVRs — no lost-packet artifacts, firewall-friendly, at the cost of head-of-line blocking under loss. UDP (rtsp_transport=udp) gives lower latency but needs a jitterbuffer and tolerates loss with visible corruption; use it only on clean LANs. In FFmpeg set AVDictionary opts: rtsp_transport=tcp, stimeout/timeout (socket timeout in us), max_delay low, reorder_queue_size small, fflags=nobuffer, flags=low_delay, probesize/analyzeduration small to cut startup. Disable extra buffering in your own frame queue (target 1-2 frames). Camera-side: request a short GOP/keyframe interval (1-2 s) so tiles paint quickly on open and after loss, but note short GOP raises bitrate. GStreamer equivalent: rtspsrc latency=100-200, drop-on-latency=true, protocols=tcp, plus a leaky queue.

### Scaling to 16 streams: threading and stream selection

Model: one demux+decode worker thread per stream (16 threads), each owning its AVFormatContext and AVCodecContext, pushing decoded frames into a small lock-free/mutex ring buffer (drop oldest on overflow). A single render thread owns one shared GL/Vulkan context and draws all 16 tiles per vsync, sampling each tile's latest frame (present-latest, not queue-drain, so a slow camera never stalls the grid). Decode budget: hardware decoders (VAAPI/NVDEC) handle 16 sub-streams easily — NVDEC has no artificial concurrent-decode session limit on consumer GeForce (contrast NVENC encode, capped ~8, recently up to ~12), and Intel/AMD fixed-function decode blocks handle many low-res streams. The decisive design choice: the 16-tile grid subscribes to each camera's SECOND/sub-stream (e.g., 640x360-720p, low bitrate); switch to the MAIN stream only for the maximized/selected pane. This slashes decode, upload, and shader cost by ~5-10x versus 16 main streams and is how commercial NVRs (and Frigate-style apps) do it.

### Audio decode and playback (PipeWire)

Cameras carry AAC or G.711 (PCMU/PCMA, i.e., a-law/mu-law), sometimes G.726. Decode with libavcodec (aac, pcm_mulaw/pcm_alaw) alongside video from the same AVFormatContext. Play out via PipeWire — on modern Linux PipeWire is the server, exposing both a native libpipewire API (pw_stream) and PulseAudio and JACK compatibility shims, so a PulseAudio-targeted client or GStreamer pipewiresink/pulsesink just works. For a 16-grid, only unmute/route the selected pane's audio (mixing 16 camera audios is rarely wanted). Keep a small audio ring buffer and let audio drive A/V sync for the focused stream (resample with libswresample to the device rate).

### Two-way audio (mic -> encode -> send) feasibility

Feasible via the ONVIF Profile T RTSP audio backchannel. Flow: in DESCRIBE, request the backchannel (Require: www.onvif.org/ver20/backchannel header); the SDP advertises a sendonly media line whose rtpmap names the accepted codec (almost always G.711 PCMU/PCMA, sometimes AAC/G.726 — enumerated via ONVIF GetAudioDecoderConfigurationOptions). SETUP that media, PLAY, then after the 200 OK you send RTP packets of encoded mic audio back to the camera on that channel. Capture the mic with PipeWire (pw_stream capture or via Pulse/GStreamer pipewiresrc), encode to G.711 (trivial, ffmpeg pcm_mulaw/pcm_alaw), packetize as RTP, and write on the backchannel socket. FFmpeg's RTSP muxer support for backchannel is limited, so many implementations drive the RTSP/RTP backchannel with a small custom client (or GStreamer with rtspsrc backchannel=onvif, which supports it). Verify bidirectional RTP with Wireshark.

### Recording and snapshots without re-encode

Record by REMUXING, not transcoding: open an output AVFormatContext (mp4 or fragmented mp4/fMP4 for crash-safety, or Matroska), avformat_new_stream() copying codecpar from the input, and write the demuxed AVPackets straight through with av_interleaved_write_frame() after av_packet_rescale_ts() to the output timebase. Zero decode/encode cost, near-zero CPU — you can record all 16 main streams while only decoding sub-streams for display. Note H.264/HEVC in MP4 need bitstream filtering from Annex-B to AVCC/HVCC (h264_mp4toannexb is the reverse; for RTSP->MP4 use the extradata/parser to produce length-prefixed NALs — FFmpeg's mp4 muxer handles this when codecpar extradata is set). For JPEG snapshots: decode one frame (you already have decoded frames for the displayed stream, or decode-on-demand for a hidden one), convert to YUVJ/RGB, and encode with the mjpeg encoder (or write PNG via png encoder). If you only have a hw surface, av_hwframe_transfer_data() to system memory first, then swscale to the JPEG encoder's expected pixfmt.

### Playback scrubbing from NVR recordings

If reading recorded files (local mp4/mkv), seek with av_seek_frame()/avformat_seek_file(ctx, stream, min, target, max, AVSEEK_FLAG_BACKWARD) which lands on the nearest preceding keyframe, then decode-and-discard forward to the exact target PTS for frame-accurate scrubbing (GOP-length dependent latency). For live NVR playback over ONVIF (Profile G replay), seeking is done in the RTSP layer: RTSP PLAY with a Range header (npt= or clock= absolute time), plus ONVIF replay headers (Rate-Control, Frames, Scale for fast-forward/reverse and I-frame-only trick modes). Practically: for smooth scrubbing pull an I-frame-only (Scale/Frames=intra) stream while dragging, then resume normal playback on release. Short camera GOPs make scrubbing more responsive but cost bitrate — the same tradeoff as live.

## Open Questions

- Exact GPU target(s): a fixed Intel/AMD deployment lets you commit to the DMA-BUF/EGL zero-copy path only; NVIDIA-inclusive deployment forces a second CUDA-interop render backend and doubles render-path testing.
- Wayland vs X11 target: DMA-BUF sink offload and modifier negotiation are cleanest on Wayland; X11/EGL still works but with more edge cases.
- Do target cameras reliably expose usable sub-streams and short-GOP options via ONVIF, or will some force main-stream-only tiles (changing the decode budget)?
- Is frame-accurate playback scrubbing required, or is keyframe-granular seeking acceptable — this drives whether you need decode-to-target and possibly an intra-only trick stream.
- HEVC 10-bit/Main10 and 4:2:2 prevalence among target cameras: some VAAPI drivers reject certain profiles, affecting how often software fallback triggers at 16x.
- Toolkit choice (Qt vs GTK4) and whether you build custom GL/Vulkan tiles vs. use framework sinks — this is the single biggest determinant of render-path control at 16 panes.
- Whether two-way audio must interoperate with non-ONVIF/proprietary camera backchannels (e.g., vendor SDKs), which FFmpeg/GStreamer won't cover out of the box.

## Sources

- https://gstreamer.freedesktop.org/documentation/gtk4/
- https://centricular.com/devlog/2024-04/gtk4-dmabuf-import/
- https://discourse.libsdl.org/t/sdl-added-support-for-0-copy-decode-and-display-using-vaapi-and-egl/46499
- https://github.com/fmor/demo_ffmpeg_vaapi_gl
- https://github.com/wang-bin/QtAV/wiki/Hardware-Accelerated-Decoding
- https://doc.qt.io/qt-6/qtmultimedia-gstreamer.html
- https://doc.qt.io/qt-6/qvideosink.html
- https://doc.qt.io/qt-6/qrhi.html
- https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/ffmpeg-with-nvidia-gpu/index.html
- https://developer.nvidia.com/blog/nvidia-ffmpeg-transcoding-guide/
- https://www.tomshardware.com/news/nvidia-increases-concurrent-nvenc-sessions-on-consumer-gpus
- https://www.happytimesoft.com/knowledge/audio-back-channel.html
- https://www.onvif.org/specs/stream/ONVIF-Streaming-Spec.pdf
- https://muratdemirci.com.tr/en/ffmpeg-gstreamer/
- https://medium.com/@fanzongshaoxing/use-nvidia-deepstream-to-accelerate-h-264-video-stream-decoding-8f0fec764778
- https://github.com/clearlinux-pkgs/gstreamer-vaapi/blob/main/NEWS
