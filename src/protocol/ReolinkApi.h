#pragma once

#include "Json.h"

#include <QDateTime>
#include <QString>
#include <QVariant>
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
    RspLoginRequired = -6,   // "please login first" — token missing/expired
    RspLoginFailed = -7,     // generic login failure (older firmware)
    RspNotSupported = -9,    // command not supported by this device
    RspPasswordWrong = -502, // wrong password (verified on real firmware; carries
                             // error.auth_warning_info.remain_times before lockout)
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

// JSON <-> QVariant bridge so QML settings panels can bind directly to device
// config without a typed parser per command.
QVariant toVariant(const Json &j);
Json toJson(const QVariant &v);

QString apiUrl(const QString &host, int port, bool https, const QString &firstCmd,
               const QString &token = {});

BatchResult parseBatch(const QByteArray &body);

struct LoginResult {
    bool ok = false;
    QString token;
    int leaseTimeSec = 0;
    QString error;
    bool wrongPassword = false; // rspCode -502
    int remainingAttempts = -1; // error.auth_warning_info.remain_times (-1 = unknown)
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
    bool talk = false; // two-way audio (verified per-channel on real firmware)
    bool siren = false;
    bool floodlight = false;
    bool battery = false;
    bool doorbell = false;
    bool supportsBalanced = false; // exposes a third ("Balanced") stream
};
struct Capabilities {
    bool valid = false;
    bool talk = false;    // any channel supports two-way audio
    bool p2p = false;
    bool isAdmin = false; // logged-in user may change settings / reboot / manage users
    QVector<ChannelCaps> channels;
};
Capabilities parseAbility(const Json &value);

// ---- OSD ------------------------------------------------------------------
Json getOsd(int channel);

// ---- Detection state (polled for the event inbox) -------------------------
// GetMdState -> plain motion; GetAiState -> per-object-type alarm flags.
struct DetectionState {
    bool motion = false;
    bool person = false;
    bool vehicle = false;
    bool pet = false;
};
bool parseMdState(const Json &value);              // GetMdState -> motion bool
DetectionState parseAiState(const Json &value);    // GetAiState -> per-type flags

// ---- Battery (GetBatteryInfo, battery/solar cameras) ----------------------
struct BatteryInfo {
    bool present = false;
    int percent = 0;
    bool charging = false; // adapter or solar input active
};
BatteryInfo parseBatteryInfo(const Json &value);

// ---- Playback search ------------------------------------------------------
// Search recorded files for a channel in [start,end]. streamType is "main"/"sub".
Json searchBody(int channel, const QDateTime &start, const QDateTime &end,
                const QString &streamType = QStringLiteral("sub"));

struct RecordingFile {
    QString name;      // file handle (empty on NVR firmware — identify by start)
    QDateTime start;
    QDateTime end;
    QString streamType; // "main"/"sub" (NOT the trigger type on NVR firmware)
    qint64 size = 0;
};
struct SearchResult {
    bool ok = false;
    QVector<RecordingFile> files;
    QVector<int> recordingDays; // days-of-month (1..31) with recordings, from Status
    QString error;
};
SearchResult parseSearch(const Json &value);

// HTTP-FLV playback stream for an NVR recording (verified on RLN8-410):
//   <scheme>://host/flv?port=1935&app=bcs&stream=playback.bcs&channel=N
//     &type=0|1&start=YYYYMMDDHHMMSS&seek=0&user=U&password=P
// mainStream picks type=1 (main) vs type=0 (sub). Credentials are embedded
// (openable directly by libavformat) — callers must redact them from logs.
QString playbackFlvUrl(const QString &host, int port, bool https, int channel,
                       bool mainStream, const QDateTime &start, const QString &username,
                       const QString &password);

} // namespace api
} // namespace rl
