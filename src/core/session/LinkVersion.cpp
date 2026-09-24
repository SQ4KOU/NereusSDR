// =================================================================
// src/core/session/LinkVersion.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.
//
// See LinkVersion.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): link
//                                    majors, their agreement and the
//                                    refusal wording. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/LinkVersion.h"

#include <QStringList>

#include <algorithm>

namespace NereusSDR::LinkVersion {

namespace {

quint16 newest(const QList<quint16>& majors)
{
    return majors.isEmpty() ? quint16(0) : *std::max_element(majors.cbegin(), majors.cend());
}

} // namespace

QList<quint16> supportedMajors()
{
    return QList<quint16>(kSupportedSessionMajors.cbegin(), kSupportedSessionMajors.cend());
}

std::optional<quint16> agreeMajor(QList<quint16> ours, QList<quint16> theirs)
{
    std::optional<quint16> agreed;
    for (const quint16 major : ours) {
        if (theirs.contains(major) && (!agreed || major > *agreed)) {
            agreed = major;
        }
    }
    return agreed;
}

QString refusalText(QList<quint16> station, QList<quint16> client)
{
    const quint16 stationNewest = newest(station);
    const quint16 clientNewest = newest(client);
    // The side with the older newest version is the one to update.
    const QString update = stationNewest < clientNewest
                               ? QStringLiteral("Update the station.")
                               : QStringLiteral("Update this app.");
    return QStringLiteral("This station runs link version %1 and this app runs version %2. %3")
        .arg(stationNewest)
        .arg(clientNewest)
        .arg(update);
}

QList<quint16> parseMajorList(const QString& text, QString* error)
{
    QList<quint16> majors;
    const QStringList parts = text.split(QLatin1Char(','));
    for (const QString& part : parts) {
        bool ok = false;
        const uint value = part.trimmed().toUInt(&ok);
        if (!ok || value < 1 || value > 65535) {
            if (error) {
                *error = QStringLiteral("Give the link versions as whole numbers from 1 to "
                                        "65535, separated by commas, for example 1,2.");
            }
            return {};
        }
        if (!majors.contains(quint16(value))) {
            majors.append(quint16(value));
        }
    }
    std::sort(majors.begin(), majors.end());
    return majors;
}

bool testLinkMajorsAllowed()
{
#ifdef QT_NO_DEBUG
    return false;
#else
    return true;
#endif
}

QList<quint16> resolveTestLinkMajors(bool optionSet, const QString& value, bool allowed,
                                     QString* error)
{
    if (!optionSet) {
        return supportedMajors();
    }
    if (!allowed) {
        if (error) {
            *error = QStringLiteral("--test-link-majors works only in a debug build of "
                                    "nereusd.");
        }
        return {};
    }
    return parseMajorList(value, error);
}

} // namespace NereusSDR::LinkVersion
