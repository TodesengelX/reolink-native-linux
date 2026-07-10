#include "DeviceManager.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "protocol/ReolinkHttpClient.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QUrl>
#include <QVariant>
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
    for (const Entry &e : m_entries)
        validateAsync(e.rec.id);

    // Poll detection state for the event inbox. 5s balances latency vs load;
    // the real event push (Baichuan) lands with M12.
    m_pollTimer.setInterval(5000);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceManager::pollDetections);
    m_pollTimer.start();
}

void DeviceManager::pollDetections()
{
    for (int row = 0; row < m_entries.size(); ++row) {
        Entry &e = m_entries[row];
        if (!e.online || e.rec.kind == QLatin1String("stream") || !e.client)
            continue;
        const qint64 hostId = e.rec.id;
        if (m_pollInFlight.value(hostId, false))
            continue;
        m_pollInFlight[hostId] = true;
        auto client = e.client;
        const QString camera = e.rec.name;
        const bool wantAi = e.caps.ai;

        m_pending.addFuture(QtConcurrent::run([this, client, hostId, camera, wantAi] {
            Json cmds = Json::array({api::command(QStringLiteral("GetMdState"),
                                                  Json{{"channel", 0}})});
            if (wantAi)
                cmds.push_back(api::command(QStringLiteral("GetAiState"), Json{{"channel", 0}}));
            const api::BatchResult batch = client->call(cmds);

            api::DetectionState st;
            if (batch.transportOk) {
                for (const api::CommandResult &r : batch.results) {
                    if (r.cmd == QLatin1String("GetMdState") && r.ok)
                        st.motion = api::parseMdState(r.value);
                    else if (r.cmd == QLatin1String("GetAiState") && r.ok) {
                        const api::DetectionState ai = api::parseAiState(r.value);
                        st.person = ai.person;
                        st.vehicle = ai.vehicle;
                        st.pet = ai.pet;
                    }
                }
            }
            QMetaObject::invokeMethod(
                this,
                [this, hostId, camera, st] {
                    m_pollInFlight[hostId] = false;
                    const api::DetectionState prev = m_lastDetection.value(hostId);
                    // Emit an event for each object type on a 0->1 transition.
                    auto edge = [&](bool now, bool was, const char *type) {
                        if (now && !was)
                            emit detectionEvent(hostId, 0, QString::fromUtf8(type), camera);
                    };
                    // AI types take precedence over bare motion to avoid duplicates.
                    edge(st.person, prev.person, "person");
                    edge(st.vehicle, prev.vehicle, "vehicle");
                    edge(st.pet, prev.pet, "pet");
                    const bool aiActive = st.person || st.vehicle || st.pet;
                    const bool prevAi = prev.person || prev.vehicle || prev.pet;
                    if (!aiActive)
                        edge(st.motion, prev.motion || prevAi, "motion");
                    m_lastDetection[hostId] = st;
                },
                Qt::QueuedConnection);
        }));
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
    case HasPtzRole:
        return e.caps.ptz;
    case HasPtzPresetRole:
        return e.caps.ptzPreset;
    case HasZoomRole:
        return e.caps.zoom;
    case HasAudioRole:
        return e.caps.audio;
    case HasSirenRole:
        return e.caps.siren;
    case HasFloodlightRole:
        return e.caps.floodlight;
    case HasBatteryRole:
        return e.caps.battery;
    case HasTalkRole:
        return e.talk;
    case IsAdminRole:
        return e.isAdmin;
    case BatteryPercentRole:
        return e.battery.present ? e.battery.percent : -1;
    case BatteryChargingRole:
        return e.battery.charging;
    }
    return {};
}

