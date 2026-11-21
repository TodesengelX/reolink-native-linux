#pragma once

#include "core/CredentialStore.h"
#include "core/Database.h"
#include "protocol/ReolinkApi.h"
// Not forward-declared on purpose — see the note by the other forward
// declarations below.
#include "media/StreamPlayer.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QFutureSynchronizer>
#include <QHash>
#include <QSet>
#include <QSize>
#include <QTimer>
#include <QMutex>

#include <memory>

namespace rl {
// These two are only ever held behind pointers in private members and never
// appear in a signal/slot/Q_INVOKABLE signature, so moc needs nothing from
// them and a forward declaration is right.
//
// StreamPlayer is different and is INCLUDED above: it appears as a pointer
// parameter of Q_INVOKABLE methods, so moc instantiates Qt's metatype traits
// for rl::StreamPlayer* in its own translation unit. Qt decides
// IsPointerToTypeDerivedFromQObject by overload resolution against QObject*,
// which silently answers "false" for an incomplete type and "true" once the
// class is visible — so the same template specialisation had different values
// in the moc TU than in the TUs that include StreamPlayer.h. That is an ODR
// violation (GCC 16 reports it as -Wsfinae-incomplete) and it left the
// registered metatype without its PointerToQObject flag.
class BaichuanClient;
class ReolinkHttpClient;
}

