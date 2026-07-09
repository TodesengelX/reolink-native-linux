#include "CredentialStore.h"

#include "Log.h"

// glib's GDBus headers have struct members named "signals"; Qt's signals
// macro (== public) breaks them. This file uses no Qt signals — drop the macro.
#undef signals
#include <libsecret/secret.h>

namespace rl {

static const SecretSchema *schema()
{
    static const SecretSchema s = {
        "io.github.reolink_linux.device", SECRET_SCHEMA_NONE,
        {
            // String, not integer: SQLite rowids are 64-bit and would collide
            // (or go negative) if truncated to the libsecret INTEGER attribute.
            {"host_id", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        // Reserved fields required by the C struct definition.
        0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    return &s;
}

bool CredentialStore::store(qint64 hostId, const QString &password)
{
    GError *error = nullptr;
    const bool ok = secret_password_store_sync(
        schema(), SECRET_COLLECTION_DEFAULT, "Reolink device password",
        password.toUtf8().constData(), nullptr, &error,
        "host_id", QString::number(hostId).toUtf8().constData(), nullptr);
    if (!ok && error) {
        m_lastError = QString::fromUtf8(error->message);
        qCWarning(lcCore) << "Keyring store failed:" << m_lastError;
        g_error_free(error);
    }
    return ok;
}

QString CredentialStore::lookup(qint64 hostId, bool *ok)
{
    GError *error = nullptr;
    gchar *secret = secret_password_lookup_sync(schema(), nullptr, &error,
                                                "host_id", QString::number(hostId).toUtf8().constData(), nullptr);
    if (error) {
        m_lastError = QString::fromUtf8(error->message);
        qCWarning(lcCore) << "Keyring lookup failed:" << m_lastError;
        g_error_free(error);
        if (ok)
            *ok = false;
        return {};
    }
    if (!secret) {
        if (ok)
            *ok = false;
        return {};
    }
    const QString password = QString::fromUtf8(secret);
    secret_password_free(secret);
    if (ok)
        *ok = true;
    return password;
}

bool CredentialStore::remove(qint64 hostId)
{
    GError *error = nullptr;
    const bool ok = secret_password_clear_sync(schema(), nullptr, &error,
                                               "host_id", QString::number(hostId).toUtf8().constData(), nullptr);
    if (error) {
        m_lastError = QString::fromUtf8(error->message);
        g_error_free(error);
    }
    return ok;
}

} // namespace rl
