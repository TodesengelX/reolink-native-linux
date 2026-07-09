#include "DeviceManager.h"

#include "core/Log.h"
#include "protocol/ReolinkHttpClient.h"

#include <QUrl>
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
        e.online = rec.kind == QLatin1String("stream");
        e.status = e.online ? tr("ready") : tr("connecting…");
        m_entries.append(e);
    }
    // Prime credentials + refresh status for stored camera/NVR devices on startup
    // (also loads passwords into memory so liveUrl never blocks on the keyring).
    for (const Entry &e : m_entries) {
        if (e.rec.kind != QLatin1String("stream"))
            validateAsync(e.rec.id);
        else
            validateAsync(e.rec.id); // stream: just load stripped creds into memory
    }
}

DeviceManager::~DeviceManager()
{
    // Wait for in-flight validation tasks so their `this`-capturing lambdas can't
    // outlive us. Bounded by the HTTP timeouts (short Logout included).
    m_pending.waitForFinished();
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

    beginInsertRows({}, m_entries.size(), m_entries.size());
    Entry e;
    e.rec = rec;
    e.status = tr("connecting…");
    m_entries.append(e);
    endInsertRows();
    emit countChanged();

    // Store the password to the keyring and validate, both on the worker thread.
    validateAsync(rec.id, password, /*storeNew=*/true);
}

void DeviceManager::addStreamUrl(const QString &name, const QString &url)
{
    // Strip any embedded credentials: they go to the keyring, never the DB.
    QUrl u(url);
    const QString user = u.userName();
    const QString pass = u.password();
    if (!user.isEmpty() || !pass.isEmpty()) {
        u.setUserName({});
        u.setPassword({});
    }
    const QString cleanUrl = u.isValid() && !u.scheme().isEmpty() ? u.toString(QUrl::FullyEncoded)
                                                                  : url;

    HostRecord rec;
    rec.kind = QStringLiteral("stream");
    rec.name = name.isEmpty() ? cleanUrl : name;
    rec.addr = cleanUrl;
    rec.https = false;
    rec.port = 0;
    rec.username = user;
    rec.id = m_db->addHost(rec);
    if (rec.id < 0) {
        emit deviceError(url, m_db->lastError());
        return;
    }
    if (!pass.isEmpty() && !m_credentials->store(rec.id, pass))
        qCWarning(lcCore) << "Keyring unavailable; stream credentials not saved";

    beginInsertRows({}, m_entries.size(), m_entries.size());
    Entry e;
    e.rec = rec;
    e.online = true;
    e.status = tr("ready");
    e.password = pass;
    e.primed = true;
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

void DeviceManager::applyValidation(qint64 hostId, bool online, const QString &status,
                                    const QString &name, const QString &model,
                                    const QString &codec, int channelNum, const QString &password)
{
    const int row = rowForHostId(hostId);
    if (row < 0)
        return;
    Entry &e = m_entries[row];
    e.online = online;
    e.status = status;
    e.password = password;
    e.primed = true;
    if (online) {
        if (!name.isEmpty())
            e.rec.name = name;
        e.rec.model = model;
        if (!codec.isEmpty())
            e.mainCodec = codec;
        if (channelNum > 1)
            e.rec.kind = QStringLiteral("nvr");
        m_db->updateHost(e.rec);
    } else {
        emit deviceError(e.rec.addr, status);
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void DeviceManager::validateAsync(qint64 hostId, const QString &newPassword, bool storeNew)
{
    const int row = rowForHostId(hostId);
    if (row < 0)
        return;
    const HostRecord rec = m_entries.at(row).rec;
    const bool isStream = rec.kind == QLatin1String("stream");

    m_pending.addFuture(QtConcurrent::run([this, rec, newPassword, storeNew, isStream, hostId] {
        // Keyring work happens here, off the GUI thread.
        if (storeNew && !newPassword.isEmpty())
            m_credentials->store(hostId, newPassword);

        bool havePassword = false;
        QString password = newPassword;
        if (password.isEmpty())
            password = m_credentials->lookup(hostId, &havePassword);
        else
            havePassword = true;

        // Stream devices just need their (optional) credential loaded into memory.
        if (isStream) {
            const QString pw = password;
            QMetaObject::invokeMethod(
                this,
                [this, hostId, pw] {
                    const int r = rowForHostId(hostId);
                    if (r >= 0) {
                        m_entries[r].password = pw;
                        m_entries[r].primed = true;
                    }
                },
                Qt::QueuedConnection);
            return;
        }

        if (!havePassword && storeNew) {
            QMetaObject::invokeMethod(
                this,
                [this, hostId] {
                    applyValidation(hostId, false, tr("keyring unavailable — password not saved"),
                                    {}, {}, {}, 0, {});
                },
                Qt::QueuedConnection);
            return;
        }

        ReolinkHttpClient client(rec.addr, rec.port, rec.https, rec.username, password);
        const api::BatchResult batch = client.call(Json::array({
            api::command(QStringLiteral("GetDevInfo")),
            api::command(QStringLiteral("GetEnc")),
        }));

        bool online = false;
        QString status = tr("unreachable");
        QString name, model, codec;
        int channelNum = 0;

        if (batch.transportOk) {
            for (const api::CommandResult &r : batch.results) {
                if (r.cmd == QLatin1String("GetDevInfo") && r.ok) {
                    const Json info = jsonObj(r.value, "DevInfo");
                    name = QString::fromStdString(jsonStr(info, "name"));
                    model = QString::fromStdString(jsonStr(info, "model"));
                    channelNum = jsonInt(info, "channelNum", 1);
                    online = true;
                    status = tr("online");
                } else if (r.cmd == QLatin1String("GetEnc") && r.ok) {
                    const Json enc = jsonObj(r.value, "Enc");
                    const Json mainStream = jsonObj(enc, "mainStream");
                    codec = QString::fromStdString(jsonStr(mainStream, "vType", "h264"));
                }
            }
        }
        if (!online && !batch.error.isEmpty())
            status = batch.error;

        const QString pw = password;
        QMetaObject::invokeMethod(
            this,
            [this, hostId, online, status, name, model, codec, channelNum, pw] {
                applyValidation(hostId, online, status, name, model, codec, channelNum, pw);
            },
            Qt::QueuedConnection);
    }));
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

    if (e.rec.kind == QLatin1String("stream")) {
        if (e.rec.username.isEmpty() && e.password.isEmpty())
            return e.rec.addr;
        QUrl u(e.rec.addr);
        u.setUserName(e.rec.username);
        u.setPassword(e.password);
        return u.toString(QUrl::FullyEncoded);
    }

    if (!e.primed) // credentials not loaded yet; the pane retries when data changes
        return {};
    // Sub stream ("Fluent") is h264 on all models; main ("Clear") may be h265.
    const QString codec = mainStream ? e.mainCodec : QStringLiteral("h264");
    return api::rtspUrl(e.rec.addr, e.rec.username, e.password, /*channel=*/0, mainStream, codec);
}

} // namespace rl