namespace rl {

// The device tree behind the sidebar: hosts from the database, exposed to QML.
// Adding a camera/NVR validates it over the HTTP-CGI API on a worker thread
// (Login + GetDevInfo + GetEnc) and fills in name/model/codec. Credentials live
// in the keyring; the in-memory copy (loaded on a worker thread) lets liveUrl()
// build RTSP URLs without blocking the GUI thread on DBus.
class DeviceManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        AddrRole,
        KindRole,
        ModelRole,
        OnlineRole,
        StatusRole,
        HostIdRole,
        HasPtzRole,
        HasPtzPresetRole,
        HasZoomRole,
        HasAudioRole,
        HasSirenRole,
        HasLightRole,
        LightOnRole,
        LightTypeRole,
        HasLightBrightnessRole,
        HasBatteryRole,
        HasTalkRole,
        IsAdminRole,
        BatteryPercentRole,
        BatteryChargingRole,
        ChannelRole,
        ProblemRole,
        RotationRole,
        TransportRole,
    };

    DeviceManager(Database *db, CredentialStore *credentials, QObject *parent = nullptr);
    ~DeviceManager() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Camera/NVR by IP or hostname. Validates asynchronously; the row appears
    // immediately with status "connecting…".
    // Probe a device WITHOUT persisting anything: the Add Device dialog calls
    // this first so a typo'd address or rejected password becomes an inline
    // error instead of a phantom "connecting…" row. Result via testDeviceResult.
    Q_INVOKABLE void testDevice(const QString &addr, const QString &username,
                                const QString &password, bool https, int port);
    // User-driven revalidation of a host (sidebar "Reconnect").
    Q_INVOKABLE void reconnect(int row);
    // Fix a host's credentials in place (sidebar "Update credentials…"): stores
    // the new username/password and revalidates. Empty password keeps the stored
    // one (username-only fix).
    Q_INVOKABLE void updateCredentials(int row, const QString &username,
                                       const QString &password);
    // Manual view-rotation correction for a camera (degrees, multiples of 90),
    // persisted per hostId:channel. For streams whose rotation our heuristics
    // can't know (hub-attached doorbells and the like) — issue #3.
    Q_INVOKABLE void setRotationOverride(int row, int degrees);

    Q_INVOKABLE void addDevice(const QString &addr, const QString &username,
                               const QString &password, bool https = true, int port = 0);
    // Direct stream URL (rtsp://…, file …) — testing and generic-RTSP escape hatch.
    // Any embedded credentials are stripped to the keyring, not persisted in the DB.
    Q_INVOKABLE void addStreamUrl(const QString &name, const QString &url);
    Q_INVOKABLE void removeDevice(int row);

    // Playable URL for a device row; empty when credentials aren't loaded yet.
    Q_INVOKABLE QString liveUrl(int row, bool mainStream = true);

    // GetEnc-declared display size for a channel's main/sub stream (e.g. main
    // 7680x2160). Lets StreamPlayer detect a transmitted-rotated stream by
    // comparing this against the decoded frame size. Invalid if not yet primed.
    Q_INVOKABLE QSize declaredSize(int row, bool mainStream) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE QString transportAt(int row) const
    {
        return row >= 0 && row < m_entries.size() ? m_entries.at(row).rec.transport : QString();
    }
    Q_INVOKABLE int rowOfHost(qint64 hostId) const { return rowForHostId(hostId); }
    // The exact camera row for a host+channel (an NVR shares a host across cameras).
    Q_INVOKABLE int rowOfHostChannel(qint64 hostId, int channel) const
    {
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries.at(i).rec.id == hostId && m_entries.at(i).channel == channel)
                return i;
        return rowForHostId(hostId);
    }
    Q_INVOKABLE bool isAdminAt(int row) const
    {
        return row >= 0 && row < m_entries.size() && m_entries.at(row).isAdmin;
    }
    // This camera's channel index on its host (0 for a standalone camera; the NVR
    // channel for a fanned-out one). Lets QML target a live action (siren, etc.) at
    // the correct camera instead of a hardcoded channel 0.
    Q_INVOKABLE int channelOf(int row) const
    {
        return (row >= 0 && row < m_entries.size()) ? m_entries.at(row).channel : 0;
    }

    // Host-level summary (an NVR or a standalone camera), keyed by hostId — feeds
    // the sidebar's NVR header and the NVR properties dialog. Keys: name, kind,
    // model, addr, port, https, username, online, channelCount, onlineCount,
    // isAdmin, firstRow, transport.
    Q_INVOKABLE QVariantMap hostInfo(qint64 hostId) const;
    // Per-camera summary for the camera properties dialog. Keys: name, hostName,
    // hostId, channel, kind, model, codec, mainSize, subSize, uid, online, isAdmin,
    // and cap* booleans (ptz/zoom/audio/siren/light/lightBrightness/battery/talk),
    // plus lightOn, effective lightType, and raw reportedLightType. capLight is
    // true only for positively established physical visible illumination.
    Q_INVOKABLE QVariantMap cameraInfo(int row) const;
    // Distinct host ids in list order (NVRs and standalone cameras), for grouping.
    Q_INVOKABLE QVariantList hostIds() const;

    // Live controls (channel 0). Fire-and-forget on the worker pool.
    Q_INVOKABLE void ptzMove(int row, const QString &op, int speed = 32);
    Q_INVOKABLE void ptzStop(int row);
    Q_INVOKABLE void ptzPreset(int row, int presetId);
    // Everything the PTZ panel shows, read in one pass: GetZoomFocus (positions and
    // ranges), GetPtzPreset, GetPtzPatrol, GetPtzGuard and GetAiCfg.
    // Emits ptzStateLoaded(row, { cmd: value }); commands the device refuses are
    // left out. fetchZoomFocus re-reads just the lens positions after a move.
    Q_INVOKABLE void fetchPtzState(int row);
    Q_INVOKABLE void fetchZoomFocus(int row);
    // what: "zoom" | "focus"; pos within GetZoomFocus's range.
    Q_INVOKABLE void ptzZoomFocusTo(int row, const QString &what, int pos);
    // One-shot auto focus: zoom one step and back, which makes the camera run its
    // own autofocus. Reports ptzCommandDone("AutoFocus").
    Q_INVOKABLE void ptzAutoFocus(int row);
    // 3D positioning: pan, tilt and zoom so the box at (left, top), w x h, fills
    // the view; all fractions (0..1) of the frame of the stream being shown
    // (mainStream = HD). w = h = 0 centres the camera on (left, top).
    Q_INVOKABLE void ptz3D(int row, double left, double top, double w, double h, int speed,
                           bool mainStream);
    Q_INVOKABLE void ptzPatrol(int row, int patrolId, bool start);
    // Save the current position as preset presetId, or delete it (and its thumbnail).
    Q_INVOKABLE void ptzPresetSave(int row, int presetId, const QString &name);
    Q_INVOKABLE void ptzPresetDelete(int row, int presetId);
    // Where this app keeps a preset's thumbnail (the camera doesn't store one).
    Q_INVOKABLE QString ptzPresetImagePath(int row, int presetId) const;
    // cmdStr "toPos" goes to the guard point, "setPos" saves the current position.
    Q_INVOKABLE void ptzGuard(int row, const QString &cmdStr);
    Q_INVOKABLE void ptzGuardOptions(int row, bool enable, int timeoutSecs);
    // SetAiCfg with just these fields (bSmartTrack, trackType, aiStopBackTime...).
    Q_INVOKABLE void setAiTracking(int row, const QVariantMap &fields);
    // Capture a JPEG snapshot; emits snapshotSaved/snapshotFailed.
    Q_INVOKABLE void snapshot(int row);
    // Quiet snapshot for an event thumbnail: fetches a Snap JPEG, downscales it,
    // writes it under Paths::thumbnailsDir(), and emits eventThumbnailReady with
    // the caller's cookie (no snapshotSaved toast). Silent no-op on failure.
    void captureEventThumbnail(qint64 hostId, int channel, qint64 eventId);

    // Playback: search a day's recordings (emits recordingsFound with a list of
    // {start,end,type,name} where start/end are seconds into the day), and build
    // a playable URL for a recording file handle.
    Q_INVOKABLE void searchRecordings(int row, int year, int month, int day);
    // The day's detections from the device's own index (Baichuan alarm search):
    // reports alarmEventsFound with [{ start, end, kinds }], seconds into the day.
    Q_INVOKABLE void searchAlarmEvents(int row, int year, int month, int day);
    // Playable HTTP-FLV URL for a recording starting at startEpoch (Unix seconds).
    Q_INVOKABLE QString playbackUrl(int row, qint64 startEpoch, bool mainStream = false);
    // Full-resolution (main-stream) playback: download the clip covering
    // [startEpoch, startEpoch+durationSecs] to a local file, then emit hdClipReady
    // with its path (the file is playable/seekable locally, unlike the FLV path).
    Q_INVOKABLE void requestHdClip(int row, qint64 startEpoch, int durationSecs = 15);
    // Save a main-stream clip covering [startEpoch, +durationSecs] into
    // ~/Videos/Reolink with a friendly name. Emits clipExported/clipExportFailed.
    Q_INVOKABLE void exportClip(int row, qint64 startEpoch, int durationSecs);
    // Save [startEpoch, endEpoch) (up to a day) as one MP4 in ~/Videos/Reolink,
    // pulled over Baichuan playback and stream-copied (no re-encode), across
    // recording files. mainStream picks HD or SD. `folder` overrides the save
    // location (empty = ~/Videos/Reolink). One export at a time; emits
    // clipExportProgress while running, then clipExported or clipExportFailed.
    // Returns false, emitting nothing, when an export is already running.
    Q_INVOKABLE bool exportRange(int row, qint64 startEpoch, qint64 endEpoch, bool mainStream,
                                 const QString &folder = QString());
    // Where exports go when no folder is chosen.
    Q_INVOKABLE QString defaultExportFolder() const;
    // Stop the running export; what was saved so far is kept.
    Q_INVOKABLE void cancelExport();

    // Native (Baichuan/TCP 9000) recorded playback: stream the recording from
    // startEpoch straight into `player` — realtime, frame-accurate, full HEVC main.
    Q_INVOKABLE void startBaichuanPlayback(int row, qint64 startEpoch, rl::StreamPlayer *player,
                                           bool mainStream = true);
    // Native LIVE view over Baichuan (cmd 3 Preview), for the maximized pane's HD
    // stream. The NVR's RTSP output for 8K duo main streams is itself corrupt
    // (verified: raw ffmpeg -c copy captures carry invalid NALUs), so HD live must
    // go over Baichuan — the transport the official clients use. The NVR only
    // allows ~1 concurrent Baichuan session; callers fall back to RTSP on error.
    Q_INVOKABLE void startBaichuanLive(int row, rl::StreamPlayer *player,
                                       bool mainStream = true);
    // Seek the active Baichuan playback (same row) to a new instant in place — no
    // reconnect. Returns false if there's no live session for `row` (caller should
    // then startBaichuanPlayback instead).
    Q_INVOKABLE bool seekBaichuanPlayback(int row, qint64 startEpoch);
    // True while that seek is still waiting for its first frame (the picture
    // still shows the old position).
    Q_INVOKABLE bool baichuanPlaybackSeeking(int row) const;

    // Settings: fetch a batch of Get* commands (emits settingsLoaded with a map
    // of cmd -> value) and apply one Set* command (emits settingApplied).
    Q_INVOKABLE void fetchSettings(int row, const QStringList &getCommands);
    Q_INVOKABLE void applySetting(int row, const QString &setCommand, const QVariantMap &param);
    // Toggle a positively-capable camera's controllable visible light on<->off.
    // Reads GetWhiteLed first, then writes ONLY {channel,state}, leaving
    // brightness/mode/schedule/AI behavior untouched. Generic GetWhiteLed support
    // alone never authorizes this write.
    Q_INVOKABLE void toggleLight(int row);
    // Change illumination intensity without rewriting on/off state, mode,
    // schedules, or AI-trigger configuration. Native Baichuan task RMW first,
    // HTTP WhiteLed partial write as compatibility fallback.
    Q_INVOKABLE void setLightBrightness(int row, int brightness);
    Q_INVOKABLE void reboot(int row);

    // Alert-action config (push / email / ftp enable) over the native Baichuan
    // protocol (TCP 9000). The NVR's HTTP-CGI 502s on these commands under load,
    // but the app-native BC transport the official clients use is reliable.
    // fetchAlerts reads the current enables (emits alertsLoaded with keys
    // ok/push/email/ftp, each 0/1 or -1); setAlertEnable toggles one over BC and
    // emits settingApplied(kind, ...). kind = "push" | "email" | "ftp".
    Q_INVOKABLE void fetchAlerts(int row);
    Q_INVOKABLE void setAlertEnable(int row, const QString &kind, bool enable);

    // Whether this camera's Push Notifications action is enabled, from a cache
    // warmed shortly after startup and updated whenever alerts are read/toggled.
    // Gates desktop notifications for new detections; unknown => treated enabled.
    bool pushEnabledFor(qint64 hostId, int channel) const;

    // Motion-detection zone (grid mask) helpers. The device stores the zone as a
    // base64 <valueTable> in the MdAlarm config: one bit per cell (1 = detect),
    // row-major over columns x rows. mdZoneBits decodes it to a '0'/'1' string of
    // `cells` chars for the editor; mdZoneTable re-encodes a bit string to the
    // base64 the device expects (write it back via writeBcConfig(row, 46, 47, ...)).
    Q_INVOKABLE QString mdZoneBits(const QString &valueTable, int cells) const;
    Q_INVOKABLE QString mdZoneTable(const QString &bits) const;

    // Generic settings over Baichuan: read a command's config into a flat
    // { tag: value } map (emits bcConfigLoaded), and read-modify-write leaf tags
    // (emits settingApplied("Set<setCmd>", ...)). reqBody is the optional GET
    // request XML some commands need (e.g. AI detection selects an ai_type).
    // These bypass the 502-prone HTTP-CGI, like the official apps.
    Q_INVOKABLE void fetchBcConfig(int row, int cmdId, const QString &reqBody = QString());
    // Weekly recording schedule (BC cmd 81/82): per-type 168-char '0'/'1'
    // tables (7 days x 24 hours). fetchRecSchedule emits recScheduleLoaded with
    // { enable, <type>: table } (types e.g. Normal/MD/people/vehicle/dog_cat);
    // writeRecSchedule RMWs one type's table (emits settingApplied).
    Q_INVOKABLE void fetchRecSchedule(int row);
    Q_INVOKABLE void writeRecSchedule(int row, const QString &type, const QString &table);
    Q_INVOKABLE void writeBcConfig(int row, int getCmd, int setCmd, const QVariantMap &changes,
                                   const QString &reqBody = QString());

