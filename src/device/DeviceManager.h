#pragma once

#include "core/CredentialStore.h"
#include "core/Database.h"
#include "protocol/ReolinkApi.h"

#include <QAbstractListModel>
#include <QFutureSynchronizer>
#include <QHash>
#include <QSize>
#include <QTimer>

#include <memory>

namespace rl {
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
        HasFloodlightRole,
        HasBatteryRole,
        HasTalkRole,
        IsAdminRole,
        BatteryPercentRole,
        BatteryChargingRole,
        ChannelRole,
    };

    DeviceManager(Database *db, CredentialStore *credentials, QObject *parent = nullptr);
    ~DeviceManager() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Camera/NVR by IP or hostname. Validates asynchronously; the row appears
    // immediately with status "connecting…".
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

    // Live controls (channel 0). Fire-and-forget on the worker pool.
    Q_INVOKABLE void ptzMove(int row, const QString &op, int speed = 32);
    Q_INVOKABLE void ptzStop(int row);
    Q_INVOKABLE void ptzPreset(int row, int presetId);
    // Capture a JPEG snapshot; emits snapshotSaved/snapshotFailed.
    Q_INVOKABLE void snapshot(int row);

    // Playback: search a day's recordings (emits recordingsFound with a list of
    // {start,end,type,name} where start/end are seconds into the day), and build
    // a playable URL for a recording file handle.
    Q_INVOKABLE void searchRecordings(int row, int year, int month, int day);
    // Playable HTTP-FLV URL for a recording starting at startEpoch (Unix seconds).
    Q_INVOKABLE QString playbackUrl(int row, qint64 startEpoch, bool mainStream = false);
    // Full-resolution (main-stream) playback: download the clip covering
    // [startEpoch, startEpoch+durationSecs] to a local file, then emit hdClipReady
    // with its path (the file is playable/seekable locally, unlike the FLV path).
    Q_INVOKABLE void requestHdClip(int row, qint64 startEpoch, int durationSecs = 15);

    // Settings: fetch a batch of Get* commands (emits settingsLoaded with a map
    // of cmd -> value) and apply one Set* command (emits settingApplied).
    Q_INVOKABLE void fetchSettings(int row, const QStringList &getCommands);
    Q_INVOKABLE void applySetting(int row, const QString &setCommand, const QVariantMap &param);
    Q_INVOKABLE void reboot(int row);

signals:
    void countChanged();
    void deviceError(const QString &addr, const QString &message);
    void snapshotSaved(int row, const QString &path);
    void snapshotFailed(int row, const QString &error);
    void recordingsFound(int row, const QVariantList &segments);
    void recordingDaysFound(int row, int year, int month, const QVariantList &days);
    void recordingsFailed(int row, const QString &error);
    void hdClipReady(int row, const QString &localPath, qint64 startEpoch);
    void hdClipFailed(int row, const QString &error);
    void settingsLoaded(int row, const QVariantMap &values);
    void settingsFailed(int row, const QString &error);
    void settingApplied(int row, const QString &command, bool ok, const QString &error);
    // Emitted on each detection 0->1 transition (feeds the event inbox).
    void detectionEvent(qint64 hostId, int channel, const QString &type, const QString &camera);

private slots:
    void pollDetections();

private:
    // One model row = one camera. A standalone camera is a host with a single
    // channel (0); an NVR fans out into one Entry per online channel, all sharing
    // the same host connection (hostId, addr, credentials, client).
    struct Entry {
        HostRecord rec;    // host connection (shared across an NVR's channels)
        int channel = 0;   // channel index on the host
        QString chanName;  // camera name for this channel
        bool online = false;
        QString status;
        QString mainCodec = QStringLiteral("h264"); // sub stream is always h264
        QSize mainSize;                             // GetEnc-declared main resolution
        QSize subSize;                              // GetEnc-declared sub resolution
        QString password;                           // in-memory only (keyring at rest)
        bool primed = false;                        // password loaded?
        api::ChannelCaps caps;                      // this channel's capabilities
        bool talk = false;                          // channel supports two-way audio
        bool isAdmin = false;                       // logged-in user may edit settings
        api::BatteryInfo battery;                   // battery/solar state (if any)
        // Seconds to add to a wall-clock epoch to get the NVR's playback reference
        // clock (PlaybackTime − StartTime), learned from the last recording search.
        // The HTTP-FLV playback endpoint seeks by this reference, not wall-clock.
        qint64 playbackOffsetSecs = 0;
        // Persistent authenticated client for the host, shared by its channels.
        std::shared_ptr<ReolinkHttpClient> client;
    };

    // One camera's validated state (channel within a host).
    struct ChannelResult {
        int channel = 0;
        QString name;
        bool online = false;
        QString codec = QStringLiteral("h264");
        QSize mainSize; // GetEnc-declared main/sub resolution (for rotation detection)
        QSize subSize;
        api::ChannelCaps caps;
    };

    // Outcome of a worker-thread validation, applied back on the GUI thread.
    struct Validation {
        bool online = false;
        QString status;
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
    void applyValidation(qint64 hostId, const Validation &v);
    void postValidation(qint64 hostId, const Validation &v); // marshals to GUI thread
    std::shared_ptr<ReolinkHttpClient> clientFor(int row);

    Database *m_db;
    CredentialStore *m_credentials;
    QVector<Entry> m_entries;
    QFutureSynchronizer<void> m_pending; // drains in-flight validations at teardown

    QTimer m_pollTimer;
    // Keyed by "hostId:channel" so each NVR camera is tracked independently.
    QHash<QString, api::DetectionState> m_lastDetection; // for 0->1 edge detection
    QHash<QString, bool> m_pollInFlight;                 // avoid overlapping polls
};

} // namespace rl
