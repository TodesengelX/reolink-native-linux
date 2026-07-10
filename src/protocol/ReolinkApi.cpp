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

QVariant toVariant(const Json &j)
{
    switch (j.type()) {
    case Json::value_t::object: {
        QVariantMap m;
        for (auto it = j.begin(); it != j.end(); ++it)
            m.insert(QString::fromStdString(it.key()), toVariant(it.value()));
        return m;
    }
    case Json::value_t::array: {
        QVariantList l;
        for (const Json &e : j)
            l.append(toVariant(e));
        return l;
    }
    case Json::value_t::string:
        return QString::fromStdString(j.get<std::string>());
    case Json::value_t::boolean:
        return j.get<bool>();
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
        return static_cast<qlonglong>(j.get<qint64>());
    case Json::value_t::number_float:
        return j.get<double>();
    default:
        return {};
    }
}

Json toJson(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::QVariantMap: {
        Json o = Json::object();
        const QVariantMap m = v.toMap();
        for (auto it = m.begin(); it != m.end(); ++it)
            o[it.key().toStdString()] = toJson(it.value());
        return o;
    }
    case QMetaType::QVariantList: {
        Json a = Json::array();
        for (const QVariant &e : v.toList())
            a.push_back(toJson(e));
        return a;
    }
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong:
        return static_cast<qint64>(v.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString().toStdString();
    default:
        return v.isNull() ? Json() : Json(v.toString().toStdString());
    }
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
        out.wrongPassword = r.rspCode == RspPasswordWrong;
        // Surface the lockout counter (error.auth_warning_info.remain_times) so
        // the UI can warn before the account locks. Re-parse for the nested field
        // parseBatch does not expose.
        const Json doc = Json::parse(body.constData(), body.constData() + body.size(),
                                     nullptr, false);
        if (doc.is_array() && !doc.empty()) {
            const Json warn = jsonObj(jsonObj(doc.front(), "error"), "auth_warning_info");
            if (warn.contains("remain_times"))
                out.remainingAttempts = jsonInt(warn, "remain_times", -1);
        }
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
    caps.p2p = capVer(ability, "p2p"); // p2p is host-level; talk is per-channel (below)
    // The per-ability "permit" is a bitmask of the user's rights; admins can write
    // config and manage the device. Use privileged abilities as the signal.
    auto permit = [&](const char *key) {
        const Json &v = jsonRef(ability, key);
        return v.is_object() ? jsonInt(v, "permit", 0) : 0;
    };
    caps.isAdmin = permit("userManage") > 0 || permit("reboot") > 0 || permit("restore") > 0;

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
        c.talk = capVer(chn, "talk"); // per-channel (verified on RLN8-410)
        c.siren = capVer(chn, "supportAudioAlarm") || capVer(chn, "alarmAudio");
        c.floodlight = capVer(chn, "floodLight") || capVer(chn, "supportFLIntensity") ||
                       capVer(chn, "whiteLed");
        c.battery = capVer(chn, "battery") || capVer(chn, "supportBattery");
        c.doorbell = capVer(chn, "supportVisitor") || capVer(chn, "supportDoorbell");
        // supportBalanced only; mainEncType is an encoder flag, NOT a third stream.
        c.supportsBalanced = capVer(chn, "supportBalanced");
        caps.talk = caps.talk || c.talk;
        caps.channels.append(c);
    }
    return caps;
}

QVector<ChannelInfo> parseChannelStatus(const Json &value)
{
    // value = {"count":N,"status":[{"channel":i,"name":..,"online":0/1,"uid":..}]}
    QVector<ChannelInfo> out;
    for (const Json &c : jsonArr(value, "status")) {
        ChannelInfo ci;
        ci.channel = jsonInt(c, "channel", 0);
        ci.name = QString::fromStdString(jsonStr(c, "name"));
        ci.online = jsonBool(c, "online");
        ci.uid = QString::fromStdString(jsonStr(c, "uid"));
        out.append(ci);
    }
    return out;
}

