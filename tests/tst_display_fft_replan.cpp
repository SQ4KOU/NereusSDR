// =================================================================
// tests/tst_display_fft_replan.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Display pill plan Task 7 (R-DSP-30, R-DSP-33): MainWindow's wiring of
// the per-stream FFT replan. A zoom on any pan sizes the engine of the
// stream that pan shows (it was pan 0's widget alone, against stream 0's
// engine), and applyFftBaseline() makes the Size control's choice the
// floor on every engine. The sizing rule itself is tested in
// tst_auto_zoom_fft; the expected sizes here come from the same
// ControlRanges::planAutoZoomFft, read from the widgets' own rate and span.
// =================================================================
#include <QtTest/QtTest>

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>

#include "core/AppSettings.h"
#include "core/ControlRanges.h"
#include "core/FFTEngine.h"
#include "core/HpsdrModel.h"
#include "core/spectrum/FftEnginePool.h"
#include "fakes/MainWindowTestSettings.h"
#include "gui/MainWindow.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestDisplayFftReplan : public QObject {
    Q_OBJECT
    QString m_isolatedSettingsPath;

    // The size one pan plans from its widget's own rate and span.
    static int plannedSize(const SpectrumWidget* sw, int baseline)
    {
        return ControlRanges::planAutoZoomFft(baseline, sw->sampleRate(), sw->bandwidth(), 0.0)
            .size;
    }

    // What the operator's zoom does: the widget takes the span, then asks.
    static void zoomTo(SpectrumWidget* sw, double spanHz)
    {
        sw->setFrequencyRange(sw->centerFrequency(), spanHz);
        sw->requestAutoZoomReplan();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        const QString profile =
            QStringLiteral("display-fft-replan-%1").arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        m_isolatedSettingsPath = AppSettings::resolveSettingsPath(profile);
        QCOMPARE(AppSettings::instance().filePath(), m_isolatedSettingsPath);
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        if (AppSettings::instance().filePath() == m_isolatedSettingsPath) {
            AppSettings::instance().clear();
        }
    }

    // Two pans, each showing its own slice. A zoom on the second pan sizes
    // the engine of the stream that pan shows, and zooming back returns it
    // to the baseline.
    void zoomOnTheSecondPanReplansTheStreamItShows()
    {
        Test::markAudioFirstRunDone();
        AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.resize(1280, 800);
        window.show();
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        QVERIFY(model->addSlice(QStringLiteral("pan-0")) >= 0);
        QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                          Q_ARG(QString, QStringLiteral("2v"))));
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        SpectrumWidget* secondary = stack->spectrum(QStringLiteral("pan-1"));
        QVERIFY(secondary);
        QTRY_VERIFY(secondary->vfoWidget(1));
        SliceModel* second = model->sliceById(1);
        QVERIFY(second);
        const int stream = second->streamIndex();
        QVERIFY(stream >= 0);
        FftEnginePool* pool = window.fftEnginePoolForTest();
        QVERIFY(pool);
        QVERIFY(pool->streams().contains(stream));
        NereusSDR::FFTEngine* engine = pool->engineForStream(stream);
        const int baseline = engine->fftSizeBaseline();
        const double rate = secondary->sampleRate();
        QVERIFY(rate > 0.0);

        // Every pan of this stream at full span: the engine runs the baseline.
        for (PanadapterApplet* applet : stack->allApplets()) {
            SpectrumWidget* sw = stack->spectrum(applet->panId());
            QVERIFY(sw);
            zoomTo(sw, sw->sampleRate());
        }
        QCOMPARE(secondary->bandwidth(), rate);
        QCOMPARE(engine->fftSize(), baseline);

        // The second pan zooms to a quarter of the span.
        zoomTo(secondary, rate / 4.0);
        const int zoomed = plannedSize(secondary, baseline);
        QCOMPARE(zoomed, baseline * 4);
        QCOMPARE(engine->fftSize(), zoomed);
        QCOMPARE(engine->fftSizeBaseline(), baseline);

        // And back out.
        zoomTo(secondary, rate);
        QCOMPARE(engine->fftSize(), baseline);
    }

    // The FFT Size slider's size sets what runs in the Fixed sizing mode
    // only (design D38). In Auto it is saved and nothing that runs changes:
    // a zoom plans from 4096. In Fixed the engine runs exactly the size at
    // every zoom, and a new size runs at once. (Before the sizing mode the
    // slider's size was a floor the zoom multiplied: 8192 at full span ran
    // 16384 at half span.)
    void fftSizeSliderSetsTheRunningSizeInFixedOnly()
    {
        Test::markAudioFirstRunDone();
        AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.resize(1280, 800);
        window.show();
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        QVERIFY(model->addSlice(QStringLiteral("pan-0")) >= 0);
        SliceModel* slice = model->sliceById(0);
        QVERIFY(slice);
        const int stream = slice->streamIndex();
        QVERIFY(stream >= 0);
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        SpectrumWidget* primary = stack->spectrum(QStringLiteral("pan-0"));
        QVERIFY(primary);
        FftEnginePool* pool = window.fftEnginePoolForTest();
        QVERIFY(pool);
        QVERIFY(pool->streams().contains(stream));
        NereusSDR::FFTEngine* engine = pool->engineForStream(stream);
        const double rate = primary->sampleRate();
        QVERIFY(rate > 0.0);
        zoomTo(primary, rate);
        QCOMPARE(primary->bandwidth(), rate);
        QCOMPARE(engine->fftSize(), 4096);

        using ControlRanges::DisplayFftSizingMode;
        const auto storedSize = [] {
            return AppSettings::instance()
                .value(QLatin1String(ControlRanges::kDisplayFftSizeKey)).toString();
        };

        // Auto, the mode of a station that has never chosen one.
        QCOMPARE(pool->config().sizingMode, DisplayFftSizingMode::Auto);
        window.applyFftBaseline(8192);
        QCOMPARE(storedSize(), QStringLiteral("8192"));
        QCOMPARE(engine->fftSizeBaseline(), 4096);
        QCOMPARE(engine->fftSize(), 4096);
        QCOMPARE(pool->config().fftSize, 4096);
        zoomTo(primary, rate / 2.0);
        QCOMPARE(engine->fftSize(), 8192);
        zoomTo(primary, rate);
        QCOMPARE(engine->fftSize(), 4096);

        // Fixed: the stored 8192 runs, whatever the pan shows.
        window.applyDisplayFftSizingMode(DisplayFftSizingMode::Fixed);
        QCOMPARE(pool->config().sizingMode, DisplayFftSizingMode::Fixed);
        QCOMPARE(AppSettings::instance()
                     .value(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)).toString(),
                 QStringLiteral("Fixed"));
        QCOMPARE(storedSize(), QStringLiteral("8192"));
        QCOMPARE(engine->fftSizeBaseline(), 8192);
        QCOMPARE(engine->fftSize(), 8192);
        QCOMPARE(pool->config().fftSize, 8192);
        for (double divisor : {2.0, 64.0, 640.0, 1.0}) {
            zoomTo(primary, rate / divisor);
            QCOMPARE(engine->fftSize(), 8192);
        }
        // A new size runs at once, zoomed in or not, and below the size the
        // zoom would plan.
        zoomTo(primary, rate / 64.0);
        window.applyFftBaseline(65536);
        QCOMPARE(engine->fftSize(), 65536);
        window.applyFftBaseline(4096);
        QCOMPARE(engine->fftSize(), 4096);
        QCOMPARE(storedSize(), QStringLiteral("4096"));
        // The largest there is.
        window.applyFftBaseline(262144);
        QCOMPARE(engine->fftSize(), 262144);

        // A size that is not one of the control's changes nothing and is
        // not saved.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("FFT size 5000 is not one of")));
        window.applyFftBaseline(5000);
        QCOMPARE(engine->fftSizeBaseline(), 262144);
        QCOMPARE(storedSize(), QStringLiteral("262144"));

        // Back to Auto: the zoom plans from 4096 again and the stored size
        // stays for the next time Fixed is chosen.
        zoomTo(primary, rate);
        window.applyDisplayFftSizingMode(DisplayFftSizingMode::Auto);
        QCOMPARE(engine->fftSizeBaseline(), 4096);
        QCOMPARE(engine->fftSize(), 4096);
        QCOMPARE(storedSize(), QStringLiteral("262144"));
    }

    // The Hz/bin control through the mode: a target is the Hz/bin mode, Off
    // is Auto, and entering Hz/bin with no target remembered starts from the
    // bin width running, so nothing changes until the control moves.
    void hzPerBinTargetIsAModeAndOffIsAuto()
    {
        Test::markAudioFirstRunDone();
        AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.resize(1280, 800);
        window.show();
        RadioModel* model = window.radioModel();
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(5, 5, 192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        QVERIFY(model->addSlice(QStringLiteral("pan-0")) >= 0);
        SliceModel* slice = model->sliceById(0);
        QVERIFY(slice);
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        SpectrumWidget* primary = stack->spectrum(QStringLiteral("pan-0"));
        QVERIFY(primary);
        FftEnginePool* pool = window.fftEnginePoolForTest();
        QVERIFY(pool);
        NereusSDR::FFTEngine* engine = pool->engineForStream(slice->streamIndex());
        // The pan at its stream's rate, as a connected radio leaves it (this
        // window never connects, and the widget starts on a rate of its own).
        primary->setSampleRate(engine->sampleRate());
        const double rate = primary->sampleRate();
        QCOMPARE(rate, 192000.0);
        using ControlRanges::DisplayFftSizingMode;
        auto& settings = AppSettings::instance();
        const auto stored = [&settings](const char* key) {
            return settings.value(QLatin1String(key)).toString();
        };

        // Half span in Auto runs 8192.
        zoomTo(primary, rate / 2.0);
        QCOMPARE(engine->fftSize(), 8192);

        // Entering Hz/bin with nothing remembered: the bin width running,
        // up to the control's step, which plans the same 8192.
        window.applyDisplayFftSizingMode(DisplayFftSizingMode::HzPerBin);
        QCOMPARE(pool->config().sizingMode, DisplayFftSizingMode::HzPerBin);
        QCOMPARE(stored(ControlRanges::kDisplayFftSizingModeKey), QStringLiteral("HzPerBin"));
        const double entered = stored(ControlRanges::kDisplayHzPerBinTargetKey).toDouble();
        QCOMPARE(entered, ControlRanges::hzPerBinTargetForBinWidth(rate / 8192.0));
        QVERIFY(entered >= rate / 8192.0);
        QVERIFY(entered < rate / 8192.0 + ControlRanges::kDisplayHzPerBinTargetStep);
        QCOMPARE(engine->hzPerBinTarget(), entered);
        QCOMPARE(engine->fftSize(), 8192);
        // The zoom no longer sets the size.
        zoomTo(primary, rate / 16.0);
        QCOMPARE(engine->fftSize(), 8192);

        // The control moves: a target a sixteenth of that asks sixteen times
        // the points.
        const double finer = entered / 16.0;
        const int finerSize = ControlRanges::planDisplayFft(DisplayFftSizingMode::HzPerBin,
                                                            4096, rate, rate / 16.0, finer).size;
        QVERIFY(finerSize > 65536);
        window.applyDisplayHzPerBinTarget(finer);
        QCOMPARE(stored(ControlRanges::kDisplayFftSizingModeKey), QStringLiteral("HzPerBin"));
        QCOMPARE(engine->fftSize(), finerSize);

        // Off is Auto; the target key reads 0 and the last target is kept.
        window.applyDisplayHzPerBinTarget(0.0);
        QCOMPARE(stored(ControlRanges::kDisplayFftSizingModeKey), QStringLiteral("Auto"));
        QCOMPARE(stored(ControlRanges::kDisplayHzPerBinTargetKey).toDouble(), 0.0);
        QCOMPARE(stored(ControlRanges::kDisplayHzPerBinLastTargetKey).toDouble(), finer);
        QCOMPARE(engine->hzPerBinTarget(), 0.0);
        QCOMPARE(engine->fftSize(), 65536); // 4096 x 16
        // Back in: the remembered target.
        window.applyDisplayFftSizingMode(DisplayFftSizingMode::HzPerBin);
        QCOMPARE(stored(ControlRanges::kDisplayHzPerBinTargetKey).toDouble(), finer);
        QCOMPARE(engine->fftSize(), finerSize);

        // Off in Fixed leaves Fixed.
        window.applyDisplayFftSizingMode(DisplayFftSizingMode::Fixed);
        QCOMPARE(stored(ControlRanges::kDisplayHzPerBinTargetKey).toDouble(), 0.0);
        window.applyDisplayHzPerBinTarget(0.0);
        QCOMPARE(stored(ControlRanges::kDisplayFftSizingModeKey), QStringLiteral("Fixed"));
        QCOMPARE(engine->fftSize(), 4096);
    }
};

QTEST_MAIN(TestDisplayFftReplan)
#include "tst_display_fft_replan.moc"
