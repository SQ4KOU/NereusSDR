// =================================================================
// src/core/daemon/DaemonConfig.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DaemonConfig.h for the on-disk
// format and the design rationale.
// =================================================================

#include "DaemonConfig.h"

#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace NereusSDR {

DaemonConfig DaemonConfig::defaults()
{
    return DaemonConfig{};
}

DaemonConfig DaemonConfig::fromFile(const QString& path, QString* errorOut)
{
    DaemonConfig cfg = defaults();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) {
            *errorOut = QStringLiteral("could not open \"%1\": %2")
                            .arg(path, file.errorString());
        }
        return cfg;
    }

    QTextStream in(&file);
    int lineNo = 0;
    while (!in.atEnd()) {
        ++lineNo;
        QString line = in.readLine();

        // '#' starts a comment, whether it is the whole line or trails a
        // value; truncate before trimming so "key = value  # note" and
        // "# note" both work.
        const int hashIdx = line.indexOf(QLatin1Char('#'));
        if (hashIdx >= 0) {
            line.truncate(hashIdx);
        }
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const int eqIdx = line.indexOf(QLatin1Char('='));
        if (eqIdx < 0) {
            qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                              << "has no '=', ignored:" << line;
            continue;
        }

        const QString key = line.left(eqIdx).trimmed();
        const QString value = line.mid(eqIdx + 1).trimmed();

        // Unknown keys warn rather than fail (see DaemonConfig.h): a
        // config file written for a newer nereusd must still start an
        // older one instead of refusing to boot.
        if (key == QLatin1String("radio_mac")) {
            cfg.radioMac = value;
        } else if (key == QLatin1String("audio_device")) {
            cfg.audioDevice = value;
        } else if (key == QLatin1String("sample_rate_hz")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                cfg.sampleRateHz = v;
                cfg.sampleRateExplicit = true;
            } else {
                qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                                  << "sample_rate_hz is not a number, keeping"
                                  << cfg.sampleRateHz << ":" << value;
            }
        } else if (key == QLatin1String("slice_count")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                cfg.sliceCount = v;
            } else {
                qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                                  << "slice_count is not a number, keeping"
                                  << cfg.sliceCount << ":" << value;
            }
        } else if (key == QLatin1String("remote_port")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                cfg.remotePort = v;
            } else {
                qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                                  << "remote_port is not a number, keeping"
                                  << cfg.remotePort << ":" << value;
            }
        } else if (key == QLatin1String("audio_bitrate")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok && (v == kDefaultAudioBitrate || v == kHighAudioBitrate)) {
                cfg.audioBitrate = v;
            } else {
                cfg.audioBitrate = kDefaultAudioBitrate;
                qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                                  << "audio_bitrate must be 24000 or 48000, keeping"
                                  << cfg.audioBitrate << ":" << value;
            }
        } else if (key == QLatin1String("audio_lossless")) {
            if (value.compare(QLatin1String("allow"), Qt::CaseInsensitive) == 0) {
                cfg.audioLosslessAllowed = true;
            } else if (value.compare(QLatin1String("deny"), Qt::CaseInsensitive) == 0) {
                cfg.audioLosslessAllowed = false;
            } else {
                cfg.audioLosslessAllowed = true;
                qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                                  << "audio_lossless must be allow or deny, keeping allow :"
                                  << value;
            }
        } else if (key == QLatin1String("core_name")) {
            cfg.coreName = value;
        } else if (key == QLatin1String("remote_bind")) {
            cfg.remoteBind = value;
        } else if (key == QLatin1String("display_application_bytes_per_second")
                   || key == QLatin1String("spectrum_sample_units_per_second")) {
            bool ok = false;
            const quint64 parsed = value.toULongLong(&ok, 10);
            const quint64 limit = ok && !value.startsWith(QLatin1Char('-')) ? parsed : 0;
            if (key == QLatin1String("display_application_bytes_per_second")) {
                cfg.displayApplicationBytesPerSecond = limit;
            } else {
                cfg.spectrumSampleUnitsPerSecond = limit;
            }
        } else {
            qCWarning(lcApp) << "nereusd.conf" << path << "line" << lineNo
                              << "unknown key, ignored:" << key;
        }
    }

    if (errorOut) {
        errorOut->clear();
    }
    return cfg;
}