Json getOsd(int channel)
{
    return command(QStringLiteral("GetOsd"), Json{{"channel", channel}}, /*action=*/1);
}

bool parseMdState(const Json &value)
{
    return jsonInt(value, "state", 0) != 0;
}

DetectionState parseAiState(const Json &value)
{
    // Each object type is {"alarm_state":0/1,"support":0/1}. Field names follow
    // reolink_aio; dog_cat covers pets.
    DetectionState d;
    auto alarm = [&](const char *key) { return jsonInt(jsonObj(value, key), "alarm_state", 0) != 0; };
    d.person = alarm("people");
    d.vehicle = alarm("vehicle");
    d.pet = alarm("dog_cat");
    return d;
}

BatteryInfo parseBatteryInfo(const Json &value)
{
    // GetBatteryInfo -> {"Battery":{"batteryPercent":N,"chargeStatus":0/1,...}} or
    // the fields directly under value on some firmware.
    //
    // Mains-powered devices (NVRs) answer with code 0 but UNINITIALIZED garbage
    // (e.g. batteryPercent in the billions) rather than an error — so validate the
    // value range and treat out-of-range as "no battery" (verified on RLN8-410).
    BatteryInfo b;
    const Json bat = value.contains("Battery") ? jsonObj(value, "Battery") : value;
    if (!bat.is_object() || !bat.contains("batteryPercent"))
        return b;
    const int raw = jsonInt(bat, "batteryPercent", -1);
    if (raw < 0 || raw > 100)
        return b; // garbage from a non-battery device
    b.present = true;
    b.percent = raw;
    // chargeStatus: 0 none, 1 charging, 2 charge-complete; adapterStatus also seen.
    const int cs = jsonInt(bat, "chargeStatus", 0);
    b.charging = cs == 1 || cs == 2 || jsonInt(bat, "adapterStatus", 0) == 1;
    return b;
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
        rf.name = QString::fromStdString(jsonStr(f, "name")); // empty on NVR firmware
        rf.start = parseTimeObj(jsonObj(f, "StartTime"));
        rf.end = parseTimeObj(jsonObj(f, "EndTime"));
        rf.streamType = QString::fromStdString(jsonStr(f, "type")); // "main"/"sub"
        rf.size = jsonInt(f, "size", 0); // arrives as a string on NVR firmware
        out.files.append(rf);
    }
    // Status[].table is a 31-char bitmap ("0000011110…"); char[day-1]=='1' means
    // that day has recordings. Drives the calendar dots. Take the first month
    // (a single-month query only returns one).
    const Json status = jsonArr(sr, "Status");
    if (!status.empty()) {
        const std::string table = jsonStr(status.front(), "table");
        for (int i = 0; i < static_cast<int>(table.size()); ++i)
            if (table[i] == '1')
                out.recordingDays.append(i + 1);
    }
    return out;
}

QString playbackFlvUrl(const QString &host, int port, bool https, int channel, bool mainStream,
                       const QDateTime &start, const QString &username, const QString &password,
                       const QString &token)
{
    Q_UNUSED(port); // the flv endpoint is on the web port; app=bcs handles routing
    const QString scheme = https ? QStringLiteral("https") : QStringLiteral("http");
    const QString ts = start.toString(QStringLiteral("yyyyMMddHHmmss"));
    QString base = QStringLiteral("%1://%2/flv?port=1935&app=bcs&stream=playback.bcs&channel=%3"
                                  "&type=%4&start=%5&seek=0")
                       .arg(scheme, hostForUrl(host))
                       .arg(channel)
                       .arg(mainStream ? 1 : 0)
                       .arg(ts);
    if (!token.isEmpty())
        return base + QStringLiteral("&token=") +
               QString::fromUtf8(QUrl::toPercentEncoding(token));
    return base + QStringLiteral("&user=%1&password=%2")
                      .arg(QString::fromUtf8(QUrl::toPercentEncoding(username)),
                           QString::fromUtf8(QUrl::toPercentEncoding(password)));
}

} // namespace api
} // namespace rl