QHash<int, QByteArray> DeviceManager::roleNames() const
{
    return {
        {NameRole, "name"},
        {AddrRole, "addr"},
        {KindRole, "kind"},
        {ModelRole, "model"},
        {OnlineRole, "online"},
        {StatusRole, "status"},
        {HostIdRole, "hostId"},
        {HasPtzRole, "hasPtz"},
        {HasPtzPresetRole, "hasPtzPreset"},
        {HasZoomRole, "hasZoom"},
        {HasAudioRole, "hasAudio"},
        {HasSirenRole, "hasSiren"},
        {HasFloodlightRole, "hasFloodlight"},
        {HasBatteryRole, "hasBattery"},
        {HasTalkRole, "hasTalk"},
        {IsAdminRole, "isAdmin"},
        {BatteryPercentRole, "batteryPercent"},
        {BatteryChargingRole, "batteryCharging"},
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

void DeviceManager::applyValidation(qint64 hostId, const Validation &v)
{
    const int row = rowForHostId(hostId);
    if (row < 0)
        return;
    Entry &e = m_entries[row];
    e.online = v.online;
    e.status = v.status;
    e.password = v.password;
    e.primed = true;
    e.client = v.client;
    if (v.online) {
        if (!v.name.isEmpty())
            e.rec.name = v.name;
        e.rec.model = v.model;
        if (!v.codec.isEmpty())
            e.mainCodec = v.codec;
        if (v.channelNum > 1)
            e.rec.kind = QStringLiteral("nvr");
        e.isAdmin = v.caps.isAdmin;
        e.battery = v.battery;
        if (!v.caps.channels.isEmpty())
            e.caps = v.caps.channels.first();
        e.talk = e.caps.talk; // the pane shows channel 0; use its talk capability
        m_db->updateHost(e.rec);
    } else {
        emit deviceError(e.rec.addr, v.status);
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void DeviceManager::postValidation(qint64 hostId, const Validation &v)
{
    QMetaObject::invokeMethod(
        this, [this, hostId, v] { applyValidation(hostId, v); }, Qt::QueuedConnection);
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
            Validation v;
            v.status = tr("keyring unavailable — password not saved");
            postValidation(hostId, v);
            return;
        }

        auto client = std::make_shared<ReolinkHttpClient>(rec.addr, rec.port, rec.https,
                                                          rec.username, password);
        const api::BatchResult batch = client->call(Json::array({
            api::command(QStringLiteral("GetDevInfo")),
            api::command(QStringLiteral("GetEnc")),
            api::command(QStringLiteral("GetAbility"),
                         Json{{"User", {{"userName", rec.username.toStdString()}}}}),
            // Harmless on mains-powered devices (returns an error we ignore).
            api::command(QStringLiteral("GetBatteryInfo"), Json{{"channel", 0}}),
        }));

        Validation v;
        v.password = password;
        v.status = tr("unreachable");
        if (batch.transportOk) {
            for (const api::CommandResult &r : batch.results) {
                if (r.cmd == QLatin1String("GetDevInfo") && r.ok) {
                    const Json info = jsonObj(r.value, "DevInfo");
                    v.name = QString::fromStdString(jsonStr(info, "name"));
                    v.model = QString::fromStdString(jsonStr(info, "model"));
                    v.channelNum = jsonInt(info, "channelNum", 1);
                    v.online = true;
                    v.status = tr("online");
                    v.client = client;
                } else if (r.cmd == QLatin1String("GetEnc") && r.ok) {
                    const Json mainStream = jsonObj(jsonObj(r.value, "Enc"), "mainStream");
                    v.codec = QString::fromStdString(jsonStr(mainStream, "vType", "h264"));
                } else if (r.cmd == QLatin1String("GetAbility") && r.ok) {
                    v.caps = api::parseAbility(r.value);
                } else if (r.cmd == QLatin1String("GetBatteryInfo") && r.ok) {
                    v.battery = api::parseBatteryInfo(r.value);
                }
            }
        }
        if (!v.online && !batch.error.isEmpty())
            v.status = batch.error;

        postValidation(hostId, v);
    }));
}

