#include "StreamPlayer.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

namespace rl {

namespace {

constexpr int kOpenTimeoutMs = 10000;
constexpr int kReadStallTimeoutMs = 10000; // §5.8 watchdog: no packet in 10s → reconnect
constexpr int kBackoffStartMs = 1000;
constexpr int kBackoffCapMs = 30000;

using Session = StreamPlayer::Session;
using State = StreamPlayer::State;

bool isLiveUrl(const QString &source)
{
    return source.startsWith(QLatin1String("rtsp://")) ||
           source.startsWith(QLatin1String("rtmp://")) ||
           source.startsWith(QLatin1String("tcp://")) ||
           source.startsWith(QLatin1String("udp://"));
}

// Never log credentials — they appear both as userinfo (rtsp://user:pass@host)
// and as query params (the http-flv playback URL: ...&user=X&password=Y).
QString redacted(const QString &source)
{
    QUrl u(source);
    if (!u.isValid())
        return source;
    bool changed = !u.userInfo().isEmpty();
    QUrlQuery q(u);
    for (const QString &key : {QStringLiteral("user"), QStringLiteral("password"),
                               QStringLiteral("token")}) {
        if (q.hasQueryItem(key)) {
            q.removeAllQueryItems(key);
            changed = true;
        }
    }
    if (!changed)
        return source;
    u.setQuery(q);
    return u.toDisplayString(QUrl::RemoveUserInfo);
}

// Determine how the sink should rotate a stream's frames for upright display.
//
// Priority order:
//  1. A display-matrix in the stream side-data (standard rotation signalling;
//     Reolink sends none, but other sources may).
//  2. Declared-vs-decoded swap test [primary]. Some Reolink cameras (Duo 3-class)
//     transmit the main stream transposed — the true image is landscape (GetEnc
//     declares e.g. 7680x2160) but it decodes as 2160x7680 portrait, so the coded
//     width stays within the HEVC level cap. No orientation metadata rides along
//     on RTSP/FLV, so we reconcile the two: if the decoded frame is exactly the
//     declared size transposed, the stream is rotated and we correct it. This is
//     self-calibrating — it leaves already-landscape sub streams and genuine
//     corridor-mode portrait feeds (declared == decoded) untouched, unlike a
//     resolution threshold. `expected` is the GetEnc size for THIS stream.
//  3. Extreme-portrait heuristic [fallback], for raw-URL playback with no NVR
//     session to supply a declared size.
//
// Direction is Clockwise270 (verified on an RLN8-410). Nothing on the wire
// encodes winding direction, so this is a per-model constant, not inferred.
QtVideo::Rotation streamRotation(const AVStream *stream, QSize expected)
{
    const AVPacketSideData *sd =
        stream->codecpar->coded_side_data
            ? av_packet_side_data_get(stream->codecpar->coded_side_data,
                                      stream->codecpar->nb_coded_side_data,
                                      AV_PKT_DATA_DISPLAYMATRIX)
            : nullptr;
    if (sd && sd->size >= 9 * static_cast<int>(sizeof(int32_t))) {
        // av_display_rotation_get returns the CCW angle; the clockwise display
        // rotation is its negation, normalized to [0,360).
        int cw = static_cast<int>(std::llround(-av_display_rotation_get(
            reinterpret_cast<const int32_t *>(sd->data))));
        cw = ((cw % 360) + 360) % 360;
        switch (cw) {
        case 90:
            return QtVideo::Rotation::Clockwise90;
        case 180:
            return QtVideo::Rotation::Clockwise180;
        case 270:
            return QtVideo::Rotation::Clockwise270;
        default:
            return QtVideo::Rotation::None;
        }
    }

    const int w = stream->codecpar->width;
    const int h = stream->codecpar->height;

    // When the NVR told us the declared size, it is authoritative: rotate iff the
    // decoded frame is its exact transpose (and no rotation otherwise).
    if (expected.isValid()) {
        if (w > 0 && h > 0 && expected.width() == h && expected.height() == w)
            return QtVideo::Rotation::Clockwise270;
        return QtVideo::Rotation::None;
    }

    // No declared size: fall back to the extreme-portrait signature. The 2x
    // threshold sits above a deliberate 9:16 portrait mount (1.78x).
    if (w > 0 && h > w * 2)
        return QtVideo::Rotation::Clockwise270;
    return QtVideo::Rotation::None;
}

QVideoFrameFormat::PixelFormat mapPixelFormat(int avFormat)
{
    switch (avFormat) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return QVideoFrameFormat::Format_YUV420P;
    case AV_PIX_FMT_NV12:
        return QVideoFrameFormat::Format_NV12;
    default:
        return QVideoFrameFormat::Format_Invalid;
    }
}

// Post a state change to the StreamPlayer on the GUI thread. backMutex guarantees
// the player is alive across the invokeMethod call; invokeMethod(context, fn)
// then cancels itself if the player is destroyed before the event is delivered.
void postState(const std::shared_ptr<Session> &s, State st, const QString &err)
{
    QMutexLocker lock(&s->backMutex);
    if (!s->player)
        return;
    StreamPlayer *p = s->player;
    QMetaObject::invokeMethod(
        p, [p, st, err] { p->applyStateFromWorker(st, err); }, Qt::QueuedConnection);
}

void postFramesTick(const std::shared_ptr<Session> &s)
{
    QMutexLocker lock(&s->backMutex);
    if (!s->player)
        return;
    StreamPlayer *p = s->player;
    QMetaObject::invokeMethod(p, [p] { emit p->framesDecodedChanged(); }, Qt::QueuedConnection);
}

// Deliver a decoded frame to the QML-owned sink, on the GUI thread where the sink
// lives and is destroyed (finding: cross-thread sink teardown race).
void deliverFrame(const std::shared_ptr<Session> &s, const QVideoFrame &frame)
{
    QMutexLocker lock(&s->backMutex);
    if (!s->player)
        return;
    StreamPlayer *p = s->player;
    QMetaObject::invokeMethod(
        p,
        [s, frame] {
            QMutexLocker sinkLock(&s->sinkMutex);
            if (s->sink)
                s->sink->setVideoFrame(frame);
        },
        Qt::QueuedConnection);
}

void postRecording(const std::shared_ptr<Session> &s, bool recording, const QString &path,
                   const QString &error)
{
    QMutexLocker lock(&s->backMutex);
    if (!s->player)
        return;
    StreamPlayer *p = s->player;
    QMetaObject::invokeMethod(
        p, [p, recording, path, error] { p->applyRecordingState(recording, path, error); },
        Qt::QueuedConnection);
}

// Stream-copy recorder: taps the live demux, muxing video packets into an MP4
// without re-encoding (DESIGN §5.5). Starts on the next keyframe and rebases
// timestamps so the file begins at t=0.
struct Recorder {
    AVFormatContext *oc = nullptr;
    int outIndex = -1;
    int inIndex = -1;
    AVRational inTb{};
    bool waitingKey = true;
    qint64 startTs = AV_NOPTS_VALUE;
    QString path;

