#include "StreamPlayer.h"

#include "core/Log.h"

#include <QDeadlineTimer>
#include <QThread>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
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
           source.startsWith(QLatin1String("rtmp://"));
}

// Never log credentials embedded in an RTSP/RTMP URL (finding: password-in-logs).
QString redacted(const QString &source)
{
    const QUrl u(source);
    if (!u.isValid() || u.userInfo().isEmpty())
        return source;
    return u.toDisplayString(QUrl::RemoveUserInfo);
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
               QVideoFrameFormat::PixelFormat outFormat)
{
    QVideoFrameFormat format(QSize(out->width, out->height), outFormat);
    // Full-range (JPEG) YUV must be tagged or the sink renders it as limited range.
    if (out->color_range == AVCOL_RANGE_JPEG || out->format == AV_PIX_FMT_YUVJ420P)
        format.setColorRange(QVideoFrameFormat::ColorRange_Full);

    QVideoFrame videoFrame(format);
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

// One open→decode session. Returns true when the caller should retry (reconnect).
bool runSession(const std::shared_ptr<Session> &s, bool *streamingOut)
{
    const QString source = s->source;
    const QByteArray url = source.toUtf8();
    const bool live = isLiveUrl(source);

    InterruptContext interrupt{s.get(), QDeadlineTimer(kOpenTimeoutMs)};

    AVFormatContext *fmt = avformat_alloc_context();
    fmt->interrupt_callback.callback = &InterruptContext::callback;
    fmt->interrupt_callback.opaque = &interrupt;

    AVDictionary *opts = nullptr;
    if (live) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        av_dict_set(&opts, "flags", "low_delay", 0);
    }
    int rc = avformat_open_input(&fmt, url.constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(rc, buf, sizeof(buf));
        postState(s, State::Error, QString::fromUtf8(buf));
        return live;
    }

    struct FmtGuard {
        AVFormatContext *ctx;
        ~FmtGuard() { avformat_close_input(&ctx); }
    } fmtGuard{fmt};

    interrupt.deadline = QDeadlineTimer(kOpenTimeoutMs);
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        postState(s, State::Error, QStringLiteral("could not read stream info"));
        return live;
    }

    const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        postState(s, State::Error, QStringLiteral("no video stream"));
        return false;
    }
    AVStream *stream = fmt->streams[videoIndex];

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
    if (avcodec_open2(dec, codec, nullptr) < 0) {
        postState(s, State::Error, QStringLiteral("could not open decoder"));
        return false;
    }

    qCInfo(lcMedia) << redacted(source) << "opened:" << codec->name << stream->codecpar->width
                    << "x" << stream->codecpar->height;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *converted = av_frame_alloc();
    SwsContext *sws = nullptr;
    struct LoopGuard {
        AVPacket *p;
        AVFrame *f1, *f2;
        SwsContext **s;
        ~LoopGuard()
        {
            av_packet_free(&p);
            av_frame_free(&f1);
            av_frame_free(&f2);
            sws_freeContext(*s);
        }
    } loopGuard{packet, frame, converted, &sws};

    bool streaming = *streamingOut;
    qint64 playbackStartUs = 0;
    qint64 firstPtsUs = AV_NOPTS_VALUE;

    // Drain one decoded frame at a time; shared by the normal and flush paths.
    auto receiveFrames = [&]() {
        while (avcodec_receive_frame(dec, frame) == 0 && !s->abort.load()) {
            const QVideoFrameFormat::PixelFormat qtFormat = mapPixelFormat(frame->format);
            AVFrame *out = frame;
            QVideoFrameFormat::PixelFormat outFormat = qtFormat;
            if (qtFormat == QVideoFrameFormat::Format_Invalid) {
                sws = sws_getCachedContext(sws, frame->width, frame->height,
                                           static_cast<AVPixelFormat>(frame->format), frame->width,
                                           frame->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                                           nullptr, nullptr);
                if (!sws) {
                    av_frame_unref(frame);
                    continue; // swscale can't ingest this pixel format; drop
                }
                converted->width = frame->width;
                converted->height = frame->height;
                converted->format = AV_PIX_FMT_YUV420P;
                if (av_frame_get_buffer(converted, 0) < 0) {
                    av_frame_unref(frame);
                    continue;
                }
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, converted->data,
                          converted->linesize);
                out = converted;
                outFormat = QVideoFrameFormat::Format_YUV420P;
            }

            // File pacing: map the stream clock onto the wall clock (live is paced
            // by the network).
            if (!live && frame->pts != AV_NOPTS_VALUE) {
                const qint64 ptsUs =
                    av_rescale_q(frame->pts, stream->time_base, AVRational{1, 1000000});
                if (firstPtsUs == static_cast<qint64>(AV_NOPTS_VALUE) || ptsUs < firstPtsUs) {
                    firstPtsUs = ptsUs;
                    playbackStartUs = av_gettime_relative();
                }
                const qint64 waitUs = (playbackStartUs + (ptsUs - firstPtsUs)) - av_gettime_relative();
                if (waitUs > 0 && waitUs < 2000000)
                    abortableSleepUs(s.get(), waitUs);
            }

            if (emitFrame(s, out, outFormat)) {
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
            av_frame_unref(frame);
        }
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
            return live;
        }
        if (rc < 0) {
            postState(s, State::Error, QStringLiteral("read error / stalled"));
            return live;
        }
        if (packet->stream_index != videoIndex) {
            av_packet_unref(packet);
            continue;
        }
        rc = avcodec_send_packet(dec, packet);
        av_packet_unref(packet);
        if (rc < 0 && rc != AVERROR(EAGAIN))
            continue; // tolerate bitstream hiccups on live sources
        receiveFrames();
    }
    return false; // clean stop
}

void runWorker(std::shared_ptr<Session> s)
{
    int backoffMs = kBackoffStartMs;
    bool streaming = false;
    while (!s->abort.load()) {
        const qint64 framesBefore = s->framesDecoded.load();
        const bool retry = runSession(s, &streaming);
        if (s->abort.load() || !retry)
            break;
        if (s->framesDecoded.load() - framesBefore > 100)
            backoffMs = kBackoffStartMs;
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

StreamPlayer::StreamPlayer(QObject *parent) : QObject(parent) {}

StreamPlayer::~StreamPlayer()
{
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
    if (m_source == source)
        return;
    stop();
    m_source = source;
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

void StreamPlayer::start()
{
    if (m_source.isEmpty())
        return;
    stop();

    auto s = std::make_shared<Session>();
    s->player = this;
    s->source = m_source;
    s->loop.store(m_loop);
    {
        QMutexLocker lock(&s->sinkMutex);
        s->sink = m_pendingSink;
    }
    m_session = s;
    applyState(State::Connecting, QString());
    std::thread(runWorker, s).detach();
}

void StreamPlayer::stop()
{
    if (!m_session)
        return;
    {
        QMutexLocker lock(&m_session->backMutex);
        m_session->player = nullptr; // stale callbacks become no-ops
    }
    m_session->abort.store(true);
    m_session.reset();
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
