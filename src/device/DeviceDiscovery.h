#pragma once

#include <QObject>
#include <QStringList>

namespace rl {

// Finds Reolink cameras/NVRs on the local network. WiFi APs usually drop
// multicast, so the reliable path is a subnet scan for the Baichuan port (9000)
// confirmed by an unauthenticated api.cgi probe; ONVIF WS-Discovery is attempted
// too for wired setups. Results stream in via deviceFound as they're confirmed.
class DeviceDiscovery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
    explicit DeviceDiscovery(QObject *parent = nullptr);
    ~DeviceDiscovery() override;

    bool scanning() const { return m_scanning; }

    // Scan every local IPv4 /24 the machine is on. Idempotent while running.
    Q_INVOKABLE void scan();

signals:
    void scanningChanged();
    // A confirmed Reolink device at ip (info: a short hint, e.g. "https" / "onvif").
    void deviceFound(const QString &ip, const QString &info);
    void scanFinished();

private:
    void setScanning(bool s);
    void reportFound(const QString &ip, const QString &info);

    bool m_scanning = false;
    int m_pending = 0;          // outstanding probe tasks (GUI thread only)
    QStringList m_found;        // dedupe
};

} // namespace rl
