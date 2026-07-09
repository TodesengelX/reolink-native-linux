#include "Paths.h"

#include <QDir>
#include <QStandardPaths>

namespace rl {

static QString ensured(const QString &path)
{
    QDir().mkpath(path);
    return path;
}

QString Paths::dataDir()
{
    return ensured(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

QString Paths::configDir()
{
    return ensured(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
}

QString Paths::databaseFile()
{
    return dataDir() + QStringLiteral("/reolink.db");
}

QString Paths::recordingsDir()
{
    return ensured(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                   + QStringLiteral("/Reolink"));
}

} // namespace rl
