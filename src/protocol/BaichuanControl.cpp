#include "BaichuanControl.h"

#include "protocol/BcCrypto.h"

#include <QTcpSocket>
#include <QtEndian>

namespace rl {

namespace {

constexpr quint32 kMagic = 0x0ABCDEF0;
constexpr quint32 kMagicRev = 0x0FEDCBA0;
constexpr quint16 kClassModern = 0x6414; // 24-byte header, settings/control
constexpr quint16 kClassLegacy = 0x6514; // 20-byte header, only the nonce/hello

void putLE32(QByteArray &b, quint32 v)
{
    char t[4];
    qToLittleEndian(v, t);
    b.append(t, 4);
}
void putLE16(QByteArray &b, quint16 v)
{
    char t[2];
    qToLittleEndian(v, t);
    b.append(t, 2);
}
quint32 getLE32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}
quint16 getLE16(const char *p)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

struct Msg {
    quint32 cmdId = 0;
    quint8 chId = 0;
    quint16 status = 0;
    quint16 cls = 0;
    quint32 payloadOffset = 0;
    QByteArray body;
};

// The legacy nonce/hello: cmd_id 1, class 0x6514, enc-type "12dc", empty body.
QByteArray helloMessage()
{
    QByteArray m;
    putLE32(m, kMagic);
    putLE32(m, 1);            // cmd_id
    putLE32(m, 0);            // mess_len
    m.append(char(0));       // ch_id
    m.append(char(0));       // mess_id (3 bytes LE)
    m.append(char(0));
    m.append(char(0));
    putLE16(m, 0xDC12);      // enc-type field: bytes 0x12 0xDC
    putLE16(m, kClassLegacy);
    return m;
}

// A modern (0x6414) message. `encExt`/`encBody` are already-ciphered segments;
// payload_offset = encExt length (the split point the receiver uses).
QByteArray modernMessage(quint32 cmdId, quint8 chId, quint32 messId, const QByteArray &encExt,
                         const QByteArray &encBody)
{
    QByteArray m;
    putLE32(m, kMagic);
    putLE32(m, cmdId);
    putLE32(m, static_cast<quint32>(encExt.size() + encBody.size())); // mess_len
    m.append(char(chId));
    m.append(char(messId & 0xFF)); // mess_id (3 bytes LE)
    m.append(char((messId >> 8) & 0xFF));
    m.append(char((messId >> 16) & 0xFF));
    putLE16(m, 0);              // status (0 on send)
    putLE16(m, kClassModern);
    putLE32(m, static_cast<quint32>(encExt.size())); // payload_offset
    m.append(encExt);
    m.append(encBody);
    return m;
}

// Pull one complete message from `buf` (consuming it). False if incomplete.
bool parseMessage(QByteArray &buf, Msg &m)
{
    if (buf.size() < 20)
        return false;
    const quint32 magic = getLE32(buf.constData());
    if (magic != kMagic && magic != kMagicRev)
        return false;
    const quint32 bodyLen = getLE32(buf.constData() + 8);
    const quint16 cls = getLE16(buf.constData() + 18);
    const bool modern = (cls == kClassModern || cls == 0x0000);
    const int headerLen = modern ? 24 : 20;
    if (buf.size() < headerLen + static_cast<int>(bodyLen))
        return false;
    m.cmdId = getLE32(buf.constData() + 4);
    m.chId = static_cast<quint8>(buf[12]);
    m.status = getLE16(buf.constData() + 16); // 200/201/300 = ok on a modern reply
    m.cls = cls;
    m.payloadOffset = modern ? getLE32(buf.constData() + 20) : 0;
    m.body = buf.mid(headerLen, static_cast<int>(bodyLen));
    buf.remove(0, headerLen + static_cast<int>(bodyLen));
    return true;
}

QByteArray channelExtension(int channel)
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                             "<Extension version=\"1.1\">\n<channelId>")
        + QByteArray::number(channel) + QByteArrayLiteral("</channelId>\n</Extension>\n");
}

} // namespace

BaichuanControl::BaichuanControl(Params params) : m_p(std::move(params)) {}

BaichuanControl::~BaichuanControl()
{
    close();
}

bool BaichuanControl::isOpen() const
{
    return m_sock && m_sock->state() == QAbstractSocket::ConnectedState;
}

void BaichuanControl::close()
{
    if (m_sock) {
        m_sock->disconnectFromHost();
        m_sock.reset();
    }
    m_netBuf.clear();
    m_aesKey.clear();
    m_messId = 0;
}

// Read messages until one with the wanted cmd_id arrives (skipping unsolicited
// pushes like cmd 580). False on timeout/abort.
static bool recvMatch(QTcpSocket *sock, QByteArray &netBuf, quint32 wantCmd, Msg &out)
{
    for (int i = 0; i < 40; ++i) {
        while (parseMessage(netBuf, out))
            if (out.cmdId == wantCmd)
                return true;
        if (!sock->waitForReadyRead(300))
            continue;
        netBuf.append(sock->readAll());
    }
    return false;
}

