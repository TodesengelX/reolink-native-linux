#include "core/SingleInstance.h"

#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

using namespace rl;

// The guard exists because every launch used to build a whole second app -
// its own tray icon, NVR login and decoder set - and they piled up. What has
// to hold: a later launch must not survive, and it must reach the running one
// so the window is actually raised. A dropped message would look like a fixed
// pile-up but leave the launcher doing nothing at all, which is worse.
class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Own runtime dir so the suite never collides with a real client
        // running on the developer's session (same uid, same socket name).
        QVERIFY(m_runtime.isValid());
        QFile::setPermissions(m_runtime.path(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
    }

    void firstLaunchBecomesPrimary()
    {
        SingleInstance primary;
        QVERIFY(primary.acquire(false));
        QVERIFY(QFile::exists(primary.socketPath()));
    }

    // The socket is released when the primary goes away, so the next launch
    // can take over instead of being locked out by a dead predecessor.
    void primaryReleasesSocketOnDestruction()
    {
        QString path;
        {
            SingleInstance primary;
            QVERIFY(primary.acquire(false));
            path = primary.socketPath();
        }
        SingleInstance next;
        QVERIFY(next.acquire(false));
        QCOMPARE(next.socketPath(), path);
    }

    void secondLaunchHandsOffAndRaisesPrimary()
    {
        SingleInstance primary;
        QVERIFY(primary.acquire(false));
        QSignalSpy raised(&primary, &SingleInstance::activationRequested);

        SingleInstance second;
        QVERIFY(!second.acquire(false)); // false => the caller must exit

        // The handoff must actually arrive; the second process disconnects
        // immediately after writing, so this is where a lost message shows up.
        QVERIFY(raised.wait(3000));
        QCOMPARE(raised.count(), 1);
    }

    // Autostart launches only want the monitoring, which is already running -
    // they must not yank a window onto the screen the user didn't ask for.
    void hiddenLaunchHandsOffWithoutRaising()
    {
        SingleInstance primary;
        QVERIFY(primary.acquire(false));
        QSignalSpy raised(&primary, &SingleInstance::activationRequested);

        SingleInstance second;
        QVERIFY(!second.acquire(true)); // --start-hidden

        QTest::qWait(500);
        QCOMPARE(raised.count(), 0);
    }

    // KWin ignores requestActivate() from a process that wasn't the one the
    // user just clicked, so the token the launcher handed the second process
    // has to end up in the primary's environment before it tries to raise.
    void activationTokenIsAppliedBeforeRaising()
    {
        SingleInstance primary;
        QVERIFY(primary.acquire(false));
        qputenv("XDG_ACTIVATION_TOKEN", "stale-token");
        QSignalSpy raised(&primary, &SingleInstance::activationRequested);

        QLocalSocket client;
        client.connectToServer(primary.socketPath());
        QVERIFY(client.waitForConnected(2000));
        client.write("ACTIVATE fresh-token-42\n");
        QVERIFY(client.waitForBytesWritten(2000));

        QVERIFY(raised.wait(3000));
        QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray("fresh-token-42"));
    }

    // A launch with no token still deserves a raise - it just may not win
    // focus. Blank must not be written over whatever is already there.
    void raiseStillHappensWithoutAToken()
    {
        SingleInstance primary;
        QVERIFY(primary.acquire(false));
        qputenv("XDG_ACTIVATION_TOKEN", "keep-me");
        QSignalSpy raised(&primary, &SingleInstance::activationRequested);

        QLocalSocket client;
        client.connectToServer(primary.socketPath());
        QVERIFY(client.waitForConnected(2000));
        client.write("ACTIVATE\n");
        QVERIFY(client.waitForBytesWritten(2000));

        QVERIFY(raised.wait(3000));
        QCOMPARE(qgetenv("XDG_ACTIVATION_TOKEN"), QByteArray("keep-me"));
    }

    // A crashed primary leaves the socket file behind. The next launch has to
    // reclaim it, or the app becomes unstartable until someone deletes it.
    void staleSocketFileIsReclaimed()
    {
        const QString path =
            m_runtime.path() + QStringLiteral("/reolink-client-%1.sock").arg(::getuid());
        QFile stale(path);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("not a socket");
        stale.close();

        SingleInstance primary;
        QVERIFY(primary.acquire(false));
        QCOMPARE(primary.socketPath(), path);

        SingleInstance second;
        QVERIFY(!second.acquire(false));
    }

private:
    QTemporaryDir m_runtime;
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
