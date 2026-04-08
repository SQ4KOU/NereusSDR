#pragma once

#include <QString>

namespace NereusSDR {

class RadioModel;

// Collects diagnostic information into a zip archive for bug reports.
// Bundle includes: log files, system info, radio info, sanitized settings,
// and enabled logging categories.
class SupportBundle {
public:
    struct SystemInfo {
        QString appVersion;
        QString qtVersion;
        QString osName;
        QString kernelVersion;
        QString cpuArch;
        QString buildDate;
    };

    struct RadioDiagInfo {
        QString model;
        QString macAddress;     // Redacted: only last segment
        QString firmware;
        QString ipAddress;      // Redacted: only last octet
        bool connected{false};
        int protocol{0};
    };

    // Collect system info from QSysInfo + app version.
    static SystemInfo collectSystemInfo();

    // Collect radio info from RadioModel (safe if null/disconnected).
    static RadioDiagInfo collectRadioInfo(const RadioModel* model);

    // Create a timestamped support bundle archive.
    // Returns the full path to the created archive, or empty on failure.
    static QString createBundle(const RadioModel* model);

    // Open the folder containing support bundles in the file manager.
    static void openBundleFolder();

private:
    static QString bundleDirPath();
    static void writeSystemInfo(const QString& dir, const SystemInfo& sys);
    static void writeRadioInfo(const QString& dir, const RadioDiagInfo& radio);
    static void copyLogFiles(const QString& dir);
    static void writeSanitizedSettings(const QString& dir);
    static void writeEnabledCategories(const QString& dir);
    static QString createArchive(const QString& sourceDir, const QString& archiveName);
    static QString sanitizeLine(const QString& line);
};

} // namespace NereusSDR