    bool active() const { return oc != nullptr; }

    bool open(const QString &outPath, const AVStream *in)
    {
        path = outPath;
        inIndex = in->index;
        inTb = in->time_base;
        const QByteArray p = outPath.toUtf8();
        if (avformat_alloc_output_context2(&oc, nullptr, "mp4", p.constData()) < 0 || !oc)
            return false;
        AVStream *out = avformat_new_stream(oc, nullptr);
        if (!out || avcodec_parameters_copy(out->codecpar, in->codecpar) < 0)
            return false;
        out->codecpar->codec_tag = 0; // let the muxer choose (avc1/hvc1)
        outIndex = out->index;
        if (!(oc->oformat->flags & AVFMT_NOFILE) &&
            avio_open(&oc->pb, p.constData(), AVIO_FLAG_WRITE) < 0)
            return false;
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "movflags", "+faststart", 0);
        const int rc = avformat_write_header(oc, &opts);
        av_dict_free(&opts);
        if (rc < 0)
            return false;
        waitingKey = true;
        startTs = AV_NOPTS_VALUE;
        return true;
    }

    void write(const AVPacket *src)
    {
        if (src->stream_index != inIndex)
            return;
        if (waitingKey) {
            if (!(src->flags & AV_PKT_FLAG_KEY))
                return; // start on a keyframe so the file is decodable
            waitingKey = false;
            startTs = src->pts != AV_NOPTS_VALUE ? src->pts : src->dts;
        }
        AVPacket *pkt = av_packet_clone(src); // original continues to the decoder
        if (!pkt)
            return;
        pkt->stream_index = outIndex;
        const AVRational outTb = oc->streams[outIndex]->time_base;
        const qint64 off = startTs == AV_NOPTS_VALUE ? 0 : startTs;
        const auto rnd = static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        if (pkt->pts != AV_NOPTS_VALUE)
            pkt->pts = av_rescale_q_rnd(pkt->pts - off, inTb, outTb, rnd);
        if (pkt->dts != AV_NOPTS_VALUE)
            pkt->dts = av_rescale_q_rnd(pkt->dts - off, inTb, outTb, rnd);
        pkt->duration = av_rescale_q(pkt->duration, inTb, outTb);
        pkt->pos = -1;
        av_interleaved_write_frame(oc, pkt); // takes ownership of pkt's contents
        av_packet_free(&pkt);
    }

