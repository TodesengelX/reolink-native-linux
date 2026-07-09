#include "ReolinkHttpClient.h"

#include "core/Log.h"

#include <curl/curl.h>

#include <mutex>

namespace rl {

namespace {

// Relogin this many seconds before the advertised lease expires. Firmware
// reports leaseTime dynamically (usually 3600s but not guaranteed).
constexpr int kLeaseMarginSec = 300;
constexpr long kConnectTimeoutSec = 5;
constexpr long kTotalTimeoutSec = 15;

void ensureCurlGlobalInit()
{
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t writeToByteArray(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<QByteArray *>(userdata);
    out->append(ptr, static_cast<qsizetype>(size * nmemb));
    return size * nmemb;
}

} // namespace

ReolinkHttpClient::ReolinkHttpClient(QString host, int port, bool https, QString username,
                                     QString password)
    : m_host(std::move(host)), m_port(port), m_https(https), m_username(std::move(username)),
      m_password(std::move(password))
{
    ensureCurlGlobalInit();
    if (m_password.size() > 31) {
        // Observed firmware behavior (reolink_aio): passwords are truncated to 31
        // chars at the device; longer ones fail to authenticate.
        qCWarning(lcProto) << m_host << "password exceeds 31 characters — Reolink firmware"
                           << "is known to truncate; login will likely fail";
    }
}

ReolinkHttpClient::~ReolinkHttpClient()
{
    logout();
}

ReolinkHttpClient::HttpResponse ReolinkHttpClient::post(const QString &url,
                                                        const QByteArray &body)
{
    HttpResponse resp;
    CURL *curl = curl_easy_init();
    if (!curl) {
        resp.error = QStringLiteral("curl_easy_init failed");
        return resp;
    }
    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
    const QByteArray urlUtf8 = url.toUtf8();

    curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.constData());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.constData());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToByteArray);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTotalTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    // Reolink devices ship self-signed certificates; the official client accepts
    // them. TODO(security, DESIGN §10): trust-on-first-use certificate pinning.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        resp.error = QString::fromUtf8(curl_easy_strerror(rc));
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
        resp.ok = resp.status >= 200 && resp.status < 300;
        if (!resp.ok)
            resp.error = QStringLiteral("HTTP %1").arg(resp.status);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

bool ReolinkHttpClient::tokenValidLocked() const
{
    return !m_token.isEmpty() && m_tokenExpiry > QDateTime::currentDateTimeUtc();
}

bool ReolinkHttpClient::loginLocked(QString *error)
{
    const QString url = api::apiUrl(m_host, m_port, m_https, QStringLiteral("Login"));
    const Json body = api::loginBody(m_username, m_password);
    const HttpResponse resp = post(url, QByteArray::fromStdString(body.dump()));
    if (!resp.ok) {
        if (error)
            *error = resp.error;
        qCWarning(lcProto) << m_host << "login transport error:" << resp.error;
        return false;
    }
    const api::LoginResult login = api::parseLogin(resp.body);
    if (!login.ok) {
        if (error)
            *error = login.error;
        qCWarning(lcProto) << m_host << "login rejected:" << login.error;
        return false;
    }
    m_token = login.token;
    const int lease = login.leaseTimeSec > kLeaseMarginSec ? login.leaseTimeSec - kLeaseMarginSec
                                                           : login.leaseTimeSec;
    m_tokenExpiry = QDateTime::currentDateTimeUtc().addSecs(lease);
    qCInfo(lcProto) << m_host << "logged in, lease" << login.leaseTimeSec << "s";
    return true;
}

bool ReolinkHttpClient::ensureLogin(QString *error)
{
    QMutexLocker lock(&m_mutex);
    if (tokenValidLocked())
        return true;
    return loginLocked(error);
}

void ReolinkHttpClient::logout()
{
    QMutexLocker lock(&m_mutex);
    if (m_token.isEmpty())
        return;
    // Observed firmware quirk: Logout requires a valid token; without one it
    // fails harmlessly. Best-effort — devices also expire tokens server-side.
    const QString url =
        api::apiUrl(m_host, m_port, m_https, QStringLiteral("Logout"), m_token);
    const Json body = Json::array({api::command(QStringLiteral("Logout"))});
    post(url, QByteArray::fromStdString(body.dump()));
    m_token.clear();
    m_tokenExpiry = {};
}

api::BatchResult ReolinkHttpClient::call(const Json &commands)
{
    api::BatchResult out;
    if (!commands.is_array() || commands.empty()) {
        out.error = QStringLiteral("call() requires a non-empty command array");
        return out;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        QString loginError;
        if (!ensureLogin(&loginError)) {
            out.error = loginError;
            return out;
        }
        QString token;
        {
            QMutexLocker lock(&m_mutex);
            token = m_token;
        }
        const QString firstCmd =
            QString::fromStdString(commands.front().value("cmd", std::string{}));
        const QString url = api::apiUrl(m_host, m_port, m_https, firstCmd, token);
        const HttpResponse resp = post(url, QByteArray::fromStdString(commands.dump()));
        if (!resp.ok) {
            out.error = resp.error;
            return out;
        }
        out = api::parseBatch(resp.body);
        if (out.transportOk && out.needsRelogin() && attempt == 0) {
            // Token invalidated server-side (reboot, credential change) — relogin once.
            QMutexLocker lock(&m_mutex);
            m_token.clear();
            m_tokenExpiry = {};
            continue;
        }
        return out;
    }
    return out;
}

api::CommandResult ReolinkHttpClient::callOne(const QString &cmd, Json param, int action)
{
    const api::BatchResult batch = call(Json::array({api::command(cmd, std::move(param), action)}));
    if (!batch.transportOk || batch.results.isEmpty()) {
        api::CommandResult r;
        r.cmd = cmd;
        r.detail = batch.error.isEmpty() ? QStringLiteral("no response") : batch.error;
        return r;
    }
    return batch.results.first();
}

} // namespace rl
