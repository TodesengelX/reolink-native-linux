#pragma once

#include <QString>

namespace rl {

// Device credentials live in the OS keyring (Secret Service via libsecret),
// never in the database. Keyed by host id. Synchronous — call from worker
// threads for UI paths that can't block.
class CredentialStore
{
public:
    bool store(qint64 hostId, const QString &password);
    // ok is set to false when the keyring is unavailable or the lookup failed.
    QString lookup(qint64 hostId, bool *ok = nullptr);
    bool remove(qint64 hostId);
    QString lastError() const { return m_lastError; }

private:
    QString m_lastError;
};

} // namespace rl
