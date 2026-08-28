#pragma once

#include <QSize>
#include <QString>

namespace rl {

// Minimal synchronous Baichuan session helper for discovery and validation.
//
// Provenance: port/header/framing, nonce login, XOR, AES key derivation, and
// login XML shape are protocol facts implemented from the existing in-tree
// Baichuan code and MIT reolink_aio. Do not port this layer from AGPL neolink.
class BaichuanSession
{
public:
    enum class FailKind { None, Transport, Auth, Locked, Protocol };

    struct DeviceInfo {
        QString name;
        QString model;
        int channelNum = 1;
        QString mainCodec; // empty = let FFmpeg probe
        QSize mainSize;
        QSize subSize;
        bool isAdmin = false;
    };

    struct LoginResult {
        bool ok = false;
        FailKind failKind = FailKind::None;
        QString error;
        DeviceInfo device;
    };

    // Unauthenticated protocol confirmation: stops after the nonce/hello reply,
    // so LAN discovery cannot burn firmware login-attempt counters.
    static bool probe(const QString &host, int port = 9000);

    // One credentialed login attempt. Callers must avoid retry loops on Auth or
    // Locked because Reolink firmware enforces a failed-login lockout.
    static LoginResult login(const QString &host, int port, const QString &username,
                             const QString &password);

    static DeviceInfo parseDeviceInfo(const QByteArray &xml);
};

} // namespace rl
