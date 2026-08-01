#include "core/CredentialStore.h"
#include "core/Database.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Updater.h"
#include "ui/TrayIcon.h"
#include "device/DeviceDiscovery.h"
#include "device/DeviceManager.h"
#include "device/EventManager.h"
#include "media/StreamPlayer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <chrono>
#include <thread>
#include <unistd.h>

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication): QSystemTrayIcon needs the widgets
    // layer for the tray icon + menu. The UI itself remains pure QML.
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("reolink-linux"));
    QCoreApplication::setApplicationName(QStringLiteral("reolink-client"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.4-dev"));

    // Give the app a real identity in the taskbar/dock. setDesktopFileName lets
    // Wayland compositors match the window to the installed .desktop file and
    // pull its icon (the fix for the generic fallback glyph); setWindowIcon
    // supplies the icon directly for X11 and environments without the install.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.todesengelx.ReolinkLinux"));
    {
        QIcon icon;
        for (const int size : {16, 24, 32, 48, 64, 128, 256})
            icon.addFile(QStringLiteral(":/icons/reolink-%1.png").arg(size), QSize(size, size));
        if (!icon.isNull())
            QGuiApplication::setWindowIcon(icon);
    }

    rl::installLogging();

    // All Controls are custom-drawn against Theme.qml; Basic avoids
    // platform-style repaints fighting the dark palette.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    // --smoke <png>: load the UI, wait for first frames, grab the window to a
    // file and exit — headless end-to-end verification for CI and agents.
    QCommandLineOption smokeOption(QStringLiteral("smoke"),
                                   QStringLiteral("Capture a screenshot then exit."),
                                   QStringLiteral("png-path"));
    QCommandLineOption smokeDelayOption(QStringLiteral("smoke-delay"),
                                        QStringLiteral("Delay before capture (ms)."),
                                        QStringLiteral("ms"), QStringLiteral("3000"));
    // Headless recording verification: --record <source> --record-out <file>.
    QCommandLineOption recordOption(QStringLiteral("record"),
                                    QStringLiteral("Record a source then exit (no UI)."),
                                    QStringLiteral("source-url"));
    QCommandLineOption recordOutOption(QStringLiteral("record-out"),
                                       QStringLiteral("Output MP4 path for --record."),
                                       QStringLiteral("path"));
    QCommandLineOption recordSecsOption(QStringLiteral("record-secs"),
                                        QStringLiteral("Seconds to record."),
                                        QStringLiteral("s"), QStringLiteral("4"));
    QCommandLineOption discoverOption(QStringLiteral("discover"),
                                      QStringLiteral("Scan the LAN for Reolink devices then exit."));
    // Autostart entries launch with the window hidden — monitoring only.
    QCommandLineOption hiddenOption(QStringLiteral("start-hidden"),
                                    QStringLiteral("Start minimized to the system tray."));
    parser.addOption(smokeOption);
    parser.addOption(smokeDelayOption);
    parser.addOption(recordOption);
    parser.addOption(recordOutOption);
    parser.addOption(recordSecsOption);
    parser.addOption(discoverOption);
    parser.addOption(hiddenOption);
    parser.process(app);

    if (parser.isSet(discoverOption)) {
        auto *disc = new rl::DeviceDiscovery(&app);
        QObject::connect(disc, &rl::DeviceDiscovery::deviceFound, &app,
                         [](const QString &ip, const QString &info) {
                             qCInfo(lcUi) << "DISCOVERED Reolink device:" << ip << info;
                         });
        QObject::connect(disc, &rl::DeviceDiscovery::scanFinished, &app,
                         [] { qCInfo(lcUi) << "discovery finished"; QCoreApplication::exit(0); });
        disc->scan();
        QTimer::singleShot(30000, &app, [] { QCoreApplication::exit(1); });
        return app.exec();
    }

    if (parser.isSet(recordOption)) {
        auto *player = new rl::StreamPlayer(&app);
        const QString out = parser.value(recordOutOption);
        const int secs = parser.value(recordSecsOption).toInt();
        QObject::connect(player, &rl::StreamPlayer::stateChanged, &app, [player, out] {
            if (player->state() == rl::StreamPlayer::State::Streaming && !player->recording())
                player->startRecording(out);
        });
        QObject::connect(player, &rl::StreamPlayer::recordingSaved, &app, [](const QString &p) {
            qCInfo(lcUi) << "Recording saved:" << p;
            QCoreApplication::exit(0);
        });
        QObject::connect(player, &rl::StreamPlayer::recordingFailed, &app, [](const QString &e) {
            qCCritical(lcUi) << "Recording failed:" << e;
            QCoreApplication::exit(1);
        });
        player->setSource(parser.value(recordOption));
        player->start();
        QTimer::singleShot(secs * 1000, player, [player] { player->stopRecording(); });
        QTimer::singleShot((secs + 15) * 1000, &app, [] {
            qCCritical(lcUi) << "Recording timed out";
            QCoreApplication::exit(2);
        });
        return app.exec();
    }

    rl::Database database(rl::Paths::databaseFile());
    if (!database.open()) {
        qCCritical(lcCore) << "Cannot open database:" << database.lastError();
        return 1;
    }
    rl::CredentialStore credentials;
    rl::DeviceManager devices(&database, &credentials);
    rl::EventManager events(&database, &devices);
    rl::DeviceDiscovery discovery;
    rl::Updater updater;
    rl::TrayIcon tray;

    qmlRegisterType<rl::StreamPlayer>("ReolinkApp.Core", 1, 0, "StreamPlayer");
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Devices", &devices);
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Events", &events);
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Discovery", &discovery);
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Updater", &updater);
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Tray", &tray);

    QQmlApplicationEngine engine;
    // Lets tests/screenshots open a specific page (0=Live,1=Playback,2=Events,3=Settings).
    engine.rootContext()->setContextProperty(
        QStringLiteral("initialPage"), qEnvironmentVariableIntValue("RL_INITIAL_PAGE"));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mockRecordings"), qEnvironmentVariableIsSet("RL_MOCK_RECORDINGS"));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mockDoorbell"), qEnvironmentVariableIsSet("RL_MOCK_DOORBELL"));
    engine.rootContext()->setContextProperty(
        QStringLiteral("playbackAutoplay"), qEnvironmentVariableIsSet("RL_PLAYBACK_AUTOPLAY"));
    engine.rootContext()->setContextProperty(QStringLiteral("startHidden"),
                                             parser.isSet(hiddenOption) && tray.available());
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("ReolinkApp", "Main");

    // Tray -> window plumbing (QML handles close-to-tray via the Tray singleton).
    QObject::connect(&tray, &rl::TrayIcon::quitRequested, &app, [&app] {
        // Quit must be final: a worker blocked on an NVR socket can stall
        // teardown (the destructors join them), leaving a process with no tray
        // icon and no window. Arm an independent watchdog so "Quit" always
        // exits; SQLite/keyring writes are synchronous, so nothing is lost.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            // Reached only when graceful teardown stalled — worth knowing.
            qCWarning(lcUi) << "shutdown stalled; forcing exit";
            ::_exit(0);
        }).detach();
        app.quit();
    });
    QObject::connect(&tray, &rl::TrayIcon::openRequested, &app, [&engine] {
        const QList<QObject *> roots = engine.rootObjects();
        if (roots.isEmpty())
            return;
        if (auto *w = qobject_cast<QQuickWindow *>(roots.first())) {
            w->show();
            w->raise();
            w->requestActivate();
        }
    });
    QObject::connect(&events, &rl::EventManager::unreadChanged, &tray,
                     [&tray, &events] { tray.setUnread(events.unread()); });
    // With the window closed the app lives in the tray; don't exit.
    app.setQuitOnLastWindowClosed(!tray.available());

    // Check GitHub for a newer release shortly after the UI is up (skipped in
    // the headless screenshot mode). Silent unless an update is found.
    if (!parser.isSet(smokeOption))
        QTimer::singleShot(3000, &updater, &rl::Updater::check);

    // TEST HOOK (local): RL_TEST_NOTIFY=1 fires a synthetic detection through
    // the full pipeline (event inbox -> desktop notification -> click-to-raise)
    // so the notification flow can be exercised without waiting for real motion.
    if (qEnvironmentVariableIsSet("RL_TEST_NOTIFY")) {
        QTimer::singleShot(6000, &devices, [&devices] {
            if (devices.rowCount() == 0)
                return;
            const QVariantMap c = devices.cameraInfo(0);
            QMetaObject::invokeMethod(&devices, "detectionEvent", Qt::DirectConnection,
                                      Q_ARG(qint64, c.value(QStringLiteral("hostId")).toLongLong()),
                                      Q_ARG(int, c.value(QStringLiteral("channel")).toInt()),
                                      Q_ARG(QString, QStringLiteral("person")),
                                      Q_ARG(QString, c.value(QStringLiteral("name")).toString()));
        });
    }

    if (parser.isSet(smokeOption)) {
        const QString outPath = parser.value(smokeOption);
        const int delayMs = parser.value(smokeDelayOption).toInt();
        QTimer::singleShot(delayMs, &app, [&engine, outPath] {
            const QList<QObject *> roots = engine.rootObjects();
            int exitCode = 1;
            if (!roots.isEmpty()) {
                if (auto *window = qobject_cast<QQuickWindow *>(roots.first())) {
                    const QImage shot = window->grabWindow();
                    if (!shot.isNull() && shot.save(outPath)) {
                        qCInfo(lcUi) << "Smoke screenshot saved to" << outPath;
                        exitCode = 0;
                    }
                }
            }
            QCoreApplication::exit(exitCode);
        });
    }

    return app.exec();
}
