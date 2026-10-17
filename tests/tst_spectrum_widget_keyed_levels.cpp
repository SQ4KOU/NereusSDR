// =================================================================
// tests/tst_spectrum_widget_keyed_levels.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test. The Thetis behaviour it checks
// is cited in src/gui/SpectrumWidget.cpp (setDisplayKeyed) and
// src/core/spectrum/DisplayFollowers.h.
//
// Display pill plan Task 13: what one pan does on a key edge
// (SpectrumWidget::setDisplayKeyed).
//   - The hold length comes from the FFT the pan shows; one second with
//     none.
//   - Keyed, a pan with waterfall AGC keeps its levels against rows 40 dB
//     hotter and keeps drawing rows.
//   - The un-key puts back the level the pan had on its band, or starts at
//     the restart floor on a band with none, and holds for the given length.
//   - A re-key inside the hold ends it; the next un-key starts a new one.
//   - Either edge starts the noise-floor fast attack with the hold length
//     as its minimum; the other triggers keep one second.
//   - Clarity stays on the pan.
//   - MainWindow purges the FFT input on the display's edge, and no longer
//     starts the fast attack from the transmit model.
//
// Modification history (NereusSDR):
//   2026-10-10 : Created for display pill plan Task 13 by J.J. Boyd
//                 (KG4VCF). AI-assisted implementation via Anthropic Claude
//                 Code.
// =================================================================

#include <QtTest/QtTest>

#include <QFile>
#include <QRegularExpression>

#include <optional>
#include <utility>

#define private public
#include "gui/SpectrumWidget.h"
#undef private

#include "core/AppSettings.h"
#include "core/spectrum/MoxDisplayHold.h"

using namespace NereusSDR;

