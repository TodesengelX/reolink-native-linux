#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace rl {

struct HostRecord {
    qint64 id = -1;
    QString kind;     // "camera" | "nvr" | "stream" (direct URL, for testing/generic RTSP)
    QString name;
    QString addr;     // IP/hostname, or full URL when kind == "stream"
    int port = 443;
    bool https = true;
    QString username;
    QString model;
};

// SQLite-backed application store: devices, channels, layouts.
// Schema is versioned; open() runs pending migrations.
class Database
{
public:
    // connectionName lets tests open isolated databases.
    explicit Database(const QString &filePath,
                      const QString &connectionName = QStringLiteral("main"));
    ~Database();

    bool open();
    QString lastError() const { return m_lastError; }
    int schemaVersion() const;

    qint64 addHost(const HostRecord &rec);
    bool removeHost(qint64 id);
    bool updateHost(const HostRecord &rec);
    QVector<HostRecord> hosts() const;

private:
    bool migrate();
    QSqlDatabase db() const;

    QString m_filePath;
    QString m_connectionName;
    QString m_lastError;
};

} // namespace rl
