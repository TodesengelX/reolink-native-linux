#include "DeviceManager.h"

#include "core/Log.h"
#include "protocol/ReolinkHttpClient.h"

#include <QtConcurrent/QtConcurrent>

namespace rl {

DeviceManager::DeviceManager(Database *db, CredentialStore *credentials, QObject *parent)
    : QAbstractListModel(parent), m_db(db), m_credentials(credentials)
{
    const QVector<HostRecord> stored = m_db->hosts();
    m_entries.reserve(stored.size());
    for (const HostRecord &rec : stored) {
        Entry e;
        e.rec = rec;
        // Direct-URL streams have no reachability handshake — consider them ready.
        e.online = rec.kind == QLatin1String("stream");
        e.status = e.online ? tr("ready") : tr("not connected");
        m_entries.append(e);
    }
}

int DeviceManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DeviceManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case NameRole:
        return e.rec.name;
    case AddrRole:
        return e.rec.addr;
    case KindRole:
        return e.rec.kind;
    case ModelRole:
        return e.rec.model;
    case OnlineRole:
        return e.online;
    case StatusRole:
        return e.status;
    case HostIdRole:
        return e.rec.id;
    }
    return {};
}

QHash<int, QByteArray> DeviceManager::roleNames() const
{
    return {
        {NameRole, "name"},     {AddrRole, "addr"},     {KindRole, "kind"},
        {ModelRole, "model"},   {OnlineRole, "online"}, {StatusRole, "status"},
        {HostIdRole, "hostId"},
    };
}

void DeviceManager::addDevice(const QString &addr, const QString &username,
                              const QString &password, bool https, int port)
{
    HostRecord rec;
    rec.kind = QStringLiteral("camera"); // refined to "nvr" after GetDevInfo
    rec.name = addr;
    rec.addr = addr;
    rec.https = https;
    rec.port = port > 0 ? port : (https ? 443 : 80);
    rec.username = username;
    rec.id = m_db->addHost(rec);
    if (rec.id < 0) {
        emit deviceError(addr, m_db->lastError());
        return;
    }
    if (!m_credentials->store(rec.id, password))
        qCWarning(lcCore) << "Keyring unavailable; device will require re-entry of password";

    beginInsertRows({}, m_entries.size(), m_entries.size());
    Entry e;
    e.rec = rec;
    e.status = tr("connecting…");
    m_entries.append(e);
    endInsertRows();
    emit countChanged();

    validateAsync(rec.id);
}

void DeviceManager::addStreamUrl(const QString &name, const QString &url)
{
    HostRecord rec;
    rec.kind = QStringLiteral("stream");
    rec.name = name.isEmpty() ? url : name;
    rec.addr = url;
    rec.https = false;
    rec.port = 0;
    rec.id = m_db->addHost(rec);
    if (rec.id < 0) {
        emit deviceError(url, m_db->lastError());
        return;
    }
    beginInsertRows({}, m_entries.size(), m_entries.size());
    Entry e;
    e.rec = rec;
    e.online = true;
    e.status = tr("ready");
    m_entries.append(e);
    endInsertRows();
    emit countChanged();
}

void DeviceManager::removeDevice(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const qint64 id = m_entries.at(row).rec.id;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    m_db->removeHost(id);
    m_credentials->remove(id);
    emit countChanged();
}

int DeviceManager::rowForHostId(qint64 hostId) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).rec.id == hostId)
            return i;
    return -1;
}

void DeviceManager::validateAsync(qint64 hostId)
{
    const int row = rowForHostId(hostId);
    if (row < 0)
        return;
    const HostRecord rec = m_entries.at(row).rec;
    const QString password = m_credentials->lookup(hostId);

    auto future = QtConcurrent::run([this, rec, password, hostId] {
        ReolinkHttpClient client(rec.addr, rec.port, rec.https, rec.username, password);
        const api::CommandResult devInfo = client.callOne(QStringLiteral("GetDevInfo"));

        QMetaObject::invokeMethod(
            this,
            [this, hostId, devInfo] {
                const int row = rowForHostId(hostId);
                if (row < 0)
                    return;
                Entry &e = m_entries[row];
                if (devInfo.ok) {
                    const Json info = devInfo.value.value("DevInfo", Json::object());
                    const QString name =
                        QString::fromStdString(info.value("name", std::string{}));
                    e.rec.name = name.isEmpty() ? e.rec.addr : name;
                    e.rec.model = QString::fromStdString(info.value("model", std::string{}));
                    if (info.value("channelNum", 1) > 1)
                        e.rec.kind = QStringLiteral("nvr");
                    e.online = true;
                    e.status = tr("online");
                    m_db->updateHost(e.rec);
                } else {
                    e.online = false;
                    e.status = devInfo.detail.isEmpty() ? tr("unreachable") : devInfo.detail;
                    emit deviceError(e.rec.addr, e.status);
                }
                const QModelIndex idx = index(row);
                emit dataChanged(idx, idx);
            },
            Qt::QueuedConnection);
    });
    Q_UNUSED(future)
}

QString DeviceManager::nameAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).rec.name : QString();
}

QString DeviceManager::liveUrl(int row, bool mainStream)
{
    if (row < 0 || row >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(row);
    if (e.rec.kind == QLatin1String("stream"))
        return e.rec.addr;
    // TODO(async): keyring lookup blocks on DBus; move URL resolution off the
    // GUI thread when the grid grows beyond a handful of panes.
    bool ok = false;
    const QString password = m_credentials->lookup(e.rec.id, &ok);
    if (!ok)
        return {};
    return api::rtspUrl(e.rec.addr, e.rec.username, password, /*channel=*/0, mainStream);
}

} // namespace rl