namespace {

constexpr int kBand20m = 5;
constexpr int kBand40m = 3;

QVector<float> flatRow(float dbm) { return QVector<float>(512, dbm); }

void prime(SpectrumWidget& w, float dbm)
{
    for (int row = 0; row < 200; ++row) {
        w.composeWaterfallActiveThresholds(flatRow(dbm));
    }
}

QString readSource(const QString& relative)
{
    QFile file(QStringLiteral(NEREUS_SOURCE_DIR "/") + relative);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class TestSpectrumWidgetKeyedLevels : public QObject
{
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void holdLengthComesFromTheLocalFftShape()
    {
        SpectrumWidget w;
        // No engine of its own: the minimum.
        QCOMPARE(w.moxHoldLengthMs(), kMoxHoldMinMs);
        QCOMPARE(w.moxHoldLengthMs(), qint64(1000));
        // Twice the fill time: 262144 samples at 48 kHz fill in 5461 ms.
        w.setLocalFftShape(262144, 1, 48000.0);
        QCOMPARE(w.moxHoldLengthMs(), qint64(10922));
        // Decimation lengthens the fill: 65536 x 2 at 48 kHz is 2730 ms.
        w.setLocalFftShape(65536, 2, 48000.0);
        QCOMPARE(w.moxHoldLengthMs(), qint64(5460));
        // A short fill still holds a second.
        w.setLocalFftShape(4096, 1, 192000.0);
        QCOMPARE(w.moxHoldLengthMs(), qint64(1000));
        // The engine gone again.
        w.setLocalFftShape(0, 0, 0.0);
        QCOMPARE(w.moxHoldLengthMs(), qint64(1000));
    }

    void keyedPanKeepsItsLevelsAgainstHotRowsAndKeepsDrawing()
    {
        SpectrumWidget w;
        w.resize(800, 400);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        qint64 now = 1'000'000;
        w.setDisplayClockForTest([&now]() { return now; });
        w.setWfAgcEnabled(true);
        QVERIFY(!w.clarityActive());

        for (int row = 0; row < 200; ++row) {
            w.pushWaterfallRowForTest(flatRow(-110.0f));
        }
        QVERIFY(w.waterfallAgcRunLevel().has_value());
        const float level = *w.waterfallAgcRunLevel();
        const std::pair<float, float> levels = w.effectiveWaterfallLevelsForTest();

        w.setDisplayKeyed(true, 3000, kBand20m);
        QVERIFY(w.displayKeyed());
        QVERIFY(!w.isMoxOverlayActive());
        // The receiver hears the transmitter: every row 40 dB hotter.
        int drawn = 0;
        for (int row = 0; row < 60; ++row) {
            const int before = w.liveWaterfallWriteRowForTest();
            w.pushWaterfallRowForTest(flatRow(-70.0f));
            if (w.liveWaterfallWriteRowForTest() != before) { ++drawn; }
            QCOMPARE(w.effectiveWaterfallLevelsForTest(), levels);
        }
        QCOMPARE(drawn, 60);
        QCOMPARE(*w.waterfallAgcRunLevel(), level);
    }

    void unkeyRestoresTheLevelAndHoldsForTheGivenLength()
    {
        SpectrumWidget w;
        qint64 now = 1'000'000;
        w.setDisplayClockForTest([&now]() { return now; });
        prime(w, -110.0f);
        const float level = *w.waterfallAgcRunLevel();
        const float low = w.wfActiveLowThreshold();

        w.setDisplayKeyed(true, 3000, kBand20m);
        for (int row = 0; row < 50; ++row) {
            w.composeWaterfallActiveThresholds(flatRow(-70.0f));
        }
        now += 20'000;
        w.setDisplayKeyed(false, 3000, kBand20m);
        QVERIFY(!w.displayKeyed());
        QCOMPARE(*w.waterfallAgcRunLevel(), level);

        // The first receive row, and every row inside the hold, draws at
        // the level from before the key, however hot it still is.
        w.composeWaterfallActiveThresholds(flatRow(-70.0f));
        QCOMPARE(*w.waterfallAgcRunLevel(), level);
        QCOMPARE(w.wfActiveLowThreshold(), low);
        now += 2999;
        w.composeWaterfallActiveThresholds(flatRow(-70.0f));
        QCOMPARE(*w.waterfallAgcRunLevel(), level);
        QCOMPARE(w.wfActiveLowThreshold(), low);

        // The hold over, the follower takes readings again.
        now += 1;
        w.composeWaterfallActiveThresholds(flatRow(-70.0f));
        QVERIFY(*w.waterfallAgcRunLevel() > level);
    }

    void bandWithNoSavedLevelStartsAtTheRestartFloor()
    {
        SpectrumWidget w;
        qint64 now = 1'000'000;
        w.setDisplayClockForTest([&now]() { return now; });
        prime(w, -110.0f);
        const float level20 = *w.waterfallAgcRunLevel();

        // Keyed on 20 m, un-keyed on 40 m, which has no level saved.
        w.setDisplayKeyed(true, 1000, kBand20m);
        w.setDisplayKeyed(false, 1000, kBand40m);
        QCOMPARE(*w.waterfallAgcRunLevel(), -150.0f);
        QCOMPARE(*w.waterfallAgcRunLevel(), WaterfallLevelFollower::kAgcRestartFloorDbm);

        // 40 m finds its own level, and each band then gets its own back.
        now += 1000;
        prime(w, -95.0f);
        const float level40 = *w.waterfallAgcRunLevel();
        QVERIFY(std::abs(level40 - level20) > 5.0f);
        w.setDisplayKeyed(true, 1000, kBand40m);
        w.setDisplayKeyed(false, 1000, kBand20m);
        QCOMPARE(*w.waterfallAgcRunLevel(), level20);
        now += 1000;
        w.setDisplayKeyed(true, 1000, kBand20m);
        w.setDisplayKeyed(false, 1000, kBand40m);
        QCOMPARE(*w.waterfallAgcRunLevel(), level40);
    }

    void panThatNeverFoundALevelSavesNone()
    {
        SpectrumWidget w;
        QVERIFY(!w.waterfallAgcRunLevel().has_value());
        w.setDisplayKeyed(true, 1000, kBand20m);
        QVERIFY(w.m_agcRunLevelByBand.isEmpty());
        w.setDisplayKeyed(false, 1000, kBand20m);
        QCOMPARE(*w.waterfallAgcRunLevel(), -150.0f);
    }

    void rekeyInsideTheHoldEndsItAndTheNextUnkeyStartsANewOne()
    {
        SpectrumWidget w;
        qint64 now = 1'000'000;
        w.setDisplayClockForTest([&now]() { return now; });
        prime(w, -110.0f);
        const float level = *w.waterfallAgcRunLevel();

        w.setDisplayKeyed(true, 3000, kBand20m);
        w.setDisplayKeyed(false, 3000, kBand20m);
        QVERIFY(w.m_wfLevels.inHold(now + 2999));
        now += 1000;
        w.setDisplayKeyed(true, 3000, kBand20m);
        QVERIFY(!w.m_wfLevels.inHold(now));
        QCOMPARE(*w.waterfallAgcRunLevel(), level);
        now += 1000;
        w.setDisplayKeyed(false, 3000, kBand20m);
        QCOMPARE(*w.waterfallAgcRunLevel(), level);
        QVERIFY(w.m_wfLevels.inHold(now + 2999));
        QVERIFY(!w.m_wfLevels.inHold(now + 3000));
    }

    void eitherEdgeStartsTheFastAttackWithTheHoldAsItsMinimum()
    {
        {
            SpectrumWidget w;
            w.setDisplayKeyed(true, 5000, kBand20m);
            QVERIFY(w.noiseFloorFastAttack());
            QCOMPARE(w.m_noiseFloor.m_fastAttackMinMs, qint64(5000));
        }
        {
            SpectrumWidget w;
            qint64 now = 1'000'000;
            w.setDisplayClockForTest([&now]() { return now; });
            w.setDisplayKeyed(true, 1000, kBand20m);
            now += 60'000;
            w.setDisplayKeyed(false, 7000, kBand20m);
            QVERIFY(w.noiseFloorFastAttack());
            QCOMPARE(w.m_noiseFloor.m_fastAttackMinMs, qint64(7000));
            QCOMPARE(w.m_noiseFloor.m_lastFastAttackMs, now);
        }
        // A band change, a tune jump and an attenuator change keep a second.
        {
            SpectrumWidget w;
            w.setNoiseFloorFastAttack(true);
            QVERIFY(w.noiseFloorFastAttack());
            QCOMPARE(w.m_noiseFloor.m_fastAttackMinMs, NoiseFloorFollower::kFastAttackMinMs);
            QCOMPARE(NoiseFloorFollower::kFastAttackMinMs, qint64(1000));
        }
    }

    void repeatOfTheSameStateDoesNothing()
    {
        SpectrumWidget w;
        qint64 now = 1'000'000;
        w.setDisplayClockForTest([&now]() { return now; });
        const bool fast = w.noiseFloorFastAttack();
        // Not keyed, told it is not keyed.
        w.setDisplayKeyed(false, 3000, kBand20m);
        QVERIFY(!w.m_wfLevels.inHold(now));
        QVERIFY(!w.waterfallAgcRunLevel().has_value());
        QCOMPARE(w.noiseFloorFastAttack(), fast);

        prime(w, -110.0f);
        const float level = *w.waterfallAgcRunLevel();
        w.setDisplayKeyed(true, 3000, kBand20m);
        // Keyed, told again on another band: nothing is saved for it.
        w.setDisplayKeyed(true, 3000, kBand40m);
        QCOMPARE(w.m_agcRunLevelByBand.size(), 1);
        QCOMPARE(w.m_agcRunLevelByBand.value(kBand20m), level);
    }

    void clarityStaysOnThePanThroughAKey()
    {
        SpectrumWidget w;
        w.setClarityActive(true);
        w.setClarityWaterfallThresholds(-128.0f, -68.0f);
        const std::pair<float, float> levels = w.effectiveWaterfallLevelsForTest();
        w.setDisplayKeyed(true, 1000, kBand20m);
        QVERIFY(w.clarityActive());
        w.composeWaterfallActiveThresholds(flatRow(-70.0f));
        QCOMPARE(w.effectiveWaterfallLevelsForTest(), levels);
        w.setDisplayKeyed(false, 1000, kBand20m);
        QVERIFY(w.clarityActive());
        w.composeWaterfallActiveThresholds(flatRow(-70.0f));
        QCOMPARE(w.effectiveWaterfallLevelsForTest(), levels);
    }

    // Rulings wave R4: after a key or un-key edge the waterfall adds no rows
    // until the first spectrum from after the edge; with no edge the ticker
    // draws its cached row on every tick, as before.
    void theWaterfallAddsNoRowsAfterAnEdgeUntilAFreshSpectrum()
    {
        SpectrumWidget w;
        w.resize(800, 400);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.setWaterfallTickerPausedForTest(true);
        constexpr double kCentreHz = 14'200'000.0;
        constexpr double kSpanHz = 192'000.0;
        w.setFrequencyRange(kCentreHz, kSpanHz);
        w.setDdcCenterFrequency(kCentreHz);
        w.setSampleRate(kSpanHz);
        const QVector<float> bins(4096, 1e-12f);
        // Rows the ticker adds over `ticks` ticks.
        const auto rowsAdded = [&w](int ticks) {
            int added = 0;
            for (int i = 0; i < ticks; ++i) {
                const int before = w.liveWaterfallWriteRowForTest();
                w.tickWaterfallForTest();
                if (w.liveWaterfallWriteRowForTest() != before) { ++added; }
            }
            return added;
        };

        // No edge: nothing cached yet draws nothing; one spectrum is then
        // drawn on every tick, fresh or not.
        QCOMPARE(rowsAdded(3), 0);
        w.updateSpectrumLinear(0, bins, 2.0, -10.0);
        QCOMPARE(rowsAdded(5), 5);

        for (const bool keyed : {true, false}) {
            w.setDisplayKeyed(keyed, 3000, kBand20m);
            QCOMPARE(rowsAdded(20), 0);
            // The first spectrum after the edge adds one row a tick again.
            w.updateSpectrumLinear(0, bins, 2.0, -10.0);
            QCOMPARE(rowsAdded(1), 1);
            QCOMPARE(rowsAdded(4), 4);
            // A repeat of the same state is no edge.
            w.setDisplayKeyed(keyed, 3000, kBand20m);
            QCOMPARE(rowsAdded(3), 3);
        }
    }

    // Follow-up wave F6: a frame computed before the purge and delivered
    // after the edge does not end the hold, however many of them arrive;
    // the first frame computed after the purge does.
    void aFrameFromBeforeThePurgeDoesNotEndTheHold()
    {
        SpectrumWidget w;
        w.resize(800, 400);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.setWaterfallTickerPausedForTest(true);
        constexpr double kCentreHz = 14'200'000.0;
        constexpr double kSpanHz = 192'000.0;
        w.setFrequencyRange(kCentreHz, kSpanHz);
        w.setDdcCenterFrequency(kCentreHz);
        w.setSampleRate(kSpanHz);
        const QVector<float> bins(4096, 1e-12f);
        const auto rowsAdded = [&w](int ticks) {
            int added = 0;
            for (int i = 0; i < ticks; ++i) {
                const int before = w.liveWaterfallWriteRowForTest();
                w.tickWaterfallForTest();
                if (w.liveWaterfallWriteRowForTest() != before) { ++added; }
            }
            return added;
        };
        w.updateSpectrumLinear(0, bins, 2.0, -10.0);
        QCOMPARE(rowsAdded(2), 2);

        for (const bool keyed : {true, false}) {
            w.setDisplayKeyed(keyed, 3000, kBand20m);
            for (int i = 0; i < 3; ++i) {
                w.updateSpectrumLinear(0, bins, 2.0, -10.0, false);
                QCOMPARE(rowsAdded(4), 0);
            }
            w.updateSpectrumLinear(0, bins, 2.0, -10.0, true);
            QCOMPARE(rowsAdded(4), 4);
            // With no hold, a late frame from before a purge changes nothing.
            w.updateSpectrumLinear(0, bins, 2.0, -10.0, false);
            QCOMPARE(rowsAdded(3), 3);
        }
    }

    // The window tells each pan which side of the purge a frame is from, by
    // the frame's generation against the pool's.
    void theWindowHandsEachPanTheFramesSideOfThePurge()
    {
        const QString window = readSource(QStringLiteral("src/gui/MainWindow.cpp"));
        QVERIFY(!window.isEmpty());
        QCOMPARE(window.count(QStringLiteral(
                     "inputGeneration >= m_fftEnginePool->inputGeneration()")), 1);
        const QRegularExpression handed(QStringLiteral(
            "sw->updateSpectrumLinear\\(streamIndex, binsLinear,\\s*windowEnb, dbmOffset,"
            "\\s*computedAfterInputPurge\\)"));
        QCOMPARE(window.count(handed), 1);
    }

    // The window's half: every FFT engine's input is purged on the
    // display's own edge, once, and the fast attack on a MOX edge no longer
    // comes from the transmit model on the active pan.
    void theWindowPurgesOnTheDisplaysEdge()
    {
        const QString window = readSource(QStringLiteral("src/gui/MainWindow.cpp"));
        QVERIFY(!window.isEmpty());
        const QRegularExpression purge(QStringLiteral(
            "&MoxDisplayController::keyedChanged,\\s*m_fftEnginePool,\\s*"
            "&FftEnginePool::resetInputHistoryAll"));
        QCOMPARE(window.count(purge), 1);
        const QRegularExpression oldFastAttack(QStringLiteral(
            "TransmitModel::moxChanged[^;]*setNoiseFloorFastAttack"));
        QVERIFY(!window.contains(oldFastAttack));
        // Each place the window makes its key-edge owner wires the edge.
        QCOMPARE(window.count(QStringLiteral("m_moxDisplay = new MoxDisplayController(")), 2);
        QCOMPARE(window.count(QRegularExpression(QStringLiteral("\\n\\s+wireDisplayKeyEdges\\(\\);"))),
                 2);
        QVERIFY(window.contains(QStringLiteral("setPanBandProvider(")));
        QVERIFY(window.contains(QStringLiteral("setHoldLengthProvider(")));
    }
};

QTEST_MAIN(TestSpectrumWidgetKeyedLevels)
#include "tst_spectrum_widget_keyed_levels.moc"
