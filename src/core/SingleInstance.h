#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace rl {

// One client per user session.
//
// The app is designed to keep running in the tray with its window closed, so
// "launch it again" is the normal way users come back to it. Without a guard
// every launch built a whole second app — its own tray icon, NVR login and
// decoder set — and they piled up over a day of use. The first process listens
// on a per-user socket; later launches hand it their activation token and exit,
// so the running window is raised instead.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // Become the primary, or hand this launch off to the instance already
    // running. Returns true when this process should carry on and start the UI,
    // false when it should exit immediately.
    bool acquire(bool startHidden);

signals:
    // A later launch asked for the window. Any XDG activation token it carried
    // is already in this process's environment, so requestActivate() is honoured
    // by the compositor rather than dropped as focus stealing.
    void activationRequested();

private:
    QString socketPath() const;
    void readCommands(QLocalSocket *client);

    QLocalServer *m_server = nullptr;
};

} // namespace rl
