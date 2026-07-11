#include "BaichuanClient.h"

#include "protocol/BcCrypto.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QThread>
#include <QtEndian>

extern "C" {
#include <libavutil/error.h>
}

namespace rl {

namespace {

constexpr quint32 kMagic = 0x0ABCDEF0;
constexpr quint32 kMagicRev = 0x0FEDCBA0;
constexpr int kMaxRing = 8 * 1024 * 1024; // backpressure cap for the decoded buffer

void putLE32(QByteArray &b, quint32 v)
{
    char t[4];
    qToLittleEndian(v, t);
    b.append(t, 4);
}
void putLE16(QByteArray &b, quint16 v)
{
    char t[2];
    qToLittleEndian(v, t);
    b.append(t, 2);
}
quint32 getLE32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}
quint16 getLE16(const char *p)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

struct Msg {
    quint32 id = 0;
    quint8 channel = 0;
    quint16 msgNum = 0;
    quint16 code = 0;
    quint16 cls = 0;
    quint32 payloadOffset = 0;
    QByteArray body;
};

// Serialize one BC message. `cls` selects legacy(0x6514, 20-byte) vs modern.
QByteArray frame(quint32 id, quint8 ch, quint8 streamType, quint16 msgNum, quint16 code,
                 quint16 cls, const QByteArray &body)
{
    const bool hasPayloadOffset = (cls == 0x6414 || cls == 0x0000);
    QByteArray m;
    putLE32(m, kMagic);
    putLE32(m, id);
    putLE32(m, static_cast<quint32>(body.size()));
    m.append(static_cast<char>(ch));
    m.append(static_cast<char>(streamType));
    putLE16(m, msgNum);
    putLE16(m, code);
    putLE16(m, cls);
    if (hasPayloadOffset)
        putLE32(m, 0); // no Extension on anything we send
    m.append(body);
    return m;
}

// Pull one complete message from `buf` (consuming it). False if incomplete.
bool parseMessage(QByteArray &buf, Msg &m)
{
    if (buf.size() < 20)
        return false;
    const quint32 magic = getLE32(buf.constData());
    if (magic != kMagic && magic != kMagicRev)
        return false; // caller resyncs
    const quint32 bodyLen = getLE32(buf.constData() + 8);
    const quint16 cls = getLE16(buf.constData() + 18);
    const bool hasPayloadOffset = (cls == 0x6414 || cls == 0x0000);
    const int headerLen = hasPayloadOffset ? 24 : 20;
    if (buf.size() < headerLen + static_cast<int>(bodyLen))
        return false;
    m.id = getLE32(buf.constData() + 4);
    m.channel = static_cast<quint8>(buf[12]);
    m.msgNum = getLE16(buf.constData() + 14);
    m.code = getLE16(buf.constData() + 16);
    m.cls = cls;
    m.payloadOffset = hasPayloadOffset ? getLE32(buf.constData() + 20) : 0;
    m.body = buf.mid(headerLen, static_cast<int>(bodyLen));
    buf.remove(0, headerLen + static_cast<int>(bodyLen));
    return true;
}

QByteArray timeXml(const QString &tag, const QDateTime &dt)
{
    const QDate d = dt.date();
    const QTime t = dt.time();
    return QStringLiteral("<%1><year>%2</year><month>%3</month><day>%4</day>"
                          "<hour>%5</hour><minute>%6</minute><second>%7</second></%1>")
        .arg(tag)
        .arg(d.year())
        .arg(d.month())
        .arg(d.day())
        .arg(t.hour())
        .arg(t.minute())
        .arg(t.second())
        .toUtf8();
}

} // namespace

BaichuanClient::BaichuanClient(Params params) : m_p(std::move(params)) {}

BaichuanClient::~BaichuanClient()
{
    stop();
    // Join (not detach): the worker touches our members, so it must finish before
    // they are destroyed. It never holds a reference to us, so this can't
    // self-deadlock, and every network wait is short + abort-checked, so the join
    // is bounded (sub-second). The last owning reference is dropped by the
    // StreamPlayer decode worker, so this runs off the GUI thread in practice.
    if (m_thread.joinable())
        m_thread.join();
}