    // Writes the trailer and closes; returns the path (empty on nothing recorded).
    QString finalize()
    {
        if (!oc)
            return {};
        QString result;
        if (!waitingKey) { // at least one frame was written
            av_write_trailer(oc);
            result = path;
        }
        if (!(oc->oformat->flags & AVFMT_NOFILE) && oc->pb)
            avio_closep(&oc->pb);
        avformat_free_context(oc);
        oc = nullptr;
        return result;
    }
};

struct InterruptContext {
    Session *session = nullptr;
    QDeadlineTimer deadline;

    static int callback(void *opaque)
    {
        auto *ctx = static_cast<InterruptContext *>(opaque);
        if (ctx->session->abort.load(std::memory_order_relaxed))
            return 1;
        return ctx->deadline.hasExpired() ? 1 : 0;
    }
};

void abortableSleepUs(Session *s, qint64 waitUs)
{
    for (qint64 remaining = waitUs;
         remaining > 0 && !s->abort.load(std::memory_order_relaxed); remaining -= 10000)
        av_usleep(static_cast<unsigned>(qMin<qint64>(remaining, 10000)));
}

// Convert one AVFrame to a QVideoFrame and hand it to the sink. Returns false on
// an unrecoverable mapping failure (frame dropped).
bool emitFrame(const std::shared_ptr<Session> &s, AVFrame *out,
               QVideoFrameFormat::PixelFormat outFormat, QtVideo::Rotation rotation)
{
    QVideoFrameFormat format(QSize(out->width, out->height), outFormat);
    // Full-range (JPEG) YUV must be tagged or the sink renders it as limited range.
    if (out->color_range == AVCOL_RANGE_JPEG || out->format == AV_PIX_FMT_YUVJ420P)
        format.setColorRange(QVideoFrameFormat::ColorRange_Full);

    QVideoFrame videoFrame(format);
    // Rotation is honored at the frame level by the sink/renderer (the format's
    // rotation is not); set it here so cameras that transmit the main stream
    // rotated (portrait) are presented upright.
    if (rotation != QtVideo::Rotation::None)
        videoFrame.setRotation(rotation);
    if (!videoFrame.map(QVideoFrame::WriteOnly))
        return false;
    for (int plane = 0; plane < videoFrame.planeCount(); ++plane) {
        const int planeHeight = plane == 0 ? out->height : (out->height + 1) / 2;
        // linesize may be negative for bottom-up frames; qAbs stops the memcpy
        // size from wrapping to ~2^64.
        const int rowBytes = qMin(videoFrame.bytesPerLine(plane), qAbs(out->linesize[plane]));
        for (int y = 0; y < planeHeight; ++y)
            memcpy(videoFrame.bits(plane) + y * videoFrame.bytesPerLine(plane),
                   out->data[plane] + y * out->linesize[plane], static_cast<size_t>(rowBytes));
    }
    videoFrame.unmap();
    deliverFrame(s, videoFrame);
    return true;
}

// ---- Hardware decode ------------------------------------------------------
// Offload H.264/H.265 decode to the GPU (VAAPI/CUDA/VDPAU), then transfer the
// surface back to system memory for the sink-upload path. This is not yet the
// zero-copy path (DMA-BUF→GL lands with the GPU render spike, DESIGN §5.2) but
// it already moves decode off the CPU, which is what lets a 16-tile grid scale.
// Any failure falls back to software decode, per-stream and silently.
bool hwDecodeEnabled()
{
    return qgetenv("RL_HWDECODE") != "0"; // default on
}

