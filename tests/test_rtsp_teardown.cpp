#include "media/StreamPlayer.h"

#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>

using namespace rl;

namespace {

// Minimal RTSP server side: reads whole requests off the connection and
// answers OPTIONS/DESCRIBE/SETUP/PLAY like a camera that never sends media.
class FakeCamera
{
public:
    explicit FakeCamera(QTcpSocket *conn) : m_conn(conn) {}

    // Serve requests until one with `method` arrives (true) or `ms` pass.
    bool serveUntil(const QByteArray &method, int ms)
    {
        QDeadlineTimer deadline(ms);
        while (!deadline.hasExpired()) {
            QByteArray req;
            while ((req = takeRequest()).isEmpty()) {
                if (deadline.hasExpired() || m_conn->state() != QAbstractSocket::ConnectedState
                    || !m_conn->waitForReadyRead(qMax<qint64>(1, deadline.remainingTime())))
                    return false;
                m_buf += m_conn->readAll();
            }
            const QByteArray verb = req.left(req.indexOf(' '));
            m_seen << verb;
            if (verb == method)
                return true;
            respond(verb, req);
        }
        return false;
    }

    QList<QByteArray> seen() const { return m_seen; }

private:
    QByteArray takeRequest()
    {
        const int end = m_buf.indexOf("\r\n\r\n");
        if (end < 0)
            return {};
        QByteArray req = m_buf.left(end + 4);
        m_buf.remove(0, end + 4);
        return req;
    }

    void respond(const QByteArray &verb, const QByteArray &req)
    {
        QByteArray cseq;
        for (const QByteArray &line : req.split('\n'))
            if (line.toLower().startsWith("cseq:"))
                cseq = line.mid(5).trimmed();
        QByteArray head = "RTSP/1.0 200 OK\r\nCSeq: " + cseq + "\r\n";
        QByteArray body;
        if (verb == "OPTIONS") {
            head += "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n";
        } else if (verb == "DESCRIBE") {
            body = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=test\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\n"
                   "m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
                   "a=fmtp:96 packetization-mode=1\r\na=control:trackID=0\r\n";
            head += "Content-Type: application/sdp\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n";
        } else if (verb == "SETUP") {
            head += "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nSession: 1234;timeout=60\r\n";
        } else if (verb == "PLAY") {
            head += "Session: 1234\r\n";
        }
        m_conn->write(head + "\r\n" + body);
        m_conn->flush();
    }

    QTcpSocket *m_conn;
    QByteArray m_buf;
    QList<QByteArray> m_seen;
};

} // namespace

class TestRtspTeardown : public QObject
{
    Q_OBJECT

private slots:
    // Stopping a stream must still send TEARDOWN: the abort that unwinds the
    // worker used to veto every write, so cameras were left streaming
    // abandoned sessions (which some firmware then mixes into new ones).
    void stopSendsTeardown()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        StreamPlayer player;
        player.setSource(QStringLiteral("rtsp://127.0.0.1:%1/stream").arg(server.serverPort()));
        player.start();

        QVERIFY(server.waitForNewConnection(5000));
        QTcpSocket *conn = server.nextPendingConnection();
        FakeCamera camera(conn);
        // Set up and start playing; the session then waits for media that
        // never comes, like a live stream between packets.
        QVERIFY2(!camera.serveUntil("TEARDOWN", 1500), "TEARDOWN before stop()");
        QVERIFY2(camera.seen().contains("PLAY"), "session never reached PLAY");

        player.stop();
        QVERIFY2(camera.serveUntil("TEARDOWN", 5000), "no TEARDOWN after stop()");
        QVERIFY(StreamPlayer::waitForWorkers(5000));
    }
};

QTEST_MAIN(TestRtspTeardown)
#include "test_rtsp_teardown.moc"
