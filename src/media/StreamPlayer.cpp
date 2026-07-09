#include "StreamPlayer.h"

#include "core/Log.h"

#include <QDeadlineTimer>
#include <QThread>
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

bool isLiveUrl(const QString &source)
{
    return source.startsWith(QLatin1String("rtsp://")) ||
           source.startsWith(QLatin1String("rtmp://"));
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

} // namespace

// libavformat blocks inside network reads; this callback lets stop() and the
// stall watchdog break it out.
struct InterruptContext {
    StreamPlayer *player = nullptr;
    QDeadlineTimer deadline;

    static int callback(void *opaque)
    {
        auto *ctx = static_cast<InterruptContext *>(opaque);
        if (ctx->player->m_abort.load(std::memory_order_relaxed))
            return 1;
        return ctx->deadline.hasExpired() ? 1 : 0;
    }
};

StreamPlayer::StreamPlayer(QObject *parent) : QObject(parent) {}

StreamPlayer::~StreamPlayer()
{
    stop();
}

void StreamPlayer::setSource(const QString &source)
{
    if (m_source == source)
        return;
    const bool wasRunning = m_thread.joinable();
    stop();
    m_source = source;
    emit sourceChanged();
    if (wasRunning && !source.isEmpty())
        start();
}

QVideoSink *StreamPlayer::videoSink() const
{
    QMutexLocker lock(&m_sinkMutex);
    return m_sink;
}

void StreamPlayer::setVideoSink(QVideoSink *sink)
{
    {
        QMutexLocker lock(&m_sinkMutex);
        if (m_sink == sink)
            return;
        m_sink = sink;
    }
    emit videoSinkChanged();
}

void StreamPlayer::setLoop(bool loop)
{
    if (m_loop == loop)
        return;
    m_loop = loop;
    emit loopChanged();
}

void StreamPlayer::start()
{
    if (m_thread.joinable() || m_source.isEmpty())
        return;
    m_abort.store(false);
    m_framesDecoded.store(0);
    setStateFromWorker(State::Connecting);
    m_thread = std::thread([this] { workerLoop(); });
}

void StreamPlayer::stop()
{
    m_abort.store(true);
    if (m_thread.joinable())
        m_thread.join();
    if (m_state != State::Idle)
        setStateFromWorker(State::Stopped);
}

void StreamPlayer::setStateFromWorker(State state, const QString &error)
{
    // Worker thread → GUI thread; also called directly on the GUI thread by
    // start()/stop(), where invokeMethod degenerates to a direct call.
    QMetaObject::invokeMethod(
        this,
        [this, state, error] {
            if (m_errorString != error) {
                m_errorString = error;
                emit errorStringChanged();
            }
            if (m_state != state) {
                m_state = state;
                emit stateChanged();
            }
        },
        m_thread.joinable() && QThread::currentThread() != thread() ? Qt::QueuedConnection
                                                                     : Qt::AutoConnection);
}

void StreamPlayer::deliverFrame(const QVideoFrame &frame)
{
    QMutexLocker lock(&m_sinkMutex);
    if (m_sink)
        m_sink->setVideoFrame(frame);
}

void StreamPlayer::workerLoop()
{
    int backoffMs = kBackoffStartMs;
    while (!m_abort.load()) {
        QString error;
        const qint64 framesBefore = m_framesDecoded.load();
        const bool retryable = runSession(&error);
        if (m_abort.load() || !retryable)
            break;
        // A session that streamed for a while earns a fresh backoff.
        if (m_framesDecoded.load() - framesBefore > 100)
            backoffMs = kBackoffStartMs;
        setStateFromWorker(State::Connecting,
                           error.isEmpty() ? QString()
                                           : QStringLiteral("%1 — reconnecting").arg(error));
        qCInfo(lcMedia) << m_source << "reconnecting in" << backoffMs << "ms:" << error;
        QDeadlineTimer wait(backoffMs);
        while (!wait.hasExpired() && !m_abort.load())
            QThread::msleep(50);
        backoffMs = qMin(backoffMs * 2, kBackoffCapMs);
    }
}