AVPixelFormat getHwFormat(AVCodecContext *ctx, const AVPixelFormat *fmts)
{
    const auto want = static_cast<AVPixelFormat>(reinterpret_cast<intptr_t>(ctx->opaque));
    for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == want)
            return *p;
    // Wanted GPU surface not offered (or hwaccel init failed and ffmpeg is
    // re-asking): pick the first SOFTWARE format, not fmts[0] — the list leads
    // with hw formats, and returning a refused one loops the failure (e.g. VAAPI
    // rejecting 2160x7680, which exceeds its 4352 max height).
    for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(*p);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL))
            return *p;
    }
    return fmts[0];
}

// Configures dec for hardware decode; returns the hw pixel format (or NONE).
AVPixelFormat setupHwDecode(AVCodecContext *dec, const AVCodec *codec, AVBufferRef **hwCtx)
{
    static const AVHWDeviceType order[] = {AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_CUDA,
                                           AV_HWDEVICE_TYPE_VDPAU};
    for (AVHWDeviceType type : order) {
        AVPixelFormat pixfmt = AV_PIX_FMT_NONE;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig *cfg = avcodec_get_hw_config(codec, i);
            if (!cfg)
                break;
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == type) {
                pixfmt = cfg->pix_fmt;
                break;
            }
        }
        if (pixfmt == AV_PIX_FMT_NONE)
            continue;
        AVBufferRef *ctx = nullptr;
        if (av_hwdevice_ctx_create(&ctx, type, nullptr, nullptr, 0) < 0)
            continue;
        *hwCtx = ctx;
        dec->hw_device_ctx = av_buffer_ref(ctx);
        dec->opaque = reinterpret_cast<void *>(static_cast<intptr_t>(pixfmt));
        dec->get_format = getHwFormat;
        qCInfo(lcMedia) << "hw decode via" << av_hwdevice_get_type_name(type);
        return pixfmt;
    }
    return AV_PIX_FMT_NONE;
}

// AVIOContext read callback for a custom packet source (native Baichuan): pull
// elementary-stream bytes from the session's blocking reader.
int avioReadThunk(void *opaque, uint8_t *buf, int size)
{
    auto *s = static_cast<Session *>(opaque);
    if (s->abort.load())
        return AVERROR_EOF;
    return s->readPacket(buf, size);
}

