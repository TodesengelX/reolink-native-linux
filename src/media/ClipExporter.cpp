#include "ClipExporter.h"

#include "core/Log.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QThread>

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

namespace rl {

namespace {

constexpr qint64 kIdleTimeoutMs = 30000;   // no frame for this long: the device is done
constexpr qint64 kDefaultFrameUs = 66667;  // 15 fps, until a real frame interval is seen
constexpr qint64 kMaxFrameGapUs = 5000000; // a larger jump is a gap between recordings

// The parameter-set NAL units (H.264 SPS/PPS, H.265 VPS/SPS/PPS) of an Annex-B
// access unit, as Annex-B. The MP4 muxer builds avcC/hvcC from them.
QByteArray parameterSets(const QByteArray &annexB, bool hevc)
{
    const auto *d = reinterpret_cast<const uchar *>(annexB.constData());
    const int n = annexB.size();
    QVector<int> starts; // first byte after each start code
    for (int i = 0; i + 3 <= n;) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            starts << i + 3;
            i += 3;
        } else {
            ++i;
        }
    }
    QByteArray out;
    for (int k = 0; k < starts.size(); ++k) {
        const int s = starts[k];
        int e = k + 1 < starts.size() ? starts[k + 1] - 3 : n;
        while (e > s && d[e - 1] == 0) // a 4-byte start code's leading zero
            --e;
        if (s >= e)
            continue;
        const int type = hevc ? (d[s] >> 1) & 0x3f : d[s] & 0x1f;
        const bool ps = hevc ? (type >= 32 && type <= 34) : (type == 7 || type == 8);
        if (ps) {
            out.append("\x00\x00\x00\x01", 4);
            out.append(annexB.mid(s, e - s));
        }
    }
    return out;
}

// Picture size from the bitstream's SPS, via libavcodec's parser (no decoder).
QSize bitstreamSize(AVCodecID id, const QByteArray &bytes)
{
    AVCodecParserContext *pc = av_parser_init(id);
    AVCodecContext *cc = avcodec_alloc_context3(nullptr);
    QSize size;
    if (pc && cc) {
        QByteArray buf = bytes;
        buf.append(QByteArray(AV_INPUT_BUFFER_PADDING_SIZE, '\0'));
        const auto *p = reinterpret_cast<const uint8_t *>(buf.constData());
        int left = bytes.size();
        uint8_t *out = nullptr;
        int outSize = 0;
        while (left > 0) {
            const int used = av_parser_parse2(pc, cc, &out, &outSize, p, left, AV_NOPTS_VALUE,
                                              AV_NOPTS_VALUE, 0);
            if (used <= 0)
                break;
            p += used;
            left -= used;
        }
        av_parser_parse2(pc, cc, &out, &outSize, nullptr, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        const int w = pc->width > 0 ? pc->width : cc->width;
        const int h = pc->height > 0 ? pc->height : cc->height;
        if (w > 0 && h > 0)
            size = QSize(w, h);
    }
    if (pc)
        av_parser_close(pc);
    avcodec_free_context(&cc);
    return size;
}

QString averr(int rc)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof buf);
    return QString::fromUtf8(buf);
}

// Owned by run(); touched by the Baichuan worker (frame sink) until the client
// is joined, then by run() alone.
struct ExportState {
    const ClipExporter::Request *req = nullptr;
    const std::function<void(double)> *progress = nullptr;
    QElapsedTimer *clock = nullptr;

    AVFormatContext *oc = nullptr;
    AVStream *st = nullptr;
    bool headerWritten = false;

    std::atomic<qint64> frames{0};
    std::atomic<qint64> lastFrameMs{0};
    std::atomic<bool> done{false};    // reached endEpoch
    std::atomic<bool> failed{false};
    QString error;                    // set by the sink before `failed`

    bool tzKnown = false;
    qint64 tzOffset = 0;              // frame clock minus wall clock, whole quarter-hours
    bool haveMicros = false;
    quint32 lastMicros = 0;
    qint64 lastDeltaUs = kDefaultFrameUs;
    qint64 streamUs = 0;
    qint64 lastProgressMs = -1000;
    qint64 lastLogMs = 0;

