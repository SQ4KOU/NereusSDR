// =================================================================
// src/core/session/RemoteStationOptions.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 20.
//
// See RemoteStationOptions.h for why the AppSettings key literals are not
// in this file.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 20: --station
//                                    and --token, and the remote-mode GUI
//                                    gate. AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include "core/session/RemoteStationOptions.h"

#include <QLatin1String>
#include <QUrl>

namespace NereusSDR {

bool RemoteStationOptions::isValidStationUrl(const QString& candidate, QString* whyNot)
{
    if (candidate.isEmpty()) {
        if (whyNot != nullptr) {
            *whyNot = QStringLiteral("Station address is empty.");
        }
        return false;
    }

    const QUrl url(candidate, QUrl::StrictMode);
    if (!url.isValid()) {
        if (whyNot != nullptr) {
            *whyNot = QStringLiteral("Station address is not a valid URL: %1")
                          .arg(url.errorString());
        }
        return false;
    }

    const QString scheme = url.scheme();
    if (scheme != QLatin1String("wss") && scheme != QLatin1String("ws")) {
        if (whyNot != nullptr) {
            // Naming the scheme back is the point: an operator who typed
            // https:// gets told which two words the field wants, rather
            // than a QWebSocket connect failure several seconds later.
            *whyNot = QStringLiteral(
                          "Station address must start with wss:// or ws:// "
                          "(got \"%1\").")
                          .arg(scheme.isEmpty() ? QStringLiteral("no scheme")
                                                : scheme);
        }
        return false;
    }

    if (url.host().isEmpty()) {
        if (whyNot != nullptr) {
            *whyNot = QStringLiteral("Station address has no host.");
        }
        return false;
    }

    return true;
}

} // namespace NereusSDR