// One open→decode session. Returns true when the caller should retry (reconnect).
bool runSession(const std::shared_ptr<Session> &s, bool *streamingOut)
{
    const QString source = s->source;
    const QByteArray url = source.toUtf8();
    // A custom packet source (native Baichuan) is treated as live: real-time paced
    // by the device, no URL to open.
    const bool packetSource = static_cast<bool>(s->readPacket);
    const bool live = packetSource || isLiveUrl(source);

    InterruptContext interrupt{s.get(), QDeadlineTimer(kOpenTimeoutMs)};

    AVFormatContext *fmt = avformat_alloc_context();
    fmt->interrupt_callback.callback = &InterruptContext::callback;
    fmt->interrupt_callback.opaque = &interrupt;

    // Custom-IO cleanup must outlive fmt close (avformat_close_input leaves a
    // caller-supplied AVIOContext alone), so this guard is declared first.
    AVIOContext *avioCtx = nullptr;
    struct AvioGuard {
        AVIOContext **pb;
        ~AvioGuard()
        {
            if (*pb) {
                av_freep(&(*pb)->buffer);
                avio_context_free(pb);
            }
        }
    } avioGuard{&avioCtx};

    AVDictionary *opts = nullptr;
    const AVInputFormat *ifmt = nullptr;
    if (packetSource) {
        constexpr int kAvioBuf = 1 << 16;
        avioCtx = avio_alloc_context(static_cast<unsigned char *>(av_malloc(kAvioBuf)), kAvioBuf,
                                     0, s.get(), &avioReadThunk, nullptr, nullptr);
        fmt->pb = avioCtx;
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        ifmt = av_find_input_format(s->forcedFormat.toUtf8().constData());
        // Raw elementary stream over non-seekable IO: a big probesize lets the
        // parser find the SPS/VPS in the first I-frame (can be ~600 KB) so the size
        // isn't "unspecified". Keep analyzeduration SHORT — the codec params are all
        // in the first IDR, and waiting to estimate the framerate over many frames
        // adds seconds of startup latency on a realtime live stream (playback is
        // unaffected since its frames arrive faster than realtime).
        av_dict_set(&opts, "probesize", "4000000", 0);
        av_dict_set(&opts, "analyzeduration", "300000", 0);
    } else if (live) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        // Don't spend seconds estimating the framerate — the codec params arrive in
        // the SDP/first IDR, so cap the probe to show live video sooner. Do NOT set
        // fflags=nobuffer here: it discards the keyframe find_stream_info already
        // read, forcing the decode loop to wait a whole extra GOP (~4s on the sub
        // stream) for the next IDR before the first frame appears.
        av_dict_set(&opts, "analyzeduration", "500000", 0);
        av_dict_set(&opts, "probesize", "500000", 0);
    }
    int rc = avformat_open_input(&fmt, packetSource ? nullptr : url.constData(), ifmt, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(rc, buf, sizeof(buf));
        postState(s, State::Error, QString::fromUtf8(buf));
        return !packetSource && (live || s->retryOnError.load());
    }

    struct FmtGuard {
        AVFormatContext *ctx;
        ~FmtGuard() { avformat_close_input(&ctx); }
    } fmtGuard{fmt};

    interrupt.deadline = QDeadlineTimer(kOpenTimeoutMs);
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        postState(s, State::Error, QStringLiteral("could not read stream info"));
        return !packetSource && (live || s->retryOnError.load());
    }

    const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        postState(s, State::Error, QStringLiteral("no video stream"));
        return false;
    }
    AVStream *stream = fmt->streams[videoIndex];
    const QtVideo::Rotation rotation = streamRotation(stream, s->expectedSize);

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        postState(s, State::Error, QStringLiteral("unsupported codec"));
        return false;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    struct DecGuard {
        AVCodecContext *ctx;
        ~DecGuard() { avcodec_free_context(&ctx); }
    } decGuard{dec};

    avcodec_parameters_to_context(dec, stream->codecpar);
    dec->thread_count = 0; // auto

    AVBufferRef *hwCtx = nullptr;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
    if (hwDecodeEnabled())
        hwPixFmt = setupHwDecode(dec, codec, &hwCtx);
    struct HwGuard {
        AVBufferRef **ctx;
        ~HwGuard() { av_buffer_unref(ctx); }
    } hwGuard{&hwCtx};

    if (avcodec_open2(dec, codec, nullptr) < 0) {
        // Hardware path can fail at open on some driver/profile combos; retry
        // software before giving up.
        if (hwPixFmt != AV_PIX_FMT_NONE) {
            qCInfo(lcMedia) << redacted(source) << "hw decoder open failed — software fallback";
            av_buffer_unref(&dec->hw_device_ctx);
            av_buffer_unref(&hwCtx);
            dec->get_format = nullptr;
            dec->opaque = nullptr;
            hwPixFmt = AV_PIX_FMT_NONE;
            if (avcodec_open2(dec, codec, nullptr) < 0) {
                postState(s, State::Error, QStringLiteral("could not open decoder"));
                return false;
            }
        } else {
            postState(s, State::Error, QStringLiteral("could not open decoder"));
            return false;
        }
    }

    qCInfo(lcMedia) << redacted(source) << "opened:" << codec->name << stream->codecpar->width
                    << "x" << stream->codecpar->height
                    << (hwPixFmt != AV_PIX_FMT_NONE ? "[hw]" : "[sw]");

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *hwTransfer = av_frame_alloc();
    AVFrame *converted = av_frame_alloc();
    SwsContext *sws = nullptr;
    struct LoopGuard {
        AVPacket *p;
        AVFrame *f1, *f2, *f3;
        SwsContext **s;
        ~LoopGuard()
        {
            av_packet_free(&p);
            av_frame_free(&f1);
            av_frame_free(&f2);
            av_frame_free(&f3);
            sws_freeContext(*s);
        }
    } loopGuard{packet, frame, hwTransfer, converted, &sws};

    bool streaming = *streamingOut;
    qint64 playbackStartUs = 0;
    qint64 firstPtsUs = AV_NOPTS_VALUE;

    // Drain one decoded frame at a time; shared by the normal and flush paths.
    auto receiveFrames = [&]() {
        while (avcodec_receive_frame(dec, frame) == 0 && !s->abort.load()) {
            // Don't render frames the decoder knows are damaged (missing slices or
            // references — e.g. Reolink's flawed RTSP output for 8K duo mains).
            // A brief freeze beats a band of macroblock garbage.
            if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags) {
                av_frame_unref(frame);
                continue;
            }
            // Hardware frames live on the GPU; pull them into system memory.
            AVFrame *decoded = frame;
            if (hwPixFmt != AV_PIX_FMT_NONE && frame->format == hwPixFmt) {
                if (av_hwframe_transfer_data(hwTransfer, frame, 0) < 0) {
                    av_frame_unref(frame);
                    continue;
                }
                hwTransfer->pts = frame->pts; // transfer drops timing metadata
                decoded = hwTransfer;
            }

            const QVideoFrameFormat::PixelFormat qtFormat = mapPixelFormat(decoded->format);
            AVFrame *out = decoded;
            QVideoFrameFormat::PixelFormat outFormat = qtFormat;
            if (qtFormat == QVideoFrameFormat::Format_Invalid) {
                sws = sws_getCachedContext(sws, decoded->width, decoded->height,
                                           static_cast<AVPixelFormat>(decoded->format),
                                           decoded->width, decoded->height, AV_PIX_FMT_YUV420P,
                                           SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!sws) {
                    if (decoded == hwTransfer)
                        av_frame_unref(hwTransfer);
                    av_frame_unref(frame);
                    continue; // swscale can't ingest this pixel format; drop
                }
                converted->width = decoded->width;
                converted->height = decoded->height;
                converted->format = AV_PIX_FMT_YUV420P;
                if (av_frame_get_buffer(converted, 0) < 0) {
                    if (decoded == hwTransfer)
                        av_frame_unref(hwTransfer);
                    av_frame_unref(frame);
                    continue;
                }
                sws_scale(sws, decoded->data, decoded->linesize, 0, decoded->height,
                          converted->data, converted->linesize);
                out = converted;
                outFormat = QVideoFrameFormat::Format_YUV420P;
            }

            // File pacing: map the stream clock onto the wall clock (live is paced
            // by the network).
            if (!live && decoded->pts != AV_NOPTS_VALUE) {
                const qint64 ptsUs =
                    av_rescale_q(decoded->pts, stream->time_base, AVRational{1, 1000000});
                if (firstPtsUs == static_cast<qint64>(AV_NOPTS_VALUE) || ptsUs < firstPtsUs) {
                    firstPtsUs = ptsUs;
                    playbackStartUs = av_gettime_relative();
                }
                const qint64 waitUs = (playbackStartUs + (ptsUs - firstPtsUs)) - av_gettime_relative();
                if (waitUs > 0 && waitUs < 2000000)
                    abortableSleepUs(s.get(), waitUs);
            }

            if (emitFrame(s, out, outFormat, rotation)) {
                const qint64 n = s->framesDecoded.fetch_add(1) + 1;
                if (!streaming) {
                    streaming = true;
                    *streamingOut = true;
                    postState(s, State::Streaming, QString());
                    qCInfo(lcMedia) << redacted(source) << "first frame delivered";
                }
                if (n % 100 == 0)
                    postFramesTick(s);
            }
            if (out == converted)
                av_frame_unref(converted);
            if (decoded == hwTransfer)
                av_frame_unref(hwTransfer);
            av_frame_unref(frame);
        }
    };

    Recorder recorder;
    // Opens/closes the recorder to match the requested state. Called each packet.
    auto syncRecording = [&]() {
        if (s->recordRequested.load() && !recorder.active()) {
            QString path;
            {
                QMutexLocker lock(&s->recMutex);
                path = s->recPath;
            }
            if (recorder.open(path, stream)) {
                s->recording.store(true);
                postRecording(s, true, path, QString());
                qCInfo(lcMedia) << redacted(source) << "recording to" << path;
            } else {
                recorder.finalize();
                s->recordRequested.store(false);
                postRecording(s, false, QString(), QStringLiteral("could not open recording file"));
            }
        } else if (!s->recordRequested.load() && recorder.active()) {
            const QString saved = recorder.finalize();
            s->recording.store(false);
            postRecording(s, false, saved, saved.isEmpty()
                                               ? QStringLiteral("no frames captured")
                                               : QString());
        }
    };
    // Finalize on any session exit (EOF, reconnect, stop). Recording does not
    // survive a reconnect — it saves what it has and disarms.
    auto finalizeRecording = [&]() {
        if (!recorder.active())
            return;
        const QString saved = recorder.finalize();
        s->recording.store(false);
        s->recordRequested.store(false);
        postRecording(s, false, saved,
                      saved.isEmpty() ? QStringLiteral("no frames captured") : QString());
    };

    while (!s->abort.load()) {
        interrupt.deadline = QDeadlineTimer(kReadStallTimeoutMs);
        rc = av_read_frame(fmt, packet);
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(dec, nullptr); // flush: drain buffered frames
            receiveFrames();
            if (!live && s->loop.load()) {
                if (av_seek_frame(fmt, videoIndex, 0, AVSEEK_FLAG_BACKWARD) >= 0) {
                    avcodec_flush_buffers(dec);
                    firstPtsUs = AV_NOPTS_VALUE;
                    continue;
                }
                // Non-seekable input can't loop; fall through to a reconnect.
            }
            finalizeRecording();
            return live && !packetSource; // a custom source is one-shot: don't retry
        }
        if (rc < 0) {
            postState(s, State::Error, QStringLiteral("read error / stalled"));
            finalizeRecording();
            return !packetSource && (live || s->retryOnError.load());
        }
        if (packet->stream_index != videoIndex) {
            av_packet_unref(packet);
            continue;
        }
        syncRecording();
        if (recorder.active())
            recorder.write(packet); // stream-copy before the packet is consumed
        rc = avcodec_send_packet(dec, packet);
        av_packet_unref(packet);
        if (rc < 0 && rc != AVERROR(EAGAIN))
            continue; // tolerate bitstream hiccups on live sources
        receiveFrames();
    }
    finalizeRecording(); // clean stop
    return false;
}