bool BaichuanControl::open()
{
    m_sock = std::make_unique<QTcpSocket>();
    m_sock->connectToHost(m_p.host, static_cast<quint16>(m_p.port));
    if (!m_sock->waitForConnected(5000)) {
        m_sock.reset();
        return false;
    }

    // Step 1: legacy hello, receive the AES nonce.
    m_sock->write(helloMessage());
    m_sock->flush();
    Msg nonceMsg;
    if (!recvMatch(m_sock.get(), m_netBuf, 1, nonceMsg)) {
        close();
        return false;
    }
    const QByteArray nonceXml = bc::xorCrypt(nonceMsg.body, nonceMsg.chId);
    const int a = nonceXml.indexOf("<nonce>");
    const int z = nonceXml.indexOf("</nonce>");
    if (a < 0 || z < 0) {
        close();
        return false;
    }
    const QString nonce = QString::fromUtf8(nonceXml.mid(a + 7, z - a - 7));
    m_aesKey = bc::aesKey(nonce, m_p.password);

    // Step 2: modern login (XOR body, but the modern 0x6414 header).
    const QByteArray user = bc::modernHash(m_p.username, nonce);
    const QByteArray pass = bc::modernHash(m_p.password, nonce);
    const QByteArray login =
        QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body>\n"
                          "<LoginUser version=\"1.1\">\n<userName>")
        + user + QByteArrayLiteral("</userName>\n<password>") + pass
        + QByteArrayLiteral("</password>\n<userVer>1</userVer>\n</LoginUser>\n"
                            "<LoginNet version=\"1.1\"><type>LAN</type><udpPort>0</udpPort>"
                            "</LoginNet>\n</body>\n");
    m_sock->write(modernMessage(1, 0, 0, QByteArray(), bc::xorCrypt(login, 0)));
    m_sock->flush();
    Msg loginReply;
    if (!recvMatch(m_sock.get(), m_netBuf, 1, loginReply)) {
        close();
        return false;
    }
    if (loginReply.status != 200) {
        close();
        return false;
    }
    return true;
}

// Decrypt a reply body. Settings replies are usually a single AES-CFB segment
// (the config XML); some carry two independently-encrypted segments split at
// payload_offset. Try the whole body first, fall back to the split, sanity-checked
// on a leading XML declaration.
static QByteArray decryptReply(const Msg &m, const QByteArray &key)
{
    if (m.body.isEmpty())
        return {};
    const QByteArray whole = bc::aesCfb(m.body, key, /*decrypt=*/true);
    if (whole.contains("<?xml") || whole.contains("<body"))
        return whole;
    if (m.payloadOffset > 0 && m.payloadOffset < static_cast<quint32>(m.body.size())) {
        const QByteArray s1 = bc::aesCfb(m.body.left(m.payloadOffset), key, true);
        const QByteArray s2 = bc::aesCfb(m.body.mid(m.payloadOffset), key, true);
        return s1 + s2;
    }
    return whole;
}

QByteArray BaichuanControl::transact(quint32 cmdId, int channel, const QByteArray &bodyXml,
                                     quint16 *statusOut)
{
    if (!isOpen())
        return {};
    const quint8 chId = channel < 0 ? 250 : static_cast<quint8>(channel + 1);
    const QByteArray ext = channel < 0 ? QByteArray() : channelExtension(channel);
    const QByteArray encExt = ext.isEmpty() ? QByteArray() : bc::aesCfb(ext, m_aesKey, false);
    const QByteArray encBody =
        bodyXml.isEmpty() ? QByteArray() : bc::aesCfb(bodyXml, m_aesKey, false);
    const quint32 messId = (++m_messId) & 0xFFFFFF;

    m_sock->write(modernMessage(cmdId, chId, messId, encExt, encBody));
    m_sock->flush();

    Msg reply;
    if (!recvMatch(m_sock.get(), m_netBuf, cmdId, reply))
        return {};
    if (statusOut)
        *statusOut = reply.status;
    return decryptReply(reply, m_aesKey);
}

QByteArray BaichuanControl::get(quint32 cmdId, int channel, quint16 *statusOut)
{
    return transact(cmdId, channel, QByteArray(), statusOut);
}

int BaichuanControl::readEnable(quint32 getCmdId, int channel)
{
    const QByteArray xml = get(getCmdId, channel);
    if (xml.isEmpty())
        return -1;
    const int a = xml.indexOf("<enable>");
    if (a < 0)
        return -1;
    const int b = xml.indexOf("</enable>", a);
    if (b < 0)
        return -1;
    return xml.mid(a + 8, b - a - 8).trimmed().toInt();
}

bool BaichuanControl::writeEnable(quint32 getCmdId, quint32 setCmdId, int channel, bool enable)
{
    // Read-modify-write: GET the config, flip only the first <enable>, SET it back.
    QByteArray xml = get(getCmdId, channel);
    if (xml.isEmpty())
        return false;
    const int a = xml.indexOf("<enable>");
    if (a < 0)
        return false;
    const int b = xml.indexOf("</enable>", a);
    if (b < 0)
        return false;
    const QByteArray mutated =
        xml.left(a + 8) + (enable ? "1" : "0") + xml.mid(b);
    quint16 status = 0;
    transact(setCmdId, channel, mutated, &status);
    return status == 200 || status == 201 || status == 300;
}

} // namespace rl
