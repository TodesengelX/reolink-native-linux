#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace rl {

// Checks GitHub Releases for a newer version and (only when running as an
// AppImage) can download the new AppImage, swap it in atomically, and relaunch.
// For Flatpak/AUR installs the OS package manager owns updates, so self-apply is
// disabled there and the UI just links to the release page.
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY infoChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY infoChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY infoChanged)
    Q_PROPERTY(QString notes READ notes NOTIFY infoChanged)
    Q_PROPERTY(bool canSelfUpdate READ canSelfUpdate CONSTANT)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    enum State { Idle, Checking, Downloading, Failed };
    Q_ENUM(State)

    explicit Updater(QObject *parent = nullptr);

    State state() const { return m_state; }
    bool available() const { return m_available; }
    bool downloading() const { return m_state == Downloading; }
    bool failed() const { return m_state == Failed; }
    QString currentVersion() const;
    QString latestVersion() const { return m_latest; }
    QString releaseUrl() const { return m_releaseUrl; }
    QString notes() const { return m_notes; }
    bool canSelfUpdate() const;   // true only when launched as an AppImage
    int progress() const { return m_progress; }
    QString error() const { return m_error; }

    // Query the latest release (silent on failure — no UI unless an update is
    // actually found).
    Q_INVOKABLE void check();
    // Open the release page in the browser.
    Q_INVOKABLE void openReleasePage();
    // AppImage only: download the new AppImage, replace this one, relaunch.
    // Falls back to opening the release page if self-update isn't possible.
    Q_INVOKABLE void applyUpdate();

signals:
    void stateChanged();
    void infoChanged();
    void progressChanged();

private:
    void setState(State s);

    QNetworkAccessManager *m_net;
    State m_state = Idle;
    bool m_available = false;
    int m_progress = 0;
    QString m_latest, m_releaseUrl, m_notes, m_assetUrl, m_error;
};

} // namespace rl
