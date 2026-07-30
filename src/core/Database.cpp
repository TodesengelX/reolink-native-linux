#include "Database.h"

#include "Log.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace rl {

// A null QString binds as SQL NULL, which violates our NOT NULL text columns;
// the schema wants empty strings.
static QString sqlText(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

Database::Database(const QString &filePath, const QString &connectionName)
    : m_filePath(filePath), m_connectionName(connectionName)
{
}

Database::~Database()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QSqlDatabase Database::db() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool Database::open()
{
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(m_filePath);
    if (!database.open()) {
        m_lastError = database.lastError().text();
        qCCritical(lcCore) << "Failed to open database" << m_filePath << ":" << m_lastError;
        return false;
    }
    // Owner-only permissions: no secrets live here (credentials are in the
    // keyring), but the device list / event history is still private.
    QFile::setPermissions(m_filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QSqlQuery pragma(database);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return migrate();
}

int Database::schemaVersion() const
{
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT version FROM schema_version")) && q.next())
        return q.value(0).toInt();
    return 0;
}

bool Database::migrate()
{
    static const QStringList migrationsV1 = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS hosts ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " kind TEXT NOT NULL,"
                       " name TEXT NOT NULL,"
                       " addr TEXT NOT NULL,"
                       " port INTEGER NOT NULL DEFAULT 443,"
                       " https INTEGER NOT NULL DEFAULT 1,"
                       " username TEXT NOT NULL DEFAULT '',"
                       " model TEXT NOT NULL DEFAULT '',"
                       " created_at TEXT NOT NULL DEFAULT (datetime('now')))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS channels ("
                       " host_id INTEGER NOT NULL REFERENCES hosts(id) ON DELETE CASCADE,"
                       " idx INTEGER NOT NULL,"
                       " name TEXT NOT NULL DEFAULT '',"
                       " model TEXT NOT NULL DEFAULT '',"
                       " PRIMARY KEY (host_id, idx))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS layouts ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " name TEXT NOT NULL,"
                       " preset INTEGER NOT NULL DEFAULT 4,"
                       " panes_json TEXT NOT NULL DEFAULT '[]')"),
    };

    static const QStringList migrationsV2 = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS events ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " host_id INTEGER NOT NULL,"
                       " channel INTEGER NOT NULL DEFAULT 0,"
                       " ts INTEGER NOT NULL,"
                       " type TEXT NOT NULL,"
                       " camera TEXT NOT NULL DEFAULT '',"
                       " thumbnail TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts DESC)"),
    };

    // Ordered migrations; each bumps the stored version by one.
    const QVector<QPair<int, const QStringList *>> steps = {
        {1, &migrationsV1},
        {2, &migrationsV2},
    };

    const int current = schemaVersion();
    QSqlDatabase database = db();
    for (const auto &[version, stmts] : steps) {
        if (current >= version)
            continue;
        if (!database.transaction()) {
            m_lastError = database.lastError().text();
            return false;
        }
        for (const QString &stmt : *stmts) {
            QSqlQuery q(database);
            if (!q.exec(stmt)) {
                m_lastError = q.lastError().text();
                qCCritical(lcCore) << "Migration to v" << version << "failed:" << m_lastError;
                database.rollback();
                return false;
            }
        }
        QSqlQuery q(database);
        q.exec(QStringLiteral("DELETE FROM schema_version"));
        q.prepare(QStringLiteral("INSERT INTO schema_version (version) VALUES (:v)"));
        q.bindValue(QStringLiteral(":v"), version);
        q.exec();
        if (!database.commit()) {
            m_lastError = database.lastError().text();
            database.rollback();
            return false;
        }
        qCInfo(lcCore) << "Database migrated to schema v" << version;
    }
    return true;
}

qint64 Database::addEvent(const EventRecord &rec)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO events (host_id, channel, ts, type, camera, thumbnail)"
        " VALUES (:host, :ch, :ts, :type, :cam, :thumb)"));
    q.bindValue(QStringLiteral(":host"), rec.hostId);
    q.bindValue(QStringLiteral(":ch"), rec.channel);
    q.bindValue(QStringLiteral(":ts"), rec.timestamp);
    q.bindValue(QStringLiteral(":type"), rec.type);
    q.bindValue(QStringLiteral(":cam"), sqlText(rec.camera));
    q.bindValue(QStringLiteral(":thumb"), sqlText(rec.thumbnail));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QVector<EventRecord> Database::recentEvents(int limit) const
{
    QVector<EventRecord> out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, host_id, channel, ts, type, camera, thumbnail"
                             " FROM events ORDER BY ts DESC, id DESC LIMIT :lim"));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        EventRecord r;
        r.id = q.value(0).toLongLong();
        r.hostId = q.value(1).toLongLong();
        r.channel = q.value(2).toInt();
        r.timestamp = q.value(3).toLongLong();
        r.type = q.value(4).toString();
        r.camera = q.value(5).toString();
        r.thumbnail = q.value(6).toString();
        out.append(r);
    }
    return out;
}

bool Database::clearEvents()
{
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("DELETE FROM events"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool Database::trimEvents(int keep)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "DELETE FROM events WHERE id NOT IN "
        "(SELECT id FROM events ORDER BY ts DESC, id DESC LIMIT :keep)"));
    q.bindValue(QStringLiteral(":keep"), keep);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

qint64 Database::addHost(const HostRecord &rec)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO hosts (kind, name, addr, port, https, username, model)"
        " VALUES (:kind, :name, :addr, :port, :https, :username, :model)"));
    q.bindValue(QStringLiteral(":kind"), rec.kind);
    q.bindValue(QStringLiteral(":name"), sqlText(rec.name));
    q.bindValue(QStringLiteral(":addr"), rec.addr);
    q.bindValue(QStringLiteral(":port"), rec.port);
    q.bindValue(QStringLiteral(":https"), rec.https ? 1 : 0);
    q.bindValue(QStringLiteral(":username"), sqlText(rec.username));
    q.bindValue(QStringLiteral(":model"), sqlText(rec.model));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::removeHost(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM hosts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool Database::updateHost(const HostRecord &rec)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE hosts SET kind=:kind, name=:name, addr=:addr, port=:port, https=:https,"
        " username=:username, model=:model WHERE id=:id"));
    q.bindValue(QStringLiteral(":kind"), rec.kind);
    q.bindValue(QStringLiteral(":name"), sqlText(rec.name));
    q.bindValue(QStringLiteral(":addr"), rec.addr);
    q.bindValue(QStringLiteral(":port"), rec.port);
    q.bindValue(QStringLiteral(":https"), rec.https ? 1 : 0);
    q.bindValue(QStringLiteral(":username"), sqlText(rec.username));
    q.bindValue(QStringLiteral(":model"), sqlText(rec.model));
    q.bindValue(QStringLiteral(":id"), rec.id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVector<HostRecord> Database::hosts() const
{
    QVector<HostRecord> out;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral(
            "SELECT id, kind, name, addr, port, https, username, model FROM hosts ORDER BY id")))
        return out;
    while (q.next()) {
        HostRecord r;
        r.id = q.value(0).toLongLong();
        r.kind = q.value(1).toString();
        r.name = q.value(2).toString();
        r.addr = q.value(3).toString();
        r.port = q.value(4).toInt();
        r.https = q.value(5).toInt() != 0;
        r.username = q.value(6).toString();
        r.model = q.value(7).toString();
        out.append(r);
    }
    return out;
}

} // namespace rl
