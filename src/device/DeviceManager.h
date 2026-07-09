#pragma once

#include "core/CredentialStore.h"
#include "core/Database.h"

#include <QAbstractListModel>
#include <QFutureSynchronizer>

namespace rl {

// The device tree behind the sidebar: hosts from the database, exposed to QML.
// Adding a camera/NVR validates it over the HTTP-CGI API on a worker thread
// (Login + GetDevInfo + GetEnc) and fills in name/model/codec. Credentials live
// in the keyring; the in-memory copy (loaded on a worker thread) lets liveUrl()
// build RTSP URLs without blocking the GUI thread on DBus.
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
    ~DeviceManager() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Camera/NVR by IP or hostname. Validates asynchronously; the row appears
    // immediately with status "connecting…".
    Q_INVOKABLE void addDevice(const QString &addr, const QString &username,
                               const QString &password, bool https = true, int port = 0);
    // Direct stream URL (rtsp://…, file …) — testing and generic-RTSP escape hatch.
    // Any embedded credentials are stripped to the keyring, not persisted in the DB.
    Q_INVOKABLE void addStreamUrl(const QString &name, const QString &url);
    Q_INVOKABLE void removeDevice(int row);

    // Playable URL for a device row; empty when credentials aren't loaded yet.
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
        QString mainCodec = QStringLiteral("h264"); // sub stream is always h264
        QString password;                           // in-memory only (keyring at rest)
        bool primed = false;                        // password loaded?
    };

    // Runs on a worker: loads the password from the keyring (or stores newPassword
    // first), logs in, fetches device info + encoding, updates the row on the GUI
    // thread. storeNew persists newPassword to the keyring before validating.
    void validateAsync(qint64 hostId, const QString &newPassword = QString(),
                       bool storeNew = false);
    int rowForHostId(qint64 hostId) const;
    void applyValidation(qint64 hostId, bool online, const QString &status, const QString &name,
                         const QString &model, const QString &codec, int channelNum,
                         const QString &password);

    Database *m_db;
    CredentialStore *m_credentials;
    QVector<Entry> m_entries;
    QFutureSynchronizer<void> m_pending; // drains in-flight validations at teardown
};

} // namespace rl
