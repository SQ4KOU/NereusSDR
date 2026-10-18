// no-port-check: NereusSDR-original. Where a process keeps its saved FFT plans.

#include "core/DspCacheLocation.h"

#include "core/AppSettings.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>

namespace NereusSDR::DspCacheLocation {

namespace {

QMutex s_mutex;
QString s_directory;

} // namespace

QString directory()
{
    {
        const QMutexLocker locker(&s_mutex);
        if (!s_directory.isEmpty()) {
            return s_directory;
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

void setDirectory(const QString& directory)
{
    const QMutexLocker locker(&s_mutex);
    s_directory = directory.isEmpty() ? QString() : QDir::cleanPath(directory);
}

QString impulseCacheFileName(const QString& profile)
{
    if (!AppSettings::isValidProfileName(profile)) {
        return QStringLiteral("impulse_cache.bin");
    }
    return QStringLiteral("impulse_cache-%1.bin").arg(profile);
}

QStringList impulseCacheFileNames(const QString& folder)
{
    // The names impulseCacheFileName() gives; a profile name is
    // [A-Za-z0-9_-]+ (AppSettings::isValidProfileName).
    static const QRegularExpression cacheName(
        QStringLiteral("^impulse_cache(-[A-Za-z0-9_-]+)?\\.bin$"));
    QStringList names;
    const QStringList candidates =
        QDir(folder).entryList({QStringLiteral("impulse_cache*.bin")}, QDir::Files);
    for (const QString& candidate : candidates) {
        if (cacheName.match(candidate).hasMatch()) {
            names << candidate;
        }
    }
    return names;
}

} // namespace NereusSDR::DspCacheLocation
