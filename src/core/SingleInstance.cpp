#include "SingleInstance.h"

#include "Log.h"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

#include <unistd.h>

namespace rl {
namespace {
// Long enough to cover a loaded machine, short enough that a stale socket
// doesn't visibly delay startup.
constexpr int kConnectTimeoutMs = 500;
} // namespace

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

QString SingleInstance::socketPath() const
{
    // Per-user, and in the runtime dir so the kernel cleans it up at logout.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + QStringLiteral("/reolink-client-%1.sock").arg(::getuid());
}

bool SingleInstance::acquire(bool startHidden)
{
    const QString path = socketPath();

    // Serialise the connect-then-listen sequence: without this, a double-click
    // on the launcher can have both processes fail to connect and then both
    // claim the socket, which is the very pile-up we're preventing. The lock is
    // released when this function returns — by then the primary is listening,
    // so whoever was waiting finds a live socket and hands off.
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    lock.tryLock(3000); // on timeout, fall through and try the socket anyway

    QLocalSocket sock;
    sock.connectToServer(path);
    if (sock.waitForConnected(kConnectTimeoutMs)) {
        // An instance is already running. Autostart launches only want the
        // monitoring, which is already happening, so they ask for nothing.
        QByteArray msg("HIDDEN");
        if (!startHidden)
            msg = QByteArray("ACTIVATE ") + qgetenv("XDG_ACTIVATION_TOKEN");
        sock.write(msg + '\n');
        sock.waitForBytesWritten(kConnectTimeoutMs);
        sock.disconnectFromServer();
        return false;
    }

    // Nothing answered, so any socket file here is a leftover from a crashed
    // run — removeServer would be unsafe before this point, safe after it.
    QLocalServer::removeServer(path);
    m_server = new QLocalServer(this);
    if (!m_server->listen(path)) {
        // Better to run un-guarded than to refuse to start.
        qCWarning(lcCore) << "single-instance socket unavailable:" << m_server->errorString();
        return true;
    }

    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server->nextPendingConnection()) {
            connect(client, &QLocalSocket::readyRead, this, [this, client] { readCommands(client); });
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });
    return true;
}

void SingleInstance::readCommands(QLocalSocket *client)
{
    while (client->canReadLine()) {
        const QByteArray line = client->readLine().trimmed();
        if (!line.startsWith("ACTIVATE"))
            continue; // HIDDEN: monitoring is already up, nothing to do
        const QByteArray token = line.mid(qstrlen("ACTIVATE")).trimmed();
        if (!token.isEmpty())
            qputenv("XDG_ACTIVATION_TOKEN", token);
        emit activationRequested();
    }
}

} // namespace rl