void BaichuanClient::start()
{
    if (m_started)
        return;
    m_started = true;
    m_thread = std::thread([this] { run(); });
}

void BaichuanClient::stop()
{
    m_abort.store(true);
    QMutexLocker lock(&m_mutex);
    m_finished = true;
    m_cond.wakeAll();
}

QString BaichuanClient::ffmpegFormat()
{
    QMutexLocker lock(&m_mutex);
    return m_format;
}

int BaichuanClient::read(unsigned char *buf, int size)
{
    QMutexLocker lock(&m_mutex);
    while (m_ring.isEmpty() && !m_finished && !m_abort.load())
        m_cond.wait(&m_mutex);
    if (m_abort.load() && m_ring.isEmpty())
        return AVERROR_EOF;
    if (m_ring.isEmpty())
        return m_finished ? AVERROR_EOF : 0;
    const int n = qMin(size, static_cast<int>(m_ring.size()));
    memcpy(buf, m_ring.constData(), n);
    m_ring.remove(0, n);
    m_cond.wakeAll(); // unblock the producer if it was backpressured
    return n;
}

void BaichuanClient::pushAnnexB(const QByteArray &bytes)
{
    QMutexLocker lock(&m_mutex);
    // Backpressure: block the producer if the reader falls far behind.
    while (m_ring.size() > kMaxRing && !m_abort.load())
        m_cond.wait(&m_mutex);
    if (m_abort.load())
        return;
    m_ring.append(bytes);
    m_cond.wakeAll();
}

void BaichuanClient::run()
{
    QTcpSocket sock;
    QByteArray aesKey;
    if (connectAndLogin(sock, aesKey) && !m_abort.load()) {
        if (startStream(sock, aesKey))
            pumpMedia(sock, aesKey, /*streamMsgNum=*/20);
    }
    QMutexLocker lock(&m_mutex);
    m_finished = true;
    m_cond.wakeAll();
}

bool BaichuanClient::connectAndLogin(QTcpSocket &sock, QByteArray &aesKey)
{
    sock.connectToHost(m_p.host, static_cast<quint16>(m_p.port));
    if (!sock.waitForConnected(5000))
        return false;

    QByteArray netBuf;
    auto recvMatch = [&](quint16 wantMsgNum, Msg &out) -> bool {
        // Short per-wait timeout so abort (destruction) is honored within ~0.3s;
        // the loop count still allows several seconds total for a slow login.
        for (int i = 0; i < 40 && !m_abort.load(); ++i) {
            while (parseMessage(netBuf, out))
                if (out.msgNum == wantMsgNum)
                    return true;
            if (!sock.waitForReadyRead(300))
                continue;
            netBuf.append(sock.readAll());
        }
        return false;
    };

    // Step 1: legacy-upgrade requesting AES (this firmware refuses BCEncrypt).
    // The login handshake is connection-level, ALWAYS channel 0 — the NVR rejects
    // a login framed with a camera channel. The camera is selected later by the
    // stream request (header channel byte + <channelId> in its XML).
    sock.write(frame(1, 0, 0, 1, 0xDC12, 0x6514, QByteArray()));
    sock.flush();
    Msg nonceMsg;
    if (!recvMatch(1, nonceMsg))
        return false;
    const QByteArray nonceXml = bc::xorCrypt(nonceMsg.body, nonceMsg.channel);
    const int a = nonceXml.indexOf("<nonce>");
    const int z = nonceXml.indexOf("</nonce>");
    if (a < 0 || z < 0)
        return false;
    const QString nonce = QString::fromUtf8(nonceXml.mid(a + 7, z - a - 7));
    aesKey = bc::aesKey(nonce, m_p.password);

    // Step 2: modern login (XOR body — msg_id 1 is always XOR).
    const QByteArray user = bc::modernHash(m_p.username, nonce);
    const QByteArray pass = bc::modernHash(m_p.password, nonce);
    QByteArray login = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body>\n<LoginUser version=\"1.1\">\n"
                       "<userName>" + user + "</userName>\n<password>" + pass +
                       "</password>\n<userVer>1</userVer>\n</LoginUser>\n"
                       "<LoginNet version=\"1.1\"><type>LAN</type><udpPort>0</udpPort></LoginNet>\n</body>\n";
    sock.write(frame(1, 0, 0, 1, 0, 0x6414, bc::xorCrypt(login, 0)));
    sock.flush();
    Msg loginReply;
    if (!recvMatch(1, loginReply))
        return false;
    // Carry any leftover bytes into the media pump.
    m_loginTail = netBuf;
    return loginReply.code == 200;
}

