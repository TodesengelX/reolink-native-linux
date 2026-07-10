#pragma once

#include <QByteArray>
#include <QString>

// Cryptography for the Reolink "Baichuan" (BC, TCP 9000) protocol.
//
// Clean-room: implemented from the documented wire facts (constants, byte
// layouts) and the MIT-licensed reolink_aio reference — NOT from AGPL neolink
// (DESIGN firewall, docs/proposals/performance-first.md §risks). The values here
// are protocol facts (a fixed XOR table, an MD5 recipe), not copyrightable code.
namespace rl::bc {

// BCEncrypt: the light XOR cipher used for control-channel bodies (and for the
// whole session when AES isn't negotiated). Encrypt and decrypt are identical.
// `offset` is the message header's channel_id byte (0 for a standalone host).
QByteArray xorCrypt(const QByteArray &in, quint8 offset);

// Modern-login credential hash: uppercase-hex MD5(value + nonce), first 31 chars.
// Used for both the userName and password elements of the modern LoginUser body.
QByteArray modernHash(const QString &value, const QString &nonce);

// AES-128 session key when the camera negotiates AES/FullAES: the first 16 ASCII
// characters of uppercase-hex MD5(nonce + "-" + password). Note the dash and the
// nonce-first ordering — distinct from modernHash's recipe.
QByteArray aesKey(const QString &nonce, const QString &password);

// AES-128-CFB (full 128-bit feedback) with the fixed protocol IV "0123456789abcdef".
// State resets each call (per-message, as the wire protocol requires). Used for
// control bodies and, under FullAES, the media payload.
QByteArray aesCfb(const QByteArray &in, const QByteArray &key, bool decrypt);

} // namespace rl::bc
