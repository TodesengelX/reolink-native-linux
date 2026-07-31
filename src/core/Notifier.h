#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace rl {

// Sends desktop notifications via the org.freedesktop.Notifications D-Bus
// service — the standard Linux notification surface (pop-ups shown near the
// system tray / notification area). A no-op if the session bus or the service
// isn't available, so it's always safe to call.
//
// Notifications carry a default click action: clicking one emits activated()
// with the event context that was passed to notify(), so the app can jump
// straight to that moment in Playback.
class Notifier : public QObject
{
    Q_OBJECT
public:
    explicit Notifier(QObject *parent = nullptr);

    // title = summary line (e.g. the camera name), body = detail line,
    // iconName = a freedesktop icon name or absolute path (optional).
    // hostId/channel/timestamp identify the event a click should jump to
    // (hostId < 0 = not clickable).
    void notify(const QString &title, const QString &body,
                const QString &iconName = QString(), qint64 hostId = -1,
                int channel = 0, qint64 timestamp = 0);

signals:
    // The user clicked a notification; jump to this event.
    void activated(qint64 hostId, int channel, qint64 timestamp);

private slots:
    void onActionInvoked(quint32 id, const QString &actionKey);
    void onActivationToken(quint32 id, const QString &token);
    void onNotificationClosed(quint32 id, quint32 reason);

private:
    struct Payload {
        qint64 hostId;
        int channel;
        qint64 timestamp;
        QString token; // xdg-activation token (Wayland focus grant)
    };
    QHash<quint32, Payload> m_pending; // notification id -> event to jump to
};

} // namespace rl
