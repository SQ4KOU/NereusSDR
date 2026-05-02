// =================================================================
// tests/tst_multimeter_timing.cpp  (NereusSDR)
// =================================================================
//
// Task 3.1 — MultimeterPage + configurable MeterPoller tests.
//
// Verifies the new setIntervalMs / intervalMs and setAverageWindow /
// averageWindow round-trip accessors added to MeterPoller for the
// Display → Multimeter setup page.
//
// No Thetis port — tests NereusSDR-native accessors.
//
// Uses QTEST_APPLESS_MAIN (no QApplication, WDSP-free, pure data logic).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

#include <QTest>

#include "gui/meters/MeterPoller.h"

using namespace NereusSDR;

class TestMultimeterTiming : public QObject {
    Q_OBJECT
private slots:
    // ── setIntervalMs / intervalMs ────────────────────────────────────────────

    void poll_interval_round_trips()
    {
        MeterPoller poller;
        poller.setIntervalMs(50);
        QCOMPARE(poller.intervalMs(), 50);
    }

    void poll_interval_default_is_100ms()
    {
        MeterPoller poller;
        // Default 100ms from Thetis MeterManager.cs UpdateInterval [v2.10.3.13]
        QCOMPARE(poller.intervalMs(), 100);
    }

    void interval_clamped_below_minimum()
    {
        MeterPoller poller;
        poller.setIntervalMs(0);
        // Clamp floor is 10 ms (matches MultimeterPage spinbox minimum)
        QVERIFY(poller.intervalMs() >= 10);
    }

    void interval_clamped_above_maximum()
    {
        MeterPoller poller;
        poller.setIntervalMs(99999);
        // Clamp ceiling is 2000 ms (matches MultimeterPage spinbox maximum)
        QVERIFY(poller.intervalMs() <= 2000);
    }

    void interval_boundary_10ms_accepted()
    {
        MeterPoller poller;
        poller.setIntervalMs(10);
        QCOMPARE(poller.intervalMs(), 10);
    }

    void interval_boundary_2000ms_accepted()
    {
        MeterPoller poller;
        poller.setIntervalMs(2000);
        QCOMPARE(poller.intervalMs(), 2000);
    }

    // ── setAverageWindow / averageWindow ──────────────────────────────────────

    void average_window_round_trips()
    {
        MeterPoller poller;
        poller.setAverageWindow(8);
        QCOMPARE(poller.averageWindow(), 8);
    }

    void average_window_default_is_1()
    {
        MeterPoller poller;
        // Default 1 (no averaging) from Thetis udDisplayMeterAvg minimum [v2.10.3.13]
        QCOMPARE(poller.averageWindow(), 1);
    }

    void average_window_clamped_below_minimum()
    {
        MeterPoller poller;
        poller.setAverageWindow(0);
        // Clamp floor is 1 (matches MultimeterPage spinbox minimum)
        QVERIFY(poller.averageWindow() >= 1);
    }

    void average_window_clamped_above_maximum()
    {
        MeterPoller poller;
        poller.setAverageWindow(999);
        // Clamp ceiling is 32 (matches MultimeterPage spinbox maximum)
        QVERIFY(poller.averageWindow() <= 32);
    }

    void average_window_boundary_1_accepted()
    {
        MeterPoller poller;
        poller.setAverageWindow(1);
        QCOMPARE(poller.averageWindow(), 1);
    }

    void average_window_boundary_32_accepted()
    {
        MeterPoller poller;
        poller.setAverageWindow(32);
        QCOMPARE(poller.averageWindow(), 32);
    }

    // ── Independence ──────────────────────────────────────────────────────────

    void interval_and_window_are_independent()
    {
        MeterPoller poller;
        poller.setIntervalMs(500);
        poller.setAverageWindow(16);
        QCOMPARE(poller.intervalMs(), 500);
        QCOMPARE(poller.averageWindow(), 16);
    }
};

QTEST_APPLESS_MAIN(TestMultimeterTiming)
#include "tst_multimeter_timing.moc"
