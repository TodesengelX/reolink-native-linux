#pragma once

#include "Json.h"

#include <QString>
#include <QVector>

namespace rl {

// Pure request-building and response-parsing for the Reolink HTTP-CGI JSON API.
// No I/O here — everything is unit-testable. Transport lives in ReolinkHttpClient.
//
// Wire shape (from the official API guide and reolink_aio):
//   POST http(s)://host:port/cgi-bin/api.cgi?cmd=<FirstCmd>&token=<token>
//   body: JSON array of {"cmd": ..., "action": 0|1, "param": {...}}
//   response: JSON array of {"cmd": ..., "code": 0, "value": {...}}
//        or   {"cmd": ..., "code": 1, "error": {"rspCode": <neg>, "detail": "..."}}
namespace api {

// rspCode values observed across firmware (behaviors validated against reolink_aio/HA;
// treat as "observed, verify against target firmware" per DESIGN.md §4).
enum RspCode {
    RspLoginRequired = -6, // "please login first" — token missing/expired
    RspLoginFailed = -7,   // bad credentials
    RspNotSupported = -9,  // command not supported by this device
};

struct CommandResult {
    QString cmd;
    bool ok = false;
    Json value;      // "value" object when ok
    int rspCode = 0; // negative device error when !ok
    QString detail;
};

struct BatchResult {
    bool transportOk = false; // false: malformed/unparseable body
    QString error;
    QVector<CommandResult> results;
    // True when any command failed because the token is missing/expired.
    bool needsRelogin() const;
};

Json command(const QString &cmd, Json param = Json::object(), int action = 0);
Json loginBody(const QString &username, const QString &password);

QString apiUrl(const QString &host, int port, bool https, const QString &firstCmd,
               const QString &token = {});

BatchResult parseBatch(const QByteArray &body);

struct LoginResult {
    bool ok = false;
    QString token;
    int leaseTimeSec = 0;
    QString error;
};
LoginResult parseLogin(const QByteArray &body);

// rtsp://user:pass@host:554/<codec>Preview_<NN>_<main|sub>
// channel is 0-based (as the HTTP API uses); the RTSP path is 1-based zero-padded.
QString rtspUrl(const QString &host, const QString &username, const QString &password,
                int channel = 0, bool mainStream = true, const QString &codec = QStringLiteral("h264"),
                int port = 554);

} // namespace api
} // namespace rl
