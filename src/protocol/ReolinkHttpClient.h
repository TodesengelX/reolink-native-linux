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
//
// Thread-safe, and deliberately SERIAL: one api.cgi request in flight per
// device at a time. The NVR's web server degrades under concurrent commands
// (404/502 bursts — the same behavior reolink_aio works around by putting
// every request behind one per-device lock), and a multi-pane playback page
// fires several Search calls at once. Queueing them here costs a little
// latency; racing them costs whole requests. Bulk clip downloads are exempt —
// they run for minutes and would starve every command behind them.
class ReolinkHttpClient
{
public:
    ReolinkHttpClient(QString host, int port, bool https, QString username, QString password);
    ~ReolinkHttpClient();

    // Sends a batch of commands (api::command(...)), handling login transparently.
    api::BatchResult call(const Json &commands);

    // Convenience for a single command; returns the lone CommandResult.
    api::CommandResult callOne(const QString &cmd, Json param = Json::object(), int action = 0);

    // Fetch a JPEG snapshot of a channel (cmd=Snap, binary GET). Returns empty on
    // failure with *error set.
    QByteArray fetchSnapshot(int channel, QString *error = nullptr);

    // Stream an authenticated GET straight to a file (e.g. a recording clip via
    // cmd=Download). Streams to disk rather than buffering, so it handles the large
    // clips the endpoint produces. Returns false with *error set on failure (and
    // removes any partial file). timeoutSec allows a long cap (0 = default).
    bool downloadToFile(const QString &url, const QString &destPath, long timeoutSec = 0,
                        QString *error = nullptr);

    bool ensureLogin(QString *error = nullptr);
    void logout();

    QString host() const { return m_host; }
    // Current session token (may be empty if not logged in / expired). Thread-safe.
    QString token();

private:
    struct HttpResponse {
        bool ok = false;
        long status = 0;
        QByteArray body;
        QString error;
        QByteArray contentType;
    };
    // totalTimeoutSec == 0 uses the default; pass a smaller value for best-effort
    // calls (e.g. Logout) that must not pin a thread on a dead device.
    HttpResponse post(const QString &url, const QByteArray &body, long totalTimeoutSec = 0);
    // totalTimeoutSec == 0 uses the default; large clip downloads from a slow NVR
    // need a generous value.
    HttpResponse get(const QString &url, long totalTimeoutSec = 0);
    bool loginLocked(QString *error);
    bool tokenValidLocked() const;

    QString m_host;
    int m_port;
    bool m_https;
    QString m_username;
    QString m_password;

    // Lock order: m_requestMutex (outer, held across a whole HTTP exchange)
    // then m_mutex (inner, brief). Never the reverse.
    QMutex m_requestMutex; // serializes api.cgi requests to this device
    QMutex m_mutex;        // guards token state
    QString m_token;
    QDateTime m_tokenExpiry;
};

} // namespace rl
