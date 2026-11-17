#pragma once

#include "core/Database.h"
#include "core/Notifier.h"

#include <QAbstractListModel>
#include <QHash>

namespace rl {

class DeviceManager;

// The event inbox: persists detection events (from DeviceManager's poller) and
// exposes them newest-first to QML, with a type filter and an unread counter.
class EventManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int unread READ unread NOTIFY unreadChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString cameraFilter READ cameraFilter WRITE setCameraFilter NOTIFY cameraFilterChanged)
    // Desktop pop-ups for new events. Off still records every event in the
    // inbox; it only silences the notification. Persisted across runs.
    Q_PROPERTY(bool popupsEnabled READ popupsEnabled WRITE setPopupsEnabled NOTIFY popupsEnabledChanged)
    // Inbox cleanup, all persisted:
    // - repeats of an event on the same camera are folded into the first one
    //   until the camera has been quiet for mergeSeconds (0 = log every one);
    // - motionEvents off drops plain-motion events (AI types are kept);
    // - events older than retentionDays are deleted (0 = keep forever).
    Q_PROPERTY(int mergeSeconds READ mergeSeconds WRITE setMergeSeconds NOTIFY cleanupChanged)
    Q_PROPERTY(bool motionEvents READ motionEvents WRITE setMotionEvents NOTIFY cleanupChanged)
    Q_PROPERTY(int retentionDays READ retentionDays WRITE setRetentionDays NOTIFY cleanupChanged)

public:
    enum Role {
        TypeRole = Qt::UserRole + 1,
        CameraRole,
        TimestampRole,
        TimeTextRole,
        ThumbnailRole,
        HostIdRole,
        ChannelRole,
    };

    EventManager(Database *db, DeviceManager *devices, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int unread() const { return m_unread; }
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString cameraFilter() const { return m_cameraFilter; }
    void setCameraFilter(const QString &camera);
    bool popupsEnabled() const { return m_popupsEnabled; }
    void setPopupsEnabled(bool on);
    int mergeSeconds() const { return m_mergeSeconds; }
    void setMergeSeconds(int seconds);
    bool motionEvents() const { return m_motionEvents; }
    void setMotionEvents(bool on);
    int retentionDays() const { return m_retentionDays; }
    void setRetentionDays(int days);

    Q_INVOKABLE void markAllRead();
    // Seconds-into-day of each detection event for this camera on the given day
    // (dayStart = local midnight epoch). Feeds the playback timeline's markers.
    Q_INVOKABLE QVariantList eventTimesFor(qint64 hostId, int channel, qint64 dayStart) const;
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void unreadChanged();
    void filterChanged();
    void cameraFilterChanged();
    void popupsEnabledChanged();
    void cleanupChanged();
    // The user clicked a desktop notification — jump to this event's playback.
    void eventActivated(qint64 hostId, int channel, qint64 timestamp);

private slots:
    void onDetection(qint64 hostId, int channel, const QString &type, const QString &camera);
    void onThumbnailReady(qint64 eventId, const QString &path);
    void onConnectivity(qint64 hostId, int channel, const QString &name, bool online);

private:
    void reload();
    // True when this detection continues one already in the inbox (same
    // camera, still active within the merge window). Records it as seen.
    bool isRepeat(qint64 hostId, int channel, const QString &type, qint64 now);
    void purgeExpired();
    bool matches(const EventRecord &e) const;

    Database *m_db;
    DeviceManager *m_devices;
    Notifier m_notifier;             // desktop notifications for new detections
    QVector<EventRecord> m_all;      // newest first
    QVector<int> m_view;             // indices into m_all passing the filter
    QString m_filter;                // "" = all, else a type
    QString m_cameraFilter;          // "" = all cameras, else a camera name
    int m_unread = 0;
    bool m_popupsEnabled = true;
    int m_mergeSeconds = 60;
    bool m_motionEvents = true;
    int m_retentionDays = 7;
    QHash<QString, qint64> m_lastSeen; // "host:channel:type" -> last detection (Unix s)
};

} // namespace rl
