#pragma once

#include "Json.h"

#include <QDateTime>
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

// ---- PTZ ------------------------------------------------------------------
// Operations accepted by PtzCtrl (Reolink HTTP API). Directional ops run until a
// matching Stop; ToPos/Auto/Patrol take an id.
namespace ptz {
inline constexpr auto Left = "Left";
inline constexpr auto Right = "Right";
inline constexpr auto Up = "Up";
inline constexpr auto Down = "Down";
inline constexpr auto LeftUp = "LeftUp";
inline constexpr auto RightUp = "RightUp";
inline constexpr auto LeftDown = "LeftDown";
inline constexpr auto RightDown = "RightDown";
inline constexpr auto ZoomInc = "ZoomInc";
inline constexpr auto ZoomDec = "ZoomDec";
inline constexpr auto FocusInc = "FocusInc";
inline constexpr auto FocusDec = "FocusDec";
inline constexpr auto Stop = "Stop";
inline constexpr auto ToPos = "ToPos"; // go to preset (needs presetId)
} // namespace ptz

// Build a PtzCtrl command. presetId >= 0 is included (for ToPos); speed is clamped
// by the device to its own range (typically 1..64).
Json ptzCtrl(int channel, const QString &op, int speed = 32, int presetId = -1);

// GET URL that returns a JPEG snapshot of the channel (not JSON). rs is a
// cache-buster the device expects.
QString snapUrl(const QString &host, int port, bool https, int channel, const QString &token,
                const QString &rs = QStringLiteral("reolink"));

// ---- Capabilities (GetAbility) --------------------------------------------
// Per-channel capability flags parsed from Ability.abilityChn[i]. Field names
// follow reolink_aio; unknown/absent capabilities degrade to false so the UI
// simply hides the control. Verify against target firmware (DESIGN §6.10).
struct ChannelCaps {
    bool ptz = false;
    bool ptzPreset = false;
    bool zoom = false;
    bool focus = false;
    bool ai = false;
    bool aiPeople = false;
    bool aiVehicle = false;
    bool aiDogCat = false;
    bool audio = false;
    bool siren = false;
    bool floodlight = false;
    bool battery = false;
    bool doorbell = false;
    bool supportsBalanced = false; // exposes a third ("Balanced") stream
};
struct Capabilities {
    bool valid = false;
    bool talk = false; // two-way audio (host-level)
    bool p2p = false;
    QVector<ChannelCaps> channels;
};
Capabilities parseAbility(const Json &value);

// ---- OSD ------------------------------------------------------------------
Json getOsd(int channel);

// ---- Playback search ------------------------------------------------------
// Search recorded files for a channel in [start,end]. streamType is "main"/"sub".
Json searchBody(int channel, const QDateTime &start, const QDateTime &end,
                const QString &streamType = QStringLiteral("sub"));

struct RecordingFile {
    QString name;   // opaque handle for Download/Playback
    QDateTime start;
    QDateTime end;
    QString type;   // "md" (motion/alarm) vs "" / other (timer/continuous)
    qint64 size = 0;
};
struct SearchResult {
    bool ok = false;
    QVector<RecordingFile> files;
    QString error;
};
SearchResult parseSearch(const Json &value);

} // namespace api
} // namespace rl
