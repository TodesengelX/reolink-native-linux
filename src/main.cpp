#include "core/CredentialStore.h"
#include "core/Database.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "device/DeviceManager.h"
#include "media/StreamPlayer.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
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
    parser.addOption(smokeOption);
    parser.addOption(smokeDelayOption);
    parser.process(app);

    rl::Database database(rl::Paths::databaseFile());
    if (!database.open()) {
        qCCritical(lcCore) << "Cannot open database:" << database.lastError();
        return 1;
    }
    rl::CredentialStore credentials;
    rl::DeviceManager devices(&database, &credentials);

    qmlRegisterType<rl::StreamPlayer>("ReolinkApp.Core", 1, 0, "StreamPlayer");
    qmlRegisterSingletonInstance("ReolinkApp.Core", 1, 0, "Devices", &devices);

    QQmlApplicationEngine engine;
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