bool BaichuanClient::startStream(QTcpSocket &sock, const QByteArray &aesKey)
{
    const quint8 streamByte = m_p.mainStream ? 0 : 1;
    const QString streamName = m_p.mainStream ? QStringLiteral("mainStream")
                                              : QStringLiteral("subStream");
    QByteArray netBuf = m_loginTail;

    if (m_p.startEpoch <= 0) {
        // Live preview: msg_id 3.
        QByteArray preview =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body><Preview version=\"1.1\">\n"
            "<channelId>" + QByteArray::number(m_p.channel) + "</channelId>\n<handle>" +
            QByteArray::number(m_p.mainStream ? 0 : 256) + "</handle>\n<streamType>" +
            streamName.toUtf8() + "</streamType>\n</Preview></body>\n";
        sock.write(frame(3, m_p.channel, streamByte, 20, 0, 0x6414, bc::aesCfb(preview, aesKey, false)));
        sock.flush();
        m_streamTail = netBuf;
        return true;
    }

    // Recorded playback by time (cmd 143). Media follows on msg_num 20.
    sendPlayback(sock, aesKey, m_p.startEpoch);
    m_streamTail = netBuf;
    return true;
}

// Request recorded playback from `startEpoch` by time (cmd 143). Used both to
// start playback and to seek in place. Structure verified across independent
// PCAP-derived implementations: a FileInfoList/FileInfo with the channel, stream,
// and a start/end time range.
void BaichuanClient::sendPlayback(QTcpSocket &sock, const QByteArray &aesKey, qint64 startEpoch)
{
    const quint8 streamByte = m_p.mainStream ? 0 : 1;
    const QByteArray streamName = m_p.mainStream ? QByteArrayLiteral("mainStream")
                                                 : QByteArrayLiteral("subStream");
    const QDateTime start = QDateTime::fromSecsSinceEpoch(startEpoch);
    const QDateTime dayEnd(start.date(), QTime(23, 59, 59));
    const QByteArray uid = m_p.uid.toUtf8();
    const QByteArray ch = QByteArray::number(m_p.channel);
    const QByteArray uidEl = uid.isEmpty() ? QByteArray() : ("<uid>" + uid + "</uid>\n");

    const QByteArray byTime =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body>\n<FileInfoList version=\"1.1\">\n<FileInfo>\n"
        "<logicChnBitmap>255</logicChnBitmap>\n<channelId>" + ch + "</channelId>\n" + uidEl +
        "<supportSub>" + QByteArray(m_p.mainStream ? "0" : "1") + "</supportSub>\n"
        "<streamType>" + streamName + "</streamType>\n"
        + timeXml(QStringLiteral("startTime"), start) + "\n"
        + timeXml(QStringLiteral("endTime"), dayEnd) + "\n</FileInfo>\n</FileInfoList>\n</body>\n";
    sock.write(frame(143, m_p.channel, streamByte, 20, 0, 0x6414, bc::aesCfb(byTime, aesKey, false)));
    sock.flush();
}

bool BaichuanClient::seek(qint64 startEpoch)
{
    if (startEpoch <= 0)
        return false;
    QMutexLocker lock(&m_mutex);
    if (m_finished || m_abort.load())
        return false;
    m_seekEpoch = startEpoch;
    return true;
}

