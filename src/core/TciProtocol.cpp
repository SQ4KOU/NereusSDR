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
#include "AppSettings.h"

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

// From Thetis TCIServer.cs:2512-2552 [v2.10.3.13] — sendInitialisationData
// emits 8 wrapper lines + buildInitialRadioState() + ready;.
QStringList TciProtocol::buildInitBurst() const
{
    QStringList lines;
    auto& s = AppSettings::instance();

    // From Thetis TCIServer.cs:2515-2520 [v2.10.3.13]
    //MW0LGE_22 emulate ee3 protocol
    const bool emulateEsdr3 = s.value(QStringLiteral("TciEmulateExpertSDR3Protocol"),
                                      QStringLiteral("False")).toString()
                              == QStringLiteral("True");
    const QString protocolName = emulateEsdr3
        ? QStringLiteral("ExpertSDR3")
        : QStringLiteral("Thetis");
    lines << QStringLiteral("protocol:%1,2.0;").arg(protocolName);

    // From Thetis TCIServer.cs:2523-2528 [v2.10.3.13]
    //MW0LGE_22 emulate sunsdr
    const bool emulateSunSdr = s.value(QStringLiteral("TciEmulateSunSDR2Pro"),
                                       QStringLiteral("False")).toString()
                               == QStringLiteral("True");
    // NereusSDR divergence: HardwareSpecific.Model has no NereusSDR equivalent;
    // hardcode "NereusSDR" until Phase 4 Task 4.2 wires RadioModel state.
    const QString deviceName = emulateSunSdr
        ? QStringLiteral("SunSDR2PRO")
        : QStringLiteral("NereusSDR");
    lines << QStringLiteral("device:%1;").arg(deviceName);

    // From Thetis TCIServer.cs:2529 [v2.10.3.13]
    lines << QStringLiteral("receive_only:false;");

    // From Thetis TCIServer.cs:2530 [v2.10.3.13] — locked at 2 per design doc §1.2;
    // Slice C/D are NereusSDR-internal and not exposed via TCI in Phase 3J-1.
    lines << QStringLiteral("trx_count:2;");

    // From Thetis TCIServer.cs:2531 [v2.10.3.13]
    lines << QStringLiteral("channels_count:2;");

    // From Thetis TCIServer.cs:2533 [v2.10.3.13] — sendVFOLimits(0, MaxFreq*1e6).
    // Phase 4 Task 4.1 hardcodes 64 MHz; Task 4.2 wires RadioModel state.
    lines << QStringLiteral("vfo_limits:0,64000000;");

    // From Thetis TCIServer.cs:2535-2536 [v2.10.3.13] — sendIFLimits(-halfSample, halfSample).
    // halfSample = SampleRateRX1 / 2. Phase 4 Task 4.1 hardcodes 96000 (192 kHz / 2);
    // Task 4.2 wires RadioModel state.
    lines << QStringLiteral("if_limits:-96000,96000;");

    // From Thetis TCIServer.cs:2538-2544 [v2.10.3.13]
    // MW0LGE_22b modulations are upper in sun, so replicate
    const bool cwluBecomesCw = s.value(QStringLiteral("TciCwluBecomesCw"),
                                       QStringLiteral("False")).toString()
                               == QStringLiteral("True");
    const QString cwSuffix = cwluBecomesCw
        ? QStringLiteral("cwl,cwu,cw")
        : QStringLiteral("cwl,cwu");
    const QString modList = QStringLiteral("am,sam,dsb,lsb,usb,nfm,fm,digl,digu,%1")
                                .arg(cwSuffix).toUpper();
    lines << QStringLiteral("modulations_list:%1;").arg(modList);

    // From Thetis TCIServer.cs:2546 [v2.10.3.13] — sendInitialRadioState body.
    // Phase 4 Task 4.2 fills the body (~75-87 wire lines per Sweep D).
    lines.append(buildInitialRadioStateLines());

    // From Thetis TCIServer.cs:2548 [v2.10.3.13]
    lines << QStringLiteral("ready;");

    return lines;
}

// Phase 4 Task 4.2 implements the body. Returns empty list for now so
// buildInitBurst can call this without conditional logic.
QStringList TciProtocol::buildInitialRadioStateLines() const
{
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
