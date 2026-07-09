#pragma once

#include "core/CredentialStore.h"
#include "core/Database.h"

#include <QAbstractListModel>

namespace rl {

// The device tree behind the sidebar: hosts from the database, exposed to QML.
// Adding a camera/NVR validates it over the HTTP-CGI API on a worker thread
// (Login + GetDevInfo) and fills in name/model. Credentials go to the keyring.
class DeviceManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        AddrRole,
        KindRole,
        ModelRole,
        OnlineRole,
        StatusRole,
        HostIdRole,
    };

    DeviceManager(Database *db, CredentialStore *credentials, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Camera/NVR by IP or hostname. Validates asynchronously; the row appears
    // immediately with status "connecting…".
    Q_INVOKABLE void addDevice(const QString &addr, const QString &username,
                               const QString &password, bool https = true, int port = 0);
    // Direct stream URL (rtsp://…, file …) — testing and generic-RTSP escape hatch.
    Q_INVOKABLE void addStreamUrl(const QString &name, const QString &url);
    Q_INVOKABLE void removeDevice(int row);

    // Playable URL for a device row; empty when none is available yet.
    Q_INVOKABLE QString liveUrl(int row, bool mainStream = true);
    Q_INVOKABLE QString nameAt(int row) const;

signals:
    void countChanged();
    void deviceError(const QString &addr, const QString &message);

private:
    struct Entry {
        HostRecord rec;
        bool online = false;
        QString status;
    };
    void validateAsync(qint64 hostId);
    int rowForHostId(qint64 hostId) const;

    Database *m_db;
    CredentialStore *m_credentials;
    QVector<Entry> m_entries;
};

} // namespace rl
