// no-port-check: NereusSDR-original TciProtocol skeleton — parser ported from
// Thetis TCIServer.cs:4900-4924 [v2.10.3.13] (split on first ':', case-insensitive
// command lookup), dispatch shape from TCIServer.cs:4924-5197 [v2.10.3.13] (two
// switches: set if parts.Length==2, query if parts.Length==1). Empty handler
// stubs land Phase 5+. AetherSDR seam pattern reference: TciProtocol.{h,cpp} [@0cd4559].

// src/core/TciProtocol.h  (NereusSDR)
// NereusSDR-original — TCI command protocol handler.
//
// Parser ported from Thetis TCIServer.cs:4900-4924 [v2.10.3.13].
// Two-switch dispatch shape from Thetis TCIServer.cs:4924-5197 [v2.10.3.13].
// AetherSDR seam pattern reference: TciProtocol.{h,cpp} [@0cd4559].
//
// This file REPLACES the Phase 1 stub (commit 77d27b3).
//
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 3.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace NereusSDR {

// ─────────────────────────────────────────────────────────────────────────
// AppSettings keys introduced by Phase 3J-1 TCI Server.
// All persisted to ~/.config/NereusSDR/NereusSDR.settings via AppSettings
// (NOT QSettings; per project convention). PascalCase key names.
//
// | Key                                  | Type   | Default | Notes
// | TciServerEnabled                     | bool   | False   | Server on/off
// | TciServerPort                        | int    | 50001   | Bind port (addr always 127.0.0.1)
// | TciSendInitialFrequencyStateOnConnect| bool   | True    | VFO/IF/DDS in init burst
// | TciRateLimitMsgsPerSec               | int    | 60      | Per-client message rate cap
// | TciAudioStreamSamples                | int    | 2048    | Audio block size [100..2048]
// | TciTxChannel                         | string | "Both"  | TX audio channel: Left/Right/Both
// | TciRxSensorIntervalMs                | int    | 200     | RX sensor push [30..1000]
// | TciTxSensorIntervalMs                | int    | 200     | TX sensor push [30..1000]
// | TciEmulateExpertSDR3Protocol         | bool   | False   | Compat flag
// | TciEmulateSunSDR2Pro                 | bool   | False   | Compat flag
// | TciCwluBecomesCw                     | bool   | False   | Compat flag
// | TciCwBecomesCwuAbove10mhz            | bool   | False   | Compat flag (W2PA #559)
// | TciIqSwap                            | bool   | True    | Compat flag
// | TciAlwaysStreamIq                    | bool   | False   | Compat flag
// | TciForgetRx2VfoBOnDisconnect         | bool   | False   | VFO quirk
// | TciUseRx1VfoaForRx2Vfoa              | bool   | False   | VFO quirk
// | TciCopyRx2VfobToVfoa                 | bool   | False   | VFO quirk
//
// Phases 5+ wire these into compat-flag handling. Phase 20 surfaces them in
// Setup → Network → TCI Server. See design doc Section 10.
// ─────────────────────────────────────────────────────────────────────────

class TciProtocol : public QObject {
    Q_OBJECT
public:
    explicit TciProtocol(QObject* radio = nullptr, QObject* parent = nullptr);
    ~TciProtocol() override = default;

    // Dispatch a single TCI command line. Returns the synchronous response
    // (empty for commands with no reply, or for the silent-error invariant
    // — unknown commands produce zero outbound traffic per design doc §4.1).
    QString handleCommand(const QString& command);

    // Notification queue — drained by TciServer after each handleCommand.
    bool hasPendingNotification() const;
    QString takePendingNotification();

    // Build the post-connect init burst. Stub returns empty list in Phase 3;
    // Phase 4 Task 4.1 replaces with the 8-line wrapper from
    // Thetis TCIServer.cs:2512-2552 [v2.10.3.13].
    QStringList buildInitBurst() const;

    // Slice ↔ trx mapping (NereusSDR architectural divergence per design doc §1.2):
    //   Slice A | trx:0,    Slice B | trx:1,    Slice C | trx:2,    Slice D | trx:3
    // Identity mapping in Phase 3; same mapping persists through subsequent phases.
    static int sliceToTrx(int slice);
    static int trxToSlice(int trx);

    // Phase 3 test-only instrumentation — incremented in handleSetCommand /
    // handleQueryCommand stubs so the dispatch-seam test can verify routing.
    // Phase 5+ may keep or remove these counters.
    int setDispatchCount() const { return m_setDispatchCount; }
    int queryDispatchCount() const { return m_queryDispatchCount; }
    void resetDispatchCounters();

private:
    // From Thetis TCIServer.cs:4924-5128 [v2.10.3.13] — 60-case set-command switch.
    // Phase 5+ adds individual cases via the matrix runner.
    QString handleSetCommand(const QString& name, const QStringList& args);

    // From Thetis TCIServer.cs:5134-5197 [v2.10.3.13] — 21-case query-command switch.
    // Phase 5+ adds individual cases via the matrix runner.
    QString handleQueryCommand(const QString& name);

    QObject* m_radio{nullptr};
    QStringList m_pendingNotifications;
    int m_setDispatchCount{0};
    int m_queryDispatchCount{0};
};

} // namespace NereusSDR
