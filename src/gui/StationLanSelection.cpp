// no-port-check: NereusSDR-original. R-R3-38 saved trust for untrusted discovery.
#include "gui/StationLanSelection.h"
namespace NereusSDR {
QList<SavedCoreTarget> matchingSavedCores(const StationLanEndpoint& endpoint,
                                         const QList<SavedCoreTarget>& saved)
{
    QList<SavedCoreTarget> matches;
    // Validate even callers other than the UDP decoder. Empty or malformed
    // identity can never select credentials by matching another empty field.
    if (encodeStationLanAnnouncement(endpoint.announcement).isEmpty()) { return matches; }
    for (const SavedCoreTarget& target : saved) {
        if (!target.connection.token.isEmpty()
            && target.connection.fingerprint.compare(endpoint.announcement.fingerprint,
                                                       Qt::CaseInsensitive) == 0) {
            matches.append(target);
        }
    }
    return matches;
}
}