signals:
    void countChanged();
    void deviceError(const QString &addr, const QString &message);
    // Outcome of testDevice. problem is "", "transport", "auth", "locked" or
    // "protocol" so the dialog can phrase the failure usefully.
    void testDeviceResult(bool ok, const QString &message, const QString &name,
                          const QString &model, const QString &problem);
    void snapshotSaved(int row, const QString &path);
    void snapshotFailed(int row, const QString &error);
    void recordingsFound(int row, const QVariantList &segments);
    void recordingDaysFound(int row, int year, int month, const QVariantList &days);
    void recordingsFailed(int row, const QString &error);
    void alarmEventsFound(int row, int year, int month, int day, const QVariantList &events);
    void alarmEventsFailed(int row, int year, int month, int day, const QString &error);
    void hdClipReady(int row, const QString &localPath, qint64 startEpoch);
    void hdClipFailed(int row, const QString &error);
    void clipExported(int row, const QString &path);
    void clipExportFailed(int row, const QString &error);
    void clipExportProgress(int row, double fraction);
    void settingsLoaded(int row, const QVariantMap &values);
    void ptzStateLoaded(int row, const QVariantMap &values);
    // Outcome of a PTZ write (preset save/delete, guard, patrol, tracking, 3D...).
    void ptzCommandDone(int row, const QString &command, bool ok, const QString &error);
    void settingsFailed(int row, const QString &error);
    void settingStarted(int row, const QString &command);
    void settingApplied(int row, const QString &command, bool ok, const QString &error);
    void alertsLoaded(int row, const QVariantMap &values);
    void bcConfigLoaded(int row, int cmdId, const QVariantMap &values);
    void bcSettingsLoaded(int row, int cmdId, bool ok, bool transport, bool protocol,
                          int status, const QVariantMap &fields,
                          const QVariantMap &unknownFields);
    void recScheduleLoaded(int row, const QVariantMap &values);
    // Emitted on each detection 0->1 transition (feeds the event inbox).
    void detectionEvent(qint64 hostId, int channel, const QString &type, const QString &camera);
    // A quiet event-thumbnail capture finished (path is a local JPEG).
    void eventThumbnailReady(qint64 eventId, const QString &path);
    // A camera (channel >= 0) or a whole host (channel == -1) changed
    // connectivity. name is the camera/host display name.
    void connectivityChanged(qint64 hostId, int channel, const QString &name, bool online);