void runWorker(std::shared_ptr<Session> s)
{
    int backoffMs = kBackoffStartMs;
    bool streaming = false;
    // Live sources reconnect indefinitely; a non-live retry (playback FLV on a
    // busy NVR) is capped so it can't loop forever.
    const bool live = isLiveUrl(s->source);
    int retriesLeft = 6;
    while (!s->abort.load()) {
        const qint64 framesBefore = s->framesDecoded.load();
        const bool retry = runSession(s, &streaming);
        if (s->abort.load() || !retry)
            break;
        if (s->framesDecoded.load() - framesBefore > 100) {
            backoffMs = kBackoffStartMs;
            retriesLeft = 6; // made progress; reset the budget
        }
        if (!live && --retriesLeft <= 0)
            break;
        postState(s, State::Connecting, QStringLiteral("reconnecting"));
        QDeadlineTimer wait(backoffMs);
        while (!wait.hasExpired() && !s->abort.load())
            QThread::msleep(50);
        backoffMs = qMin(backoffMs * 2, kBackoffCapMs);
    }
    if (!s->abort.load()) // terminal non-retryable end (EOF/no-video)
        postState(s, State::Stopped, QString());
}

} // namespace

// applyStateFromWorker runs on the GUI thread (posted via invokeMethod).
void StreamPlayer::applyStateFromWorker(State state, const QString &error)
{
    applyState(state, error);
}

