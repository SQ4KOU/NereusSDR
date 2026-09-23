// =================================================================
// tests/tst_display_load_governor.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. R-R3-08, R-R3-37, R-R3-40: the Core
// lowers the display budget when its computer is busy, never below the
// floor, and restores it one step at a time. Pure: an injected clock and
// injected readings, no radio, no WDSP, no host files.
// =================================================================

#include <QtTest/QtTest>

#include <algorithm>
#include <functional>

#include "core/session/media/DisplayLoadGovernor.h"

using namespace NereusSDR;

namespace {

DisplayBudgetCharge panCharge(int pixels, int fps, bool wide = false)
{
    return spectrumDisplayCost(pixels, fps, wide)->charge;
}

// Four pans at 1024 px and 30 fps: what the app has accepted.
DisplayBudgetCharge fourPans()
{
    const DisplayBudgetCharge pan = panCharge(1024, 30);
    return *sumDisplayCharges({pan, pan, pan, pan});
}

DisplayLoadReading loadReading(qint64 nowMs, double load,
                               DisplayBudgetCharge accepted = fourPans())
{
    DisplayLoadReading reading;
    reading.nowMs = nowMs;
    reading.highestReceiverLoad = load;
    reading.acceptedCharge = accepted;
    return reading;
}

DisplayLoadReading cpuReading(qint64 nowMs, double cpu,
                              DisplayBudgetCharge accepted = fourPans())
{
    DisplayLoadReading reading;
    reading.nowMs = nowMs;
    reading.systemCpuPercent = cpu;
    reading.acceptedCharge = accepted;
    return reading;
}

// One step down from `base`: half of it, never below the floor. The floor
// reserves PureSignal's display bytes, which are larger than four ordinary
// pans' bytes, so for these pans a step lowers spectrum samples while bytes
// stay at the floor.
DisplayBudgetLimits stepFrom(const DisplayBudgetCharge& base, quint32 generation)
{
    const DisplayBudgetCharge floor = DisplayLoadGovernor::floorCharge();
    return {std::max(floor.applicationBytesPerSecond, base.applicationBytesPerSecond / 2),
            std::max(floor.spectrumSampleUnitsPerSecond, base.spectrumSampleUnitsPerSecond / 2),
            generation};
}

// Feeds one reading every 500 ms (the load sampler's period) from `fromMs`
// to `toMs` inclusive; returns every decision made.
QList<DisplayLoadDecision> feed(DisplayLoadGovernor& governor, qint64 fromMs, qint64 toMs,
                                const std::function<DisplayLoadReading(qint64)>& make)
{
    QList<DisplayLoadDecision> decisions;
    for (qint64 t = fromMs; t <= toMs; t += 500) {
        if (const auto decision = governor.update(make(t))) {
            decisions.append(*decision);
        }
    }
    return decisions;
}

} // namespace

class TstDisplayLoadGovernor : public QObject {
    Q_OBJECT

private slots:
    void thresholdsSitBelowTheNoiseReductionStepBack()
    {
        // The NNR step-back acts at a receiver load of 0.90 held for 2 s;
        // spectrum must yield first.
        QVERIFY(DisplayLoadGovernor::kBusyReceiverLoad < 0.90);
        QVERIFY(DisplayLoadGovernor::kBusyHoldMs <= 2'000);
        QVERIFY(DisplayLoadGovernor::kCalmReceiverLoad < DisplayLoadGovernor::kBusyReceiverLoad);
        QVERIFY(DisplayLoadGovernor::kCalmSystemCpuPercent
                < DisplayLoadGovernor::kBusySystemCpuPercent);
    }

    void ceilingAndFloorAreTheDocumentedCharges()
    {
        const DisplayBudgetLimits ceiling = DisplayLoadGovernor::computedCeiling();
        QVERIFY(ceiling.isValid());
        QCOMPARE(ceiling.generation, quint32{1});
        const DisplayBudgetCharge widest = panCharge(DisplayCodecEncoder::kMaxSamplesPerPlane,
                                                     60, true);
        QCOMPARE(ceiling.applicationBytesPerSecond,
                 8 * widest.applicationBytesPerSecond
                     + ps3DisplayCharge().applicationBytesPerSecond);
        QCOMPARE(ceiling.spectrumSampleUnitsPerSecond, 8 * widest.spectrumSampleUnitsPerSecond);

        const DisplayBudgetCharge floor = DisplayLoadGovernor::floorCharge();
        const DisplayBudgetCharge onePan = panCharge(256, 10, true);
        QCOMPARE(floor.applicationBytesPerSecond,
                 onePan.applicationBytesPerSecond + ps3DisplayCharge().applicationBytesPerSecond);
        QCOMPARE(floor.spectrumSampleUnitsPerSecond, onePan.spectrumSampleUnitsPerSecond);
        QVERIFY(displayChargeFits(ceiling, floor));
    }

    void noMeasurementChangesNothing()
    {
        DisplayLoadGovernor governor(DisplayLoadGovernor::computedCeiling());
        // macOS with no receivers, or every receiver idle: nothing measured.
        const auto decisions = feed(governor, 0, 60'000, [](qint64 t) {
            DisplayLoadReading reading;
            reading.nowMs = t;
            reading.acceptedCharge = fourPans();
            return reading;
        });
        QVERIFY(decisions.isEmpty());
        QCOMPARE(governor.steps(), 0);
        QCOMPARE(governor.reason(), DisplayBudgetReason::None);

        // A gap without measurements restarts the busy hold.
        QVERIFY(feed(governor, 100'000, 101'500,
                     [](qint64 t) { return loadReading(t, 0.9); }).isEmpty());
        DisplayLoadReading gap;
        gap.nowMs = 102'000;
        gap.acceptedCharge = fourPans();
        QVERIFY(!governor.update(gap));
        QVERIFY(feed(governor, 102'500, 104'000,
                     [](qint64 t) { return loadReading(t, 0.9); }).isEmpty());
        QCOMPARE(governor.steps(), 0);
    }

