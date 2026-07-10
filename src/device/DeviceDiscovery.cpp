#include "DeviceDiscovery.h"

#include "core/Log.h"

#include <QNetworkInterface>
#include <QTcpSocket>
#include <QtConcurrent/QtConcurrent>

#include <curl/curl.h>

namespace rl {

namespace {

constexpr int kPortConnectMs = 500; // per-host TCP connect timeout to :9000
constexpr int kReolinkPort = 9000;  // Baichuan — a strong Reolink signature

// Confirm the host really speaks the Reolink HTTP-CGI API: an unauthenticated
// GetAbility returns a JSON array with an rspCode error ("please login first").
size_t discardBody(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<QByteArray *>(userdata);
    if (out->size() < 4096)
        out->append(ptr, static_cast<qsizetype>(size * nmemb));
    return size * nmemb;
}

bool confirmReolink(const QString &ip)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;
    QByteArray body;
    const QByteArray url = QStringLiteral("https://%1/cgi-bin/api.cgi?cmd=GetAbility").arg(ip).toUtf8();
    const char *payload = R"([{"cmd":"GetAbility","action":0,"param":{"User":{"userName":"x"}}}])";
    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url.constData());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    const CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    // The Reolink API answers with a JSON array carrying "rspCode".
    return rc == CURLE_OK && body.contains("rspCode") && body.contains("cmd");
}

bool portOpen(const QString &ip, int port)
{
    QTcpSocket sock;
    sock.connectToHost(ip, static_cast<quint16>(port));
    return sock.waitForConnected(kPortConnectMs);
}

// All host IPs on the machine's IPv4 /24 subnets (private ranges only).
QStringList localSubnetHosts()
{
    QStringList ips;
    QSet<QString> prefixes;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol || addr.isLoopback())
                continue;
            const quint32 v4 = addr.toIPv4Address();
            // Private ranges only (10/8, 172.16/12, 192.168/16).
            const bool priv = (v4 >> 24) == 10 || (v4 >> 20) == 0xAC1 || (v4 >> 16) == 0xC0A8;
            if (!priv)
                continue;
            const QString prefix = addr.toString().section(u'.', 0, 2); // a.b.c
            prefixes.insert(prefix);
        }
    }
    for (const QString &prefix : prefixes)
        for (int host = 1; host <= 254; ++host)
            ips.append(prefix + u'.' + QString::number(host));
    return ips;
}

} // namespace

DeviceDiscovery::DeviceDiscovery(QObject *parent) : QObject(parent)
{
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

DeviceDiscovery::~DeviceDiscovery() = default;

void DeviceDiscovery::setScanning(bool s)
{
    if (m_scanning == s)
        return;
    m_scanning = s;
    emit scanningChanged();
    if (!s)
        emit scanFinished();
}

void DeviceDiscovery::reportFound(const QString &ip, const QString &info)
{
    if (!m_found.contains(ip)) {
        m_found.append(ip);
        emit deviceFound(ip, info);
    }
}

void DeviceDiscovery::scan()
{
    if (m_scanning)
        return;
    m_found.clear();
    setScanning(true);

    const QStringList hosts = localSubnetHosts();
    m_pending = hosts.size();
    if (m_pending == 0) {
        setScanning(false);
        return;
    }
    qCInfo(lcCore) << "discovery: scanning" << m_pending << "hosts for Reolink devices";

    // A dedicated pool so 254 network probes run wide (I/O-bound, not CPU-bound)
    // without starving the app's other worker tasks.
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(48);
        return p;
    }();

    for (const QString &ip : hosts) {
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
            const QString ip = watcher->result();
            watcher->deleteLater();
            if (!ip.isEmpty())
                reportFound(ip, QStringLiteral("https"));
            if (--m_pending <= 0)
                setScanning(false);
        });
        watcher->setFuture(QtConcurrent::run(pool, [ip]() -> QString {
            if (portOpen(ip, kReolinkPort) && confirmReolink(ip))
                return ip;
            return {};
        }));
    }
}

} // namespace rl
