#include "EventManager.h"

#include "DeviceManager.h"

#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

namespace rl {

// Retention cap: the inbox keeps only the newest N events, discarding older ones
// from both the model and the database.
static constexpr int kMaxEvents = 100;

EventManager::EventManager(Database *db, DeviceManager *devices, QObject *parent)
    : QAbstractListModel(parent), m_db(db), m_devices(devices)
{
    m_db->trimEvents(kMaxEvents);   // enforce the cap on any pre-existing history
    reload();
    connect(m_devices, &DeviceManager::detectionEvent, this, &EventManager::onDetection);
    connect(&m_notifier, &Notifier::activated, this, &EventManager::eventActivated);
    connect(m_devices, &DeviceManager::eventThumbnailReady, this,
            &EventManager::onThumbnailReady);
    connect(m_devices, &DeviceManager::connectivityChanged, this,
            &EventManager::onConnectivity);

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
    const bool typeOk = m_filter.isEmpty() || e.type == m_filter;
    const bool camOk = m_cameraFilter.isEmpty() || e.camera == m_cameraFilter;
    return typeOk && camOk;
}

void EventManager::reload()
{
    beginResetModel();
    m_all = m_db->recentEvents(kMaxEvents);
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

void EventManager::setCameraFilter(const QString &camera)
{
    if (m_cameraFilter == camera)
        return;
    m_cameraFilter = camera;
    emit cameraFilterChanged();
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
    // Grab a frame from the camera for the inbox thumbnail (quiet; the row
    // updates in place when it lands).
    if (rec.id > 0)
        m_devices->captureEventThumbnail(hostId, channel, rec.id);

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

    // Desktop notification for this detection, gated on the camera's Push
    // Notifications being enabled (mirrors how the official apps gate push).
    if (m_devices->pushEnabledFor(hostId, channel)) {
        QString what;
        if (type == QLatin1String("person"))       what = tr("Person detected");
        else if (type == QLatin1String("vehicle")) what = tr("Vehicle detected");
        else if (type == QLatin1String("pet") || type == QLatin1String("dog_cat"))
            what = tr("Pet detected");
        else if (type == QLatin1String("visitor")) what = tr("Visitor at the door");
        else                                        what = tr("Motion detected");
        m_notifier.notify(camera.isEmpty() ? tr("Camera") : camera, what,
                          QStringLiteral("io.github.todesengelx.ReolinkLinux"),
                          hostId, channel, rec.timestamp);
    }

    // Enforce the retention cap: drop the oldest events beyond kMaxEvents from
    // the view (highest indices, since m_view is ascending), the in-memory list,
    // and the database. Normally this trims exactly one event per new arrival.
    if (m_all.size() > kMaxEvents) {
        for (int vi = m_view.size() - 1; vi >= 0 && m_view[vi] >= kMaxEvents; --vi) {
            beginRemoveRows({}, vi, vi);
            m_view.remove(vi);
            endRemoveRows();
        }
        for (int i = kMaxEvents; i < m_all.size(); ++i)
            if (!m_all.at(i).thumbnail.isEmpty())
                QFile::remove(m_all.at(i).thumbnail);
        m_all.resize(kMaxEvents);
        m_db->trimEvents(kMaxEvents);
        emit countChanged();
    }
}

void EventManager::onConnectivity(qint64 hostId, int channel, const QString &name, bool online)
{
    // Health alert: a camera (channel >= 0) or a whole host dropped or returned.
    // Recorded in the inbox and always notified — a dead camera can't announce
    // itself, so the client has to.
    EventRecord rec;
    rec.hostId = hostId;
    rec.channel = qMax(0, channel);
    rec.timestamp = QDateTime::currentSecsSinceEpoch();
    rec.type = online ? QStringLiteral("online") : QStringLiteral("offline");
    rec.camera = name;
    rec.id = m_db->addEvent(rec);

    m_all.prepend(rec);
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

    m_notifier.notify(name.isEmpty() ? tr("Device") : name,
                      online ? tr("Back online") : tr("Went offline \u26a0"),
                      QStringLiteral("io.github.todesengelx.ReolinkLinux"),
                      hostId, rec.channel, rec.timestamp);

    if (m_all.size() > kMaxEvents) {
        for (int vi = m_view.size() - 1; vi >= 0 && m_view[vi] >= kMaxEvents; --vi) {
            beginRemoveRows({}, vi, vi);
            m_view.remove(vi);
            endRemoveRows();
        }
        for (int i = kMaxEvents; i < m_all.size(); ++i)
            if (!m_all.at(i).thumbnail.isEmpty())
                QFile::remove(m_all.at(i).thumbnail);
        m_all.resize(kMaxEvents);
        m_db->trimEvents(kMaxEvents);
        emit countChanged();
    }
}

void EventManager::onThumbnailReady(qint64 eventId, const QString &path)
{
    m_db->setEventThumbnail(eventId, path);
    for (int i = 0; i < m_all.size(); ++i) {
        if (m_all.at(i).id != eventId)
            continue;
        m_all[i].thumbnail = path;
        const int vi = m_view.indexOf(i);
        if (vi >= 0)
            emit dataChanged(index(vi), index(vi), {ThumbnailRole});
        return;
    }
    // Event already trimmed away — don't keep an orphaned file.
    QFile::remove(path);
}

QVariantList EventManager::eventTimesFor(qint64 hostId, int channel, qint64 dayStart) const
{
    QVariantList out;
    const qint64 dayEnd = dayStart + 86400;
    for (const EventRecord &e : m_all) {
        if (e.hostId != hostId || e.channel != channel)
            continue;
        if (e.timestamp < dayStart || e.timestamp >= dayEnd)
            continue;
        if (e.type == QLatin1String("offline") || e.type == QLatin1String("online"))
            continue; // connectivity events aren't recordings
        out.append(int(e.timestamp - dayStart));
    }
    return out;
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
    // Drop the cached snapshot files along with the rows.
    QDir dir(Paths::thumbnailsDir());
    for (const QString &f : dir.entryList({QStringLiteral("event_*.jpg")}, QDir::Files))
        dir.remove(f);
    m_db->clearEvents();
    m_unread = 0;
    emit unreadChanged();
    reload();
}

} // namespace rl
