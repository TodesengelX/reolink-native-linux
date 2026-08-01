#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;

namespace rl {

// System-tray presence: keeps the app monitoring (detection poll, desktop
// notifications) with the window closed, KDE StatusNotifier-style. Exposed to
// QML as the "Tray" singleton so Main.qml can route window-close to hide.
class TrayIcon : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)

public:
    explicit TrayIcon(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool on);

    bool startOnLogin() const;
    void setStartOnLogin(bool on);

    void setUnread(int unread); // tooltip badge

signals:
    void closeToTrayChanged();
    void openRequested();
    void quitRequested();

private:
    QString autostartFile() const;
    QString launchCommand() const;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    bool m_available = false;
    bool m_closeToTray = true;
};

} // namespace rl