std::shared_ptr<ReolinkHttpClient> DeviceManager::clientFor(int row)
{
    if (row < 0 || row >= m_entries.size())
        return {};
    Entry &e = m_entries[row];
    if (e.rec.kind == QLatin1String("stream") || !e.primed)
        return {};
    if (!e.client)
        e.client = std::make_shared<ReolinkHttpClient>(e.rec.addr, e.rec.port, e.rec.https,
                                                       e.rec.username, e.password);
    return e.client;
}

void DeviceManager::ptzMove(int row, const QString &op, int speed)
{
    auto client = clientFor(row);
    if (!client)
        return;
    m_pending.addFuture(QtConcurrent::run(
        [client, op, speed] { client->call(Json::array({api::ptzCtrl(0, op, speed)})); }));
}

void DeviceManager::ptzStop(int row)
{
    auto client = clientFor(row);
    if (!client)
        return;
    m_pending.addFuture(QtConcurrent::run([client] {
        client->call(Json::array({api::ptzCtrl(0, QStringLiteral("Stop"))}));
    }));
}

void DeviceManager::ptzPreset(int row, int presetId)
{
    auto client = clientFor(row);
    if (!client)
        return;
    m_pending.addFuture(QtConcurrent::run([client, presetId] {
        client->call(Json::array({api::ptzCtrl(0, QStringLiteral("ToPos"), 32, presetId)}));
    }));
}

void DeviceManager::snapshot(int row)
{
    auto client = clientFor(row);
    if (!client) {
        emit snapshotFailed(row, tr("device not ready"));
        return;
    }
    const QString name = m_entries.at(row).rec.name;
    m_pending.addFuture(QtConcurrent::run([this, client, row, name] {
        QString error;
        const QByteArray jpeg = client->fetchSnapshot(0, &error);
        QString path;
        if (!jpeg.isEmpty()) {
            const QString safe = QString(name).replace(QRegularExpression(QStringLiteral("[^\\w-]")),
                                                        QStringLiteral("_"));
            const QString stamp =
                QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
            path = Paths::recordingsDir() + u'/' + safe + u'_' + stamp + QStringLiteral(".jpg");
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly) || f.write(jpeg) != jpeg.size()) {
                error = f.errorString();
                path.clear();
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, row, path, error] {
                if (!path.isEmpty())
                    emit snapshotSaved(row, path);
                else
                    emit snapshotFailed(row, error.isEmpty() ? tr("snapshot failed") : error);
            },
            Qt::QueuedConnection);
    }));
}

void DeviceManager::searchRecordings(int row, int year, int month, int day)
{
    auto client = clientFor(row);
    const QDate date(year, month, day);
    if (!client || !date.isValid()) {
        emit recordingsFailed(row, tr("device not ready"));
        return;
    }
    const QDateTime start(date, QTime(0, 0, 0));
    const QDateTime end(date, QTime(23, 59, 59));
    m_pending.addFuture(QtConcurrent::run([this, client, row, start, end, date, year, month] {
        const api::BatchResult batch =
            client->call(Json::array({api::searchBody(0, start, end, QStringLiteral("sub"))}));
        QVariantList segments;
        QVariantList days;
        QString error;
        if (batch.transportOk && !batch.results.isEmpty() && batch.results.first().ok) {
            const api::SearchResult sr = api::parseSearch(batch.results.first().value);
            for (const api::RecordingFile &f : sr.files) {
                if (!f.start.isValid())
                    continue;
                // Seconds into the query day (clamped to [0, 86400]).
                const qint64 dayStart = date.startOfDay().secsTo(f.start);
                const qint64 dayEnd = date.startOfDay().secsTo(f.end);
                const qint64 s = qBound(qint64(0), dayStart, qint64(86400));
                const qint64 e = qBound(s, dayEnd, qint64(86400));
                QVariantMap seg;
                seg["start"] = static_cast<double>(s);
                seg["end"] = static_cast<double>(e);
                // NVR Search reports the stream type, not the trigger — so we can't
                // tell timer from alarm here. Two-tone alarm coloring needs the
                // event log (GetEvents/Baichuan, future); default to timer.
                seg["type"] = QStringLiteral("timer");
                seg["startEpoch"] = static_cast<double>(f.start.toSecsSinceEpoch());
                segments.append(seg);
            }
            for (int d : sr.recordingDays)
                days.append(d);
        } else {
            error = batch.error.isEmpty() ? tr("search failed") : batch.error;
        }
        QMetaObject::invokeMethod(
            this,
            [this, row, segments, days, year, month, error] {
                if (error.isEmpty()) {
                    emit recordingsFound(row, segments);
                    emit recordingDaysFound(row, year, month, days);
                } else {
                    emit recordingsFailed(row, error);
                }
            },
            Qt::QueuedConnection);
    }));
}