bool StreamPlayer::runSession(QString *sessionError)
{
    const QByteArray url = m_source.toUtf8();
    const bool live = isLiveUrl(m_source);

    InterruptContext interrupt{this, QDeadlineTimer(kOpenTimeoutMs)};

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
        *sessionError = QString::fromUtf8(buf);
        setStateFromWorker(State::Error, *sessionError);
        return live; // retry live sources; a bad file won't get better
    }

    struct FmtGuard {
        AVFormatContext *ctx;
        ~FmtGuard() { avformat_close_input(&ctx); }
    } fmtGuard{fmt};

    interrupt.deadline = QDeadlineTimer(kOpenTimeoutMs);
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        *sessionError = QStringLiteral("could not read stream info");
        setStateFromWorker(State::Error, *sessionError);
        return live;
    }

    const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        *sessionError = QStringLiteral("no video stream");
        setStateFromWorker(State::Error, *sessionError);
        return false;
    }
    AVStream *stream = fmt->streams[videoIndex];

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        *sessionError = QStringLiteral("unsupported codec");
        setStateFromWorker(State::Error, *sessionError);
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
        *sessionError = QStringLiteral("could not open decoder");
        setStateFromWorker(State::Error, *sessionError);
        return false;
    }

    qCInfo(lcMedia) << m_source << "opened:" << codec->name << stream->codecpar->width << "x"
                    << stream->codecpar->height;

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

    bool streaming = false;
    // File pacing: map the stream clock onto the wall clock from the first frame.
    qint64 playbackStartUs = 0;
    qint64 firstPtsUs = AV_NOPTS_VALUE;

    while (!m_abort.load()) {
        interrupt.deadline = QDeadlineTimer(kReadStallTimeoutMs);
        rc = av_read_frame(fmt, packet);
        if (rc == AVERROR_EOF) {
            if (!live && m_loop) {
                avcodec_flush_buffers(dec);
                av_seek_frame(fmt, videoIndex, 0, AVSEEK_FLAG_BACKWARD);
                firstPtsUs = AV_NOPTS_VALUE;
                continue;
            }
            *sessionError = QStringLiteral("end of stream");
            return live; // live EOF = server closed → reconnect
        }
        if (rc < 0) {
            *sessionError = QStringLiteral("read error / stalled");
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

        while (avcodec_receive_frame(dec, frame) == 0 && !m_abort.load()) {
            const QVideoFrameFormat::PixelFormat qtFormat = mapPixelFormat(frame->format);
            AVFrame *out = frame;
            if (qtFormat == QVideoFrameFormat::Format_Invalid) {
                // Uncommon decoder output (10-bit, 4:2:2, …) — normalize to YUV420P.
                sws = sws_getCachedContext(sws, frame->width, frame->height,
                                           static_cast<AVPixelFormat>(frame->format),
                                           frame->width, frame->height, AV_PIX_FMT_YUV420P,
                                           SWS_BILINEAR, nullptr, nullptr, nullptr);
                converted->width = frame->width;
                converted->height = frame->height;
                converted->format = AV_PIX_FMT_YUV420P;
                if (av_frame_get_buffer(converted, 0) < 0)
                    continue;
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, converted->data,
                          converted->linesize);
                out = converted;
            }

            const QVideoFrameFormat::PixelFormat outFormat =
                out == frame ? qtFormat : QVideoFrameFormat::Format_YUV420P;
            QVideoFrame videoFrame(QVideoFrameFormat(QSize(out->width, out->height), outFormat));
            if (videoFrame.map(QVideoFrame::WriteOnly)) {
                for (int plane = 0; plane < videoFrame.planeCount(); ++plane) {
                    const int planeHeight = plane == 0 ? out->height : out->height / 2;
                    const int rowBytes =
                        qMin(videoFrame.bytesPerLine(plane), out->linesize[plane]);
                    for (int y = 0; y < planeHeight; ++y)
                        memcpy(videoFrame.bits(plane) + y * videoFrame.bytesPerLine(plane),
                               out->data[plane] + y * out->linesize[plane],
                               static_cast<size_t>(rowBytes));
                }
                videoFrame.unmap();

                if (!live && frame->pts != AV_NOPTS_VALUE) {
                    const qint64 ptsUs =
                        av_rescale_q(frame->pts, stream->time_base, AVRational{1, 1000000});
                    if (firstPtsUs == AV_NOPTS_VALUE ||
                        static_cast<qint64>(firstPtsUs) > ptsUs) {
                        firstPtsUs = ptsUs;
                        playbackStartUs = av_gettime_relative();
                    }
                    const qint64 due = playbackStartUs + (ptsUs - firstPtsUs);
                    const qint64 waitUs = due - av_gettime_relative();
                    if (waitUs > 0 && waitUs < 2000000)
                        av_usleep(static_cast<unsigned>(waitUs));
                }

                deliverFrame(videoFrame);
                const qint64 n = m_framesDecoded.fetch_add(1) + 1;
                if (!streaming) {
                    streaming = true;
                    setStateFromWorker(State::Streaming);
                    qCInfo(lcMedia) << m_source << "first frame delivered";
                }
                if (n % 100 == 0)
                    QMetaObject::invokeMethod(this, &StreamPlayer::framesDecodedChanged,
                                              Qt::QueuedConnection);
            }
            if (out == converted)
                av_frame_unref(converted);
        }
    }
    return false; // clean stop
}

} // namespace rl
