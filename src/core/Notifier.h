#pragma once

#include <QObject>
#include <QString>

namespace rl {

// Sends desktop notifications via the org.freedesktop.Notifications D-Bus
// service — the standard Linux notification surface (pop-ups shown near the
// system tray / notification area). A no-op if the session bus or the service
// isn't available, so it's always safe to call.
class Notifier : public QObject
{
    Q_OBJECT
public:
    explicit Notifier(QObject *parent = nullptr);

    // title = summary line (e.g. the camera name), body = detail line,
    // iconName = a freedesktop icon name or absolute path (optional).
    void notify(const QString &title, const QString &body,
                const QString &iconName = QString());
};

} // namespace rl
