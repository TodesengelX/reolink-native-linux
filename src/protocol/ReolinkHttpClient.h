#pragma once

#include "ReolinkApi.h"

#include <QDateTime>
#include <QMutex>
#include <QString>

namespace rl {

// Blocking HTTP-CGI transport for one Reolink host (camera or NVR).
// Owns the login token lifecycle: logs in on demand, refreshes before the
// lease expires, retries once on "please login first". Call from a worker
// thread (DeviceManager wraps calls in QtConcurrent) — never the GUI thread.
// Thread-safe: concurrent call() invocations serialize on the token state.
class ReolinkHttpClient
{
public:
    ReolinkHttpClient(QString host, int port, bool https, QString username, QString password);
    ~ReolinkHttpClient();

    // Sends a batch of commands (api::command(...)), handling login transparently.
    api::BatchResult call(const Json &commands);

    // Convenience for a single command; returns the lone CommandResult.
    api::CommandResult callOne(const QString &cmd, Json param = Json::object(), int action = 0);

    bool ensureLogin(QString *error = nullptr);
    void logout();

    QString host() const { return m_host; }

private:
    struct HttpResponse {
        bool ok = false;
        long status = 0;
        QByteArray body;
        QString error;
    };
    HttpResponse post(const QString &url, const QByteArray &body);
    bool loginLocked(QString *error);
    bool tokenValidLocked() const;

    QString m_host;
    int m_port;
    bool m_https;
    QString m_username;
    QString m_password;

    QMutex m_mutex; // guards token state
    QString m_token;
    QDateTime m_tokenExpiry;
};

} // namespace rl
