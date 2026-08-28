#include "BaichuanSession.h"

#include "protocol/BcCrypto.h"

#include <QTcpSocket>
#include <QXmlStreamReader>
#include <QStringList>
#include <QtEndian>

namespace rl {

namespace {

constexpr quint32 kMagic = 0x0ABCDEF0;
constexpr quint32 kMagicRev = 0x0FEDCBA0;
constexpr quint16 kClassModern = 0x6414;
constexpr quint16 kClassLegacy = 0x6514;

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

QByteArray helloMessage()
{
    QByteArray m;
    putLE32(m, kMagic);
    putLE32(m, 1);
    putLE32(m, 0);
    m.append(char(0));
    m.append(char(0));
    m.append(char(0));
    m.append(char(0));
    putLE16(m, 0xDC12);
    putLE16(m, kClassLegacy);
    return m;
}

QByteArray modernMessage(quint32 cmdId, quint8 chId, quint32 messId, const QByteArray &encExt,
                         const QByteArray &encBody)
{
    QByteArray m;
    putLE32(m, kMagic);
    putLE32(m, cmdId);
    putLE32(m, static_cast<quint32>(encExt.size() + encBody.size()));
    m.append(char(chId));
    m.append(char(messId & 0xFF));
    m.append(char((messId >> 8) & 0xFF));
    m.append(char((messId >> 16) & 0xFF));
    putLE16(m, 0);
    putLE16(m, kClassModern);
    putLE32(m, static_cast<quint32>(encExt.size()));
    m.append(encExt);
    m.append(encBody);
    return m;
}

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
    m.status = getLE16(buf.constData() + 16);
    m.cls = cls;
    m.payloadOffset = modern ? getLE32(buf.constData() + 20) : 0;
    m.body = buf.mid(headerLen, static_cast<int>(bodyLen));
    buf.remove(0, headerLen + static_cast<int>(bodyLen));
    return true;
}

bool recvMatch(QTcpSocket &sock, QByteArray &netBuf, quint32 wantCmd, Msg &out)
{
    for (int i = 0; i < 40; ++i) {
        while (parseMessage(netBuf, out))
            if (out.cmdId == wantCmd)
                return true;
        if (!sock.waitForReadyRead(300))
            continue;
        netBuf.append(sock.readAll());
    }
    return false;
}

bool getNonce(QTcpSocket &sock, QByteArray &netBuf, QString *nonce)
{
    sock.write(helloMessage());
    sock.flush();
    Msg msg;
    if (!recvMatch(sock, netBuf, 1, msg))
        return false;
    const QByteArray xml = bc::xorCrypt(msg.body, msg.chId);
    const int a = xml.indexOf("<nonce>");
    const int z = xml.indexOf("</nonce>");
    if (a < 0 || z < 0 || z <= a)
        return false;
    *nonce = QString::fromUtf8(xml.mid(a + 7, z - a - 7));
    return true;
}

QString currentName(const QXmlStreamReader &r)
{
    return r.name().toString();
}

BaichuanSession::DeviceInfo parseLoginDeviceInfoImpl(const QByteArray &xml)
{
    BaichuanSession::DeviceInfo out;
    QXmlStreamReader r(xml);
    bool inDeviceInfo = false;
    int deviceDepth = 0;
    bool inEncodeTable = false;
    QString encodeType;
    int mainW = 0;
    int mainH = 0;
    int subW = 0;
    int subH = 0;
    QStringList elementStack;

    while (!r.atEnd()) {
        const auto token = r.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString tag = currentName(r);
            elementStack.append(tag);
            if (tag == QLatin1String("DeviceInfo")) {
                inDeviceInfo = true;
                deviceDepth = 1;
            } else {
                if (inDeviceInfo)
                    ++deviceDepth;
                if (tag == QLatin1String("encodeTable")) {
                    inEncodeTable = true;
                    encodeType.clear();
                }
            }
        } else if (token == QXmlStreamReader::Characters && !r.isWhitespace()) {
            const QString tag = elementStack.isEmpty() ? QString() : elementStack.last();
            const QString text = r.text().toString().trimmed();
            if (inDeviceInfo) {
                if (tag == QLatin1String("name") && out.name.isEmpty())
                    out.name = text;
                else if (tag == QLatin1String("type") && out.model.isEmpty())
                    out.model = text;
                else if (tag == QLatin1String("typeInfo") && out.model.isEmpty())
                    out.model = text;
                else if (tag == QLatin1String("channelNum"))
                    out.channelNum = qMax(1, text.toInt());
                else if (tag == QLatin1String("analogChnNum") && out.channelNum <= 1)
                    out.channelNum = qMax(1, text.toInt());
            }
            if (tag == QLatin1String("userLevel") || tag == QLatin1String("userType")
                || tag == QLatin1String("privilege") || tag == QLatin1String("authority")) {
                out.isAdmin = text.compare(QStringLiteral("admin"), Qt::CaseInsensitive) == 0
                              || text == QLatin1String("0") || text == QLatin1String("1");
            }
            if (inEncodeTable) {
                if (tag == QLatin1String("type"))
                    encodeType = text;
                else if (tag == QLatin1String("width")) {
                    if (encodeType == QLatin1String("mainStream"))
                        mainW = text.toInt();
                    else if (encodeType == QLatin1String("subStream"))
                        subW = text.toInt();
                } else if (tag == QLatin1String("height")) {
                    if (encodeType == QLatin1String("mainStream"))
                        mainH = text.toInt();
                    else if (encodeType == QLatin1String("subStream"))
                        subH = text.toInt();
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString tag = currentName(r);
            if (tag == QLatin1String("encodeTable"))
                inEncodeTable = false;
            if (inDeviceInfo) {
                --deviceDepth;
                if (deviceDepth <= 0)
                    inDeviceInfo = false;
            }
            if (!elementStack.isEmpty())
                elementStack.removeLast();
        }
    }

    if (mainW > 0 && mainH > 0)
        out.mainSize = QSize(mainW, mainH);
    if (subW > 0 && subH > 0)
        out.subSize = QSize(subW, subH);
    return out;
}

BaichuanSession::FailKind loginFailKind(quint16 status, const QByteArray &body)
{
    const QByteArray lower = body.toLower();
    if (lower.contains("lock") || lower.contains("too many"))
        return BaichuanSession::FailKind::Locked;
    if (status == 401 || lower.contains("invalid") || lower.contains("password"))
        return BaichuanSession::FailKind::Auth;
    return BaichuanSession::FailKind::Protocol;
}

QString failMessage(BaichuanSession::FailKind kind)
{
    switch (kind) {
    case BaichuanSession::FailKind::Transport:
        return QStringLiteral("couldn't connect to Baichuan port 9000");
    case BaichuanSession::FailKind::Auth:
        return QStringLiteral("invalid Baichuan username or password");
    case BaichuanSession::FailKind::Locked:
        return QStringLiteral("Baichuan login is locked after too many failed attempts");
    case BaichuanSession::FailKind::Protocol:
        return QStringLiteral("unexpected Baichuan reply");
    case BaichuanSession::FailKind::None:
        break;
    }
    return {};
}

} // namespace

bool BaichuanSession::probe(const QString &host, int port)
{
    QTcpSocket sock;
    sock.connectToHost(host, static_cast<quint16>(port));
    if (!sock.waitForConnected(500))
        return false;
    QByteArray netBuf;
    QString nonce;
    return getNonce(sock, netBuf, &nonce) && !nonce.isEmpty();
}

BaichuanSession::DeviceInfo BaichuanSession::parseDeviceInfo(const QByteArray &xml)
{
    return parseLoginDeviceInfoImpl(xml);
}

BaichuanSession::LoginResult BaichuanSession::login(const QString &host, int port,
                                                    const QString &username,
                                                    const QString &password)
{
    LoginResult out;
    QTcpSocket sock;
    sock.connectToHost(host, static_cast<quint16>(port));
    if (!sock.waitForConnected(5000)) {
        out.failKind = FailKind::Transport;
        out.error = failMessage(out.failKind);
        return out;
    }

    QByteArray netBuf;
    QString nonce;
    if (!getNonce(sock, netBuf, &nonce)) {
        out.failKind = FailKind::Protocol;
        out.error = failMessage(out.failKind);
        return out;
    }

    const QByteArray user = bc::modernHash(username, nonce);
    const QByteArray pass = bc::modernHash(password.left(31), nonce);
    const QByteArray loginXml =
        QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body>\n"
                          "<LoginUser version=\"1.1\">\n<userName>")
        + user + QByteArrayLiteral("</userName>\n<password>") + pass
        + QByteArrayLiteral("</password>\n<userVer>1</userVer>\n</LoginUser>\n"
                            "<LoginNet version=\"1.1\"><type>LAN</type><udpPort>0</udpPort>"
                            "</LoginNet>\n</body>\n");
    sock.write(modernMessage(1, 0, 0, QByteArray(), bc::xorCrypt(loginXml, 0)));
    sock.flush();

    Msg reply;
    if (!recvMatch(sock, netBuf, 1, reply)) {
        out.failKind = FailKind::Transport;
        out.error = failMessage(out.failKind);
        return out;
    }

    const QByteArray replyXml = bc::xorCrypt(reply.body, reply.chId);
    if (reply.status != 200) {
        out.failKind = loginFailKind(reply.status, replyXml);
        out.error = failMessage(out.failKind);
        return out;
    }

    out.device = parseLoginDeviceInfoImpl(replyXml);
    out.ok = true;
    out.failKind = FailKind::None;
    return out;
}

} // namespace rl
