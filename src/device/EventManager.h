#pragma once

#include "core/Database.h"
#include "core/Notifier.h"

#include <QAbstractListModel>

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

    Q_INVOKABLE void markAllRead();
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void unreadChanged();
    void filterChanged();
    // The user clicked a desktop notification — jump to this event's playback.
    void eventActivated(qint64 hostId, int channel, qint64 timestamp);

private slots:
    void onDetection(qint64 hostId, int channel, const QString &type, const QString &camera);
    void onThumbnailReady(qint64 eventId, const QString &path);

private:
    void reload();
    bool matches(const EventRecord &e) const;

    Database *m_db;
    DeviceManager *m_devices;
    Notifier m_notifier;             // desktop notifications for new detections
    QVector<EventRecord> m_all;      // newest first
    QVector<int> m_view;             // indices into m_all passing the filter
    QString m_filter;                // "" = all, else a type
    int m_unread = 0;
};

} // namespace rl
