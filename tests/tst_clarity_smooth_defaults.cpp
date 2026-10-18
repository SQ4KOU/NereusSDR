// =================================================================
// tests/tst_clarity_smooth_defaults.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test file.
//
// Reset to Smooth Defaults keeps the March (Phase 3G-9b) tuning. The
// recipe used to call setAverageAlpha(0.05f), a transient override of
// the spectrum's averaging constant that was neither saved nor kept:
// the next frame-rate change or averaging-time edit recomputed the
// constant from the saved averaging time and threw the 0.05 away.
//
// The button now sets the spectrum averaging time itself, 650 ms, which
// is saved like any other averaging-time edit.
//
// Why 650 ms: the March recipe meant "each new frame contributes 5 %"
// (docs/architecture/waterfall-tuning.md section 3). The averager keeps
// the back-multiplier, averageAlphaForTimeMs(timeMs, fps) =
// exp(-1 / (fps * tau)), so a 5 % new-frame weight is a back-multiplier
// of 0.95. At 30 FPS, exp(-1 / (30 * 0.650)) = 0.95001, a new-frame
// weight of 0.04999. 650 ms is the whole-millisecond time closest to
// that (the exact tau is 649.86 ms).
// =================================================================
#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/spectrum/DisplayFollowers.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TstClaritySmoothDefaults : public QObject {
    Q_OBJECT
private slots:
    void resetChangesOnlyManualWaterfallToAuto_data()
    {
        QTest::addColumn<bool>("clarity");
        QTest::addColumn<bool>("agc");
        QTest::addColumn<bool>("floor");
        QTest::addColumn<bool>("scale");
        QTest::newRow("Clarity retains inactive flags") << true << false << true << true;
        QTest::newRow("Floor stays Floor") << false << true << true << false;
        QTest::newRow("Scale stays Scale") << false << false << false << true;
        QTest::newRow("Scale retains inactive AGC and Floor") << false << true << true << true;
        QTest::newRow("Auto stays Auto") << false << true << false << false;
        QTest::newRow("Manual becomes Auto") << false << false << false << false;
        QTest::newRow("Manual with inactive Floor becomes coherent Auto") << false << false << true << false;
    }

    void resetChangesOnlyManualWaterfallToAuto()
    {
        QFETCH(bool, clarity);
        QFETCH(bool, agc);
        QFETCH(bool, floor);
        QFETCH(bool, scale);
        AppSettings::instance().clear();
        RadioModel model;
        SpectrumWidget widget;
        widget.loadSettings();
        widget.setClarityEnabled(clarity);
        widget.setWfAgcEnabled(agc);
        widget.setWaterfallNFAGCEnabled(floor);
        widget.setWfUseSpectrumMinMax(scale);
        widget.setWfLowThreshold(-133.0f);
        widget.setWfHighThreshold(-43.0f);
        model.setSpectrumSink(&widget);
        model.applyClaritySmoothDefaults();
        QCOMPARE(widget.clarityEnabled(), clarity);
        const bool manual = !clarity && !agc && !scale;
        QCOMPARE(widget.waterfallNFAGCEnabled(), manual ? false : floor);
        QCOMPARE(widget.wfUseSpectrumMinMax(), scale);
        QCOMPARE(widget.wfAgcEnabled(), agc || manual);
        QCOMPARE(widget.wfLowThreshold(), -133.0f);
        QCOMPARE(widget.wfHighThreshold(), -43.0f);
    }
    void sixHundredFiftyMsReproducesTheMarchWeight()
    {
        // The number the button uses, checked against the same function
        // the display uses to turn a time into an averaging constant.
        const float backMultiplier = averageAlphaForTimeMs(650, 30);
        QVERIFY2(qAbs((1.0f - backMultiplier) - 0.05f) < 0.0005f,
                 qPrintable(QString::number(backMultiplier, 'f', 6)));
    }

    void resetSetsAndSavesTheSpectrumAveragingTime()
    {
        AppSettings::instance().clear();
        {
            RadioModel model;
            SpectrumWidget widget;
            widget.setSpectrumAverageTimeMs(30);
            model.setSpectrumSink(&widget);

            model.applyClaritySmoothDefaults();

            QCOMPARE(widget.spectrumAverageTimeMs(), 650);
            // The live constant follows the saved time, not a transient.
            QVERIFY(qAbs(widget.averageAlpha()
                         - averageAlphaForTimeMs(650, 30)) < 1e-6f);
            widget.saveSettingsForTest();
        }

        // A fresh display reads the same time back.
        SpectrumWidget reloaded;
        reloaded.setSpectrumAverageTimeMs(30);
        reloaded.loadSettingsForTest();
        QCOMPARE(reloaded.spectrumAverageTimeMs(), 650);
    }

    // Final pass T1: the profile's trace is white whatever trace color was
    // chosen before, and white as before when none was.
    void resetSetsTheTraceColorToTheFillsWhite_data()
    {
        QTest::addColumn<QColor>("chosen");
        QTest::newRow("a chosen trace color") << QColor(QStringLiteral("#FF0000"));
        QTest::newRow("no trace color chosen") << QColor();
    }

    void resetSetsTheTraceColorToTheFillsWhite()
    {
        QFETCH(QColor, chosen);
        AppSettings::instance().clear();
        const QColor white(0xff, 0xff, 0xff, 230);
        RadioModel model;
        SpectrumWidget widget;
        if (chosen.isValid()) {
            widget.setTraceColor(chosen);
            QCOMPARE(widget.traceColor(), chosen);
        }
        model.setSpectrumSink(&widget);

        model.applyClaritySmoothDefaults();

        QCOMPARE(widget.fillColor(), white);
        QCOMPARE(widget.traceColor(), white);
    }
};

QTEST_MAIN(TstClaritySmoothDefaults)
#include "tst_clarity_smooth_defaults.moc"
