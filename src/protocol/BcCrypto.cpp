#include "BcCrypto.h"

#include <QCryptographicHash>

#include <cstring>

extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}

namespace rl::bc {

namespace {
// The fixed 8-byte BCEncrypt key (a documented protocol constant).
constexpr unsigned char kXorKey[8] = {0x1F, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78, 0xFF};
// The fixed AES-CFB IV (a documented protocol constant): ASCII "0123456789abcdef".
constexpr unsigned char kAesIv[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
} // namespace

QByteArray xorCrypt(const QByteArray &in, quint8 offset)
{
    QByteArray out(in.size(), Qt::Uninitialized);
    for (int i = 0; i < in.size(); ++i) {
        const unsigned char key = kXorKey[(offset + static_cast<unsigned>(i)) % 8];
        out[i] = static_cast<char>(static_cast<unsigned char>(in[i]) ^ key ^ offset);
    }
    return out;
}

QByteArray modernHash(const QString &value, const QString &nonce)
{
    const QByteArray data = (value + nonce).toUtf8();
    const QByteArray hex = QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().toUpper();
    return hex.left(31);
}

QByteArray aesKey(const QString &nonce, const QString &password)
{
    const QByteArray data = (nonce + QLatin1Char('-') + password).toUtf8();
    const QByteArray hex = QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().toUpper();
    return hex.left(16);
}

QByteArray aesCfb(const QByteArray &in, const QByteArray &key, bool decrypt)
{
    if (in.isEmpty() || key.size() < 16)
        return in;
    AVAES *aes = av_aes_alloc();
    if (!aes)
        return {};
    // CFB always runs the block cipher in the ENCRYPT direction (both ways).
    av_aes_init(aes, reinterpret_cast<const uint8_t *>(key.constData()), 128, 0);

    QByteArray out(in.size(), Qt::Uninitialized);
    unsigned char feedback[16];
    std::memcpy(feedback, kAesIv, 16);
    unsigned char keystream[16];
    unsigned char cipherBlock[16];

    for (int off = 0; off < in.size(); off += 16) {
        av_aes_crypt(aes, keystream, feedback, 1, nullptr, 0); // ECB-encrypt feedback
        const int n = std::min(16, static_cast<int>(in.size() - off));
        for (int j = 0; j < n; ++j) {
            const unsigned char ib = static_cast<unsigned char>(in[off + j]);
            const unsigned char ob = ib ^ keystream[j];
            out[off + j] = static_cast<char>(ob);
            cipherBlock[j] = decrypt ? ib : ob; // CFB feedback is the ciphertext
        }
        if (n == 16)
            std::memcpy(feedback, cipherBlock, 16);
    }
    av_free(aes);
    return out;
}

} // namespace rl::bc
