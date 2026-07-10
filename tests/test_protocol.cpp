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

    void ipv6HostsAreBracketed()
    {
        QVERIFY(api::apiUrl(QStringLiteral("fd00::12"), 443, true, QStringLiteral("Login"))
                    .startsWith(QStringLiteral("https://[fd00::12]:443/")));
        QVERIFY(api::rtspUrl(QStringLiteral("fd00::12"), QStringLiteral("a"), QStringLiteral("b"),
                             0, true)
                    .contains(QStringLiteral("@[fd00::12]:554/")));
    }

    void ptzCtrlBuild()
    {
        const Json move = api::ptzCtrl(2, QStringLiteral("Left"), 40);
        QCOMPARE(move.value("cmd", std::string{}), std::string("PtzCtrl"));
        QCOMPARE(move["param"].value("channel", -1), 2);
        QCOMPARE(move["param"].value("op", std::string{}), std::string("Left"));
        QCOMPARE(move["param"].value("speed", -1), 40);
        QVERIFY(!move["param"].contains("id"));

        // Stop carries no speed.
        const Json stop = api::ptzCtrl(0, QStringLiteral("Stop"));
        QVERIFY(!stop["param"].contains("speed"));

        // Speed is clamped to the device range.
        QCOMPARE(api::ptzCtrl(0, QStringLiteral("Up"), 999)["param"].value("speed", -1), 64);

        // ToPos includes the preset id.
        const Json preset = api::ptzCtrl(1, QStringLiteral("ToPos"), 32, 3);
        QCOMPARE(preset["param"].value("id", -1), 3);
    }

    void snapUrlBuild()
    {
        const QString url = api::snapUrl(QStringLiteral("10.0.0.5"), 443, true, 1,
                                         QStringLiteral("tok"));
        QCOMPARE(url, QStringLiteral("https://10.0.0.5:443/cgi-bin/api.cgi?cmd=Snap&channel=1"
                                     "&rs=reolink&token=tok"));
    }

    void parseAbilityCaps()
    {
        const QByteArray body = R"({"Ability":{"talk":{"ver":1,"permit":6},
            "abilityChn":[
              {"ptzCtrl":{"ver":4,"permit":6},"ptzPreset":{"ver":1,"permit":6},
               "supportAiPeople":{"ver":1,"permit":6},"floodLight":{"ver":0,"permit":0}},
              {"ptzCtrl":{"ver":0,"permit":0},"battery":{"ver":1,"permit":6}}
            ]}})";
        const Json value = Json::parse(body.constData(), body.constData() + body.size(),
                                       nullptr, false);
        const api::Capabilities caps = api::parseAbility(value);
        QVERIFY(caps.valid);
        QVERIFY(caps.talk);
        QCOMPARE(caps.channels.size(), 2);
        QVERIFY(caps.channels[0].ptz);
        QVERIFY(caps.channels[0].ptzPreset);
        QVERIFY(caps.channels[0].aiPeople);
        QVERIFY(caps.channels[0].ai);
        QVERIFY(!caps.channels[0].floodlight);
        QVERIFY(!caps.channels[1].ptz);
        QVERIFY(caps.channels[1].battery);
    }

    void parseAbilityAdmin()
    {
        const QByteArray adminBody = R"({"Ability":{"userManage":{"ver":1,"permit":6},
            "abilityChn":[{}]}})";
        Json v = Json::parse(adminBody.constData(), adminBody.constData() + adminBody.size(),
                             nullptr, false);
        QVERIFY(api::parseAbility(v).isAdmin);

        const QByteArray userBody = R"({"Ability":{"userManage":{"ver":1,"permit":0},
            "abilityChn":[{}]}})";
        v = Json::parse(userBody.constData(), userBody.constData() + userBody.size(), nullptr,
                        false);
        QVERIFY(!api::parseAbility(v).isAdmin);
    }

    void parseAbilityEmptyIsInvalid()
    {
        QVERIFY(!api::parseAbility(Json::object()).valid);
        QVERIFY(!api::parseAbility(Json::array()).valid);
    }

    void jsonVariantRoundTrip()
    {
        const QByteArray body = R"({"Enc":{"channel":0,"audio":1,"mainStream":{"size":"2560*1440",
            "bitRate":4096,"frameRate":25,"vType":"h265"},"flags":[1,2,3]}})";
        const Json j = Json::parse(body.constData(), body.constData() + body.size(), nullptr, false);
        const QVariant v = api::toVariant(j);
        const QVariantMap enc = v.toMap().value("Enc").toMap();
        QCOMPARE(enc.value("audio").toInt(), 1);
        QCOMPARE(enc.value("mainStream").toMap().value("bitRate").toInt(), 4096);
        QCOMPARE(enc.value("mainStream").toMap().value("vType").toString(), QStringLiteral("h265"));
        QCOMPARE(enc.value("flags").toList().size(), 3);

        // Round-trip back to JSON preserves types.
        const Json back = api::toJson(v);
        QCOMPARE(back["Enc"]["mainStream"]["bitRate"].get<int>(), 4096);
        QCOMPARE(QString::fromStdString(back["Enc"]["mainStream"]["vType"].get<std::string>()),
                 QStringLiteral("h265"));
        QVERIFY(back["Enc"]["flags"].is_array());
    }

    void detectionStates()
    {
        auto parse = [](const char *s) {
            const QByteArray b(s);
            return Json::parse(b.constData(), b.constData() + b.size(), nullptr, false);
        };
        QVERIFY(api::parseMdState(parse(R"({"state":1})")));
        QVERIFY(!api::parseMdState(parse(R"({"state":0})")));
        QVERIFY(!api::parseMdState(parse(R"({})")));

        const api::DetectionState d = api::parseAiState(parse(
            R"({"people":{"alarm_state":1,"support":1},"vehicle":{"alarm_state":0,"support":1},)"
            R"("dog_cat":{"alarm_state":1,"support":1}})"));
        QVERIFY(d.person);
        QVERIFY(!d.vehicle);
        QVERIFY(d.pet);
    }

    void searchRoundTrip()
    {
        const QDateTime start(QDate(2026, 7, 9), QTime(0, 0));
        const QDateTime end(QDate(2026, 7, 9), QTime(23, 59, 59));
        const Json body = api::searchBody(0, start, end, QStringLiteral("main"));
        const Json s = body["param"]["Search"];
        QCOMPARE(s.value("streamType", std::string{}), std::string("main"));
        QCOMPARE(s["StartTime"].value("year", 0), 2026);
        QCOMPARE(s["EndTime"].value("hour", 0), 23);

        const QByteArray resp = R"({"SearchResult":{"channel":0,"File":[
            {"name":"Mp4Record/2026-07-09/RecS02_x.mp4","size":1048576,"type":"main",
             "StartTime":{"year":2026,"mon":7,"day":9,"hour":8,"min":30,"sec":0},
             "EndTime":{"year":2026,"mon":7,"day":9,"hour":8,"min":31,"sec":0}}]}})";
        const Json value = Json::parse(resp.constData(), resp.constData() + resp.size(),
                                       nullptr, false);
        const api::SearchResult sr = api::parseSearch(value);
        QVERIFY(sr.ok);
        QCOMPARE(sr.files.size(), 1);
        QCOMPARE(sr.files[0].start, QDateTime(QDate(2026, 7, 9), QTime(8, 30)));
        QCOMPARE(sr.files[0].size, qint64(1048576));
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
