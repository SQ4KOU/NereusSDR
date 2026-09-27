#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/NetworkTrouble.h  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 29 step 2b (R-IOS-16; the options survey's B.6 and
// B.7): plain words for two networks that break the secure connection to
// the remote access service before it starts.
//
//   - A certificate for another name can indicate a captive portal, but
//     can also be a server or DNS configuration problem.
//   - An untrusted issuer can indicate network inspection, but can also
//     be an incomplete chain or a local trust-store problem.
//
// Only the words: the service is never pinned (the system's trusted
// authorities decide, as before), and the Core's identity check, which
// rides inside the session and survives any relay, is unchanged. No probe
// is sent; the operator is told what the failure looks like.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QList>
#include <QSslError>
#include <QString>

namespace NereusSDR::NetworkTrouble {

/// The operator's words for a secure connection that failed with
/// `errors`; empty when they point at neither case.
QString wordsForTlsErrors(const QList<QSslError>& errors);

} // namespace NereusSDR::NetworkTrouble