private slots:
    void pollDetections();

private:
    // One model row = one camera. A standalone camera is a host with a single
    // channel (0); an NVR fans out into one Entry per online channel, all sharing
    // the same host connection (hostId, addr, credentials, client).
    // Why a host isn't online. Drives both the sidebar visuals and the retry
    // policy: Unreachable is auto-retried with backoff (device off, wrong IP,
    // network down — retrying is free); Auth/Locked are NEVER auto-retried,
    // because each rejected login burns the firmware's lockout counter.
    enum class Problem { None, Connecting, Unreachable, Auth, Locked, Protocol };

    struct Entry {
        HostRecord rec;    // host connection (shared across an NVR's channels)
        int channel = 0;   // channel index on the host
        QString chanName;  // camera name for this channel
        bool online = false;
        QString status;
        Problem problem = Problem::Connecting; // until first validation lands
        int rotationOverride = 0;              // user view-rotation fix (degrees CW)
        QString mainCodec = QStringLiteral("h264"); // sub stream is always h264
        QSize mainSize;                             // GetEnc-declared main resolution
        QSize subSize;                              // GetEnc-declared sub resolution
        QString uid;                                // per-camera UID (Baichuan playback)
        QString password;                           // in-memory only (keyring at rest)
        bool primed = false;                        // password loaded?
        api::ChannelCaps caps;                      // this channel's capabilities
        int lightState = -1;                        // -1 unknown, 0 off, 1 on
        bool talk = false;                          // channel supports two-way audio
        bool isAdmin = false;                       // logged-in user may edit settings
        api::BatteryInfo battery;                   // battery/solar state (if any)
        // Seconds to add to a wall-clock epoch to get the NVR's playback reference
        // clock (PlaybackTime − StartTime), learned from the last recording search.
        // The HTTP-FLV playback endpoint seeks by this reference, not wall-clock.
        qint64 playbackOffsetSecs = 0;
        // Recording file boundaries (wall-clock start,end epochs) from the last
        // search. FLV playback must start on a boundary and use `seek` for the
        // offset into the file, so a scrub is snapped to the containing file.
        QVector<std::pair<qint64, qint64>> recordingFiles;
        // Persistent authenticated client for the host, shared by its channels.
        std::shared_ptr<ReolinkHttpClient> client;
    };

    // One camera's validated state (channel within a host).
    struct ChannelResult {
        int channel = 0;
        QString name;
        bool online = false;
        QString codec; // empty = unknown; the stream is probed instead of assumed
        QSize mainSize; // GetEnc-declared main/sub resolution (for rotation detection)
        QSize subSize;
        QString uid;
        api::ChannelCaps caps;
        int lightState = -1; // -1 unknown, 0 off, 1 on
    };

    // Outcome of a worker-thread validation, applied back on the GUI thread.
    struct Validation {
        bool online = false;
        QString status;
        Problem problem = Problem::Unreachable;
        QString hostName; // device/NVR name from GetDevInfo
        QString model;
        int channelNum = 0;
        bool isAdmin = false;
        bool talk = false;
        QString password;
        api::BatteryInfo battery;
        std::shared_ptr<ReolinkHttpClient> client;
        QVector<ChannelResult> channels; // one per camera to show
    };

    // Runs on a worker: loads the password from the keyring (or stores newPassword
    // first), logs in, fetches device info + encoding + abilities, updates the row
    // on the GUI thread. storeNew persists newPassword to the keyring first.
    void validateAsync(qint64 hostId, const QString &newPassword = QString(),
                       bool storeNew = false);
    int rowForHostId(qint64 hostId) const;
    void warmPushCache();   // fetch each camera's push state once, staggered
    void applyConnectivity(qint64 hostId, bool transportOk, const QHash<int, bool> &chanOnline);
    // Shared body of startBaichuanPlayback/startBaichuanLive (startEpoch<=0 = live).
    void startBaichuan(int row, qint64 startEpoch, rl::StreamPlayer *player, bool mainStream);
    // A wall-clock epoch as Baichuan by-time playback (cmd 143) wants it for this row.
    qint64 baichuanPlaybackEpoch(int row, qint64 epoch) const;
    std::shared_ptr<std::atomic<bool>> m_exportCancel; // set while an export runs
    void applyValidation(qint64 hostId, const Validation &v);
    void postValidation(qint64 hostId, const Validation &v); // marshals to GUI thread
    std::shared_ptr<ReolinkHttpClient> clientFor(int row);
    // Send one PTZ Set*/PtzCtrl command off the GUI thread; reports ptzCommandDone.
    void runPtzCommand(int row, const QString &label, const Json &command);
    // The same over Baichuan (TCP 9000): send body to cmdId; status 200 is success.
    void runBaichuanPtzCommand(int row, const QString &label, quint32 cmdId,
                               const QByteArray &body);

    Database *m_db;
    CredentialStore *m_credentials;
    QVector<Entry> m_entries;
    QFutureSynchronizer<void> m_pending; // drains in-flight validations at teardown
    // PtzCtrl moves and stops for one camera go out one at a time and only the
    // newest unsent one is kept. A joystick sends many updates; with a task per
    // command a stop could overtake an older move and leave the camera running.
    // Keyed by (host id, channel); one worker at a time drains `pending`.
    struct PtzMotion {
        QMutex mutex;
        Json pending;         // next command to send; null when none
        bool sending = false; // a worker is draining this camera's queue
    };
    QHash<QPair<qint64, int>, std::shared_ptr<PtzMotion>> m_ptzMotion;
    void sendPtzMotion(int row, Json command);

    QTimer m_pollTimer;
    // Keyed by "hostId:channel" so each NVR camera is tracked independently.
    QHash<QString, api::DetectionState> m_lastDetection; // for 0->1 edge detection
    QHash<QString, bool> m_pollInFlight;                 // avoid overlapping polls
    QHash<int, int> m_pushEnabled;  // row -> push enable (1/0/-1) for notif gating
    QHash<qint64, int> m_hostFails; // consecutive poll transport failures per host
    QMutex m_bcWriteMutex;
    bool m_pushWarmed = false;      // one-time warm of the push cache after priming

    // Auto-retry of Unreachable hosts, driven by the poll tick. Backoff doubles
    // 15s -> 300s cap; cleared on success or when the failure is Auth/Locked
    // (those wait for the user — see Problem).
    QHash<qint64, QDateTime> m_nextRetryAt;
    QHash<qint64, int> m_retryDelaySecs;
    QSet<qint64> m_validating;      // hosts with a validation in flight

    // The in-flight Baichuan playback session, so a scrub can seek it in place.
    // Weak: the StreamPlayer owns the client's lifetime.
    std::weak_ptr<BaichuanClient> m_playbackClient;
    int m_playbackRow = -1;
};

} // namespace rl
