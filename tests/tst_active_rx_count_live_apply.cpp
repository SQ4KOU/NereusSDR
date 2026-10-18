// =================================================================
// tests/tst_active_rx_count_live_apply.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure; the Thetis filename
// reference below is a comment-only provenance note, not a code port.
//
// NereusSDR-original infrastructure — no Thetis source ported here.
// RadioModel::setActiveRxCountLive() is a NereusSDR coordinator; the
// P1 restart it delegates to mirrors the restartStreamWithRate() pattern
// (Task 1.6) from P1RadioConnection (itself ported from
// ChannelMaster/networkproto1.c SendStopToMetis/SendStartToMetis [v2.10.3.13]).
//
// Test scope (Task 1.7):
//   Guard-path contracts (no live WDSP or connection required):
//     not_connected_returns_minus_one — disconnected → returns -1.
//     idempotent_same_count_returns_zero — same count as current → 0.
//     rebuild_completes_under_glitch_budget — elapsed < 200 ms (guard
//       path returns immediately; full timing verified on bench Task 1.8).
//     signal_not_emitted_when_guard_fires — activeRxCountChanged not emitted.
//     signal_not_emitted_on_same_count — activeRxCountChanged not emitted.
//
// All tests run without a live WDSP session or radio connection.
// =================================================================

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestActiveRxCountLiveApply : public QObject {
    Q_OBJECT

private slots:
    // ── Guard path: not connected → returns -1 ────────────────────────────────
    //
    // setActiveRxCountLive() returns -1 whenever the radio is not connected or
    // WDSP is not initialized.  An un-connected RadioModel satisfies both
    // conditions; this verifies the guard fires and no steps execute.
    void not_connected_returns_minus_one()
    {
        RadioModel radio;

        // No connection, no WDSP — guard returns -1.
        const qint64 result = radio.setActiveRxCountLive(2);
        QCOMPARE(result, qint64(-1));

        // Active count must remain at its initial disconnected value (0).
        QCOMPARE(radio.connectionActiveRxCount(), 0);
    }

    // ── Idempotent: same count as current → returns 0 ────────────────────────
    //
    // When the requested count equals m_connectionActiveRxCount the method
    // returns 0 immediately without touching any hardware or WDSP channel.
    //
    // In the unconnected state m_connectionActiveRxCount == 0, so passing 0
    // triggers the idempotent guard before the connection guard.
    void idempotent_same_count_returns_zero()
    {
        RadioModel radio;

        // 0 == m_connectionActiveRxCount{0}: idempotent path fires first.
        const qint64 result = radio.setActiveRxCountLive(0);
        QCOMPARE(result, qint64(0));
    }

    // ── Timing: coordinator must not stall the main thread ───────────────────
    //
    // In the no-WDSP path the guard returns immediately, so elapsed is
    // always well under budget.  This pins the regression: a blocking
    // cross-thread call must never appear before the guard check.
    //
    // The glitch budget (200 ms) mirrors the one from Task 1.6.
    // The integration bench (Task 1.8) verifies the full path on a real
    // radio also satisfies this bound.
    void rebuild_completes_under_glitch_budget()
    {
        RadioModel radio;

        QElapsedTimer t;
        t.start();
        radio.setActiveRxCountLive(2);
        const qint64 elapsed = t.elapsed();

        QVERIFY2(elapsed < 200,
                 qPrintable(QStringLiteral("setActiveRxCountLive took %1 ms (budget: 200 ms)")
                                .arg(elapsed)));
    }

    // ── activeRxCountChanged not emitted from guard path ─────────────────────
    //
    // When the guard fires (not connected), the signal must NOT be emitted —
    // state has not changed.
    void signal_not_emitted_when_guard_fires()
    {
        RadioModel radio;

        QSignalSpy spy(&radio, &RadioModel::activeRxCountChanged);
        radio.setActiveRxCountLive(2);

        // Guard returned -1; state unchanged; no signal.
        QCOMPARE(spy.count(), 0);
    }

    // ── activeRxCountChanged not emitted on idempotent same-count ────────────
    //
    // When the idempotent guard fires (same count as current), the signal
    // must NOT be emitted — no state transition occurred.
    void signal_not_emitted_on_same_count()
    {
        RadioModel radio;

        QSignalSpy spy(&radio, &RadioModel::activeRxCountChanged);
        radio.setActiveRxCountLive(0);  // same as m_connectionActiveRxCount{0}

        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestActiveRxCountLiveApply)
#include "tst_active_rx_count_live_apply.moc"
