#include "TrayIcon.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace rl {

TrayIcon::TrayIcon(QObject *parent) : QObject(parent)
{
    m_available = QSystemTrayIcon::isSystemTrayAvailable();
    QSettings settings;
    m_closeToTray = settings.value(QStringLiteral("tray/closeToTray"), true).toBool();
    if (!m_available)
        return;

    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128})
        icon.addFile(QStringLiteral(":/icons/reolink-%1.png").arg(size), QSize(size, size));

    m_menu = new QMenu();
    QAction *open = m_menu->addAction(tr("Open Reolink Client"));
    connect(open, &QAction::triggered, this, &TrayIcon::openRequested);
    m_menu->addSeparator();

    QAction *closeTray = m_menu->addAction(tr("Keep running when closed"));
    closeTray->setCheckable(true);
    closeTray->setChecked(m_closeToTray);
    connect(closeTray, &QAction::toggled, this, &TrayIcon::setCloseToTray);

    QAction *autostart = m_menu->addAction(tr("Start on login"));
    autostart->setCheckable(true);
    autostart->setChecked(startOnLogin());
    connect(autostart, &QAction::toggled, this, &TrayIcon::setStartOnLogin);

    m_menu->addSeparator();
    QAction *quit = m_menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, [this] {
        // Hide immediately for feedback, and emit only after this menu callback
        // has unwound — quitting from inside the menu's own handler (or its
        // nested/D-Bus event loop) can tear down the icon without ending the
        // app's main loop.
        if (m_tray)
            m_tray->hide();
        QTimer::singleShot(0, this, [this] { emit quitRequested(); });
    });

    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip(tr("Reolink Client"));
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    m_tray->show();
}

void TrayIcon::setCloseToTray(bool on)
{
    if (m_closeToTray == on)
        return;
    m_closeToTray = on;
    QSettings().setValue(QStringLiteral("tray/closeToTray"), on);
    emit closeToTrayChanged();
}

void TrayIcon::setUnread(int unread)
{
    if (m_tray)
        m_tray->setToolTip(unread > 0 ? tr("Reolink Client — %n new event(s)", nullptr, unread)
                                      : tr("Reolink Client"));
}

QString TrayIcon::autostartFile() const
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/autostart/io.github.todesengelx.ReolinkLinux.desktop");
}

QString TrayIcon::launchCommand() const
{
    // The AppImage mount point is transient — autostart must point at the
    // .AppImage file itself, not the extracted binary inside it.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

bool TrayIcon::startOnLogin() const
{
    return QFile::exists(autostartFile());
}

void TrayIcon::setStartOnLogin(bool on)
{
    const QString path = autostartFile();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Reolink Linux Client\n"
                           "Exec=%1 --start-hidden\n"
                           "Icon=io.github.todesengelx.ReolinkLinux\n"
                           "Comment=Camera monitoring starts with your session\n"
                           "X-KDE-autostart-after=panel\n")
                .arg(launchCommand())
                .toUtf8());
}

} // namespace rl
