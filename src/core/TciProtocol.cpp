// no-port-check: NereusSDR-original — parser + dispatch shells ported from
// Thetis TCIServer.cs:4900-5197 [v2.10.3.13]. Handler bodies are stubs;
// Phase 5+ adds individual cases via the matrix runner.

// src/core/TciProtocol.cpp  (NereusSDR)
// NereusSDR-original — TCI command protocol handler implementation.
//
// Parser ported from Thetis TCIServer.cs:4900-4924 [v2.10.3.13].
// Two-switch dispatch shape from Thetis TCIServer.cs:4924-5197 [v2.10.3.13].
//
// This file REPLACES the Phase 1 stub (commit 77d27b3).
//
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 3.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.

#include "TciProtocol.h"
#include "LogCategories.h"

namespace NereusSDR {

TciProtocol::TciProtocol(QObject* radio, QObject* parent)
    : QObject(parent)
    , m_radio(radio)
{
    (void)m_radio;  // dereferenced in Phase 6+ via QMetaObject::invokeMethod for setters.
}

// From Thetis TCIServer.cs:4900-4924 [v2.10.3.13] — text-frame parser.
// Strip trailing ';', split on first ':', case-insensitive command lookup.
// json_spot exception (parts[0]=="spot" AND msg contains "[json]{") routes
// to set path; the actual JSON brace-aware splitter at TCIServer.cs:4785-4900
// is a Phase 12 (spot stubs) concern.
QString TciProtocol::handleCommand(const QString& command)
{
    QString msg = command.trimmed();
    if (msg.endsWith(QLatin1Char(';'))) {
        msg.chop(1);
        msg = msg.trimmed();
    }
    if (msg.isEmpty()) {
        return {};
    }

    const int colonIdx = msg.indexOf(QLatin1Char(':'));
    QStringList parts;
    if (colonIdx >= 0) {
        parts << msg.left(colonIdx);
        parts << msg.mid(colonIdx + 1);
    } else {
        parts << msg;
    }

    const QString name = parts.at(0).toLower().trimmed();

    // From Thetis TCIServer.cs:4917 [v2.10.3.13] — json_spot detour.
    const bool jsonSpot = parts.size() >= 2 && name == QStringLiteral("spot")
                          && msg.toLower().indexOf(QStringLiteral("[json]{")) >= 0;

    if (parts.size() == 2 || jsonSpot) {
        const QStringList args = parts.at(1).split(QLatin1Char(','));
        return handleSetCommand(name, args);
    }
    return handleQueryCommand(name);
}

bool TciProtocol::hasPendingNotification() const
{
    return !m_pendingNotifications.isEmpty();
}

QString TciProtocol::takePendingNotification()
{
    if (m_pendingNotifications.isEmpty()) {
        return {};
    }
    return m_pendingNotifications.takeFirst();
}

QStringList TciProtocol::buildInitBurst() const
{
    // Phase 4 Task 4.1 replaces with the 8-line wrapper from
    // Thetis TCIServer.cs:2512-2552 [v2.10.3.13].
    return {};
}

// NereusSDR architectural divergence — Slice A/B/C/D maps to trx:N wire format.
// Identity through Phase 3; same mapping in subsequent phases (design doc §1.2).
int TciProtocol::sliceToTrx(int slice) { return slice; }
int TciProtocol::trxToSlice(int trx) { return trx; }

void TciProtocol::resetDispatchCounters()
{
    m_setDispatchCount = 0;
    m_queryDispatchCount = 0;
}

// From Thetis TCIServer.cs:4924-5128 [v2.10.3.13] — 60-case set-command switch.
// Phase 5+ adds individual cases via the matrix runner.
QString TciProtocol::handleSetCommand(const QString& name, const QStringList& args)
{
    (void)name;
    (void)args;
    ++m_setDispatchCount;
    return {};
}

// From Thetis TCIServer.cs:5134-5197 [v2.10.3.13] — 21-case query-command switch.
// Phase 5+ adds individual cases via the matrix runner.
QString TciProtocol::handleQueryCommand(const QString& name)
{
    (void)name;
    ++m_queryDispatchCount;
    return {};
}

} // namespace NereusSDR