QString DeviceManager::playbackUrl(int row, qint64 startEpoch, bool mainStream)
{
    if (row < 0 || row >= m_entries.size() || startEpoch <= 0)
        return {};
    const Entry &e = m_entries.at(row);
    if (e.rec.kind == QLatin1String("stream") || !e.primed)
        return {};
    // HTTP-FLV playback by start time (verified on RLN8-410). Credentials are
    // embedded and stripped from logs by StreamPlayer::redacted().
    const QDateTime start = QDateTime::fromSecsSinceEpoch(startEpoch);
    return api::playbackFlvUrl(e.rec.addr, e.rec.port, e.rec.https, /*channel=*/0, mainStream,
                               start, e.rec.username, e.password);
}

void DeviceManager::fetchSettings(int row, const QStringList &getCommands)
{
    auto client = clientFor(row);
    if (!client || getCommands.isEmpty()) {
        emit settingsFailed(row, tr("device not ready"));
        return;
    }
    Json cmds = Json::array();
    for (const QString &c : getCommands)
        cmds.push_back(api::command(c, Json{{"channel", 0}}, /*action=*/1));

    m_pending.addFuture(QtConcurrent::run([this, client, row, cmds] {
        const api::BatchResult batch = client->call(cmds);
        QVariantMap values;
        QString error;
        if (batch.transportOk) {
            for (const api::CommandResult &r : batch.results) {
                if (r.ok)
                    values.insert(r.cmd, api::toVariant(r.value));
            }
        } else {
            error = batch.error.isEmpty() ? tr("settings fetch failed") : batch.error;
        }
        QMetaObject::invokeMethod(
            this,
            [this, row, values, error] {
                if (error.isEmpty())
                    emit settingsLoaded(row, values);
                else
                    emit settingsFailed(row, error);
            },
            Qt::QueuedConnection);
    }));
}

void DeviceManager::applySetting(int row, const QString &setCommand, const QVariantMap &param)
{
    auto client = clientFor(row);
    if (!client) {
        emit settingApplied(row, setCommand, false, tr("device not ready"));
        return;
    }
    if (row < m_entries.size() && !m_entries.at(row).isAdmin) {
        emit settingApplied(row, setCommand, false, tr("requires an administrator account"));
        return;
    }
    const Json p = api::toJson(param);
    m_pending.addFuture(QtConcurrent::run([this, client, row, setCommand, p] {
        const api::CommandResult r = client->callOne(setCommand, p, /*action=*/0);
        const bool ok = r.ok;
        const QString error = ok ? QString()
                                 : (r.detail.isEmpty() ? tr("failed") : r.detail);
        QMetaObject::invokeMethod(
            this,
            [this, row, setCommand, ok, error] {
                emit settingApplied(row, setCommand, ok, error);
            },
            Qt::QueuedConnection);
    }));
}

void DeviceManager::reboot(int row)
{
    applySetting(row, QStringLiteral("Reboot"), {});
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
