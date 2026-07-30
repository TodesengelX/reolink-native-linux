#include "Updater.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>

namespace rl {

// The published repo. Releases are tagged vX.Y.Z with an x86_64 .AppImage asset.
static constexpr auto kRepo = "TodesengelX/reolink-native-linux";
static constexpr qint64 kMinAppImageBytes = 1'000'000; // sanity floor for a good download

static QList<int> versionParts(QString v)
{
    v.remove(QLatin1Char('v')).remove(QLatin1Char(' '));
    QList<int> out;
    const auto segs = v.split(QLatin1Char('.'));
    for (const QString &p : segs)
        out << p.split(QLatin1Char('-')).first().toInt(); // drop any -beta suffix
    return out;
}

static bool isNewer(const QString &latest, const QString &current)
{
    QList<int> a = versionParts(latest), b = versionParts(current);
    while (a.size() < b.size()) a << 0;
    while (b.size() < a.size()) b << 0;
    for (int i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

Updater::Updater(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this))
{
}

QString Updater::currentVersion() const { return QCoreApplication::applicationVersion(); }

bool Updater::canSelfUpdate() const { return qEnvironmentVariableIsSet("APPIMAGE"); }

void Updater::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void Updater::check()
{
    if (m_state == Checking || m_state == Downloading)
        return;
    setState(Checking);

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                                 .arg(QLatin1String(kRepo))));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "ReolinkClient");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        // Check failures are silent — no update UI unless we actually find one.
        if (reply->error() != QNetworkReply::NoError) {
            qCInfo(lcCore) << "update check failed:" << reply->errorString();
            setState(Idle);
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = obj.value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty()) {
            setState(Idle);
            return;
        }
        m_latest = tag;
        m_releaseUrl = obj.value(QStringLiteral("html_url")).toString();
        m_notes = obj.value(QStringLiteral("body")).toString();
        m_assetUrl.clear();
        for (const QJsonValue &a : obj.value(QStringLiteral("assets")).toArray()) {
            const QString name = a.toObject().value(QStringLiteral("name")).toString();
            if (name.endsWith(QLatin1String(".AppImage")) && name.contains(QLatin1String("x86_64"))) {
                m_assetUrl = a.toObject().value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }
        m_available = isNewer(m_latest, currentVersion());
        emit infoChanged();
        setState(Idle);
        if (m_available)
            qCInfo(lcCore) << "update available:" << m_latest;
    });
}

void Updater::openReleasePage()
{
    const QString url = m_releaseUrl.isEmpty()
        ? QStringLiteral("https://github.com/%1/releases/latest").arg(QLatin1String(kRepo))
        : m_releaseUrl;
    QDesktopServices::openUrl(QUrl(url));
}

void Updater::applyUpdate()
{
    // Only an AppImage can safely replace itself; otherwise defer to the browser.
    if (!canSelfUpdate() || m_assetUrl.isEmpty()) {
        openReleasePage();
        return;
    }
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString tmp = appImage + QStringLiteral(".new");

    auto *out = new QFile(tmp);
    if (!out->open(QIODevice::WriteOnly)) {
        m_error = tr("Can't write to %1").arg(QFileInfo(appImage).absolutePath());
        setState(Failed);
        delete out;
        return;
    }

    m_progress = 0;
    emit progressChanged();
    setState(Downloading);

    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setRawHeader("User-Agent", "ReolinkClient");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_net->get(req);

    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) {
            m_progress = static_cast<int>(got * 100 / total);
            emit progressChanged();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, tmp, appImage] {
        reply->deleteLater();
        out->write(reply->readAll());
        out->close();
        delete out;

        const bool ok = reply->error() == QNetworkReply::NoError
                        && QFileInfo(tmp).size() > kMinAppImageBytes;
        if (!ok) {
            QFile::remove(tmp);
            m_error = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                              : tr("The download was incomplete.");
            setState(Failed);
            return;
        }

        QFile::setPermissions(tmp, QFile::permissions(appImage) | QFileDevice::ExeOwner
                                       | QFileDevice::ExeGroup | QFileDevice::ExeOther);

        // Atomic swap with a backup we can roll back to if the rename fails.
        const QString bak = appImage + QStringLiteral(".bak");
        QFile::remove(bak);
        QFile::rename(appImage, bak);
        if (!QFile::rename(tmp, appImage)) {
            QFile::rename(bak, appImage); // roll back
            QFile::remove(tmp);
            m_error = tr("Couldn't replace the AppImage.");
            setState(Failed);
            return;
        }
        QFile::remove(bak);

        // Relaunch the freshly-written AppImage and bow out.
        QProcess::startDetached(appImage, QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    });
}

} // namespace rl
