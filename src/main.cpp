#include "core/CredentialStore.h"
#include "core/Database.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "device/DeviceDiscovery.h"
#include "device/DeviceManager.h"
#include "device/EventManager.h"
#include "media/StreamPlayer.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("reolink-linux"));
    QCoreApplication::setApplicationName(QStringLiteral("reolink-client"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
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
    parser.addOption(smokeOption);
    parser.addOption(smokeDelayOption);
    parser.addOption(recordOption);
    parser.addOption(recordOutOption);
    parser.addOption(recordSecsOption);
    parser.addOption(discoverOption);
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

    qmlRegisterType<rl::StreamPlayer>("ReolinkApp.Core", 1, 0, "StreamPlayer");
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Devices", &devices);
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Events", &events);
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Discovery", &discovery);

    QQmlApplicationEngine engine;
    // Lets tests/screenshots open a specific page (0=Live,1=Playback,2=Events,3=Settings).
    engine.rootContext()->setContextProperty(
        QStringLiteral("initialPage"), qEnvironmentVariableIntValue("RL_INITIAL_PAGE"));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mockRecordings"), qEnvironmentVariableIsSet("RL_MOCK_RECORDINGS"));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mockDoorbell"), qEnvironmentVariableIsSet("RL_MOCK_DOORBELL"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("ReolinkApp", "Main");

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
