// no-port-check: NereusSDR-original unit-test file.
// =================================================================
// tests/tst_tx_filter_offset_to_wdsp.cpp  (NereusSDR)
// =================================================================
//
// TDD for Plan 4 Task 6 (D8) — TX filter debounce + per-mode IQ-space mapping.
//
// Verifies:
//   1. debounceFiresOnceAfterRapidChanges — 5 rapid requestFilterChange calls
//      coalesce to exactly 1 txFilterApplied emit after the 50 ms debounce.
//   2. usbModeMapping — USB audio [100, 2900] → IQ [+100, +2900].
//   3. lsbModeMapping — LSB audio [100, 2900] → IQ [-2900, -100].
//   4. amModeMappingSymmetric — AM audio [100, 2900] → IQ [-2900, +2900].
//   5. digiUMappingMatchesUsb — DIGU audio [1000, 2000] → IQ [+1000, +2000].
//   6. digiLMappingMatchesLsb — DIGL audio [1000, 2000] → IQ [-2000, -1000].
//   7. fmModeSymmetric — FM audio [100, 3000] → IQ [-3000, +3000].
//   8. dsbModeSymmetric — DSB audio [100, 2900] → IQ [-2900, +2900].
//   9. cwlMappingMatchesLsb — CWL audio [100, 2900] → IQ [-2900, -100].
//
// Per-mode IQ-space mapping source (NereusSDR-original glue, same mapping as
// TxChannel::setTuneTone() TUN bandpass at TxChannel.cpp:505-528):
//   deskhpsdr/src/transmitter.c:2136-2186 [@120188f]
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-02 — Original implementation for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted authoring via Anthropic
//                Claude Code (Plan 4 D8).
// =================================================================

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>

#include "core/TxChannel.h"
#include "core/WdspTypes.h"

using namespace NereusSDR;

class TstTxFilterOffsetToWdsp : public QObject {
    Q_OBJECT

private:
    // Helper: call requestFilterChange and wait for the 50 ms debounce.
    // Returns the last txFilterApplied signal arguments, or empty list if
    // the signal did not fire within `waitMs`.
    static QList<QVariant> waitForFilterApplied(TxChannel& ch, int audioLow,
                                                int audioHigh, DSPMode mode,
                                                int waitMs = 120)
    {
        QSignalSpy spy(&ch, &TxChannel::txFilterApplied);
        ch.requestFilterChange(audioLow, audioHigh, mode);
        spy.wait(waitMs);
        if (spy.isEmpty()) {
            return {};
        }
        return spy.last();
    }

private slots:

    // ── 1. Debounce coalesces rapid calls ────────────────────────────────────
    // Five rapid requestFilterChange calls with different values must produce
    // exactly one txFilterApplied emission, and it must carry the LAST value.
    void debounceFiresOnceAfterRapidChanges()
    {
        TxChannel ch(1);
        QSignalSpy spy(&ch, &TxChannel::txFilterApplied);

        // Rapid-fire 5 calls — each restarts the 50 ms timer.
        ch.requestFilterChange(100, 2900, DSPMode::USB);
        ch.requestFilterChange(200, 2800, DSPMode::USB);
        ch.requestFilterChange(300, 2700, DSPMode::USB);
        ch.requestFilterChange(400, 2600, DSPMode::USB);
        ch.requestFilterChange(500, 2500, DSPMode::USB);  // last

        // Wait long enough for the debounce to fire.
        spy.wait(120);

        QCOMPARE(spy.count(), 1);

        // The emitted IQ values must correspond to the LAST call (500, 2500, USB).
        // USB family: iqLow = +audioLow, iqHigh = +audioHigh → +500, +2500.
        const int iqLow  = spy.first().at(0).toInt();
        const int iqHigh = spy.first().at(1).toInt();
        QCOMPARE(iqLow,  +500);
        QCOMPARE(iqHigh, +2500);
    }

    // ── 2. USB mode: IQ = [+low, +high] ─────────────────────────────────────
    void usbModeMapping()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 100, 2900, DSPMode::USB);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), +100);
        QCOMPARE(args.at(1).toInt(), +2900);
    }

    // ── 3. LSB mode: IQ = [-high, -low] ─────────────────────────────────────
    void lsbModeMapping()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 100, 2900, DSPMode::LSB);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), -2900);
        QCOMPARE(args.at(1).toInt(), -100);
    }

    // ── 4. AM mode: symmetric IQ = [-high, +high] ───────────────────────────
    void amModeMappingSymmetric()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 100, 2900, DSPMode::AM);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), -2900);
        QCOMPARE(args.at(1).toInt(), +2900);
    }

    // ── 5. DIGU mode: same as USB family ────────────────────────────────────
    void digiUMappingMatchesUsb()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 1000, 2000, DSPMode::DIGU);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), +1000);
        QCOMPARE(args.at(1).toInt(), +2000);
    }

    // ── 6. DIGL mode: same as LSB family ────────────────────────────────────
    void digiLMappingMatchesLsb()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 1000, 2000, DSPMode::DIGL);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), -2000);
        QCOMPARE(args.at(1).toInt(), -1000);
    }

    // ── 7. FM mode: symmetric (FM is in the symmetric family) ───────────────
    void fmModeSymmetric()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 100, 3000, DSPMode::FM);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), -3000);
        QCOMPARE(args.at(1).toInt(), +3000);
    }

    // ── 8. DSB mode: symmetric ───────────────────────────────────────────────
    void dsbModeSymmetric()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 100, 2900, DSPMode::DSB);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), -2900);
        QCOMPARE(args.at(1).toInt(), +2900);
    }

    // ── 9. CWL mode: treated as LSB family ──────────────────────────────────
    void cwlMappingMatchesLsb()
    {
        TxChannel ch(1);
        auto args = waitForFilterApplied(ch, 100, 2900, DSPMode::CWL);
        QVERIFY2(!args.isEmpty(), "txFilterApplied not emitted within timeout");
        QCOMPARE(args.at(0).toInt(), -2900);
        QCOMPARE(args.at(1).toInt(), -100);
    }
};

QTEST_MAIN(TstTxFilterOffsetToWdsp)
#include "tst_tx_filter_offset_to_wdsp.moc"