    void busyForTwoSecondsStepsDownTheAcceptedCharge()
    {
        const DisplayBudgetLimits ceiling = DisplayLoadGovernor::computedCeiling();
        DisplayLoadGovernor governor(ceiling);
        QVERIFY(feed(governor, 0, 1'500,
                     [](qint64 t) { return loadReading(t, 0.75); }).isEmpty());
        const auto decision = governor.update(loadReading(2'000, 0.75));
        QVERIFY(decision.has_value());
        QCOMPARE(decision->reason, DisplayBudgetReason::CoreBusy);
        QCOMPARE(decision->limits.generation, quint32{2});
        const DisplayBudgetCharge accepted = fourPans();
        const DisplayBudgetLimits firstStep = stepFrom(accepted, 2);
        QCOMPARE(decision->limits, firstStep);
        QVERIFY(decision->limits.spectrumSampleUnitsPerSecond
                < accepted.spectrumSampleUnitsPerSecond);
        QCOMPARE(governor.limits(), decision->limits);
        QCOMPARE(governor.steps(), 1);

        // The next step needs a full hold of its own.
        QVERIFY(feed(governor, 2'500, 3'500,
                     [](qint64 t) { return loadReading(t, 0.8); }).isEmpty());
        const auto second = governor.update(loadReading(4'000, 0.8));
        QVERIFY(second.has_value());
        QCOMPARE(second->limits,
                 stepFrom({firstStep.applicationBytesPerSecond,
                           firstStep.spectrumSampleUnitsPerSecond, 0}, 3));
    }

    void systemCpuAloneStepsDown()
    {
        DisplayLoadGovernor governor(DisplayLoadGovernor::computedCeiling());
        QVERIFY(feed(governor, 0, 10'000,
                     [](qint64 t) { return cpuReading(t, 84.9); }).isEmpty());
        const auto decisions = feed(governor, 20'000, 22'000,
                                    [](qint64 t) { return cpuReading(t, 85.0); });
        QCOMPARE(decisions.size(), 1);
        QCOMPARE(decisions.first().reason, DisplayBudgetReason::CoreBusy);
    }

    void betweenTheThresholdsHolds()
    {
        DisplayLoadGovernor governor(DisplayLoadGovernor::computedCeiling());
        QCOMPARE(feed(governor, 0, 2'000,
                      [](qint64 t) { return loadReading(t, 0.8); }).size(), 1);
        const DisplayBudgetLimits lowered = governor.limits();
        // 0.70 is neither busy nor calm: nothing moves, however long.
        QVERIFY(feed(governor, 2'500, 60'000,
                     [](qint64 t) { return loadReading(t, 0.70); }).isEmpty());
        QCOMPARE(governor.limits(), lowered);
        QCOMPARE(governor.reason(), DisplayBudgetReason::CoreBusy);
    }

    void neverBelowTheFloor()
    {
        const DisplayBudgetLimits ceiling = DisplayLoadGovernor::computedCeiling();
        DisplayLoadGovernor governor(ceiling);
        const auto decisions = feed(governor, 0, 120'000,
                                    [](qint64 t) { return loadReading(t, 1.2); });
        QVERIFY(!decisions.isEmpty());
        const DisplayBudgetCharge floor = DisplayLoadGovernor::floorCharge();
        QCOMPARE(governor.limits().applicationBytesPerSecond, floor.applicationBytesPerSecond);
        QCOMPARE(governor.limits().spectrumSampleUnitsPerSecond,
                 floor.spectrumSampleUnitsPerSecond);
        for (const DisplayLoadDecision& decision : decisions) {
            QVERIFY(decision.limits.applicationBytesPerSecond >= floor.applicationBytesPerSecond);
            QVERIFY(decision.limits.spectrumSampleUnitsPerSecond
                    >= floor.spectrumSampleUnitsPerSecond);
        }
        // At the floor nothing more is published.
        QVERIFY(feed(governor, 120'500, 140'000,
                     [](qint64 t) { return loadReading(t, 1.2); }).isEmpty());

        // Nothing above the floor being sent: nothing to lower.
        DisplayLoadGovernor quiet(ceiling);
        QVERIFY(feed(quiet, 0, 20'000, [&floor](qint64 t) {
            return loadReading(t, 0.95, floor);
        }).isEmpty());
        QCOMPARE(quiet.limits(), ceiling);
    }

    void calmForTenSecondsRestoresOneStepAtATime()
    {
        const DisplayBudgetLimits ceiling = DisplayLoadGovernor::computedCeiling();
        DisplayLoadGovernor governor(ceiling);
        QCOMPARE(feed(governor, 0, 4'000,
                      [](qint64 t) { return loadReading(t, 0.9); }).size(), 2);
        QCOMPARE(governor.steps(), 2);
        const DisplayBudgetCharge accepted = fourPans();

        // Calm: 9.5 s is not enough.
        QVERIFY(feed(governor, 4'500, 14'000, [](qint64 t) {
            DisplayLoadReading reading = loadReading(t, 0.5);
            reading.systemCpuPercent = 60.0;
            return reading;
        }).isEmpty());
        const auto first = governor.update(loadReading(14'500, 0.5));
        QVERIFY(first.has_value());
        QCOMPARE(first->reason, DisplayBudgetReason::CoreBusy);
        QCOMPARE(first->limits, stepFrom(accepted, 4));

        // A late block restarts the calm hold.
        QVERIFY(feed(governor, 15'000, 20'000,
                     [](qint64 t) { return loadReading(t, 0.5); }).isEmpty());
        DisplayLoadReading late = loadReading(20'500, 0.5);
        late.lateBlocks = 1;
        QVERIFY(!governor.update(late));
        // So does a long input wait.
        DisplayLoadReading waiting = loadReading(21'000, 0.5);
        waiting.highestInputDelayMs = DisplayLoadGovernor::kCalmInputDelayMs;
        QVERIFY(!governor.update(waiting));
        QVERIFY(feed(governor, 21'500, 31'000,
                     [](qint64 t) { return loadReading(t, 0.5); }).isEmpty());
        const auto restored = governor.update(loadReading(31'500, 0.5));
        QVERIFY(restored.has_value());
        QCOMPARE(restored->reason, DisplayBudgetReason::None);
        QCOMPARE(restored->limits.applicationBytesPerSecond, ceiling.applicationBytesPerSecond);
        QCOMPARE(restored->limits.spectrumSampleUnitsPerSecond,
                 ceiling.spectrumSampleUnitsPerSecond);
        QCOMPARE(restored->limits.generation, quint32{5});
        QCOMPARE(governor.steps(), 0);

        // At the ceiling a calm Core publishes nothing.
        QVERIFY(feed(governor, 32'000, 80'000,
                     [](qint64 t) { return loadReading(t, 0.1); }).isEmpty());
    }

    void resetReturnsToTheCeilingOnce()
    {
        const DisplayBudgetLimits ceiling = DisplayLoadGovernor::computedCeiling();
        DisplayLoadGovernor governor(ceiling);
        QVERIFY(!governor.reset());
        QCOMPARE(feed(governor, 0, 2'000,
                      [](qint64 t) { return loadReading(t, 0.9); }).size(), 1);
        const auto reset = governor.reset();
        QVERIFY(reset.has_value());
        QCOMPARE(reset->reason, DisplayBudgetReason::None);
        QCOMPARE(reset->limits.applicationBytesPerSecond, ceiling.applicationBytesPerSecond);
        QCOMPARE(reset->limits.generation, quint32{3});
        QVERIFY(!governor.reset());
    }

    void aConfiguredCeilingBelowTheFloorIsNeverRaised()
    {
        const DisplayBudgetCharge small = panCharge(128, 5);
        const DisplayBudgetLimits ceiling{small.applicationBytesPerSecond,
                                          small.spectrumSampleUnitsPerSecond, 7};
        DisplayLoadGovernor governor(ceiling);
        QVERIFY(feed(governor, 0, 20'000, [&small](qint64 t) {
            return loadReading(t, 0.95, small);
        }).isEmpty());
        QCOMPARE(governor.limits(), ceiling);
    }
};

QTEST_MAIN(TstDisplayLoadGovernor)
#include "tst_display_load_governor.moc"