bool DaemonConfig::validate(QString* errorOut) const
{
    static const QRegularExpression radioMacPattern(QStringLiteral("\\A(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\\z"));
    if (!radioMac.isEmpty() && !radioMacPattern.match(radioMac).hasMatch()) {
        if (errorOut) { *errorOut = QStringLiteral("radio_mac must be empty or six colon-separated hexadecimal pairs"); }
        return false;
    }
    const QByteArray nameBytes = coreName.toUtf8();
    bool invalidName = nameBytes.size() > 128 || QString::fromUtf8(nameBytes) != coreName;
    for (QChar character : coreName) {
        invalidName = invalidName || character.category() == QChar::Other_Control;
    }
    if (invalidName) {
        if (errorOut) { *errorOut = QStringLiteral("core_name must be valid UTF-8 without control characters, at most 128 bytes"); }
        return false;
    }
    if ((displayApplicationBytesPerSecond || spectrumSampleUnitsPerSecond)
        && !displayBudgetLimits()) {
        if (errorOut) {
            *errorOut = QStringLiteral("display_application_bytes_per_second and "
                "spectrum_sample_units_per_second must both be positive integers "
                "no greater than 9007199254740991");
        }
        return false;
    }
    if (sliceCount < 1) {
        if (errorOut) {
            *errorOut = QStringLiteral("slice_count must be at least 1, got %1")
                            .arg(sliceCount);
        }
        return false;
    }
    if (sampleRateHz <= 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("sample_rate_hz must be positive, got %1")
                            .arg(sampleRateHz);
        }
        return false;
    }
    if (audioBitrate != kDefaultAudioBitrate && audioBitrate != kHighAudioBitrate) {
        if (errorOut) {
            *errorOut = QStringLiteral("audio_bitrate must be 24000 or 48000, got %1")
                            .arg(audioBitrate);
        }
        return false;
    }
    // 0 is the documented "do not listen" value, so only a genuinely
    // impossible port is rejected. Refusing at parse time rather than
    // letting bind() fail later means the operator is told which line of
    // their config is wrong.
    if (remotePort < 0 || remotePort > 65535) {
        if (errorOut) {
            *errorOut = QStringLiteral(
                            "remote_port must be 0 (disabled) or 1-65535, got %1")
                            .arg(remotePort);
        }
        return false;
    }

    if (errorOut) {
        errorOut->clear();
    }
    return true;
}

std::optional<DisplayBudgetLimits> DaemonConfig::displayBudgetLimits() const
{
    if (!displayApplicationBytesPerSecond || !spectrumSampleUnitsPerSecond) {
        return std::nullopt;
    }
    const DisplayBudgetLimits limits{*displayApplicationBytesPerSecond,
                                    *spectrumSampleUnitsPerSecond, 1};
    return limits.isValid() ? std::optional{limits} : std::nullopt;
}

QString resolveDaemonProfileArgument(const QString& requested, bool wasSet, QString* errorOut)
{
    if (errorOut) {
        errorOut->clear();
    }
    if (!wasSet) {
        // No --profile on the command line at all: reserve nereusd's own
        // profile instead of silently sharing the GUI client's directory.
        // See this function's declaration in DaemonConfig.h for the full
        // rationale (Remote Daemon R2, Task 1).
        return QString(AppSettings::kDaemonProfileName);
    }
    if (requested.isEmpty()) {
        // Explicit --profile "": the deliberate escape hatch back to the
        // shared default directory.
        return {};
    }
    if (!AppSettings::isValidProfileName(requested)) {
        if (errorOut) {
            *errorOut = QStringLiteral(
                "invalid --profile \"%1\" (allowed: [A-Za-z0-9_-]+)").arg(requested);
        }
        return {};
    }
    return requested;
}

} // namespace NereusSDR
