#include "Notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantList>
#include <QVariantMap>

namespace rl {

static constexpr auto kService = "org.freedesktop.Notifications";
static constexpr auto kPath = "/org/freedesktop/Notifications";
static constexpr auto kInterface = "org.freedesktop.Notifications";

Notifier::Notifier(QObject *parent) : QObject(parent)
{
    // Click ("default" action) + close signals from the notification daemon.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
                    QStringLiteral("ActionInvoked"), this,
                    SLOT(onActionInvoked(quint32, QString)));
        bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
                    QStringLiteral("NotificationClosed"), this,
                    SLOT(onNotificationClosed(quint32, quint32)));
    }
}

void Notifier::notify(const QString &title, const QString &body, const QString &iconName,
                      qint64 hostId, int channel, qint64 timestamp)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    // org.freedesktop.Notifications.Notify(app_name, replaces_id, app_icon,
    //   summary, body, actions, hints, expire_timeout)
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
        QStringLiteral("Notify"));
    QStringList actions;
    if (hostId >= 0) // "default" = clicking the notification body itself
        actions << QStringLiteral("default") << tr("View recording");
    msg << QStringLiteral("Reolink Client")   // app_name
        << quint32(0)                          // replaces_id (0 = new bubble)
        << iconName                            // app_icon
        << title                               // summary
        << body                                // body
        << actions                             // actions
        << QVariantMap()                       // hints
        << qint32(7000);                       // expire timeout (ms)

    if (hostId < 0) {
        bus.asyncCall(msg); // fire-and-forget; never blocks the GUI thread
        return;
    }
    // Track the daemon-assigned id so a later click maps back to this event.
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, hostId, channel, timestamp](QDBusPendingCallWatcher *w) {
                QDBusPendingReply<quint32> reply = *w;
                w->deleteLater();
                if (!reply.isError())
                    m_pending.insert(reply.value(), {hostId, channel, timestamp});
            });
}

void Notifier::onActionInvoked(quint32 id, const QString &actionKey)
{
    Q_UNUSED(actionKey);
    const auto it = m_pending.constFind(id);
    if (it == m_pending.constEnd())
        return; // not ours (the signal is bus-wide for all apps' notifications)
    emit activated(it->hostId, it->channel, it->timestamp);
}

void Notifier::onNotificationClosed(quint32 id, quint32 reason)
{
    Q_UNUSED(reason);
    m_pending.remove(id);
}

} // namespace rl
