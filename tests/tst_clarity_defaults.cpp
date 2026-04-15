// tst_clarity_defaults.cpp
//
// Phase 3G-9b — unit smoke for the Clarity Blue palette registration
// and narrow-band design invariants. Pure math over wfSchemeStops() —
// no QWidget event loop, no AppSettings interaction.

#include <QtTest/QtTest>

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TestClarityDefaults : public QObject {
    Q_OBJECT
private slots:

    void clarityBlue_enumOrdinalIsSeven()
    {
        // ClarityBlue is the 8th scheme (index 7). Adding it before Count
        // is the only acceptable ordering — any shift breaks persisted
        // combo ordinals. This assertion locks that in.
        QCOMPARE(static_cast<int>(WfColorScheme::ClarityBlue), 7);
        QCOMPARE(static_cast<int>(WfColorScheme::Count), 8);
    }

    void clarityBlue_gradientIsMonotonicAndSpansZeroToOne()
    {
        int count = 0;
        const WfGradientStop* stops = wfSchemeStops(WfColorScheme::ClarityBlue, count);
        QVERIFY(stops != nullptr);
        QCOMPARE(count, 6);

        // First stop is exactly at 0.0, last at 1.0 — covers the full
        // normalized range.
        QCOMPARE(stops[0].pos, 0.00f);
        QCOMPARE(stops[count - 1].pos, 1.00f);

        // Positions are strictly monotonically increasing.
        for (int i = 1; i < count; ++i) {
            QVERIFY2(stops[i].pos > stops[i - 1].pos,
                     "Clarity Blue gradient stops must be monotonic");
        }

        // Narrow-band property: at least 50% of the range must map to
        // the "dark" region (first two stops). This is the design
        // invariant — if someone changes the stop positions and the
        // dark region shrinks below 50%, the "AetherSDR readability"
        // look is lost.
        QVERIFY2(stops[1].pos >= 0.50f,
                 "Clarity Blue must reserve >= 50% of the range for the "
                 "dark noise-floor region (narrow-band palette invariant)");
    }

    void clarityBlue_topStopIsNearWhite()
    {
        int count = 0;
        const WfGradientStop* stops = wfSchemeStops(WfColorScheme::ClarityBlue, count);
        QVERIFY(count >= 1);
        const WfGradientStop& top = stops[count - 1];
        // Strong signals should pop as near-white — brightness >= 200
        // on every channel keeps the "peak" visible.
        QVERIFY2(top.r >= 200 && top.g >= 200 && top.b >= 200,
                 "Clarity Blue top stop must be near-white so strong "
                 "signals pop out");
    }
};

QTEST_MAIN(TestClarityDefaults)
#include "tst_clarity_defaults.moc"
