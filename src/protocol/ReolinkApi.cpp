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

} // namespace api
} // namespace rl