    bool open(const BcMediaParser::VideoFrame &f)
    {
        const bool hevc = f.codec == BcMediaParser::Codec::H265;
        const AVCodecID id = hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
        const QByteArray ps = parameterSets(f.annexB, hevc);
        if (ps.isEmpty())
            return false; // wait for a key frame that carries its parameter sets
        QSize size = bitstreamSize(id, f.annexB);
        if (size.isEmpty())
            size = req->declaredSize;

        const QByteArray path = req->path.toUtf8();
        int rc = avformat_alloc_output_context2(&oc, nullptr, "mp4", path.constData());
        if (rc < 0 || !oc) {
            error = QStringLiteral("mp4 muxer: %1").arg(averr(rc));
            return fail();
        }
        st = avformat_new_stream(oc, nullptr);
        if (!st) {
            error = QStringLiteral("mp4 muxer: no stream");
            return fail();
        }
        st->time_base = AVRational{1, 90000};
        AVCodecParameters *par = st->codecpar;
        par->codec_type = AVMEDIA_TYPE_VIDEO;
        par->codec_id = id;
        par->width = size.width();
        par->height = size.height();
        par->extradata = static_cast<uint8_t *>(av_mallocz(ps.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        memcpy(par->extradata, ps.constData(), ps.size());
        par->extradata_size = ps.size();

        rc = avio_open(&oc->pb, path.constData(), AVIO_FLAG_WRITE);
        if (rc < 0) {
            error = QStringLiteral("cannot write %1: %2").arg(req->path, averr(rc));
            return fail();
        }
        rc = avformat_write_header(oc, nullptr);
        if (rc < 0) {
            error = QStringLiteral("mp4 header: %1").arg(averr(rc));
            return fail();
        }
        headerWritten = true;
        qCInfo(lcMedia) << "clip export:" << (hevc ? "hevc" : "h264") << size << "->" << req->path;
        return true;
    }

    bool fail()
    {
        failed.store(true);
        return false;
    }

    // Monotonic stream time from the frames' 32-bit microsecond clock: it wraps
    // every ~71 minutes, and jumps at gaps between recordings, which are closed up.
    qint64 nextTimeUs(const BcMediaParser::VideoFrame &f)
    {
        if (!haveMicros || f.microseconds == 0) {
            haveMicros = f.microseconds != 0;
            lastMicros = f.microseconds;
            return frames.load() == 0 ? 0 : streamUs + lastDeltaUs;
        }
        qint64 d = qint64(f.microseconds) - qint64(lastMicros);
        if (d < -(qint64(1) << 31))
            d += qint64(1) << 32; // wrapped
        lastMicros = f.microseconds;
        if (d > 0 && d <= kMaxFrameGapUs)
            lastDeltaUs = d;
        else
            d = lastDeltaUs;
        return streamUs + d;
    }

    void onFrame(const BcMediaParser::VideoFrame &f)
    {
        if (done.load() || failed.load())
            return;
        lastFrameMs.store(clock->elapsed());

        // Wall-clock time of the frame, to know when the range is covered. Only
        // trusted when it is near the requested time: on an RLN16-410 the field
        // holds the same unrelated number (16804607) on every frame. Otherwise
        // the frames' own stream clock measures the range.
        double wall = -1;
        if (f.time != 0) {
            const qint64 off = qint64(f.time) - req->startEpoch;
            if (!tzKnown && qAbs(off) <= 14 * 3600) {
                // A device clock in local time is off by whole quarter-hours.
                tzOffset = qRound64(double(off) / 900.0) * 900;
                tzKnown = true;
            }
            if (tzKnown) {
                wall = double(qint64(f.time) - tzOffset);
                if (qAbs(wall - double(req->startEpoch)) > 2 * 86400.0)
                    wall = -1;
            }
        }

        if (!headerWritten) {
            if (!f.keyFrame || !open(f))
                return;
        }

        const qint64 us = nextTimeUs(f);
        const double covered = wall >= 0 ? wall : req->startEpoch + us / 1e6;
        if (covered >= req->endEpoch) {
            done.store(true);
            return;
        }

        AVPacket *pkt = av_packet_alloc();
        if (!pkt || av_new_packet(pkt, f.annexB.size()) < 0) {
            av_packet_free(&pkt);
            error = QStringLiteral("out of memory");
            fail();
            return;
        }
        memcpy(pkt->data, f.annexB.constData(), f.annexB.size());
        pkt->stream_index = st->index;
        pkt->pts = pkt->dts = av_rescale_q(us, AVRational{1, 1000000}, st->time_base);
        if (f.keyFrame)
            pkt->flags |= AV_PKT_FLAG_KEY;
        const int rc = av_write_frame(oc, pkt);
        av_packet_free(&pkt);
        if (rc < 0) {
            error = QStringLiteral("write: %1").arg(averr(rc));
            fail();
            return;
        }
        streamUs = us;
        frames.fetch_add(1);

        const qint64 now = clock->elapsed();
        if (now - lastProgressMs >= 250) {
            lastProgressMs = now;
            const double span = double(req->endEpoch - req->startEpoch);
            const double frac = qBound(0.0, (covered - req->startEpoch) / span, 1.0);
            if (*progress)
                (*progress)(frac);
            if (now - lastLogMs >= 10000) {
                lastLogMs = now;
                qCDebug(lcMedia) << "clip export at" << frac << "covered" << qint64(covered)
                                 << "frame time" << f.time << "tz" << tzOffset << "stream s" << us / 1e6;
            }
        }
    }

    // Close the file; with no frames written, remove it.
    void finish()
    {
        if (oc) {
            if (headerWritten)
                av_write_trailer(oc);
            if (oc->pb)
                avio_closep(&oc->pb);
            avformat_free_context(oc);
            oc = nullptr;
        }
        if (frames.load() == 0)
            QFile::remove(req->path);
    }
};

} // namespace

ClipExporter::Result ClipExporter::run(const Request &req, const std::atomic<bool> &cancel,
                                       const std::function<void(double)> &progress)
{
    Result r;
    if (req.endEpoch <= req.startEpoch || req.params.startEpoch <= 0) {
        r.error = QStringLiteral("empty range");
        return r;
    }

    QElapsedTimer clock;
    clock.start();
    ExportState s;
    s.req = &req;
    s.progress = &progress;
    s.clock = &clock;

    auto client = std::make_shared<BaichuanClient>(req.params);
    client->setSpeed(0); // unpaced: as fast as the device sends
    client->setFrameSink([&s](const BcMediaParser::VideoFrame &f) { s.onFrame(f); });
    client->start();

    while (true) {
        QThread::msleep(100);
        if (cancel.load()) {
            r.cancelled = true;
            break;
        }
        if (s.done.load() || s.failed.load())
            break;
        if (client->finished()) { // the device closed the stream
            r.endedEarly = true;
            break;
        }
        if (clock.elapsed() - s.lastFrameMs.load() > kIdleTimeoutMs) {
            r.endedEarly = true;
            break;
        }
    }
    client->stop();
    client.reset(); // joins the worker: the sink is idle from here on

    s.finish();
    r.frames = s.frames.load();
    r.mediaSecs = s.streamUs / 1e6;
    r.bytes = r.frames > 0 ? QFileInfo(req.path).size() : 0;
    if (s.failed.load()) {
        r.error = s.error;
        r.ok = r.frames > 0; // what was written before the failure is still a valid file
    } else if (r.frames == 0) {
        r.error = r.cancelled ? QStringLiteral("cancelled")
                              : QStringLiteral("no recording in this range");
    } else {
        r.ok = true;
    }
    qCInfo(lcMedia) << "clip export finished:" << r.frames << "frames," << r.mediaSecs << "s,"
                    << r.bytes << "bytes in" << clock.elapsed() / 1000.0 << "s"
                    << (r.cancelled ? "(cancelled)" : r.endedEarly ? "(device stopped early)" : "")
                    << r.error;
    return r;
}

} // namespace rl
