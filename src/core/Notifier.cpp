#include "Notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QVariantList>
#include <QVariantMap>

namespace rl {

Notifier::Notifier(QObject *parent) : QObject(parent) {}

void Notifier::notify(const QString &title, const QString &body, const QString &iconName)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    // org.freedesktop.Notifications.Notify(app_name, replaces_id, app_icon,
    //   summary, body, actions, hints, expire_timeout)
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    msg << QStringLiteral("Reolink Client")   // app_name
        << quint32(0)                          // replaces_id (0 = new bubble)
        << iconName                            // app_icon
        << title                               // summary
        << body                                // body
        << QStringList()                       // actions
        << QVariantMap()                       // hints
        << qint32(7000);                       // expire timeout (ms)

    bus.asyncCall(msg);   // fire-and-forget; never blocks the GUI thread
}

} // namespace rl