void BaichuanClient::pumpMedia(QTcpSocket &sock, const QByteArray &aesKey, quint16 streamMsgNum)
{
    BcMediaParser parser;
    bool started = false;
    // After a seek, discard old in-flight frames until an I-frame at the new
    // position (POSIX seconds); 0 = no seek pending.
    qint64 seekTarget = 0;
    // Recorded playback is delivered faster than realtime; pace it to 1x from the
    // frame timestamps so it doesn't fast-forward. Live preview is not paced.
    const bool pace = m_p.startEpoch > 0;
    QElapsedTimer clock;
    quint32 firstMicros = 0;
    parser.onVideo = [&](const BcMediaParser::VideoFrame &f) {
        if (f.codec != BcMediaParser::Codec::Unknown) {
            QMutexLocker lock(&m_mutex);
            if (m_format.isEmpty())
                m_format = (f.codec == BcMediaParser::Codec::H265) ? QStringLiteral("hevc")
                                                                   : QStringLiteral("h264");
        }
        if (!started) {
            if (!f.keyFrame)
                return; // recorded playback opens mid-GOP; wait for the first I-frame
            // On a seek, the connection still carries old-position frames briefly;
            // drop them until an I-frame lands at the requested time.
            if (seekTarget > 0 && f.time != 0 && qAbs(qint64(f.time) - seekTarget) > 120)
                return;
            started = true;
            seekTarget = 0;
        }
        if (pace && f.microseconds) {
            if (!clock.isValid()) {
                clock.start();
                firstMicros = f.microseconds;
            }
            const qint64 targetMs = (qint64(f.microseconds) - qint64(firstMicros)) / 1000;
            const qint64 elapsed = clock.elapsed();
            if (targetMs < 0 || targetMs - elapsed > 10000) {
                clock.restart(); // timestamp discontinuity/seek — rebase
                firstMicros = f.microseconds;
            } else {
                for (qint64 wait = targetMs - elapsed; wait > 0 && !m_abort.load();
                     wait = targetMs - clock.elapsed())
                    QThread::msleep(static_cast<unsigned long>(qMin<qint64>(wait, 50)));
            }
        }
        pushAnnexB(f.annexB);
    };

    QByteArray netBuf = m_streamTail;
    Msg m;
    int stalls = 0;
    while (!m_abort.load()) {
        // In-place seek: reissue by-time playback and reset the media pipeline. The
        // BC message framing stays intact (continuous connection); only the BCMedia
        // layer resets, resyncing on the new position's first I-frame.
        qint64 seekTo = 0;
        {
            QMutexLocker lock(&m_mutex);
            if (m_seekEpoch > 0) {
                seekTo = m_seekEpoch;
                m_seekEpoch = 0;
            }
        }
        if (seekTo > 0) {
            sendPlayback(sock, aesKey, seekTo);
            parser.reset();
            started = false;
            seekTarget = seekTo; // discard until an I-frame at the new position
            clock.invalidate();
            firstMicros = 0;
            QMutexLocker lock(&m_mutex);
            m_ring.clear(); // drop queued old-position frames so playback jumps
            m_cond.wakeAll();
        }
        if (!parseMessage(netBuf, m)) {
            // Short wait so abort is honored quickly; recorded playback bursts then
            // stalls, so nudge it with a keep-alive after ~1s of silence.
            if (!sock.waitForReadyRead(300)) {
                if (++stalls >= 4) {
                    stalls = 0;
                    sock.write(frame(93, 0, 0, 99, 0, 0x6414, QByteArray())); // keep-alive: connection-level
                    sock.flush();
                }
                continue;
            }
            stalls = 0;
            netBuf.append(sock.readAll());
            continue;
        }
        if (m.msgNum != streamMsgNum)
            continue;
        // FullAES: the Extension carries <encryptLen> — AES-decrypt that many bytes
        // of the payload, the remainder is plaintext.
        int encLen = 0;
        if (m.payloadOffset > 0) {
            const QByteArray ext = bc::aesCfb(m.body.left(m.payloadOffset), aesKey, true);
            const int p = ext.indexOf("<encryptLen>");
            if (p >= 0) {
                const int q = ext.indexOf("</encryptLen>", p);
                encLen = ext.mid(p + 12, q - p - 12).toInt();
            }
        }
        const QByteArray payload = m.body.mid(m.payloadOffset);
        const QByteArray media = encLen > 0
            ? bc::aesCfb(payload.left(encLen), aesKey, true) + payload.mid(encLen)
            : payload;
        parser.append(media);
    }
}

} // namespace rl
