#include "protocol/ReolinkApi.h"

#include <QtTest>

using namespace rl;

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void loginBodyShape()
    {
        const Json body = api::loginBody(QStringLiteral("admin"), QStringLiteral("pw123"));
        QVERIFY(body.is_array());
        QCOMPARE(body.size(), std::size_t(1));
        const Json &cmd = body.front();
        QCOMPARE(cmd.value("cmd", std::string{}), std::string("Login"));
        QCOMPARE(cmd.value("action", -1), 0);
        const Json user = cmd["param"]["User"];
        QCOMPARE(user.value("userName", std::string{}), std::string("admin"));
        QCOMPARE(user.value("password", std::string{}), std::string("pw123"));
        QCOMPARE(user.value("Version", std::string{}), std::string("0"));
    }

    void apiUrlBuilding()
    {
        QCOMPARE(api::apiUrl(QStringLiteral("192.168.1.10"), 443, true,
                             QStringLiteral("Login")),
                 QStringLiteral("https://192.168.1.10:443/cgi-bin/api.cgi?cmd=Login"));
        QCOMPARE(api::apiUrl(QStringLiteral("nvr.local"), 80, false,
                             QStringLiteral("GetDevInfo"), QStringLiteral("abc123")),
                 QStringLiteral(
                     "http://nvr.local:80/cgi-bin/api.cgi?cmd=GetDevInfo&token=abc123"));
    }

    void apiUrlEncodesToken()
    {
        const QString url = api::apiUrl(QStringLiteral("h"), 443, true, QStringLiteral("X"),
                                        QStringLiteral("a+b&c"));
        QVERIFY(!url.contains(QStringLiteral("a+b&c")));
        QVERIFY(url.contains(QStringLiteral("token=a%2Bb%26c")));
    }

    void parseLoginSuccess()
    {
        const QByteArray body = R"([{"cmd":"Login","code":0,)"
                                R"("value":{"Token":{"leaseTime":3600,"name":"deadbeef"}}}])";
        const api::LoginResult r = api::parseLogin(body);
        QVERIFY(r.ok);
        QCOMPARE(r.token, QStringLiteral("deadbeef"));
        QCOMPARE(r.leaseTimeSec, 3600);
    }

    void parseLoginRejected()
    {
        const QByteArray body = R"([{"cmd":"Login","code":1,)"
                                R"("error":{"rspCode":-7,"detail":"login failed"}}])";
        const api::LoginResult r = api::parseLogin(body);
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("login failed"));
    }

    void parseBatchMixed()
    {
        const QByteArray body =
            R"([{"cmd":"GetDevInfo","code":0,"value":{"DevInfo":{"name":"Cam"}}},)"
            R"({"cmd":"GetAbility","code":1,"error":{"rspCode":-6,"detail":"please login first"}}])";
        const api::BatchResult r = api::parseBatch(body);
        QVERIFY(r.transportOk);
        QCOMPARE(r.results.size(), 2);
        QVERIFY(r.results[0].ok);
        QCOMPARE(QString::fromStdString(
                     r.results[0].value["DevInfo"].value("name", std::string{})),
                 QStringLiteral("Cam"));
        QVERIFY(!r.results[1].ok);
        QCOMPARE(r.results[1].rspCode, int(api::RspLoginRequired));
        QVERIFY(r.needsRelogin());
    }

    void parseBatchMalformed()
    {
        QVERIFY(!api::parseBatch("not json at all").transportOk);
        QVERIFY(!api::parseBatch(R"({"cmd":"x"})").transportOk); // object, not array
        QVERIFY(!api::parseBatch("").transportOk);
    }

    void rtspUrlFormat()
    {
        // Channel is 0-based in the API, 1-based zero-padded in the RTSP path
        // (docs/research/fact-check.md).
        QCOMPARE(api::rtspUrl(QStringLiteral("10.0.0.5"), QStringLiteral("admin"),
                              QStringLiteral("secret"), 0, true),
                 QStringLiteral("rtsp://admin:secret@10.0.0.5:554/h264Preview_01_main"));
        QCOMPARE(api::rtspUrl(QStringLiteral("10.0.0.5"), QStringLiteral("admin"),
                              QStringLiteral("secret"), 15, false),
                 QStringLiteral("rtsp://admin:secret@10.0.0.5:554/h264Preview_16_sub"));
    }

    void rtspUrlEscapesCredentials()
    {
        const QString url = api::rtspUrl(QStringLiteral("h"), QStringLiteral("user@x"),
                                         QStringLiteral("p:a/s"), 0, true);
        QCOMPARE(url, QStringLiteral("rtsp://user%40x:p%3Aa%2Fs@h:554/h264Preview_01_main"));
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
