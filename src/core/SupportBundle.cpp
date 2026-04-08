#include "SupportBundle.h"
#include "LogCategories.h"
#include "AppSettings.h"
#include "RadioConnection.h"
#include "models/RadioModel.h"

#include <QCoreApplication>
#include <QSysInfo>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>

namespace NereusSDR {

SupportBundle::SystemInfo SupportBundle::collectSystemInfo()
{
    SystemInfo sys;
    sys.appVersion = QCoreApplication::applicationVersion();
    sys.qtVersion = QString::fromLatin1(qVersion());
    sys.osName = QSysInfo::prettyProductName();
    sys.kernelVersion = QSysInfo::kernelVersion();
    sys.cpuArch = QSysInfo::currentCpuArchitecture();
    sys.buildDate = QString::fromLatin1(__DATE__);
    return sys;
}

SupportBundle::RadioDiagInfo SupportBundle::collectRadioInfo(const RadioModel* model)
{
    RadioDiagInfo info;
    if (!model || !model->isConnected()) {
        info.connected = false;
        return info;
    }

    const RadioConnection* conn = model->connection();
    if (!conn) {
        return info;
    }

    const RadioInfo& ri = conn->radioInfo();
    info.connected = true;
    info.model = ri.displayName();
    info.firmware = QString::number(ri.firmwareVersion);
    info.protocol = static_cast<int>(ri.protocol);

    // Redact MAC — keep only last segment
    if (ri.macAddress.length() > 3) {
        info.macAddress = QStringLiteral("**:**:**:**:**:") + ri.macAddress.right(2);
    }

    // Redact IP — keep only last octet
    QString ip = ri.address.toString();
    int lastDot = ip.lastIndexOf(QLatin1Char('.'));
    if (lastDot > 0) {
        info.ipAddress = QStringLiteral("*.*.*. ") + ip.mid(lastDot + 1);
    }

    return info;
}

QString SupportBundle::bundleDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/NereusSDR/support");
}

QString SupportBundle::createBundle(const RadioModel* model)
{
    // Create timestamped temp directory
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString tempDir = QDir::tempPath() + QStringLiteral("/nereussdr-support-") + timestamp;
    QDir().mkpath(tempDir);

    SystemInfo sys = collectSystemInfo();
    RadioDiagInfo radio = collectRadioInfo(model);

    writeSystemInfo(tempDir, sys);
    writeRadioInfo(tempDir, radio);
    copyLogFiles(tempDir);
    writeSanitizedSettings(tempDir);
    writeEnabledCategories(tempDir);

    // Create archive
    QString bundleDir = bundleDirPath();
    QDir().mkpath(bundleDir);
    QString archiveName = QStringLiteral("support-bundle-") + timestamp;
    QString archivePath = createArchive(tempDir, archiveName);

    // Clean up temp directory
    QDir(tempDir).removeRecursively();

    return archivePath;
}

void SupportBundle::openBundleFolder()
{
    QString dir = bundleDirPath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void SupportBundle::writeSystemInfo(const QString& dir, const SystemInfo& sys)
{
    QJsonObject obj;
    obj[QStringLiteral("appVersion")] = sys.appVersion;
    obj[QStringLiteral("qtVersion")] = sys.qtVersion;
    obj[QStringLiteral("os")] = sys.osName;
    obj[QStringLiteral("kernel")] = sys.kernelVersion;
    obj[QStringLiteral("cpu")] = sys.cpuArch;
    obj[QStringLiteral("buildDate")] = sys.buildDate;

    QFile f(dir + QStringLiteral("/system-info.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

void SupportBundle::writeRadioInfo(const QString& dir, const RadioDiagInfo& radio)
{
    QJsonObject obj;
    obj[QStringLiteral("connected")] = radio.connected;
    obj[QStringLiteral("model")] = radio.model;
    obj[QStringLiteral("mac")] = radio.macAddress;
    obj[QStringLiteral("firmware")] = radio.firmware;
    obj[QStringLiteral("ip")] = radio.ipAddress;
    obj[QStringLiteral("protocol")] = radio.protocol;

    QFile f(dir + QStringLiteral("/radio-info.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

void SupportBundle::copyLogFiles(const QString& dir)
{
    QString logDir = LogManager::instance().logDirPath();
    QDir d(logDir);
    QStringList logs = d.entryList({QStringLiteral("nereussdr-*.log")},
                                   QDir::Files, QDir::Name);

    // Copy up to 3 most recent log files
    int count = 0;
    for (int i = logs.size() - 1; i >= 0 && count < 3; --i) {
        QString src = d.absoluteFilePath(logs[i]);
        QFileInfo fi(src);
        if (fi.size() < 50) {
            continue;  // Skip near-empty files
        }

        QString destName;
        if (count == 0) {
            destName = QStringLiteral("nereussdr.log");
        } else {
            destName = QStringLiteral("nereussdr-%1.log").arg(count);
        }
        QFile::copy(src, dir + QLatin1Char('/') + destName);
        ++count;
    }
}

void SupportBundle::writeSanitizedSettings(const QString& dir)
{
    QString settingsPath = AppSettings::instance().filePath();
    QFile src(settingsPath);
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QFile dest(dir + QStringLiteral("/settings.xml"));
    if (!dest.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&src);
    QTextStream out(&dest);
    while (!in.atEnd()) {
        QString line = in.readLine();
        out << sanitizeLine(line) << '\n';
    }
}

QString SupportBundle::sanitizeLine(const QString& line)
{
    // Redact lines that may contain sensitive data
    QString lower = line.toLower();
    if (lower.contains(QStringLiteral("token"))
        || lower.contains(QStringLiteral("password"))
        || lower.contains(QStringLiteral("secret"))
        || lower.contains(QStringLiteral("auth"))
        || lower.contains(QStringLiteral("credential"))) {
        return QStringLiteral("<!-- [REDACTED] -->");
    }
    return line;
}

void SupportBundle::writeEnabledCategories(const QString& dir)
{
    QFile f(dir + QStringLiteral("/enabled-categories.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    const auto& mgr = LogManager::instance();
    for (const auto& cat : mgr.categories()) {
        out << cat.id << ": " << (cat.enabled ? "ENABLED" : "disabled") << '\n';
    }
}

QString SupportBundle::createArchive(const QString& sourceDir, const QString& archiveName)
{
    QString destDir = bundleDirPath();

#ifdef Q_OS_WIN
    // Windows: use PowerShell Compress-Archive
    QString zipPath = destDir + QLatin1Char('/') + archiveName + QStringLiteral(".zip");
    QProcess proc;
    proc.setProgram(QStringLiteral("powershell"));
    proc.setArguments({
        QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
        QStringLiteral("Compress-Archive -Path '%1/*' -DestinationPath '%2' -Force")
            .arg(QDir::toNativeSeparators(sourceDir),
                 QDir::toNativeSeparators(zipPath))
    });
    proc.start();
    proc.waitForFinished(10000);

    if (QFile::exists(zipPath)) {
        return zipPath;
    }
    return {};
#else
    // Unix/macOS: use tar
    QString tarPath = destDir + QLatin1Char('/') + archiveName + QStringLiteral(".tar.gz");
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(sourceDir).absolutePath());
    proc.setProgram(QStringLiteral("tar"));
    proc.setArguments({
        QStringLiteral("czf"), tarPath,
        QStringLiteral("-C"), QFileInfo(sourceDir).absolutePath(),
        QFileInfo(sourceDir).fileName()
    });
    proc.start();
    proc.waitForFinished(10000);

    if (QFile::exists(tarPath)) {
        return tarPath;
    }
    return {};
#endif
}

} // namespace NereusSDR
