#include "ReolinkApi.h"

#include <QUrl>

namespace rl {
namespace api {

// RFC 3986: IPv6 literals must be bracketed inside a URL authority.
static QString hostForUrl(const QString &host)
{
    if (host.contains(u':') && !host.startsWith(u'['))
        return u'[' + host + u']';
    return host;
}

bool BatchResult::needsRelogin() const
{
    for (const CommandResult &r : results) {
        if (!r.ok && r.rspCode == RspLoginRequired)
            return true;
    }
    return false;
}

Json command(const QString &cmd, Json param, int action)
{
    return Json{{"cmd", cmd.toStdString()}, {"action", action}, {"param", std::move(param)}};
}

Json loginBody(const QString &username, const QString &password)
{
    Json user = {{"Version", "0"},
                 {"userName", username.toStdString()},
                 {"password", password.toStdString()}};
    return Json::array({command(QStringLiteral("Login"), Json{{"User", std::move(user)}})});
}

QString apiUrl(const QString &host, int port, bool https, const QString &firstCmd,
               const QString &token)
{
    QString url = QStringLiteral("%1://%2:%3/cgi-bin/api.cgi?cmd=%4")
                      .arg(https ? QStringLiteral("https") : QStringLiteral("http"),
                           hostForUrl(host))
                      .arg(port)
                      .arg(firstCmd);
    if (!token.isEmpty())
        url += QStringLiteral("&token=") + QString::fromUtf8(QUrl::toPercentEncoding(token));
    return url;
}

BatchResult parseBatch(const QByteArray &body)
{
    BatchResult out;
    const Json doc = Json::parse(body.constData(), body.constData() + body.size(),
                                 /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_array()) {
        out.error = QStringLiteral("malformed API response");
        return out;
    }
    out.transportOk = true;
    for (const Json &item : doc) {
        if (!item.is_object())
            continue; // ignore junk array elements ([null], [42], …)
        CommandResult r;
        r.cmd = QString::fromStdString(jsonStr(item, "cmd"));
        const int code = jsonInt(item, "code", 1);
        if (code == 0) {
            r.ok = true;
            r.value = jsonObj(item, "value");
        } else {
            const Json err = jsonObj(item, "error");
            r.rspCode = jsonInt(err, "rspCode", 0);
            r.detail = QString::fromStdString(jsonStr(err, "detail"));
        }
        out.results.append(std::move(r));
    }
    return out;
}

LoginResult parseLogin(const QByteArray &body)
{
    LoginResult out;
    const BatchResult batch = parseBatch(body);
    if (!batch.transportOk) {
        out.error = batch.error;
        return out;
    }
    if (batch.results.isEmpty()) {
        out.error = QStringLiteral("empty login response");
        return out;
    }
    const CommandResult &r = batch.results.first();
    if (!r.ok) {
        out.error = r.detail.isEmpty()
                        ? QStringLiteral("login failed (rspCode %1)").arg(r.rspCode)
                        : r.detail;
        return out;
    }
    const Json token = jsonObj(r.value, "Token");
    out.token = QString::fromStdString(jsonStr(token, "name"));
    out.leaseTimeSec = jsonInt(token, "leaseTime", 0);
    out.ok = !out.token.isEmpty();
    if (!out.ok)
        out.error = QStringLiteral("login response missing token");
    return out;
}

QString rtspUrl(const QString &host, const QString &username, const QString &password,
                int channel, bool mainStream, const QString &codec, int port)
{
    const QString user = QString::fromUtf8(QUrl::toPercentEncoding(username));
    const QString pass = QString::fromUtf8(QUrl::toPercentEncoding(password));
    // Concatenation, not .arg(): percent-encoded credentials contain sequences
    // like %3A that .arg() would consume as positional placeholders.
    return QStringLiteral("rtsp://") + user + u':' + pass + u'@' + hostForUrl(host) + u':' +
           QString::number(port) + u'/' + codec + QStringLiteral("Preview_") +
           QString::number(channel + 1).rightJustified(2, u'0') + u'_' +
           (mainStream ? QStringLiteral("main") : QStringLiteral("sub"));
}

Json ptzCtrl(int channel, const QString &op, int speed, int presetId)
{
    Json param = {{"channel", channel}, {"op", op.toStdString()}};
    // Stop takes no speed; directional/zoom ops do; ToPos also takes an id.
    if (op != QLatin1String(ptz::Stop))
        param["speed"] = qBound(1, speed, 64);
    if (presetId >= 0)
        param["id"] = presetId;
    return command(QStringLiteral("PtzCtrl"), std::move(param));
}

QString snapUrl(const QString &host, int port, bool https, int channel, const QString &token,
                const QString &rs)
{
    QString url = QStringLiteral("%1://%2:%3/cgi-bin/api.cgi?cmd=Snap&channel=%4&rs=%5")
                      .arg(https ? QStringLiteral("https") : QStringLiteral("http"),
                           hostForUrl(host))
                      .arg(port)
                      .arg(channel)
                      .arg(rs);
    if (!token.isEmpty())
        url += QStringLiteral("&token=") + QString::fromUtf8(QUrl::toPercentEncoding(token));
    return url;
}

// A capability is "present" when its {ver} (or bare int) is > 0.
static bool capVer(const Json &chn, const char *key)
{
    const Json &v = jsonRef(chn, key);
    if (v.is_object())
        return jsonInt(v, "ver", 0) > 0;
    if (v.is_number_integer() || v.is_number_unsigned())
        return v.get<int>() > 0;
    return false;
}

Capabilities parseAbility(const Json &value)
{
    Capabilities caps;
    const Json ability = jsonObj(value, "Ability");
    if (ability.empty())
        return caps;
    caps.valid = true;
    caps.talk = capVer(ability, "talk");
    caps.p2p = capVer(ability, "p2p");

    const Json chns = jsonArr(ability, "abilityChn");
    for (const Json &chn : chns) {
        ChannelCaps c;
        c.ptz = capVer(chn, "ptzCtrl") || capVer(chn, "ptzType");
        c.ptzPreset = capVer(chn, "ptzPreset");
        c.zoom = capVer(chn, "ptzCtrl") || capVer(chn, "supportPtzZoom");
        c.focus = capVer(chn, "ptzCtrl") || capVer(chn, "supportPtzFocus");
        // AI: some firmware exposes supportAi, others per-type flags.
        c.aiPeople = capVer(chn, "supportAiPeople");
        c.aiVehicle = capVer(chn, "supportAiVehicle");
        c.aiDogCat = capVer(chn, "supportAiDogCat") || capVer(chn, "supportAiAnimal");
        c.ai = capVer(chn, "supportAi") || c.aiPeople || c.aiVehicle || c.aiDogCat;
        c.audio = capVer(chn, "supportAudio") || capVer(chn, "supportGop");
        c.siren = capVer(chn, "supportAudioAlarm") || capVer(chn, "alarmAudio");
        c.floodlight = capVer(chn, "floodLight") || capVer(chn, "supportFLIntensity") ||
                       capVer(chn, "whiteLed");
        c.battery = capVer(chn, "battery") || capVer(chn, "supportBattery");
        c.doorbell = capVer(chn, "supportVisitor") || capVer(chn, "supportDoorbell");
        c.supportsBalanced = capVer(chn, "supportBalanced") || capVer(chn, "mainEncType");
        caps.channels.append(c);
    }
    return caps;
}

Json getOsd(int channel)
{
    return command(QStringLiteral("GetOsd"), Json{{"channel", channel}}, /*action=*/1);
}

static Json timeObj(const QDateTime &dt)
{
    const QDate d = dt.date();
    const QTime t = dt.time();
    return Json{{"year", d.year()}, {"mon", d.month()}, {"day", d.day()},
                {"hour", t.hour()}, {"min", t.minute()}, {"sec", t.second()}};
}

static QDateTime parseTimeObj(const Json &o)
{
    if (!o.is_object())
        return {};
    const QDate d(jsonInt(o, "year"), jsonInt(o, "mon"), jsonInt(o, "day"));
    const QTime t(jsonInt(o, "hour"), jsonInt(o, "min"), jsonInt(o, "sec"));
    if (!d.isValid())
        return {};
    return QDateTime(d, t.isValid() ? t : QTime(0, 0));
}

Json searchBody(int channel, const QDateTime &start, const QDateTime &end,
                const QString &streamType)
{
    Json search = {{"channel", channel},
                   {"onlyStatus", 0},
                   {"streamType", streamType.toStdString()},
                   {"StartTime", timeObj(start)},
                   {"EndTime", timeObj(end)}};
    return command(QStringLiteral("Search"), Json{{"Search", std::move(search)}});
}

SearchResult parseSearch(const Json &value)
{
    SearchResult out;
    const Json sr = jsonObj(value, "SearchResult");
    if (sr.empty())
        return out;
    out.ok = true;
    for (const Json &f : jsonArr(sr, "File")) {
        RecordingFile rf;
        rf.name = QString::fromStdString(jsonStr(f, "name"));
        rf.start = parseTimeObj(jsonObj(f, "StartTime"));
        rf.end = parseTimeObj(jsonObj(f, "EndTime"));
        rf.type = QString::fromStdString(jsonStr(f, "type"));
        rf.size = jsonInt(f, "size", 0);
        out.files.append(rf);
    }
    return out;
}

} // namespace api
} // namespace rl