void StreamPlayer::applyRecordingState(bool recording, const QString &path, const QString &error)
{
    if (m_recording != recording) {
        m_recording = recording;
        emit recordingChanged();
    }
    if (!recording) {
        if (!path.isEmpty())
            emit recordingSaved(path);
        else if (!error.isEmpty())
            emit recordingFailed(error);
    }
}

bool StreamPlayer::recording() const
{
    return m_recording;
}

bool StreamPlayer::startRecording(const QString &path)
{
    if (!m_session || m_state != State::Streaming)
        return false;
    QString out = path;
    if (out.isEmpty()) {
        const QString dir = Paths::recordingsDir();
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
        out = dir + QStringLiteral("/clip_") + stamp + QStringLiteral(".mp4");
    }
    {
        QMutexLocker lock(&m_session->recMutex);
        m_session->recPath = out;
    }
    m_session->recordRequested.store(true);
    return true;
}

void StreamPlayer::stopRecording()
{
    if (m_session)
        m_session->recordRequested.store(false);
}

StreamPlayer::StreamPlayer(QObject *parent) : QObject(parent) {}

StreamPlayer::~StreamPlayer()
{
    if (m_activeStop)
        m_activeStop();
    if (m_pendingStop)
        m_pendingStop();
    if (m_session) {
        // Null the back-pointer under the lock so no queued callback touches us
        // after this returns, then signal abort. The detached worker drops the
        // last Session reference on its own; we never join.
        {
            QMutexLocker lock(&m_session->backMutex);
            m_session->player = nullptr;
        }
        m_session->abort.store(true);
    }
}

