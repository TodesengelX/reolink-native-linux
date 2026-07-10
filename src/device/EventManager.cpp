#include "EventManager.h"

#include "DeviceManager.h"

#include <QDateTime>

namespace rl {

EventManager::EventManager(Database *db, DeviceManager *devices, QObject *parent)
    : QAbstractListModel(parent), m_db(db), m_devices(devices)
{
    reload();
    connect(m_devices, &DeviceManager::detectionEvent, this, &EventManager::onDetection);

    // Test hook: RL_MOCK_EVENTS seeds in-memory events (no DB writes) so the
    // inbox UI can be verified without cameras.
    if (qEnvironmentVariableIsSet("RL_MOCK_EVENTS")) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const struct {
            const char *type;
            const char *cam;
            qint64 ago;
        } mock[] = {
            {"person", "Front Door", 45},   {"vehicle", "Driveway", 600},
            {"pet", "Backyard", 3600},      {"visitor", "Front Door", 7200},
            {"motion", "Side Gate", 90000},
        };
        beginResetModel();
        m_all.clear();
        m_view.clear();
        for (const auto &m : mock) {
            EventRecord r;
            r.type = QString::fromUtf8(m.type);
            r.camera = QString::fromUtf8(m.cam);
            r.timestamp = now - m.ago;
            m_all.append(r);
        }
        for (int i = 0; i < m_all.size(); ++i)
            if (matches(m_all[i]))
                m_view.append(i);
        endResetModel();
        emit countChanged();
    }
}

bool EventManager::matches(const EventRecord &e) const
{
    return m_filter.isEmpty() || e.type == m_filter;
}

void EventManager::reload()
{
    beginResetModel();
    m_all = m_db->recentEvents(500);
    m_view.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (matches(m_all[i]))
            m_view.append(i);
    endResetModel();
    emit countChanged();
}

int EventManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_view.size();
}

QVariant EventManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_view.size())
        return {};
    const EventRecord &e = m_all.at(m_view.at(index.row()));
    switch (role) {
    case TypeRole:
        return e.type;
    case CameraRole:
        return e.camera;
    case TimestampRole:
        return e.timestamp;
    case TimeTextRole: {
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(e.timestamp);
        const qint64 ago = QDateTime::currentSecsSinceEpoch() - e.timestamp;
        if (ago < 60)
            return tr("just now");
        if (ago < 3600)
            return tr("%1 min ago").arg(ago / 60);
        if (dt.date() == QDate::currentDate())
            return dt.toString(QStringLiteral("hh:mm"));
        return dt.toString(QStringLiteral("MMM d, hh:mm"));
    }
    case ThumbnailRole:
        return e.thumbnail;
    case HostIdRole:
        return e.hostId;
    case ChannelRole:
        return e.channel;
    }
    return {};
}

QHash<int, QByteArray> EventManager::roleNames() const
{
    return {
        {TypeRole, "type"},         {CameraRole, "camera"}, {TimestampRole, "timestamp"},
        {TimeTextRole, "timeText"}, {ThumbnailRole, "thumbnail"}, {HostIdRole, "hostId"},
        {ChannelRole, "channel"},
    };
}

void EventManager::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    reload();
}

void EventManager::onDetection(qint64 hostId, int channel, const QString &type,
                               const QString &camera)
{
    EventRecord rec;
    rec.hostId = hostId;
    rec.channel = channel;
    rec.timestamp = QDateTime::currentSecsSinceEpoch();
    rec.type = type;
    rec.camera = camera;
    rec.id = m_db->addEvent(rec);

    m_all.prepend(rec);
    // Shift existing view indices (they point into m_all, which just grew at 0).
    for (int &idx : m_view)
        ++idx;
    if (matches(rec)) {
        beginInsertRows({}, 0, 0);
        m_view.prepend(0);
        endInsertRows();
        emit countChanged();
    }
    ++m_unread;
    emit unreadChanged();
}

void EventManager::markAllRead()
{
    if (m_unread == 0)
        return;
    m_unread = 0;
    emit unreadChanged();
}

void EventManager::clear()
{
    m_db->clearEvents();
    m_unread = 0;
    emit unreadChanged();
    reload();
}

} // namespace rl
