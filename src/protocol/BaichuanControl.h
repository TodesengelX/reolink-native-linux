#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>

class QTcpSocket;

// Synchronous Baichuan (BC, TCP 9000) settings/config RPC client — the transport
// the official Reolink apps use for reading/writing device settings. The NVR's
// HTTP-CGI web server (api.cgi) is a fragile bolt-on that returns 502 under load;
// the native BC daemon on port 9000 does not, so settings that flake over HTTP
// (push/email/ftp enable, image/ISP, recording, AI) are reliable here.
//
// Blocking request/response: open() connects + logs in, then get()/setEnable()
// issue command RPCs multiplexed over the one connection (auto-incrementing
// mess_id). Designed to run on a worker thread (like the HTTP settings path).
//
// Clean-room: wire format (24-byte class 0x6414 header, ch_id=channel+1, two
// independently-AES-encrypted segments split at payload_offset, read-modify-write
// SET) from the documented protocol + MIT reolink_aio. No AGPL neolink code.
namespace rl {

class BaichuanControl
{
public:
    struct Params {
        QString host;
        int port = 9000;
        QString username;
        QString password;
    };

    explicit BaichuanControl(Params params);
    ~BaichuanControl();

    // Connect + modern-AES login. Returns false on failure (unreachable, session
    // limit, bad credentials). Blocking, ~sub-second on a LAN.
    bool open();
    void close();

    // GET a command's config XML for a channel (channel < 0 => host-level).
    // Returns the decrypted reply XML (may span the device's own root element),
    // or empty on failure. `statusOut` (optional) receives the BC status code.
    QByteArray get(quint32 cmdId, int channel, quint16 *statusOut = nullptr);

    // Read the recursive <enable> flag from a GET response (-1 if absent/failed).
    int readEnable(quint32 getCmdId, int channel);

    // Read-modify-write the <enable> flag: GET getCmdId, flip <enable>, SET setCmdId.
    // Returns true on a success status (200/201/300).
    bool writeEnable(quint32 getCmdId, quint32 setCmdId, int channel, bool enable);

    // Low-level: send a raw (plaintext) XML body to cmdId and return the decrypted
    // reply. bodyXml empty => header-only GET. Used by the helpers above.
    QByteArray transact(quint32 cmdId, int channel, const QByteArray &bodyXml,
                        quint16 *statusOut = nullptr);

    bool isOpen() const;

private:
    Params m_p;
    std::unique_ptr<QTcpSocket> m_sock;
    QByteArray m_aesKey;
    QByteArray m_netBuf;      // socket bytes buffered past the last parsed reply
    quint32 m_messId = 0;     // per-connection RPC counter
};

} // namespace rl
