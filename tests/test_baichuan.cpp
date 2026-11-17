#include "protocol/BaichuanClient.h"
#include "protocol/BcCrypto.h"

#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>

#include <future>

extern "C" {
#include <libavutil/error.h>
}

using namespace rl;

namespace {

void putLE32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 24) & 0xFF));
}

// A modern-class (0x6414) BC reply: 24-byte header, no Extension.
QByteArray reply(quint32 id, quint16 msgNum, quint16 code, const QByteArray &body)
{
    QByteArray m;
    putLE32(m, 0x0ABCDEF0);
    putLE32(m, id);
    putLE32(m, static_cast<quint32>(body.size()));
    m.append(char(0)); // channel
    m.append(char(0)); // stream type
    m.append(char(msgNum & 0xFF));
    m.append(char(msgNum >> 8));
    m.append(char(code & 0xFF));
    m.append(char(code >> 8));
    m.append(char(0x14));
    m.append(char(0x64));
    putLE32(m, 0); // payload offset
    m.append(body);
    return m;
}

// Wait until the client has sent at least one more complete-looking message.
bool awaitRequest(QTcpSocket *conn)
{
    if (conn->bytesAvailable() == 0 && !conn->waitForReadyRead(5000))
        return false;
    conn->readAll();
    return true;
}

} // namespace

class TestBaichuan : public QObject
{
    Q_OBJECT

private slots:
    // The device dropping a live stream must end it (EOF to the reader) instead
    // of leaving the network loop spinning on a dead socket forever.
    void liveStreamEndsWhenDeviceDisconnects()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        BaichuanClient::Params p;
        p.host = QStringLiteral("127.0.0.1");
        p.port = server.serverPort();
        p.username = QStringLiteral("admin");
        p.password = QStringLiteral("pw");
        auto client = std::make_shared<BaichuanClient>(p);
        client->start();

        QVERIFY(server.waitForNewConnection(5000));
        QTcpSocket *conn = server.nextPendingConnection();
        QVERIFY(conn);

        // Legacy upgrade -> nonce; modern login -> 200; preview request -> hang up.
        QVERIFY(awaitRequest(conn));
        conn->write(reply(1, 1, 200,
                          bc::xorCrypt("<body><Encryption><nonce>abc</nonce></Encryption></body>", 0)));
        conn->flush();
        QVERIFY(awaitRequest(conn));
        conn->write(reply(1, 1, 200, QByteArray()));
        conn->flush();
        QVERIFY(awaitRequest(conn));
        conn->abort();

        auto reader = std::async(std::launch::async, [client] {
            unsigned char buf[4096];
            return client->read(buf, sizeof(buf));
        });
        const bool ended = reader.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        if (!ended)
            client->stop(); // unblock the reader so the test can finish
        QVERIFY2(ended, "read() still blocked 5s after the device hung up");
        QCOMPARE(reader.get(), AVERROR_EOF);
    }
};

QTEST_GUILESS_MAIN(TestBaichuan)
#include "test_baichuan.moc"