void StreamPlayer::setSource(const QString &source)
{
    if (m_source == source && !m_pendingReader)
        return;
    stop();
    m_pendingReader = nullptr; // URL source supersedes any packet source
    m_pendingFormat.clear();
    m_pendingStop = nullptr;
    m_source = source;
    emit sourceChanged();
}

void StreamPlayer::setExpectedSize(const QSize &size)
{
    if (m_expectedSize == size)
        return;
    m_expectedSize = size;
    emit expectedSizeChanged();
}

void StreamPlayer::setPacketSource(std::function<int(unsigned char *, int)> reader,
                                   const QString &format, std::function<void()> onStop)
{
    stop();
    m_pendingReader = std::move(reader);
    m_pendingFormat = format;
    m_pendingStop = std::move(onStop);
    m_source.clear();
    emit sourceChanged();
}

QVideoSink *StreamPlayer::videoSink() const
{
    return m_session ? m_session->sink.data() : m_pendingSink.data();
}

void StreamPlayer::setVideoSink(QVideoSink *sink)
{
    if (m_pendingSink == sink)
        return;
    m_pendingSink = sink;
    if (m_session) {
        QMutexLocker lock(&m_session->sinkMutex);
        m_session->sink = sink;
    }
    emit videoSinkChanged();
}

qint64 StreamPlayer::framesDecoded() const
{
    return m_session ? m_session->framesDecoded.load() : 0;
}

void StreamPlayer::setLoop(bool loop)
{
    if (m_loop == loop)
        return;
    m_loop = loop;
    if (m_session)
        m_session->loop.store(loop);
    emit loopChanged();
}

void StreamPlayer::setRetryOnError(bool v)
{
    if (m_retryOnError == v)
        return;
    m_retryOnError = v;
    if (m_session)
        m_session->retryOnError.store(v);
    emit retryOnErrorChanged();
}

void StreamPlayer::start()
{
    if (m_source.isEmpty() && !m_pendingReader)
        return;
    stop();

    auto s = std::make_shared<Session>();
    s->player = this;
    s->source = m_source;
    s->readPacket = m_pendingReader;
    s->forcedFormat = m_pendingFormat;
    s->expectedSize = m_expectedSize;
    s->loop.store(m_loop);
    s->retryOnError.store(m_retryOnError);
    {
        QMutexLocker lock(&s->sinkMutex);
        s->sink = m_pendingSink;
    }
    m_session = s;
    // Adopt the pending source's teardown as the running session's teardown, so the
    // stop() above (which tears down the previous session) doesn't kill this source.
    m_activeStop = std::move(m_pendingStop);
    m_pendingStop = nullptr;
    applyState(State::Connecting, QString());
    std::thread(runWorker, s).detach();
}

void StreamPlayer::stop()
{
    // Unblock and tear down the running session's custom packet source (native
    // Baichuan). Its read() may be blocking the decode worker; this wakes it so
    // avformat unwinds. The pending (not-yet-started) source is left untouched.
    if (m_activeStop) {
        m_activeStop();
        m_activeStop = nullptr;
    }
    if (!m_session)
        return;
    {
        QMutexLocker lock(&m_session->backMutex);
        m_session->player = nullptr; // stale callbacks become no-ops
    }
    m_session->abort.store(true);
    m_session.reset();
    if (m_recording) {
        m_recording = false;
        emit recordingChanged();
    }
    if (m_state != State::Idle)
        applyState(State::Stopped, QString());
}

void StreamPlayer::applyState(State state, const QString &error)
{
    if (m_errorString != error) {
        m_errorString = error;
        emit errorStringChanged();
    }
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

} // namespace rl
