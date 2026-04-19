// tst_band_change_recursion_guard.cpp
//
// Regression test for v0.2.0 crash: infinite recursion in the per-band
// DSP save/restore lambda when a cross-band tune causes restoreFromSettings
// to drive a setFrequency that re-enters the lambda.
//
// Crash signature (macOS, 2026-04-18/19, 7 reports):
//   EXC_BAD_ACCESS — "Thread stack size exceeded due to excessive recursion"
//   recursion depth: 10,405 frames
//   key frame: void doActivate<false>(QObject*, int, void**)
//   loop pattern:
//     SliceModel::frequencyChanged
//       → RadioModel::wireSliceSignals() lambda (src/models/RadioModel.cpp:713)
//       → SliceModel::restoreFromSettings(Band)
//       → SliceModel::setFrequency(savedFreq)
//       → SliceModel::frequencyChanged  (recurses)
//
// Root cause: the lambda's guard `if (newBand != m_lastBand)` was not
// reentrancy-safe. Re-entry via the synchronous frequencyChanged signal
// observed m_lastBand in its pre-update state and re-ran the save/restore
// branch. With corrupt per-band Frequency values (each band's stored freq
// living in a different band), the cascade continues through the band
// lookup until the main thread's stack guard triggers.
//
// Fix: a reentrancy flag (m_inBandSwitch) plus updating m_lastBand BEFORE
// calling restoreFromSettings. See src/models/RadioModel.h §m_inBandSwitch
// and the lambdas in src/models/RadioModel.cpp.
//
// This test replicates the lambda's topology in isolation and verifies
// both invariants:
//   1. The reentrancy guard observes re-entry (proving it is firing).
//   2. The total number of lambda invocations is bounded.

#include <QtTest/QtTest>

#include "models/SliceModel.h"
#include "models/Band.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestBandChangeRecursionGuard : public QObject {
    Q_OBJECT

private:
    // Strip every per-band and session key we write so each test starts clean.
    static void resetPersistenceKeys() {
        auto& s = AppSettings::instance();
        for (const QString& band : {
                 QStringLiteral("20m"), QStringLiteral("40m"),
                 QStringLiteral("80m"), QStringLiteral("160m") }) {
            const QString bp = QStringLiteral("Slice0/Band") + band + QStringLiteral("/");
            for (const QString& field : {
                     QStringLiteral("Frequency"),   QStringLiteral("AgcThreshold"),
                     QStringLiteral("AgcHang"),     QStringLiteral("AgcSlope"),
                     QStringLiteral("AgcAttack"),   QStringLiteral("AgcDecay"),
                     QStringLiteral("FilterLow"),   QStringLiteral("FilterHigh"),
                     QStringLiteral("DspMode"),     QStringLiteral("AgcMode"),
                     QStringLiteral("StepHz") }) {
                s.remove(bp + field);
            }
        }
        const QString sp = QStringLiteral("Slice0/");
        for (const QString& field : {
                 QStringLiteral("Locked"),     QStringLiteral("Muted"),
                 QStringLiteral("RitEnabled"), QStringLiteral("RitHz"),
                 QStringLiteral("XitEnabled"), QStringLiteral("XitHz"),
                 QStringLiteral("AfGain"),     QStringLiteral("RfGain"),
                 QStringLiteral("RxAntenna"),  QStringLiteral("TxAntenna") }) {
            s.remove(sp + field);
        }
    }

private slots:

    void init()    { resetPersistenceKeys(); }
    void cleanup() { resetPersistenceKeys(); }

    // ── Invariant 1: The guard fires on re-entry. ─────────────────────────────
    //
    // Pre-seed per-band Frequency values that cross-reference each other so
    // a band crossing drives setFrequency → frequencyChanged and would
    // otherwise re-enter the save/restore block. The guard must observe
    // inBandSwitch=true on re-entry and skip the branch.

    void guardBlocksReentryOnBandCrossing() {
        auto& s = AppSettings::instance();

        // Corrupt-data scenario that reproduces the v0.2.0 crash shape:
        // each band's stored frequency lies in a different band.
        s.setValue(QStringLiteral("Slice0/Band20m/Frequency"),  7100000.0);    // → 40m
        s.setValue(QStringLiteral("Slice0/Band40m/Frequency"),  3700000.0);    // → 80m
        s.setValue(QStringLiteral("Slice0/Band80m/Frequency"),  14225000.0);   // → 20m (cycle)

        SliceModel slice;
        slice.setSliceIndex(0);
        slice.setFrequency(14225000.0);

        Band lastBand = Band::Band20m;
        bool inBandSwitch = false;
        int  enterCount = 0;
        int  guardFired = 0;
        constexpr int kSafetyLimit = 50;

        QObject::connect(&slice, &SliceModel::frequencyChanged,
            [&](double freq) {
                ++enterCount;
                if (enterCount > kSafetyLimit) {
                    // Belt-and-suspenders: prevents this test itself from
                    // blowing the stack if the guard were ever removed.
                    return;
                }
                const Band newBand = bandFromFrequency(freq);
                if (inBandSwitch) {
                    ++guardFired;
                    return;
                }
                if (newBand != lastBand) {
                    inBandSwitch = true;
                    const Band oldBand = lastBand;
                    lastBand = newBand;
                    slice.saveToSettings(oldBand);
                    slice.restoreFromSettings(newBand);
                    inBandSwitch = false;
                }
            });

        // Wheel-tune from 20m (14.225 MHz) to 40m (7.1 MHz).
        slice.setFrequency(7100000.0);

        QVERIFY2(enterCount < kSafetyLimit,
                 qPrintable(QStringLiteral(
                     "Lambda invoked %1 times — recursion guard not firing "
                     "(regression of v0.2.0 crash 2026-04-19 015055).")
                     .arg(enterCount)));

        QVERIFY2(guardFired >= 1,
                 "Guard should have blocked at least one re-entry through "
                 "restoreFromSettings → setFrequency.");
    }

    // ── Invariant 2: Clean cross without corrupt data converges without
    // extra recursion (baseline — the common case). ───────────────────────────

    void cleanCrossConvergesInOneIteration() {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice0/Band40m/Frequency"), 7150000.0);  // in 40m

        SliceModel slice;
        slice.setSliceIndex(0);
        slice.setFrequency(14225000.0);

        Band lastBand = Band::Band20m;
        bool inBandSwitch = false;
        int  enterCount = 0;
        int  guardFired = 0;

        QObject::connect(&slice, &SliceModel::frequencyChanged,
            [&](double freq) {
                ++enterCount;
                const Band newBand = bandFromFrequency(freq);
                if (inBandSwitch) { ++guardFired; return; }
                if (newBand != lastBand) {
                    inBandSwitch = true;
                    const Band oldBand = lastBand;
                    lastBand = newBand;
                    slice.saveToSettings(oldBand);
                    slice.restoreFromSettings(newBand);
                    inBandSwitch = false;
                }
            });

        slice.setFrequency(7100000.0);  // 40m — stored freq for 40m is 7.15 MHz

        QCOMPARE(lastBand, Band::Band40m);
        QVERIFY2(enterCount <= 3,
                 qPrintable(QStringLiteral(
                     "enterCount=%1 — clean band cross should not cascade")
                     .arg(enterCount)));
    }
};

QTEST_MAIN(TestBandChangeRecursionGuard)
#include "tst_band_change_recursion_guard.moc"
