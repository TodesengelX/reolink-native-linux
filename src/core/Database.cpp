#include "Database.h"

#include "Log.h"

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

    const int current = schemaVersion();
    if (current >= 1)
        return true;

    QSqlDatabase database = db();
    if (!database.transaction()) {
        m_lastError = database.lastError().text();
        return false;
    }
    for (const QString &stmt : migrationsV1) {
        QSqlQuery q(database);
        if (!q.exec(stmt)) {
            m_lastError = q.lastError().text();
            qCCritical(lcCore) << "Migration failed:" << m_lastError;
            database.rollback();
            return false;
        }
    }
    QSqlQuery q(database);
    q.exec(QStringLiteral("DELETE FROM schema_version"));
    q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (1)"));
    if (!database.commit()) {
        m_lastError = database.lastError().text();
        database.rollback();
        return false;
    }
    qCInfo(lcCore) << "Database ready, schema v1 at" << m_filePath;
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
