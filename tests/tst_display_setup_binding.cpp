// Modification history (NereusSDR):
//   2026-10-05 — J.J. Boyd (KG4VCF). Independent per-pan Clarity ownership.
//                 AI-assisted via OpenAI Codex.
//
// tests/tst_display_setup_binding.cpp
//
// no-port-check: NereusSDR-original test infrastructure for the 3D
// Stacked-Trace Spectrum Plan Task 21 Setup migration. Neither Thetis nor
// AetherSDR routes its Setup-equivalent dialog through a consolidating
// display-settings model (see DisplaySettingsModel.h's own header comment),
// so there is no upstream source to cite.
//
// 3D stacked-trace spectrum plan Task 21: re-points Setup -> Display's nine
// DisplaySettingsModel-backed controls (Display3DSetupPage's six, plus
// WaterfallDefaultsPage's Colour Scheme and SpectrumDefaultsPage's Fill
// Alpha and Fill trace) at the model Task 18 introduced, instead of each
// page talking straight to SpectrumWidget. Pins:
//
//   - Display3DSetupPage: a model-driven change lands on the page's own
//     widget without echoing the widget's own change signal back out
//     (QSignalBlocker on reflect), the model's own signal fires exactly
//     once per real change; a page-driven change reaches both the model
//     and (through Task 18's existing binding) the widget; the Reset
//     button writes the six ship defaults onto the model.
//   - WaterfallDefaultsPage / SpectrumDefaultsPage: Colour Scheme, Fill
//     Alpha and Fill trace round-trip the same way through
//     model()->spectrumWidget()->displaySettings().
//
// Design: docs/architecture/2026-08-08-3d-stacked-trace-spectrum-design.md
// Plan:   docs/architecture/2026-08-08-3d-stacked-trace-spectrum-plan.md

//
// Display pill plan Task 20 (D22, D23, D24, D26; R-DSP-29, R-DSP-31,
// R-DSP-32, R-DSP-33, R-DSP-61; issue #296): Spectrum Defaults' FFT
// controls. The Size slider sets the station's baseline once, as a drag is
// let go or 300 ms after the last keyboard step, and never a running size;
// the page opens on the baseline, not the zoomed size the engine runs; a
// stored size survives a restart at every slider position; a stored size
// that is not one of the seven is replaced by the default, in the key too,
// with one warning; Window, Hz/bin target, FPS and thread priority reach
// every stream's engine; and the line under the slider says what the FFT
// runs at, in the Display pill's words.
// Design: docs/architecture/2026-10-09-display-pill-and-levels-design.md

//
// Display pill plan Task 21 (D17, D26, D27; S12, S13; R-DSP-57): Setup's
// Display pages hold the defaults for every pan. A control stores the bare
// key and moves each pan that has no value of its own; a pan that has one
// keeps it. Each page shows the default, not the active pan's value, and
// follows it. Waterfall Defaults greys its level rows by who is setting the
// levels; the line width is 0.1 to 5 px in tenths; Appearance has a
// Trace and a Fill colour; Grid & Scales writes the band store of the pan
// it is given, and greys the rows with a note when it is given none.
//
// Rewritten for that task, because the spec contradicts what they pinned
// (the page following the active pan's own value):
// modelChange_reflectsOnPage_perField (now
// panOwnChangeLeavesThe3DPageOnItsDefault), waterfallColorScheme_loadReadsModel,
// spectrumFillAlpha_loadReadsModel, spectrumFillTrace_loadReadsModel (now
// the three ..._loadShowsTheDefault cases) and the saved-key half of
// waterfallModes_normalUiAndRestoreKeepBothEnabled.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <functional>
#include <memory>
#include <optional>
#include "core/AppSettings.h"

#include "core/ControlRanges.h"
#include "core/FFTEngine.h"
#include "core/HpsdrModel.h"
#include "core/NoiseFloorTracker.h"
#include "core/settings/PanDisplayKeys.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "core/spectrum/FftEnginePool.h"
#include "fakes/MainWindowTestSettings.h"
#include "gui/DisplayFftReadout.h"
#include "gui/MainWindow.h"
#include "gui/PanModels.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/SetupDialog.h"
#include "gui/ColorSwatchButton.h"
#include "gui/SetupPage.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"
// The FFT controls and their readouts are the page's private members.
#define private public
#include "gui/setup/AppearanceSetupPages.h"
#include "gui/setup/DisplaySetupPages.h"
#include "gui/setup/SpectrumPeaksPage.h"
#undef private
#include "models/DisplaySettingsModel.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

// A local window's model with a two-stream engine pool, as MainWindow gives
// a page: the model's engine is stream 0's, the pool holds both.
struct LocalPoolFixture {
    RadioModel model;
    SpectrumWidget widget;
    NereusSDR::FftEnginePool pool;
    NereusSDR::FFTEngine* stream0{nullptr};
    NereusSDR::FFTEngine* stream1{nullptr};

    LocalPoolFixture()
    {
        stream0 = pool.engineForStream(0);
        stream1 = pool.engineForStream(1);
        model.setSpectrumWidget(&widget);
        model.setFftEngine(stream0);
        model.setFftEnginePool(&pool);
    }
    ~LocalPoolFixture()
    {
        model.setSpectrumWidget(nullptr);
        model.setFftEngine(nullptr);
        model.setFftEnginePool(nullptr);
    }
};

// One keyboard step on a slider.
void keyStep(QSlider* slider, Qt::Key which)
{
    QKeyEvent press(QEvent::KeyPress, which, Qt::NoModifier);
    QApplication::sendEvent(slider, &press);
    QKeyEvent release(QEvent::KeyRelease, which, Qt::NoModifier);
    QApplication::sendEvent(slider, &release);
}

QString storedFftSize()
{
    return AppSettings::instance()
        .value(QLatin1String(ControlRanges::kDisplayFftSizeKey)).toString();
}

// A MainWindow with one receiver on a 192 kHz stream, never connected.
void startOneReceiver(MainWindow& window)
{
    window.resize(1280, 800);
    window.show();
    RadioModel* model = window.radioModel();
    model->setBoardForTest(HPSDRHW::Saturn);
    model->configureStreamPool(5, 5, 192000);
    model->setConnectionStateForTest(ConnectionState::Connected);
    QVERIFY(model->addSlice(QStringLiteral("pan-0")) >= 0);
}

// Two pans as Setup is handed them: pan 0 is the active one.
struct TwoPanFixture {
    RadioModel model;
    SpectrumWidget pan0;
    SpectrumWidget pan1;

    TwoPanFixture()
    {
        pan1.setPanIndex(1);
        pan0.loadSettings();
        pan1.loadSettings();
        model.setSpectrumWidget(&pan0);
    }
    ~TwoPanFixture() { model.setSpectrumWidget(nullptr); }

    SetupDisplayProviders providers(PanadapterModel* grid = nullptr)
    {
        SetupDisplayProviders p;
        p.panWidgets = [this]() { return QList<SpectrumWidget*>{&pan0, &pan1}; };
        p.bandGridModel = [grid]() { return grid; };
        return p;
    }
};

QString stored(const QString& key)
{
    return AppSettings::instance().value(key).toString();
}

// Counts every settings write while it lives.
struct WriteCounter {
    int writes{0};
    QStringList keys;
    WriteCounter()
    {
        AppSettings::instance().setChangeHook([this](const QString& key) {
            ++writes;
            keys.append(key);
        });
    }
    ~WriteCounter() { AppSettings::instance().setChangeHook({}); }
};

} // namespace

class TestDisplaySetupBinding : public QObject {
    Q_OBJECT
    QString m_isolatedSettingsPath;

private slots:
    // Display pill plan Task 20: these tests store station keys and build
    // MainWindows; they use a settings file of their own.
    void initTestCase()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        const QString profile =
            QStringLiteral("display-setup-binding-%1").arg(QCoreApplication::applicationPid());
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

    void selectedClosedPanWithoutAnyOpenPanHasShippedFallbacks()
    {
        RadioModel model;
        SpectrumDefaultsPage page(&model);
        SetupDisplayProviders providers;
        providers.panWidgets = [] { return QList<SpectrumWidget*>(); };
        page.setDisplayProviders(providers);
        WriteCounter writes;
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, 7)));
        QCOMPARE(writes.writes, 0);
        QCOMPARE(page.m_lineWidthSlider->value(), 10);
        page.m_lineWidthSlider->setValue(25);
        QCOMPARE(stored(QStringLiteral("DisplayLineWidth_7")), QStringLiteral("2.5"));
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, -1)));
        QCOMPARE(page.m_lineWidthSlider->value(), 10);
        QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QString());
    }

    void visibleScopeChipsSelectThePanWithoutWriting()
    {
        TwoPanFixture f;
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        WriteCounter writes;
        QVERIFY(QMetaObject::invokeMethod(&page, "enableDisplayPanScope"));
        auto* all = page.findChild<QPushButton*>(QStringLiteral("setupDisplayPanAll"));
        auto* second = page.findChild<QPushButton*>(QStringLiteral("setupDisplayPan1"));
        QVERIFY(all);
        QVERIFY(second);
        QVERIFY(all->isChecked());
        second->click();
        QCOMPARE(page.displayPanIndex(), 1);
        QVERIFY(second->isChecked());
        QVERIFY(!all->isChecked());
        QCOMPARE(writes.writes, 0);
        page.m_lineWidthSlider->setValue(25);
        QCOMPARE(f.pan1.lineWidth(), 2.5f);
        QCOMPARE(f.pan0.lineWidth(), 1.0f);
        QCOMPARE(second->property("ownValueCount").toInt(), 1);
        all->click();
        QCOMPARE(page.displayPanIndex(), -1);
        QCOMPARE(page.m_lineWidthSlider->value(), 10);
    }

    void visibleScopeKeepsAClosedPanEditableAndExplainsPageReset()
    {
        TwoPanFixture f;
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "enableDisplayPanScope"));
        page.setDisplayPanIndex(7);
        auto* closed = page.findChild<QPushButton*>(QStringLiteral("setupDisplayPan7"));
        auto* note = page.findChild<QLabel*>(QStringLiteral("setupDisplayPanNote"));
        auto* reset = page.findChild<QPushButton*>(QStringLiteral("setupDisplayPageReset"));
        auto* copy = page.findChild<QPushButton*>(QStringLiteral("setupDisplayPageCopy"));
        QVERIFY(closed);
        QVERIFY(note);
        QVERIFY(reset);
        QVERIFY(copy);
        QVERIFY(closed->text().contains(QStringLiteral("not open")));
        QVERIFY(note->text().contains(QStringLiteral("kept and used when you open it")));
        QVERIFY(!reset->isEnabled());
        QVERIFY(!copy->isEnabled());
        page.m_lineWidthSlider->setValue(25);
        QVERIFY(reset->isEnabled());
        QVERIFY(copy->isEnabled());
        QCOMPARE(closed->property("ownValueCount").toInt(), 1);
        reset->click();
        QCOMPARE(page.m_lineWidthSlider->value(), 10);
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayLineWidth"), 7));
        QVERIFY(!reset->isEnabled());
    }

    void appearanceResetPreservesThePansTraceFillAndBackground()
    {
        TwoPanFixture f;
        const QColor fill(23, 45, 67, 100);
        const QColor trace(210, 180, 130);
        f.pan1.setFillColor(fill);
        f.pan1.setTraceColor(trace);
        f.pan1.setBackgroundEnabled(true);
        f.pan1.setBackgroundFillColor(QColor(70, 40, 90));
        f.pan1.setBackgroundOpacity(63);
        f.pan1.saveSettings();
        ColorsThemePage page(&f.model);
        page.setDisplayProviders(f.providers());
        page.resetColorsToDefaultsForTest();
        QCOMPARE(f.pan1.fillColor(), fill);
        QCOMPARE(f.pan1.traceColor(), trace);
        QVERIFY(f.pan1.backgroundEnabled());
        QCOMPARE(f.pan1.backgroundFillColor(), QColor(70, 40, 90));
        QCOMPARE(f.pan1.backgroundOpacity(), 63);
        QVERIFY(PanDisplayKeys::hasOverride(QStringLiteral("DisplayFillColor"), 1));
        QVERIFY(PanDisplayKeys::hasOverride(QStringLiteral("DisplayTraceColor"), 1));
    }

    void peaksPageUsesSelectedPanAndClosedOverridesWithoutChangingMaster()
    {
        TwoPanFixture f;
        SpectrumPeaksPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QCheckBox* enabled = nullptr;
        for (QCheckBox* box : page.findChildren<QCheckBox*>()) {
            if (box->property("nereusSetupId").toString() == QStringLiteral("display.spectrumPeaks.activePeakHold")) {
                enabled = box;
            }
        }
        QVERIFY(enabled);
        page.setDisplayPanIndex(1);
        enabled->setChecked(true);
        QVERIFY(f.pan1.activePeakHoldEnabled());
        QVERIFY(!f.pan0.activePeakHoldEnabled());
        QVERIFY(stored(QStringLiteral("DisplayActivePeakHoldEnabled")).isEmpty());
        page.setDisplayPanIndex(7);
        QVERIFY(!enabled->isChecked());
        enabled->setChecked(true);
        QCOMPARE(stored(QStringLiteral("DisplayActivePeakHoldEnabled_7")), QStringLiteral("True"));
        page.resetDisplayPageOverrides();
        QVERIFY(!enabled->isChecked());
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayActivePeakHoldEnabled"), 7));
        QVERIFY(f.pan1.activePeakHoldEnabled());
    }

    void bandPickerIsFreeForAllAndClosedPansAndLockedToAnOpenPan()
    {
        TwoPanFixture f;
        PanadapterModel preview;
        preview.setBand(Band::Band40m);
        PanadapterModel open;
        open.setBand(Band::Band20m);
        SetupDisplayProviders providers = f.providers(&preview);
        providers.bandGridModelForPan = [&preview, &open](int panIndex) {
            return panIndex == 1 ? &open : &preview;
        };
        GridScalesPage page(&f.model);
        page.setDisplayProviders(providers);
        QVERIFY(QMetaObject::invokeMethod(&page, "enableDisplayBandPicker"));
        auto* picker = page.findChild<QComboBox*>(QStringLiteral("setupDisplayBandPicker"));
        QVERIFY(picker);
        QVERIFY(picker->isEnabled());
        picker->setCurrentIndex(picker->findData(static_cast<int>(Band::Band80m)));
        QCOMPARE(preview.band(), Band::Band80m);
        page.setDisplayPanIndex(1);
        QVERIFY(!picker->isEnabled());
        QCOMPARE(picker->currentData().toInt(), static_cast<int>(Band::Band20m));
        page.setDisplayPanIndex(7);
        QVERIFY(picker->isEnabled());
        QCOMPARE(picker->currentData().toInt(), static_cast<int>(Band::Band80m));
        picker->setCurrentIndex(picker->findData(static_cast<int>(Band::Band40m)));
        QCOMPARE(preview.band(), Band::Band40m);
        QCOMPARE(open.band(), Band::Band20m);
    }

    void nativeBackgroundRowsEditOnlyTheSelectedPanAndUseStoredAvailability()
    {
        TwoPanFixture f;
        Display3DSetupPage page(&f.pan0);
        page.setDisplayProviders(f.providers());
        page.setDisplayPanIndex(1);
        QVERIFY(QMetaObject::invokeMethod(&page, "addDisplayBackgroundSection"));
        QCheckBox* enabled = nullptr;
        QSlider* opacity = nullptr;
        for (QWidget* control : page.findChildren<QWidget*>()) {
            const QString id = control->property("nereusSetupId").toString();
            if (id == QStringLiteral("display.view.backgroundEnabled")) { enabled = qobject_cast<QCheckBox*>(control); }
            if (id == QStringLiteral("display.view.backgroundOpacity")) { opacity = qobject_cast<QSlider*>(control); }
        }
        QVERIFY(enabled);
        QVERIFY(opacity);
        QVERIFY(!enabled->isChecked());
        QVERIFY(!opacity->isEnabled());
        enabled->setChecked(true);
        QVERIFY(f.pan1.backgroundEnabled());
        QVERIFY(!f.pan0.backgroundEnabled());
        QVERIFY(stored(QStringLiteral("DisplayBackgroundEnabled")).isEmpty());
        QTemporaryDir pictures;
        QVERIFY(pictures.isValid());
        QImage picture(8, 8, QImage::Format_ARGB32);
        picture.fill(QColor(20, 40, 60));
        const QString picturePath = pictures.filePath(QStringLiteral("background.png"));
        QVERIFY(picture.save(picturePath));
        f.pan1.setBackgroundImagePath(picturePath);
        QVERIFY(opacity->isEnabled());
        opacity->setValue(63);
        QCOMPARE(f.pan1.backgroundOpacity(), 63);
        QCOMPARE(f.pan0.backgroundOpacity(), 80);
        QSpinBox* opacityNumber = opacity->parentWidget()->findChild<QSpinBox*>();
        QVERIFY(opacityNumber);
        opacityNumber->setValue(67);
        QCOMPARE(f.pan1.backgroundOpacity(), 67);
        f.pan1.displaySettings()->setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode3D));
        QVERIFY(!enabled->isEnabled());
        QVERIFY(!opacity->isEnabled());
        QVERIFY(enabled->toolTip().contains(QStringLiteral("3D view does not draw the background")));
    }

    void selectedSmoothConvertsManualWithInactiveFloorOnlyInThatScope_data()
    {
        QTest::addColumn<int>("panIndex");
        QTest::newRow("selected open pan") << 1;
        QTest::newRow("selected remembered closed pan") << 7;
    }

    void selectedSmoothConvertsManualWithInactiveFloorOnlyInThatScope()
    {
        QFETCH(int, panIndex);
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("DisplayWfAgc"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("WaterfallNFAGCEnabled"), QStringLiteral("True"));
        TwoPanFixture f;
        f.pan1.setClarityEnabled(false);
        f.pan1.setWfUseSpectrumMinMax(false);
        f.pan1.setWfAgcEnabled(false);
        f.pan1.setWaterfallNFAGCEnabled(true);
        f.pan1.setWfLowThreshold(-133.0f);
        f.pan1.setWfHighThreshold(-43.0f);
        settings.setValue(QStringLiteral("ClarityEnabled_7"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("DisplayWfUseSpectrumMinMax_7"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("DisplayWfAgc_7"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("WaterfallNFAGCEnabled_7"), QStringLiteral("True"));
        settings.setValue(QStringLiteral("DisplayWfLowLevel_7"), QStringLiteral("-131"));
        settings.setValue(QStringLiteral("DisplayWfHighLevel_7"), QStringLiteral("-41"));
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        page.setDisplayPanIndex(panIndex);
        QVERIFY(QMetaObject::invokeMethod(&page, "convertManualWaterfallModesForSmoothReset"));
        if (panIndex == 1) {
            QVERIFY(f.pan1.wfAgcEnabled());
            QVERIFY(!f.pan1.waterfallNFAGCEnabled());
            f.pan1.saveSettings();
        } else {
            QVERIFY(!f.pan1.wfAgcEnabled());
            QVERIFY(f.pan1.waterfallNFAGCEnabled());
        }
        const auto value = [panIndex](const QString& key) {
            return PanDisplayKeys::read(key, panIndex,
                SpectrumWidget::perPanShipDefaultForClosedPan(key)).toString();
        };
        QCOMPARE(value(QStringLiteral("ClarityEnabled")), QStringLiteral("False"));
        QCOMPARE(value(QStringLiteral("DisplayWfUseSpectrumMinMax")), QStringLiteral("False"));
        QCOMPARE(value(QStringLiteral("DisplayWfAgc")), QStringLiteral("True"));
        QCOMPARE(value(QStringLiteral("WaterfallNFAGCEnabled")), QStringLiteral("False"));
        QCOMPARE(stored(QStringLiteral("DisplayWfAgc")), QStringLiteral("False"));
        QCOMPARE(stored(QStringLiteral("WaterfallNFAGCEnabled")), QStringLiteral("True"));
        QVERIFY(f.pan0.clarityEnabled());
        QVERIFY(!f.pan0.wfAgcEnabled());
        QVERIFY(f.pan0.waterfallNFAGCEnabled());
        QCOMPARE(f.pan1.wfLowThreshold(), -133.0f);
        QCOMPARE(f.pan1.wfHighThreshold(), -43.0f);
        QCOMPARE(stored(QStringLiteral("DisplayWfLowLevel_7")), QStringLiteral("-131"));
        QCOMPARE(stored(QStringLiteral("DisplayWfHighLevel_7")), QStringLiteral("-41"));
    }

    void actualAllPanSmoothResetConvertsInactiveFloorManualAndPreservesOtherTuples()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("DisplayWfUseSpectrumMinMax"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("DisplayWfAgc"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("WaterfallNFAGCEnabled"), QStringLiteral("True"));
        settings.setValue(QStringLiteral("DisplayWfLowLevel"), QStringLiteral("-140"));
        settings.setValue(QStringLiteral("DisplayWfHighLevel"), QStringLiteral("-40"));
        TwoPanFixture f;
        // Clarity precedence preserves even its remembered Scale/Floor bits.
        f.pan0.setClarityEnabled(true);
        f.pan0.setWfUseSpectrumMinMax(true);
        f.pan0.setWfAgcEnabled(false);
        f.pan0.setWaterfallNFAGCEnabled(true);
        f.pan0.setWfLowThreshold(-133.0f);
        f.pan0.setWfHighThreshold(-43.0f);
        // Pan1 inherits the Manual master with a remembered inactive NF bit.
        QVERIFY(!f.pan1.clarityEnabled());
        QVERIFY(!f.pan1.wfAgcEnabled());
        QVERIFY(f.pan1.waterfallNFAGCEnabled());
        const QStringList modeKeys{QStringLiteral("ClarityEnabled"),
            QStringLiteral("DisplayWfAgc"), QStringLiteral("WaterfallNFAGCEnabled"),
            QStringLiteral("DisplayWfUseSpectrumMinMax")};
        const auto seed = [&settings, &modeKeys](int index, const QStringList& values) {
            for (int i = 0; i < modeKeys.size(); ++i) {
                settings.setValue(PanDisplayKeys::panKey(modeKeys[i], index), values[i]);
            }
        };
        seed(7, {QStringLiteral("False"), QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("False")});
        seed(8, {QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True")});
        seed(9, {QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True"), QStringLiteral("False")});
        seed(10, {QStringLiteral("False"), QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True")});
        settings.setValue(QStringLiteral("DisplayWfLowLevel_7"), QStringLiteral("-131"));
        settings.setValue(QStringLiteral("DisplayWfHighLevel_7"), QStringLiteral("-41"));
        settings.setValue(QStringLiteral("WaterfallNFAGCEnabled_backup"), QStringLiteral("True"));
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QCOMPARE(page.displayPanIndex(), -1);
        QPushButton* reset = nullptr;
        for (QPushButton* button : page.findChildren<QPushButton*>()) {
            if (button->property("nereusSetupId").toString()
                    == QStringLiteral("display.spectrumDefaults.smoothDefaults")) { reset = button; }
        }
        QVERIFY(reset);
        bool answered = false;
        QTimer::singleShot(0, &page, [&answered]() {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) { return; }
            answered = true;
            box->button(QMessageBox::Yes)->click();
        });
        reset->click();
        QVERIFY(answered);
        QCOMPARE(stored(QStringLiteral("DisplayWfAgc")), QStringLiteral("True"));
        QCOMPARE(stored(QStringLiteral("WaterfallNFAGCEnabled")), QStringLiteral("False"));
        QVERIFY(f.pan1.wfAgcEnabled());
        QVERIFY(!f.pan1.waterfallNFAGCEnabled());
        QVERIFY(f.pan0.clarityEnabled());
        QVERIFY(f.pan0.wfUseSpectrumMinMax());
        QVERIFY(!f.pan0.wfAgcEnabled());
        QVERIFY(f.pan0.waterfallNFAGCEnabled());
        const auto tuple = [&modeKeys](int index) {
            QStringList values;
            for (const QString& key : modeKeys) {
                values.append(PanDisplayKeys::read(key, index,
                    SpectrumWidget::perPanShipDefaultForClosedPan(key)).toString());
            }
            return values;
        };
        QCOMPARE(tuple(7), (QStringList{QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("False")}));
        QCOMPARE(tuple(8), (QStringList{QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True")}));
        QCOMPARE(tuple(9), (QStringList{QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True"), QStringLiteral("False")}));
        QCOMPARE(tuple(10), (QStringList{QStringLiteral("False"), QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True")}));
        f.pan0.saveSettings();
        f.pan1.saveSettings();
        QCOMPARE(tuple(0), (QStringList{QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("True")}));
        QCOMPARE(tuple(1), (QStringList{QStringLiteral("False"), QStringLiteral("True"), QStringLiteral("False"), QStringLiteral("False")}));
        QCOMPARE(stored(QStringLiteral("WaterfallNFAGCEnabled_8")), QStringLiteral("True"));
        QCOMPARE(stored(QStringLiteral("WaterfallNFAGCEnabled_9")), QStringLiteral("True"));
        QCOMPARE(stored(QStringLiteral("WaterfallNFAGCEnabled_10")), QStringLiteral("True"));
        QCOMPARE(stored(QStringLiteral("WaterfallNFAGCEnabled_backup")), QStringLiteral("True"));
        QCOMPARE(f.pan0.wfLowThreshold(), -133.0f);
        QCOMPARE(f.pan0.wfHighThreshold(), -43.0f);
        QCOMPARE(f.pan1.wfLowThreshold(), -140.0f);
        QCOMPARE(f.pan1.wfHighThreshold(), -40.0f);
        QCOMPARE(stored(QStringLiteral("DisplayWfLowLevel_7")), QStringLiteral("-131"));
        QCOMPARE(stored(QStringLiteral("DisplayWfHighLevel_7")), QStringLiteral("-41"));
    }

    void allPanSmoothModeConversionPreservesNonManualFlagsAndReachesClosedPans()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("DisplayWfAgc"), QStringLiteral("False"));
        TwoPanFixture f;
        f.pan1.setClarityEnabled(false);
        f.pan1.setWfAgcEnabled(false);
        settings.setValue(QStringLiteral("ClarityEnabled_7"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("DisplayWfAgc_7"), QStringLiteral("False"));
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "convertManualWaterfallModesForSmoothReset"));
        QVERIFY(f.pan0.clarityEnabled());
        QVERIFY(!f.pan0.wfAgcEnabled());
        QVERIFY(!f.pan1.clarityEnabled());
        QVERIFY(f.pan1.wfAgcEnabled());
        QCOMPARE(PanDisplayKeys::read(QStringLiteral("DisplayWfAgc"), 7, QStringLiteral("True")).toString(), QStringLiteral("True"));
        QCOMPARE(stored(QStringLiteral("DisplayWfAgc")), QStringLiteral("False"));
    }

    void nativeCustomGradientEditorsRemainVisibleAndFollowTheSelectedPalette()
    {
        TwoPanFixture f;
        WaterfallDefaultsPage receive(&f.model);
        receive.setDisplayProviders(f.providers());
        receive.setDisplayPanIndex(1);
        QVERIFY(QMetaObject::invokeMethod(&receive, "addDisplayGradientEditor", Q_ARG(bool, false)));
        TxDisplayPage transmit(&f.model);
        transmit.setDisplayProviders(f.providers());
        transmit.setDisplayPanIndex(1);
        QVERIFY(QMetaObject::invokeMethod(&transmit, "addDisplayGradientEditor", Q_ARG(bool, true)));
        const auto buttonFor = [](SetupPage& page, const QString& id) -> QPushButton* {
            for (QPushButton* button : page.findChildren<QPushButton*>()) {
                if (button->property("nereusSetupId").toString() == id) { return button; }
            }
            return nullptr;
        };
        QPushButton* rx = buttonFor(receive, QStringLiteral("display.waterfall.editCustomGradient"));
        QPushButton* tx = buttonFor(transmit, QStringLiteral("display.txDisplay.editCustomGradient"));
        QVERIFY(rx);
        QVERIFY(tx);
        QVERIFY(!rx->isHidden());
        QVERIFY(!tx->isHidden());
        QVERIFY(!rx->isEnabled());
        QVERIFY(!tx->isEnabled());
        f.pan1.displaySettings()->setWfColorScheme(static_cast<int>(WfColorScheme::Custom));
        QVERIFY(rx->isEnabled());
        QVERIFY(!tx->isEnabled());
        f.pan1.setTxWfPalette(WfColorScheme::Custom);
        QVERIFY(tx->isEnabled());
        f.pan1.displaySettings()->setWfColorScheme(static_cast<int>(WfColorScheme::Default));
        QVERIFY(!rx->isEnabled());
        QVERIFY(tx->isEnabled());
    }

    void smoothManualMasterDoesNotReplaceAnOwnClarityOrScaleTuple()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        settings.setValue(QStringLiteral("DisplayWfAgc"), QStringLiteral("False"));
        TwoPanFixture f;
        f.pan0.setClarityEnabled(true);
        settings.setValue(QStringLiteral("DisplayWfUseSpectrumMinMax_7"), QStringLiteral("True"));
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "convertManualWaterfallModesForSmoothReset"));
        QCOMPARE(stored(QStringLiteral("DisplayWfAgc")), QStringLiteral("True"));
        QVERIFY(f.pan0.clarityEnabled());
        QVERIFY(!f.pan0.wfAgcEnabled());
        QVERIFY(f.pan1.wfAgcEnabled());
        QVERIFY(!f.pan1.clarityEnabled());
        QCOMPARE(PanDisplayKeys::read(QStringLiteral("DisplayWfAgc"), 7, QStringLiteral("True")).toString(), QStringLiteral("False"));
        QCOMPARE(PanDisplayKeys::read(QStringLiteral("DisplayWfUseSpectrumMinMax"), 7, QStringLiteral("False")).toString(), QStringLiteral("True"));
    }

    void selectedOpenPanChangesOnlyItsOwnPicture()
    {
        TwoPanFixture f;
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        WriteCounter writes;
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, 1)));
        QCOMPARE(writes.writes, 0);
        page.m_lineWidthSlider->setValue(25);
        QCOMPARE(f.pan1.lineWidth(), 2.5f);
        QCOMPARE(f.pan0.lineWidth(), 1.0f);
        QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QString());
        f.pan1.storePendingPerPanSetting(QStringLiteral("DisplayLineWidth"));
        QCOMPARE(stored(QStringLiteral("DisplayLineWidth_1")), QStringLiteral("2.5"));
        QVERIFY(!writes.keys.contains(QStringLiteral("DisplayLineWidth")));
    }

    void closedPanPictureIsSavedWithoutChangingOpenPans()
    {
        TwoPanFixture f;
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, 7)));
        page.m_lineWidthSlider->setValue(25);
        QCOMPARE(stored(QStringLiteral("DisplayLineWidth_7")), QStringLiteral("2.5"));
        QCOMPARE(f.pan0.lineWidth(), 1.0f);
        QCOMPARE(f.pan1.lineWidth(), 1.0f);
        SpectrumWidget reopened;
        reopened.setPanIndex(7);
        reopened.loadSettings();
        QCOMPARE(reopened.lineWidth(), 2.5f);
    }

    void selectedPageResetKeepsOtherPagesAndViews()
    {
        TwoPanFixture f;
        f.pan1.setLineWidth(2.5f);
        f.pan1.setWfOpacity(55);
        f.pan1.setFrequencyRange(14000000, 60000);
        f.pan1.saveSettings();
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, 1)));
        QVERIFY(QMetaObject::invokeMethod(&page, "resetDisplayPageOverrides"));
        QCOMPARE(f.pan1.lineWidth(), 1.0f);
        QCOMPARE(f.pan1.wfOpacity(), 55);
        QCOMPARE(f.pan1.bandwidth(), 60000.0);
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayLineWidth"), 1));
        QVERIFY(PanDisplayKeys::hasOverride(QStringLiteral("DisplayWfOpacity"), 1));
    }

    void selectedPageCopyStopsAtTheInheritedWaterfallPairGap()
    {
        AppSettings::instance().setValue(QStringLiteral("DisplayWfHighLevel"), QStringLiteral("-100"));
        AppSettings::instance().setValue(QStringLiteral("DisplayWfLowLevel_7"), QStringLiteral("-90"));
        TwoPanFixture f;
        WaterfallDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, 7)));
        QVERIFY(QMetaObject::invokeMethod(&page, "copyDisplayPageToAll"));
        QCOMPARE(stored(QStringLiteral("DisplayWfLowLevel")), QStringLiteral("-105"));
        QCOMPARE(stored(QStringLiteral("DisplayWfHighLevel")), QStringLiteral("-100"));
    }

    void selectedPageCopyReplacesOpenAndClosedOverrides()
    {
        TwoPanFixture f;
        f.pan1.setLineWidth(2.5f);
        f.pan1.saveSettings();
        AppSettings::instance().setValue(QStringLiteral("DisplayLineWidth_7"), QStringLiteral("3"));
        SpectrumDefaultsPage page(&f.model);
        page.setDisplayProviders(f.providers());
        QVERIFY(QMetaObject::invokeMethod(&page, "setDisplayPanIndex", Q_ARG(int, 1)));
        QVERIFY(QMetaObject::invokeMethod(&page, "copyDisplayPageToAll"));
        QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QStringLiteral("2.5"));
        QCOMPARE(f.pan0.lineWidth(), 2.5f);
        QCOMPARE(f.pan1.lineWidth(), 2.5f);
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayLineWidth"), 1));
        QVERIFY(!PanDisplayKeys::hasOverride(QStringLiteral("DisplayLineWidth"), 7));
    }

    // ---- Display pill plan Task 20: Spectrum Defaults' FFT controls ----
    void fftSizeDragAsksOnceOnRelease();
    void fftSizeKeysAskOnceAfterTheLast();
    void fftSizeSetsEveryEnginesBaselineAndNoRunningSize();
    void sizingModeSetsWhichControlIsLiveAndWhatTheGrayedOnesShow();
    void sizingModeInARemoteWindow();
    void pageOpensOnTheBaselineNotTheRunningSize();
    void storedFftSizeSurvivesARestart_data();
    void storedFftSizeSurvivesARestart();
    void storedFftSizeWithNoModeStartsInAuto();
    void invalidStoredFftSizeFallsBackToTheDefault();
    void windowTargetRateAndPriorityReachEveryStream();
    void setupFftControlsGoThroughTheWindow();
    void readoutLineSaysWhatTheFftRunsAt();
    void readoutLineInARemoteWindowUsesTheGrant();

    void clarityMasterEchoAndSavedState()
    {
        AppSettings::instance().setValue(QStringLiteral("ClarityEnabled"), QStringLiteral("False"));
        int writes = 0;
        AppSettings::instance().setChangeHook([&](const QString& key) {
            if (key == QStringLiteral("ClarityEnabled")) { ++writes; }
        });
        const auto clearHook = qScopeGuard([] { AppSettings::instance().setChangeHook({}); });
        RadioModel model(RadioModel::Role::Remote);
        QCOMPARE(writes, 0);
        QVERIFY(!model.clarityEnabled());
        SpectrumDefaultsPage page(&model);
        QCheckBox* toggle = nullptr;
        for (QCheckBox* box : page.findChildren<QCheckBox*>()) {
            if (box->property("nereusSetupId").toString() == QStringLiteral("display.spectrumDefaults.clarity")) { toggle = box; }
        }
        QVERIFY(toggle); QVERIFY(!toggle->isChecked());
        QSignalSpy changes(&model, &RadioModel::clarityEnabledChanged);
        QSignalSpy echoes(toggle, &QCheckBox::toggled);
        model.setClarityEnabled(true); QCOMPARE(changes.size(), 1); QCOMPARE(echoes.size(), 0);
        QVERIFY(toggle->isChecked()); QCOMPARE(writes, 1);
        toggle->setChecked(false); QCOMPARE(changes.size(), 2); QCOMPARE(echoes.size(), 1);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("ClarityEnabled")).toString(), QStringLiteral("False"));
        model.setClarityEnabled(false); QCOMPARE(changes.size(), 2); QCOMPARE(writes, 2);
    }
    // ---- Display pill plan Task 21: Setup's pages are the defaults ----
    void masterDefaultReachesEveryPan();
    void everyPageAppliesItsDefaultsToEveryPan_data();
    void everyPageAppliesItsDefaultsToEveryPan();
    void pagesShowTheDefaultNotTheActivePansValue();
    void pageFollowsADefaultChangedElsewhereAndWritesNothing();
    void waterfallLevelsGreyByWhoIsSettingThem();
    void waterfallThresholdReadoutsShowTheirWholeValue();
    void waterfallSchemeListsTheEightSchemes();
    void lineWidthIsTenthPixelSteps();
    void appearanceHasSeparateTraceAndFillColours();
    void gridScalesWritesTheBandStoreOfThePanItIsGiven();
    void gridScalesWithNoPanGreysTheBandRowsWithANote();
    void setupInTheAppReachesEveryPan();

    // ---- Fix wave: I7, I9, M1 ----
    void masterDefaultReplacesAPansUnsavedChange();
    void smoothDefaultsAreSetupDefaultsForEveryPan();
    void avgTimeAgreesInSetupAndThePill_data();
    void avgTimeAgreesInSetupAndThePill();
    void smoothDefaultsSetTheTraceColorToWhite();
    void fftRowsFollowAChangeMadeOutsideSetup();
    void fftRowsInARemoteWindowFollowTheStationKeys();

    // ---- Rulings wave R2 ----
    void everyFftSizeChangeSetsTheOffsetAndRestartsTheNoiseFloor();
    void aSizingModeChangeSetsTheOffsetAndRestartsTheNoiseFloorOnce();

    // ---- Display3DSetupPage: a pan's own change, and a default's ----
    void panOwnChangeLeavesThe3DPageOnItsDefault();

    // ---- Display3DSetupPage: page -> model (and, through Task 18, the
    //      widget), one field at a time ----
    void pageChange_reachesModelAndWidget_perField();

    // ---- Display3DSetupPage: Reset button writes the model ----
    void resetButton_writesSixDefaultsOntoModel();

    // ---- WaterfallDefaultsPage: Colour Scheme ----
    void waterfallColorScheme_pushReachesModelAndWidget();
    void waterfallColorScheme_loadShowsTheDefault();
    void waterfallModes_normalUiAndRestoreKeepBothEnabled();

    // ---- SpectrumDefaultsPage: Fill Alpha ----
    void spectrumFillAlpha_pushReachesModelAndWidget();
    void spectrumFillAlpha_loadShowsTheDefault();

    // ---- SpectrumDefaultsPage: Fill trace ----
    void spectrumFillTrace_pushReachesModelAndWidget();
    void spectrumFillTrace_loadShowsTheDefault();
};

// Display pill plan Task 21 rewrote this case. It pinned the 3D page
// following the pan's own model for all six rows; the page now shows the
// Setup defaults (D26), so a pan's own change (the Display pill's) leaves
// five of the six where they were. 3D Floor is kept per band and still
// follows the pan. A default that changes while the page is open is shown
// without the control emitting: see
// pageFollowsADefaultChangedElsewhereAndWritesNothing.
void TestDisplaySetupBinding::panOwnChangeLeavesThe3DPageOnItsDefault()
{
    SpectrumWidget w;
    Display3DSetupPage page(&w);
    DisplaySettingsModel* settings = w.displaySettings();
    WriteCounter counter;

    settings->setSpectrumRenderMode(1);
    QCOMPARE(page.m_modeCombo->currentIndex(), 0);
    settings->setDssGain(33);
    QCOMPARE(page.m_gainSlider->value(), 70);
    settings->setDssRowSpan(15);
    QCOMPARE(page.m_spanSlider->value(), 100);
    settings->setDssAngle(80);
    QCOMPARE(page.m_angleSlider->value(), 50);
    settings->setDssRowDivider(4);
    QCOMPARE(page.m_speedSlider->value(), 0);
    QCOMPARE(page.m_speedValueLabel->text(), QStringLiteral("Match"));
    settings->setThreeDSliceDepth(true);
    QVERIFY(!page.m_sliceShadowCheck->isChecked());
    // The page wrote nothing while the pan moved.
    QCOMPARE(counter.writes, 0);

    // 3D Floor: the pan's own, shown quietly, number cell and all.
    QSignalSpy floorSpy(page.m_floorSlider, &QSlider::valueChanged);
    QSignalSpy modelSpy(settings, &DisplaySettingsModel::dssFloorDepthChanged);
    settings->setDssFloorDepth(12);
    QCOMPARE(page.m_floorSlider->value(), 12);
    QCOMPARE(page.m_floorSpin->value(), 12);
    QCOMPARE(floorSpy.count(), 0);
    QCOMPARE(modelSpy.count(), 1);
}

// A page-driven change on each of the six 3D fields must reach both the
// model directly and, through Task 18's existing SpectrumWidget <->
// DisplaySettingsModel binding, the live widget getter too.
void TestDisplaySetupBinding::pageChange_reachesModelAndWidget_perField()
{
    SpectrumWidget w;
    Display3DSetupPage page(&w);
    DisplaySettingsModel* settings = w.displaySettings();

    page.findChild<QComboBox*>(QStringLiteral("setup3DModeCombo"))->setCurrentIndex(1);
    QCOMPARE(settings->spectrumRenderMode(), 1);
    QCOMPARE(static_cast<int>(w.spectrumRenderMode()), 1);

    page.findChild<QSlider*>(QStringLiteral("setup3DFloorSlider"))->setValue(18);
    QCOMPARE(settings->dssFloorDepth(), 18);
    QCOMPARE(w.dssFloorDepth(), 18);

    // The acceptance's own example: moving the page's slider to 44 must
    // make both the model and the widget read 44.
    page.findChild<QSlider*>(QStringLiteral("setup3DGainSlider"))->setValue(44);
    QCOMPARE(settings->dssGain(), 44);
    QCOMPARE(w.dssGain(), 44);

    page.findChild<QSlider*>(QStringLiteral("setup3DSpanSlider"))->setValue(60);
    QCOMPARE(settings->dssRowSpan(), 60);
    QCOMPARE(w.dssRowSpan(), 60);

    page.findChild<QSlider*>(QStringLiteral("setup3DAngleSlider"))->setValue(15);
    QCOMPARE(settings->dssAngle(), 15);
    QCOMPARE(w.dssAngle(), 15);

    page.findChild<QCheckBox*>(QStringLiteral("setup3DSliceShadowCheck"))->setChecked(true);
    QVERIFY(settings->threeDSliceDepth());
    QVERIFY(w.threeDSliceDepth());
}

void TestDisplaySetupBinding::resetButton_writesSixDefaultsOntoModel()
{
    SpectrumWidget w;
    Display3DSetupPage page(&w);
    DisplaySettingsModel* settings = w.displaySettings();

    // Fix wave M1: the pan is moved off the ship values the way this page
    // moves it, as Setup defaults. (These were the pan's own unsaved
    // changes; a pan's own change, saved or not, is now a value of its own
    // that a Setup default leaves alone, so Reset would not move them.)
    page.m_modeCombo->setCurrentIndex(1);
    settings->setDssFloorDepth(20);
    page.m_gainSlider->setValue(10);
    page.m_spanSlider->setValue(5);
    page.m_angleSlider->setValue(90);
    page.m_sliceShadowCheck->setChecked(true);
    QCOMPARE(settings->spectrumRenderMode(), 1);
    QCOMPARE(settings->dssGain(), 10);
    QCOMPARE(settings->dssRowSpan(), 5);
    QCOMPARE(settings->dssAngle(), 90);
    QVERIFY(settings->threeDSliceDepth());

    page.resetToDefaultsForTest();

    QCOMPARE(settings->spectrumRenderMode(), 0);
    QCOMPARE(settings->dssFloorDepth(), 6);
    QCOMPARE(settings->dssGain(), 70);
    QCOMPARE(settings->dssRowSpan(), 100);
    QCOMPARE(settings->dssAngle(), 50);
    QCOMPARE(settings->threeDSliceDepth(), false);
}

// WaterfallDefaultsPage / SpectrumDefaultsPage are RadioModel-backed pages.
// No existing test constructs either directly (tests/tst_waterfall_defaults_
// changes.cpp exercises SpectrumWidget setters only, despite its name), so
// the fixture below is built per the task brief's fallback: RadioModel,
// SpectrumWidget, radioModel.setSpectrumWidget(&w), then the page.

void TestDisplaySetupBinding::waterfallColorScheme_pushReachesModelAndWidget()
{
    RadioModel radioModel;
    SpectrumWidget w;
    radioModel.setSpectrumWidget(&w);
    WaterfallDefaultsPage page(&radioModel);

    auto* combo = page.findChild<QComboBox*>(QStringLiteral("wfColorSchemeCombo"));
    QVERIFY(combo != nullptr);
    combo->setCurrentIndex(3);  // BlackWhite

    QCOMPARE(w.displaySettings()->wfColorScheme(), 3);
    QCOMPARE(static_cast<int>(w.wfColorScheme()), 3);
}

// Rewritten for Display pill plan Task 21: the combo opens on the Setup
// default, not on the scheme the active pan chose for itself.
void TestDisplaySetupBinding::waterfallColorScheme_loadShowsTheDefault()
{
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayWfColorScheme"), QStringLiteral("5"));
    RadioModel radioModel;
    SpectrumWidget w;
    w.loadSettings();
    radioModel.setSpectrumWidget(&w);
    const auto detach = qScopeGuard([&] { radioModel.setSpectrumWidget(nullptr); });
    w.displaySettings()->setWfColorScheme(2);  // the pan's own, from the Display pill

    WaterfallDefaultsPage page(&radioModel);
    auto* combo = page.findChild<QComboBox*>(QStringLiteral("wfColorSchemeCombo"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->currentIndex(), 5);  // LinRad
    QCOMPARE(w.displaySettings()->wfColorScheme(), 2);
}

// Independent toggles must remain reachable through Setup and persistence.
void TestDisplaySetupBinding::waterfallModes_normalUiAndRestoreKeepBothEnabled()
{
    AppSettings& settings = AppSettings::instance();
    QMap<QString, QVariant> saved;
    for (const QString& key : settings.allKeys()) { saved.insert(key, settings.value(key)); }
    const auto restore = qScopeGuard([&] {
        settings.clear();
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) { settings.setValue(it.key(), it.value()); }
    });
    settings.clear();
    const auto check = [](WaterfallDefaultsPage& page, const QString& id) -> QCheckBox* {
        for (QCheckBox* box : page.findChildren<QCheckBox*>()) {
            if (box->property("nereusSetupId").toString() == id) { return box; }
        }
        return nullptr;
    };
    {
        RadioModel model;
        SpectrumWidget widget;
        widget.loadSettings();
        model.setSpectrumWidget(&widget);
        const auto detach = qScopeGuard([&] { model.setSpectrumWidget(nullptr); });
        WaterfallDefaultsPage page(&model);
        QCheckBox* agc = check(page, QStringLiteral("display.waterfallDefaults.agc"));
        QCheckBox* nf = check(page, QStringLiteral("display.waterfallDefaults.nfAgc"));
        QVERIFY(agc && nf);
        QVERIFY(agc->isChecked());
        QVERIFY(!nf->isChecked());
        QVERIFY(!widget.wfUseSpectrumMinMax());
        QVERIFY(nf->isEnabled());
        nf->click();
        QVERIFY(agc->isChecked() && nf->isChecked());
        QVERIFY(widget.wfAgcEnabled() && widget.waterfallNFAGCEnabled());
        widget.saveSettings();
        // Display pill plan Task 21: the box is the Setup default. NF-AGC
        // was switched on, so the bare "WaterfallNFAGCEnabled" holds it and
        // the pan, now on the default, stores nothing of its own. (Task 1
        // had this in "WaterfallNFAGCEnabled_0", when the box set the
        // active pan only.) AGC was not touched: nothing is stored for it.
        QCOMPARE(settings.value(QStringLiteral("WaterfallNFAGCEnabled")).toString(), QStringLiteral("True"));
        QVERIFY(!settings.contains(QStringLiteral("WaterfallNFAGCEnabled_0")));
        QVERIFY(!settings.contains(QStringLiteral("DisplayWfAgc_0")));
        QVERIFY(!settings.contains(QStringLiteral("DisplayWfAgc")));
        settings.remove(QStringLiteral("WaterfallNFAGCEnabled"));
    }
    // Pan 1's AGC key is explicit; its absent NF key independently inherits
    // the Setup default (it inherited pan 0's own key before Task 1).
    settings.setValue(QStringLiteral("DisplayWfAgc_1"), QStringLiteral("True"));
    settings.setValue(QStringLiteral("WaterfallNFAGCEnabled"), QStringLiteral("True"));
    QVERIFY(!settings.contains(QStringLiteral("WaterfallNFAGCEnabled_1")));
    for (int pan : {0, 1}) {
        RadioModel model;
        SpectrumWidget widget;
        widget.setPanIndex(pan);
        widget.loadSettings();
        model.setSpectrumWidget(&widget);
        const auto detach = qScopeGuard([&] { model.setSpectrumWidget(nullptr); });
        WaterfallDefaultsPage page(&model);
        QCheckBox* agc = check(page, QStringLiteral("display.waterfallDefaults.agc"));
        QCheckBox* nf = check(page, QStringLiteral("display.waterfallDefaults.nfAgc"));
        QVERIFY(agc && nf);
        QVERIFY(agc->isChecked() && nf->isChecked());
        QVERIFY(widget.wfAgcEnabled() && widget.waterfallNFAGCEnabled());
    }
}

// SpectrumDefaultsPage::loadFromRenderer() early-returns unless BOTH
// model()->spectrumWidget() and model()->fftEngine() are non-null (it
// reads FFT size/window/fps alongside the fill controls in one pass). A
// bare RadioModel has no FFTEngine wired up, so a stack-local one is
// constructed here purely to satisfy that guard; its constructor is a
// two-line no-op (see src/core/FFTEngine.cpp) and every getter
// loadFromRenderer() calls is a trivial std::atomic::load() with a sane
// default (fftSize 4096, sampleRate 48000.0, outputFps 30, decimation 1),
// so this cannot crash or hang.
void TestDisplaySetupBinding::spectrumFillAlpha_pushReachesModelAndWidget()
{
    RadioModel radioModel;
    SpectrumWidget w;
    NereusSDR::FFTEngine fftEngine(0);
    radioModel.setSpectrumWidget(&w);
    radioModel.setFftEngine(&fftEngine);
    SpectrumDefaultsPage page(&radioModel);

    auto* slider = page.findChild<QSlider*>(QStringLiteral("specFillAlphaSlider"));
    QVERIFY(slider != nullptr);
    slider->setValue(42);

    QCOMPARE(w.displaySettings()->fillAlpha(), 42 / 100.0f);
    QCOMPARE(w.fillAlpha(), 42 / 100.0f);
}

// Rewritten for Display pill plan Task 21: the slider opens on the Setup
// default, not on the active pan's own fill.
void TestDisplaySetupBinding::spectrumFillAlpha_loadShowsTheDefault()
{
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayFftFillAlpha"), QStringLiteral("0.25"));
    RadioModel radioModel;
    SpectrumWidget w;
    w.loadSettings();
    NereusSDR::FFTEngine fftEngine(0);
    radioModel.setSpectrumWidget(&w);
    radioModel.setFftEngine(&fftEngine);
    const auto detach = qScopeGuard([&] {
        radioModel.setSpectrumWidget(nullptr);
        radioModel.setFftEngine(nullptr);
    });
    w.displaySettings()->setFillAlpha(0.6f);  // the pan's own

    SpectrumDefaultsPage page(&radioModel);
    auto* slider = page.findChild<QSlider*>(QStringLiteral("specFillAlphaSlider"));
    QVERIFY(slider != nullptr);
    QCOMPARE(slider->value(), 25);
    QCOMPARE(w.displaySettings()->fillAlpha(), 0.6f);
}

void TestDisplaySetupBinding::spectrumFillTrace_pushReachesModelAndWidget()
{
    RadioModel radioModel;
    SpectrumWidget w;
    NereusSDR::FFTEngine fftEngine(0);
    radioModel.setSpectrumWidget(&w);
    radioModel.setFftEngine(&fftEngine);
    SpectrumDefaultsPage page(&radioModel);

    auto* toggle = page.findChild<QCheckBox*>(QStringLiteral("specFillToggle"));
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->isChecked());  // ship default: on
    toggle->setChecked(false);

    QVERIFY(!w.displaySettings()->panFill());
    QVERIFY(!w.panFillEnabled());
}

// Rewritten for Display pill plan Task 21: the box opens on the Setup
// default, not on the active pan's own.
void TestDisplaySetupBinding::spectrumFillTrace_loadShowsTheDefault()
{
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayPanFill"), QStringLiteral("False"));
    RadioModel radioModel;
    SpectrumWidget w;
    w.loadSettings();
    NereusSDR::FFTEngine fftEngine(0);
    radioModel.setSpectrumWidget(&w);
    radioModel.setFftEngine(&fftEngine);
    const auto detach = qScopeGuard([&] {
        radioModel.setSpectrumWidget(nullptr);
        radioModel.setFftEngine(nullptr);
    });
    w.displaySettings()->setPanFill(true);  // the pan's own

    SpectrumDefaultsPage page(&radioModel);
    auto* toggle = page.findChild<QCheckBox*>(QStringLiteral("specFillToggle"));
    QVERIFY(toggle != nullptr);
    QVERIFY(!toggle->isChecked());
    QVERIFY(w.displaySettings()->panFill());
}

// ---------------------------------------------------------------------------
// Display pill plan Task 20: Spectrum Defaults' FFT controls
// ---------------------------------------------------------------------------

// A drag across four positions asks the window for one baseline, as the
// handle is let go. Nothing is asked, and no engine's size or baseline
// moves, while the handle is held; the two cells follow the handle.
void TestDisplaySetupBinding::fftSizeDragAsksOnceOnRelease()
{
    // The Fixed sizing mode, the one where the slider is live (design D38).
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    LocalPoolFixture f;
    SpectrumDefaultsPage page(&f.model);
    QList<int> asked;
    SetupFftHandlers handlers;
    handlers.applyBaseline = [&asked](int size) { asked.append(size); };
    page.setFftHandlers(handlers);
    QSlider* size = page.m_fftSizeSlider;
    auto* timer = page.findChild<QTimer*>(QStringLiteral("setupFftSizeKeyTimer"));
    QVERIFY(size && timer);
    QVERIFY(!size->hasTracking());
    QCOMPARE(timer->interval(), 300);
    QCOMPARE(timer->interval(), kDisplayFftSizeKeyDelayMs);
    QVERIFY(timer->isSingleShot());
    QCOMPARE(size->value(), 0);
    f.stream0->setSampleRate(192000.0);
    QSignalSpy replanRequests(&f.widget, &SpectrumWidget::bandwidthChangeRequested);

    size->setSliderDown(true);
    for (int position : {1, 2, 3, 4}) {
        size->setSliderPosition(position);
        QCOMPARE(page.m_fftSizeReadout->text(), QString::number(4096 << position));
        QCOMPARE(page.m_binWidthLabel->text(),
                 QString::number(192000.0 / (4096 << position), 'f', 3));
        QCOMPARE(asked.size(), 0);
        QCOMPARE(size->value(), 0);
        for (NereusSDR::FFTEngine* engine : {f.stream0, f.stream1}) {
            QCOMPARE(engine->fftSize(), 4096);
            QCOMPARE(engine->fftSizeBaseline(), 4096);
        }
        QVERIFY(!timer->isActive());
    }
    size->setSliderDown(false);
    QCOMPARE(asked, (QList<int>{65536}));
    QVERIFY(!timer->isActive());
    // The page asked; it set nothing itself, a running size least of all.
    for (NereusSDR::FFTEngine* engine : {f.stream0, f.stream1}) {
        QCOMPARE(engine->fftSize(), 4096);
        QCOMPARE(engine->fftSizeBaseline(), 4096);
    }
    QCOMPARE(replanRequests.count(), 0);
    QVERIFY(storedFftSize().isEmpty());

    // Pressed and let go where it was: nothing to ask.
    size->setSliderDown(true);
    size->setSliderDown(false);
    QCOMPARE(asked.size(), 1);
}

// Three keyboard steps ask once, 300 ms after the last; steps that end
// where the station already is ask nothing.
void TestDisplaySetupBinding::fftSizeKeysAskOnceAfterTheLast()
{
    // The Fixed sizing mode, the one where the slider is live (design D38).
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    LocalPoolFixture f;
    SpectrumDefaultsPage page(&f.model);
    QList<int> asked;
    SetupFftHandlers handlers;
    // The window's part: the size is stored (what the page compares the
    // slider with, in every sizing mode) and the pool takes the baseline.
    handlers.applyBaseline = [&asked, &f](int size) {
        asked.append(size);
        AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                         QString::number(size));
        f.pool.setFftSizeBaseline(size);
    };
    page.setFftHandlers(handlers);
    QSlider* size = page.m_fftSizeSlider;
    auto* timer = page.findChild<QTimer*>(QStringLiteral("setupFftSizeKeyTimer"));
    QVERIFY(size && timer);

    QElapsedTimer sinceLastKey;
    for (int step = 1; step <= 3; ++step) {
        keyStep(size, Qt::Key_Right);
        sinceLastKey.start();
        QCOMPARE(size->value(), step);
        QCOMPARE(page.m_fftSizeReadout->text(), QString::number(4096 << step));
        QCOMPARE(asked.size(), 0);
        QVERIFY(timer->isActive());
        QCOMPARE(f.stream0->fftSizeBaseline(), 4096);
    }
    QTRY_COMPARE(asked.size(), 1);
    // Qt may fire a 300 ms timer a little early; never a step's width early.
    QVERIFY2(sinceLastKey.elapsed() >= 250, qPrintable(QString::number(sinceLastKey.elapsed())));
    QCOMPARE(asked.at(0), 32768);
    QCOMPARE(f.stream0->fftSizeBaseline(), 32768);
    QCOMPARE(f.stream1->fftSizeBaseline(), 32768);
    QVERIFY(!timer->isActive());

    // Off and back again: the timer runs out and nothing is asked.
    keyStep(size, Qt::Key_Right);
    keyStep(size, Qt::Key_Left);
    QCOMPARE(size->value(), 3);
    QVERIFY(timer->isActive());
    QTRY_VERIFY(!timer->isActive());
    QCOMPARE(asked.size(), 1);
}

// A page with no window behind it, in the Fixed sizing mode, sets the
// baseline of every engine in the pool, writes the key once and asks for one
// replan. It sets no engine's running size.
void TestDisplaySetupBinding::fftSizeSetsEveryEnginesBaselineAndNoRunningSize()
{
    // The Fixed sizing mode, where the FFT Size slider sets the size every
    // stream runs (design D38). In Auto and Hz/bin the slider's size is
    // stored and plays no part.
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    LocalPoolFixture f;
    SpectrumDefaultsPage page(&f.model);
    QSlider* size = page.m_fftSizeSlider;
    QVERIFY(size);
    int keyWrites = 0;
    AppSettings::instance().setChangeHook([&keyWrites](const QString& key) {
        if (key == QLatin1String(ControlRanges::kDisplayFftSizeKey)) { ++keyWrites; }
    });
    const auto clearHook = qScopeGuard([] { AppSettings::instance().setChangeHook({}); });
    QSignalSpy replanRequests(&f.widget, &SpectrumWidget::bandwidthChangeRequested);

    size->setSliderDown(true);
    for (int position : {1, 2, 3, 4}) {
        size->setSliderPosition(position);
        QCOMPARE(f.stream0->fftSizeBaseline(), 4096);
        QCOMPARE(f.stream0->fftSize(), 4096);
    }
    QCOMPARE(keyWrites, 0);
    QCOMPARE(replanRequests.count(), 0);
    size->setSliderDown(false);

    for (NereusSDR::FFTEngine* engine : {f.stream0, f.stream1}) {
        QCOMPARE(engine->fftSizeBaseline(), 65536);
        QCOMPARE(engine->fftSize(), 4096);
    }
    QCOMPARE(f.pool.config().fftSize, 65536);
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));
    QCOMPARE(keyWrites, 1);
    QCOMPARE(replanRequests.count(), 1);
    // A stream made afterwards starts on the baseline too.
    QCOMPARE(f.pool.engineForStream(2)->fftSizeBaseline(), 65536);
}

// "Sizing" (design D38): Auto, Hz/bin, Fixed. The FFT Size slider is live in
// Fixed only and, grayed, shows the size running; the Hz/bin target is live
// in Hz/bin only and, grayed, shows the remembered target (with none, the
// bin width running on the control's step). Each grayed control says which
// choice makes it live. The two cells never show a bin width that is not
// running outside Fixed. While the station's settings are unavailable all
// three are held with the station's reason.
void TestDisplaySetupBinding::sizingModeSetsWhichControlIsLiveAndWhatTheGrayedOnesShow()
{
    auto& settings = AppSettings::instance();
    settings.setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey), QStringLiteral("65536"));
    LocalPoolFixture f;
    f.widget.setSampleRate(192000.0);
    f.stream0->setSampleRate(192000.0);
    f.stream0->setFftSize(8192);
    SpectrumDefaultsPage page(&f.model);
    QComboBox* sizing = page.m_fftSizingCombo;
    QSlider* size = page.m_fftSizeSlider;
    QDoubleSpinBox* target = page.m_hzPerBinTargetSpin;
    QVERIFY(sizing && size && target);
    const auto modeKey = [&settings] {
        return settings.value(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)).toString();
    };
    const auto targetKey = [&settings] {
        return settings.value(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey)).toString();
    };
    const auto choose = [sizing](const char* label) {
        sizing->setCurrentIndex(sizing->findText(QLatin1String(label)));
    };
    const QString sizeHeldInAuto = QStringLiteral(
        "Sizing is Auto, so the zoom sets the FFT size. Choose Fixed to set it yourself.");
    const QString sizeHeldInHzPerBin = QStringLiteral(
        "Sizing is Hz/bin, so the bin width sets the FFT size. Choose Fixed to set it yourself.");
    const QString targetHeld =
        QStringLiteral("Choose Hz/bin under Sizing to set the bin width yourself.");
    const QString targetTip = QStringLiteral(
        "The bin width to hold at every zoom. The FFT size follows it: sample rate divided "
        "by bin width, rounded up to a power of two, within the size limits.");

    // The row and its words.
    QCOMPARE(sizing->count(), 3);
    QCOMPARE(sizing->itemText(0), QStringLiteral("Auto"));
    QCOMPARE(sizing->itemText(1), QStringLiteral("Hz/bin"));
    QCOMPARE(sizing->itemText(2), QStringLiteral("Fixed"));
    QCOMPARE(sizing->toolTip(), QStringLiteral(
        "How the FFT size is chosen. Auto: the zoom picks the size to keep the picture "
        "sharp. Hz/bin: the size follows the bin width you set. Fixed: the FFT Size "
        "slider sets the size and zoom does not change it."));

    // Auto, the mode of a station that never chose one. The slider is grayed
    // on the 8192 that runs, not the stored 65536.
    QCOMPARE(sizing->currentText(), QStringLiteral("Auto"));
    QVERIFY(sizing->isEnabled());
    QVERIFY(!size->isEnabled());
    QCOMPARE(size->toolTip(), sizeHeldInAuto);
    QCOMPARE(size->value(), 1);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("8192"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("23.438"));
    QVERIFY(!target->isEnabled());
    QCOMPARE(target->toolTip(), targetHeld);
    QCOMPARE(target->value(), 23.5);
    QCOMPARE(page.m_fftReadoutLine->text(),
             QStringLiteral("23.44 Hz per bin · 0.04 s per transform at 192 kHz"));

    // The grayed slider is not a request: moved, it stores nothing and goes
    // back to the size that runs.
    {
        WriteCounter counter;
        size->setSliderDown(true);
        size->setSliderPosition(5);
        size->setSliderDown(false);
        QCOMPARE(counter.writes, 0);
    }
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));
    QCOMPARE(size->value(), 1);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("8192"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("23.438"));
    // Nor is the grayed target.
    {
        WriteCounter counter;
        target->setValue(5.0);
        QCOMPARE(counter.writes, 0);
    }
    QCOMPARE(target->value(), 23.5);
    QVERIFY(modeKey().isEmpty());

    // Hz/bin, with no target remembered: it starts from the bin width that
    // runs, so the size does not move (23.5 Hz at 192 kHz plans 8192).
    choose("Hz/bin");
    QCOMPARE(modeKey(), QStringLiteral("HzPerBin"));
    QCOMPARE(targetKey(), QStringLiteral("23.5"));
    QCOMPARE(ControlRanges::planDisplayFft(ControlRanges::DisplayFftSizingMode::HzPerBin, 65536,
                                           192000.0, 192000.0, 23.5).size, 8192);
    QCOMPARE(f.pool.config().sizingMode, ControlRanges::DisplayFftSizingMode::HzPerBin);
    for (NereusSDR::FFTEngine* engine : {f.stream0, f.stream1}) {
        QCOMPARE(engine->hzPerBinTarget(), 23.5);
        QCOMPARE(engine->fftSizeBaseline(), 4096);
    }
    QVERIFY(target->isEnabled());
    QCOMPARE(target->toolTip(), targetTip);
    QCOMPARE(target->value(), 23.5);
    QVERIFY(!size->isEnabled());
    QCOMPARE(size->toolTip(), sizeHeldInHzPerBin);
    QCOMPARE(size->value(), 1);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("8192"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("23.438"));
    target->setValue(12.5);
    QCOMPARE(targetKey(), QStringLiteral("12.5"));
    QCOMPARE(f.stream1->hzPerBinTarget(), 12.5);

    // Fixed: the slider is live on the stored size, and the two cells are
    // that size and its width. The target is grayed on the one remembered.
    choose("Fixed");
    QCOMPARE(modeKey(), QStringLiteral("Fixed"));
    QCOMPARE(targetKey(), QStringLiteral("0"));
    QVERIFY(size->isEnabled());
    QVERIFY(size->toolTip().startsWith(QStringLiteral("FFT size used for spectrum analysis.")));
    QCOMPARE(size->value(), 4);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("65536"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("2.930"));
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));
    for (NereusSDR::FFTEngine* engine : {f.stream0, f.stream1}) {
        QCOMPARE(engine->fftSizeBaseline(), 65536);
        QCOMPARE(engine->hzPerBinTarget(), 0.0);
    }
    QVERIFY(!target->isEnabled());
    QCOMPARE(target->toolTip(), targetHeld);
    QCOMPARE(target->value(), 12.5);

    // Hz/bin again: the remembered target, not the bin width running.
    choose("Hz/bin");
    QCOMPARE(targetKey(), QStringLiteral("12.5"));
    QVERIFY(target->isEnabled());
    QCOMPARE(target->value(), 12.5);
    QCOMPARE(f.stream0->hzPerBinTarget(), 12.5);

    // Auto again: the slider leaves the stored size for the running one.
    choose("Auto");
    QCOMPARE(modeKey(), QStringLiteral("Auto"));
    QVERIFY(!size->isEnabled());
    QCOMPARE(size->toolTip(), sizeHeldInAuto);
    QCOMPARE(size->value(),
             ControlRanges::displayFftSizePosition(f.stream0->fftSize()));
    QCOMPARE(page.m_fftSizeReadout->text(), QString::number(f.stream0->fftSize()));
    QCOMPARE(page.m_binWidthLabel->text(),
             QString::number(192000.0 / f.stream0->fftSize(), 'f', 3));
    QCOMPARE(target->value(), 12.5);
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));

    // The station's settings unavailable: its reason wins on all three, in
    // every mode, and the mode's own reasons come back with the settings.
    const QString reason = QStringLiteral("Waiting for the Core's settings.");
    for (const char* mode : {"Auto", "Hz/bin", "Fixed"}) {
        choose(mode);
        page.setStationSettingsAvailable(false, reason);
        for (QWidget* control : {static_cast<QWidget*>(sizing), static_cast<QWidget*>(size),
                                 static_cast<QWidget*>(target)}) {
            QVERIFY2(!control->isEnabled(), mode);
            QCOMPARE(control->toolTip(), reason);
        }
        page.setStationSettingsAvailable(true, QString());
        QVERIFY2(sizing->isEnabled(), mode);
        QCOMPARE(sizing->currentText(), QLatin1String(mode));
        QCOMPARE(size->isEnabled(), QLatin1String(mode) == QLatin1String("Fixed"));
        QCOMPARE(target->isEnabled(), QLatin1String(mode) == QLatin1String("Hz/bin"));
    }
    QCOMPARE(target->toolTip(), targetHeld);
    QVERIFY(size->toolTip().startsWith(QStringLiteral("FFT size used for spectrum analysis.")));
}

// A remote window: Sizing writes the station keys its Core follows, the
// grayed slider and the two cells show the size the Core granted the pan,
// and the Core's settings being unavailable holds all three.
void TestDisplaySetupBinding::sizingModeInARemoteWindow()
{
    RadioModel remote(RadioModel::Role::Remote);
    SpectrumWidget widget;
    NereusSDR::FFTEngine idle(-1);  // a remote window's own engine does not run
    remote.setSpectrumWidget(&widget);
    remote.setFftEngine(&idle);
    const auto detach = qScopeGuard([&] {
        remote.setSpectrumWidget(nullptr);
        remote.setFftEngine(nullptr);
    });
    auto& settings = AppSettings::instance();
    settings.setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey), QStringLiteral("8192"));
    SpectrumDefaultsPage page(&remote);
    const QString reason = QStringLiteral("Waiting for the Core's settings.");
    page.setStationSettingsAvailable(false, reason);
    QComboBox* sizing = page.m_fftSizingCombo;
    QSlider* size = page.m_fftSizeSlider;
    QDoubleSpinBox* target = page.m_hzPerBinTargetSpin;
    for (QWidget* control : {static_cast<QWidget*>(sizing), static_cast<QWidget*>(size),
                             static_cast<QWidget*>(target)}) {
        QVERIFY(!control->isEnabled());
        QCOMPARE(control->toolTip(), reason);
    }
    page.setStationSettingsAvailable(true, QString());
    QVERIFY(sizing->isEnabled());
    QCOMPARE(sizing->currentText(), QStringLiteral("Auto"));

    // The Core grants the pan 65536 points at 192 kHz.
    SpectrumEndpointContext context;
    context.codec = {51, 1, -180, 0, 128, 128, 0};
    context.exactCentreHz = 14225000;
    context.exactSpanHz = 12000;
    context.targetFps = 25;
    widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000, 65536);
    QCOMPARE(widget.remoteGrantedFftSize(), 65536);
    const QString running = QStringLiteral("2.93 Hz per bin · 0.34 s per transform at 192 kHz");
    QVERIFY(!size->isEnabled());
    QCOMPARE(size->value(), 4);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("65536"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("2.930"));
    QCOMPARE(page.m_fftReadoutLine->text(), running);
    QCOMPARE(target->value(), 3.0);

    // Hz/bin: the keys the Core follows, from the bin width it runs.
    sizing->setCurrentIndex(sizing->findText(QStringLiteral("Hz/bin")));
    QCOMPARE(settings.value(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)).toString(),
             QStringLiteral("HzPerBin"));
    QCOMPARE(settings.value(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey)).toString(),
             QStringLiteral("3"));
    QVERIFY(target->isEnabled());
    QVERIFY(!size->isEnabled());
    QCOMPARE(page.m_fftReadoutLine->text(), running);

    // Fixed: the slider is live on the stored 8192; the cells and the line
    // stay on what the Core runs until it grants the new size.
    sizing->setCurrentIndex(sizing->findText(QStringLiteral("Fixed")));
    QCOMPARE(settings.value(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)).toString(),
             QStringLiteral("Fixed"));
    QVERIFY(size->isEnabled());
    QCOMPARE(size->value(), 1);
    QVERIFY(!target->isEnabled());
    QCOMPARE(target->value(), 3.0);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("65536"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("2.930"));
    QCOMPARE(page.m_fftReadoutLine->text(), running);
    widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000, 8192);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("8192"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("23.438"));
    QCOMPARE(page.m_fftReadoutLine->text(),
             QStringLiteral("23.44 Hz per bin · 0.04 s per transform at 192 kHz"));
}

// The slider shows the baseline, the size it set, and not the size zoom
// made of it (issue #296: a 4096 baseline at a quarter of the span runs
// 16384, and the page showed 16384).
void TestDisplaySetupBinding::pageOpensOnTheBaselineNotTheRunningSize()
{
    // The slider's size is the stored one; in Fixed it is every engine's
    // baseline too.
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                     QStringLiteral("8192"));
    // The Fixed sizing mode, where the FFT Size slider sets the size every
    // stream runs (design D38). In Auto and Hz/bin the slider's size is
    // stored and plays no part.
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    LocalPoolFixture f;
    f.pool.setSizing(ControlRanges::DisplayFftSizingMode::Fixed, 8192, 0.0);
    f.stream0->setSampleRate(192000.0);
    f.stream0->setFftSize(65536);
    QCOMPARE(f.stream0->fftSizeBaseline(), 8192);
    QCOMPARE(f.stream0->fftSize(), 65536);

    SpectrumDefaultsPage page(&f.model);
    QCOMPARE(page.m_fftSizeSlider->value(), 1);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("8192"));
    QCOMPARE(page.m_binWidthLabel->text(), QString::number(192000.0 / 8192, 'f', 3));
    // Opening the page changed nothing.
    QCOMPARE(f.stream0->fftSizeBaseline(), 8192);
    QCOMPARE(f.stream0->fftSize(), 65536);
}

void TestDisplaySetupBinding::storedFftSizeSurvivesARestart_data()
{
    QTest::addColumn<int>("position");
    for (int position = 0; position <= ControlRanges::kDisplayFftSizePositionMax; ++position) {
        QTest::newRow(qPrintable(QString::number(ControlRanges::displayFftSizeAt(position))))
            << position;
    }
}

// Issue #296, at every slider position: with the size stored and the Fixed
// sizing mode, a fresh window's engines start on it and Setup's slider shows
// it, at full span and zoomed to a quarter of it, and the key is left as it
// was. (Before the sizing mode the stored size was a floor a zoom
// multiplied; in Fixed a zoom changes nothing, design D38.)
void TestDisplaySetupBinding::storedFftSizeSurvivesARestart()
{
    QFETCH(int, position);
    const int stored = ControlRanges::displayFftSizeAt(position);
    Test::markAudioFirstRunDone();
    AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                     QString::number(stored));
    // The Fixed sizing mode, where the FFT Size slider sets the size every
    // stream runs (design D38). In Auto and Hz/bin the slider's size is
    // stored and plays no part.
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    QTest::failOnWarning(QRegularExpression(QStringLiteral("is not an FFT size")));

    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    RadioModel* model = window.radioModel();
    FftEnginePool* pool = window.fftEnginePoolForTest();
    QVERIFY(pool);
    QCOMPARE(pool->config().fftSize, stored);
    const int stream = model->sliceById(0)->streamIndex();
    QVERIFY(pool->streams().contains(stream));
    NereusSDR::FFTEngine* engine = pool->engineForStream(stream);
    QCOMPARE(engine->fftSizeBaseline(), stored);
    QCOMPARE(model->fftEngine()->fftSizeBaseline(), stored);
    PanadapterStack* stack = window.findChild<PanadapterStack*>();
    QVERIFY(stack);
    SpectrumWidget* primary = stack->spectrum(QStringLiteral("pan-0"));
    QVERIFY(primary);
    const double rate = primary->sampleRate();
    QVERIFY(rate > 0.0);

    // Full span.
    primary->setFrequencyRange(primary->centerFrequency(), rate);
    primary->requestAutoZoomReplan();
    QCOMPARE(engine->fftSize(), stored);
    {
        SpectrumDefaultsPage page(model);
        QCOMPARE(page.m_fftSizeSlider->value(), position);
        QCOMPARE(page.m_fftSizeReadout->text(), QString::number(stored));
    }

    // A quarter of the span: the engine still runs the stored size, and
    // the slider shows it.
    primary->setFrequencyRange(primary->centerFrequency(), rate / 4.0);
    primary->requestAutoZoomReplan();
    QCOMPARE(engine->fftSize(), stored);
    {
        SpectrumDefaultsPage page(model);
        QCOMPARE(page.m_fftSizeSlider->value(), position);
        QCOMPARE(page.m_fftSizeReadout->text(), QString::number(stored));
    }
    QCOMPARE(engine->fftSizeBaseline(), stored);
    QCOMPARE(storedFftSize(), QString::number(stored));
}

// The upgrade (design D40): a station that stored a size and never a mode
// starts in Auto. Its engines start on 4096 whatever the size, a zoom plans
// from 4096, Setup's slider is grayed on the size that runs, the stored size
// is kept for Fixed, and no key is written for it.
void TestDisplaySetupBinding::storedFftSizeWithNoModeStartsInAuto()
{
    Test::markAudioFirstRunDone();
    auto& settings = AppSettings::instance();
    settings.setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
    settings.setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey), QStringLiteral("65536"));
    QVERIFY(!settings.contains(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)));

    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    RadioModel* model = window.radioModel();
    FftEnginePool* pool = window.fftEnginePoolForTest();
    QVERIFY(pool);
    QCOMPARE(pool->config().sizingMode, ControlRanges::DisplayFftSizingMode::Auto);
    QCOMPARE(pool->config().fftSize, 4096);
    NereusSDR::FFTEngine* engine = pool->engineForStream(model->sliceById(0)->streamIndex());
    QCOMPARE(engine->fftSizeBaseline(), 4096);
    QCOMPARE(engine->fftSizeOffsetDb(), 0.0);
    PanadapterStack* stack = window.findChild<PanadapterStack*>();
    QVERIFY(stack);
    SpectrumWidget* primary = stack->spectrum(QStringLiteral("pan-0"));
    QVERIFY(primary);
    const double rate = primary->sampleRate();
    primary->setFrequencyRange(primary->centerFrequency(), rate);
    primary->requestAutoZoomReplan();
    QCOMPARE(engine->fftSize(), 4096);
    primary->setFrequencyRange(primary->centerFrequency(), rate / 4.0);
    primary->requestAutoZoomReplan();
    QCOMPARE(engine->fftSize(), 16384);
    {
        SpectrumDefaultsPage page(model);
        QVERIFY(!page.m_fftSizeSlider->isEnabled());
        QCOMPARE(page.m_fftSizeSlider->value(), 2);
        QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("16384"));
    }
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));
    QVERIFY(!settings.contains(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)));
}

// A stored size that is not one of the seven: the engines start on 4096,
// the key is put right, and it is said once.
void TestDisplaySetupBinding::invalidStoredFftSizeFallsBackToTheDefault()
{
    Test::markAudioFirstRunDone();
    AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                     QStringLiteral("5000"));
    // One warning is expected; a second of either kind fails the test.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(
        "Stored DisplayFftSize 5000 is not an FFT size; using 4096")));
    QTest::failOnWarning(QRegularExpression(QStringLiteral("is not an FFT size")));
    QTest::failOnWarning(QRegularExpression(QStringLiteral("FFT size baseline")));

    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    RadioModel* model = window.radioModel();
    FftEnginePool* pool = window.fftEnginePoolForTest();
    QVERIFY(pool);
    QCOMPARE(ControlRanges::kDisplayFftSizeDefault, 4096);
    QCOMPARE(pool->config().fftSize, 4096);
    const QList<int> streams = pool->streams();
    QVERIFY(!streams.isEmpty());
    for (int stream : streams) {
        QCOMPARE(pool->engineForStream(stream)->fftSizeBaseline(), 4096);
    }
    QCOMPARE(storedFftSize(), QStringLiteral("4096"));
    SpectrumDefaultsPage page(model);
    QCOMPARE(page.m_fftSizeSlider->value(), 0);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("4096"));
}

// Window, Hz/bin target, FPS and thread priority act on the pool: the
// second stream's engine takes each, and so does a stream made afterwards.
void TestDisplaySetupBinding::windowTargetRateAndPriorityReachEveryStream()
{
    LocalPoolFixture f;
    SpectrumDefaultsPage page(&f.model);
    auto& settings = AppSettings::instance();

    page.m_windowCombo->setCurrentIndex(2);  // Hann
    // The target is live in the Hz/bin sizing mode (design D38).
    page.m_fftSizingCombo->setCurrentIndex(
        page.m_fftSizingCombo->findText(QStringLiteral("Hz/bin")));
    page.m_hzPerBinTargetSpin->setValue(2.5);
    page.m_fpSlider->setValue(20);
    QCOMPARE(page.m_threadPriorityCombo->currentIndex(), 3);
    page.m_threadPriorityCombo->setCurrentIndex(4);  // Highest

    NereusSDR::FFTEngine* stream2 = f.pool.engineForStream(2);
    for (NereusSDR::FFTEngine* engine : {f.stream0, f.stream1, stream2}) {
        QCOMPARE(static_cast<int>(engine->windowFunction()), 2);
        QCOMPARE(engine->hzPerBinTarget(), 2.5);
        QCOMPARE(engine->outputFps(), 20);
    }
    QCOMPARE(f.pool.threadPriority(),
             std::optional<QThread::Priority>(QThread::HighestPriority));
    QCOMPARE(settings.value(QLatin1String(ControlRanges::kDisplayFftWindowKey)).toString(),
             QStringLiteral("2"));
    QCOMPARE(settings.value(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey)).toString(),
             QStringLiteral("2.5"));
    QCOMPARE(settings.value(QLatin1String(ControlRanges::kDisplaySpectrumFpsKey)).toString(),
             QStringLiteral("20"));
    QCOMPARE(page.m_fpSpin->value(), 20);

    // With a window behind the page, each goes to the window instead.
    QStringList asked;
    SetupFftHandlers handlers;
    handlers.applyWindowType = [&asked](int v) { asked << QStringLiteral("window %1").arg(v); };
    handlers.applyHzPerBinTarget =
        [&asked](double v) { asked << QStringLiteral("target %1").arg(v); };
    handlers.applyFps = [&asked](int v) { asked << QStringLiteral("fps %1").arg(v); };
    handlers.applyDecimation = [&asked](int v) { asked << QStringLiteral("decimation %1").arg(v); };
    page.setFftHandlers(handlers);
    page.m_windowCombo->setCurrentIndex(4);
    page.m_hzPerBinTargetSpin->setValue(7.5);
    page.m_fpSlider->setValue(45);
    page.m_decimationSpin->setValue(3);
    QCOMPARE(asked, (QStringList{QStringLiteral("window 4"), QStringLiteral("target 7.5"),
                                 QStringLiteral("fps 45"), QStringLiteral("decimation 3")}));
    QCOMPARE(static_cast<int>(f.stream1->windowFunction()), 2);
    QCOMPARE(f.stream1->hzPerBinTarget(), 2.5);
    QCOMPARE(f.stream1->outputFps(), 20);
    QCOMPARE(f.stream1->decimation(), 1);
}

// In the app: Setup's FFT controls go through the window, so every stream's
// engine and both pans' Display pills follow, and a drag replans once.
void TestDisplaySetupBinding::setupFftControlsGoThroughTheWindow()
{
    Test::markAudioFirstRunDone();
    AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
    // The Fixed sizing mode, where the FFT Size slider sets the size every
    // stream runs (design D38). In Auto and Hz/bin the slider's size is
    // stored and plays no part.
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    RadioModel* model = window.radioModel();
    QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                      Q_ARG(QString, QStringLiteral("2v"))));
    PanadapterStack* stack = window.findChild<PanadapterStack*>();
    QVERIFY(stack);
    SpectrumWidget* secondary = stack->spectrum(QStringLiteral("pan-1"));
    QVERIFY(secondary);
    QTRY_VERIFY(secondary->vfoWidget(1));
    FftEnginePool* pool = window.fftEnginePoolForTest();
    QVERIFY(pool);
    const QList<int> streams = pool->streams();
    QVERIFY(streams.size() >= 2);
    for (PanadapterApplet* applet : stack->allApplets()) {
        SpectrumWidget* sw = stack->spectrum(applet->panId());
        QVERIFY(sw);
        // Each pan carries its stream's rate, as it does once I/Q arrives
        // (the Hz/bin plan below is made from it).
        sw->setSampleRate(192000.0);
        sw->setFrequencyRange(sw->centerFrequency(), sw->sampleRate());
        sw->requestAutoZoomReplan();
    }
    for (int stream : streams) {
        QCOMPARE(pool->engineForStream(stream)->fftSize(), 4096);
    }
    const QList<SpectrumOverlayPanel*> pills = window.findChildren<SpectrumOverlayPanel*>();
    QVERIFY(pills.size() >= 2);

    QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
    auto* dialog = window.findChild<SetupDialog*>();
    QVERIFY(dialog);
    const auto closeDialog = qScopeGuard([dialog] { delete dialog; });
    QWidget* realized = dialog->realizePageForTest(QStringLiteral("FFT & Engine"));
    QVERIFY(realized);
    auto* page = qobject_cast<SpectrumDefaultsPage*>(realized);
    if (!page) { page = dialog->findChild<SpectrumDefaultsPage*>(); }
    if (page) { QVERIFY(realized->isAncestorOf(page->m_fftSizeSlider)); }
    QVERIFY(page);
    QCOMPARE(page->m_fftSizeSlider->value(), 0);
    const QString rateKhz = QString::number(secondary->sampleRate() / 1000.0, 'g', 6);
    const auto lineFor = [&](int runningSize) {
        return displayFftReadoutText({runningSize, model->fftEngine()->sampleRate(), 1});
    };
    QCOMPARE(page->m_fftReadoutLine->text(), lineFor(4096));
    QVERIFY(page->m_fftReadoutLine->text().endsWith(QStringLiteral("at %1 kHz").arg(rateKhz)));

    // A drag to 16384: nothing moves while the handle is held.
    int keyWrites = 0;
    AppSettings::instance().setChangeHook([&keyWrites](const QString& key) {
        if (key == QLatin1String(ControlRanges::kDisplayFftSizeKey)) { ++keyWrites; }
    });
    const auto clearHook = qScopeGuard([] { AppSettings::instance().setChangeHook({}); });
    QSlider* size = page->m_fftSizeSlider;
    size->setSliderDown(true);
    for (int position : {4, 1, 3, 2}) {
        size->setSliderPosition(position);
        for (int stream : streams) {
            QCOMPARE(pool->engineForStream(stream)->fftSize(), 4096);
            QCOMPARE(pool->engineForStream(stream)->fftSizeBaseline(), 4096);
        }
    }
    QCOMPARE(keyWrites, 0);
    size->setSliderDown(false);
    QCOMPARE(keyWrites, 1);
    QCOMPARE(storedFftSize(), QStringLiteral("16384"));
    QCOMPARE(pool->config().fftSize, 16384);
    for (int stream : streams) {
        QCOMPARE(pool->engineForStream(stream)->fftSizeBaseline(), 16384);
        QCOMPARE(pool->engineForStream(stream)->fftSize(), 16384);
    }
    for (SpectrumOverlayPanel* pill : pills) {
        QCOMPARE(pill->fftState().baselineSize, 16384);
        QCOMPARE(pill->fftState().runningSize, 16384);
    }
    QCOMPARE(page->m_fftReadoutLine->text(), lineFor(16384));

    // Window, Hz/bin target and FPS: every engine, both pills.
    page->m_windowCombo->setCurrentIndex(2);
    page->m_fpSlider->setValue(20);
    for (int stream : streams) {
        QCOMPARE(static_cast<int>(pool->engineForStream(stream)->windowFunction()), 2);
        QCOMPARE(pool->engineForStream(stream)->outputFps(), 20);
    }
    for (SpectrumOverlayPanel* pill : pills) {
        QCOMPARE(pill->fftState().windowType, 2);
        QCOMPARE(pill->fftState().fps, 20);
    }
    // Sizing to Hz/bin: with no target remembered it starts from the bin
    // width running (192 kHz over 16384, 11.72 Hz, on the 0.5 Hz step), so
    // no stream changes size (design D39). Every engine and both pills.
    using ControlRanges::DisplayFftSizingMode;
    QComboBox* sizing = page->m_fftSizingCombo;
    QVERIFY(sizing);
    QCOMPARE(sizing->currentText(), QStringLiteral("Fixed"));
    QVERIFY(!page->m_hzPerBinTargetSpin->isEnabled());
    sizing->setCurrentIndex(sizing->findText(QStringLiteral("Hz/bin")));
    QCOMPARE(AppSettings::instance()
                 .value(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)).toString(),
             QStringLiteral("HzPerBin"));
    QCOMPARE(pool->config().sizingMode, DisplayFftSizingMode::HzPerBin);
    QVERIFY(page->m_hzPerBinTargetSpin->isEnabled());
    QVERIFY(!page->m_fftSizeSlider->isEnabled());
    QCOMPARE(page->m_hzPerBinTargetSpin->value(), 12.0);
    for (int stream : streams) {
        QCOMPARE(pool->engineForStream(stream)->hzPerBinTarget(), 12.0);
        QCOMPARE(pool->engineForStream(stream)->fftSize(), 16384);
    }
    QCOMPARE(page->m_fftReadoutLine->text(), lineFor(16384));
    QCOMPARE(page->m_fftSizeReadout->text(), QStringLiteral("16384"));
    for (SpectrumOverlayPanel* pill : pills) {
        QCOMPARE(pill->fftState().sizingMode, DisplayFftSizingMode::HzPerBin);
        QCOMPARE(pill->fftState().baselineSize, 16384);
    }

    // The target, now live: every engine, both pills, and the line and the
    // two cells are what runs at it. 1 Hz at 192 kHz asks for 262144 points
    // and the size limit (about 0.7 s per transform) gives 131072, so the
    // cells say 1.465 Hz, the width that runs, not the 1 Hz asked for.
    page->m_hzPerBinTargetSpin->setValue(1.0);
    for (int stream : streams) {
        QCOMPARE(pool->engineForStream(stream)->hzPerBinTarget(), 1.0);
        QCOMPARE(pool->engineForStream(stream)->fftSize(), 131072);
    }
    for (SpectrumOverlayPanel* pill : pills) {
        QCOMPARE(pill->fftState().hzPerBinTarget, 1.0);
        QCOMPARE(pill->fftState().runningSize, 131072);
    }
    QCOMPARE(page->m_fftReadoutLine->text(), lineFor(131072));
    QCOMPARE(page->m_fftSizeReadout->text(), QStringLiteral("131072"));
    QCOMPARE(page->m_binWidthLabel->text(), QStringLiteral("1.465"));
    QCOMPARE(page->m_fftSizeSlider->value(), 5);
    QCOMPARE(AppSettings::instance()
                 .value(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey)).toString(),
             QStringLiteral("1"));

    // Sizing to Auto: the target is no longer in force and is remembered.
    sizing->setCurrentIndex(sizing->findText(QStringLiteral("Auto")));
    QCOMPARE(AppSettings::instance()
                 .value(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)).toString(),
             QStringLiteral("Auto"));
    QCOMPARE(pool->config().sizingMode, DisplayFftSizingMode::Auto);
    for (int stream : streams) {
        QCOMPARE(pool->engineForStream(stream)->hzPerBinTarget(), 0.0);
        QCOMPARE(pool->engineForStream(stream)->fftSizeBaseline(), 4096);
    }
    for (SpectrumOverlayPanel* pill : pills) {
        QCOMPARE(pill->fftState().sizingMode, DisplayFftSizingMode::Auto);
        QCOMPARE(pill->fftState().lastHzPerBinTarget, 1.0);
    }
    QVERIFY(!page->m_hzPerBinTargetSpin->isEnabled());
    QCOMPARE(page->m_hzPerBinTargetSpin->value(), 1.0);

    // A pill's Sizing buttons and Setup's Sizing follow each other.
    auto* pillFixed = pills.first()->parentWidget()->findChild<QPushButton*>(
        QStringLiteral("pillDisplayFftSizingModeFixed"));
    QVERIFY(pillFixed);
    pillFixed->click();
    QCOMPARE(sizing->currentText(), QStringLiteral("Fixed"));
    QVERIFY(page->m_fftSizeSlider->isEnabled());
    QCOMPARE(page->m_fftSizeSlider->value(), 2);  // the stored 16384 again
    for (SpectrumOverlayPanel* pill : pills) {
        QCOMPARE(pill->fftState().sizingMode, DisplayFftSizingMode::Fixed);
    }
    sizing->setCurrentIndex(sizing->findText(QStringLiteral("Hz/bin")));
    QCOMPARE(page->m_hzPerBinTargetSpin->value(), 1.0);  // the remembered target
    for (SpectrumOverlayPanel* pill : pills) {
        QWidget* host = pill->parentWidget();
        QVERIFY(host->findChild<QPushButton*>(QStringLiteral("pillDisplayFftSizingModeHzPerBin"))
                    ->isChecked());
        QVERIFY(!host->findChild<QPushButton*>(QStringLiteral("pillDisplayFftSizingModeFixed"))
                     ->isChecked());
    }
}

// The line under the slider: bin width and time per transform at the size
// the FFT runs, or that the Hz/bin target is what set it.
void TestDisplaySetupBinding::readoutLineSaysWhatTheFftRunsAt()
{
    LocalPoolFixture f;
    f.widget.setSampleRate(192000.0);
    f.widget.setFrequencyRange(f.widget.centerFrequency(), 192000.0);
    f.stream0->setSampleRate(192000.0);
    // The slider's size is the stored one (design D38).
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                     QStringLiteral("131072"));
    f.pool.setFftSizeBaseline(131072);
    f.stream0->setFftSize(131072);
    SpectrumDefaultsPage page(&f.model);
    QLabel* line = page.findChild<QLabel*>(QStringLiteral("setupFftReadout"));
    QVERIFY(line);
    QCOMPARE(line, page.m_fftReadoutLine);
    QCOMPARE(line->text(),
             QStringLiteral("1.46 Hz per bin · 0.68 s per transform at 192 kHz"));

    // Zoomed: the running size, not the slider's.
    f.stream0->setFftSize(262144);
    f.widget.setFrequencyRange(f.widget.centerFrequency(), 96000.0);
    f.widget.requestAutoZoomReplan();
    QTRY_COMPARE(line->text(),
                 QStringLiteral("0.73 Hz per bin · 1.37 s per transform at 192 kHz"));
    // Sizing is Auto, so the grayed slider and the two cells show the size
    // running and its bin width (design D38), not the stored 131072.
    QTRY_COMPARE(page.m_fftSizeSlider->value(), 6);
    QVERIFY(!page.m_fftSizeSlider->isEnabled());
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("262144"));
    QCOMPARE(page.m_binWidthLabel->text(), QStringLiteral("0.732"));
    QCOMPARE(storedFftSize(), QStringLiteral("131072"));

    // Decimation divides the rate the transform sees.
    page.m_decimationSpin->setValue(2);
    QCOMPARE(line->text(),
             QStringLiteral("0.37 Hz per bin · 2.73 s per transform at 192 kHz"));
    page.m_decimationSpin->setValue(1);

    // The Hz/bin mode: the line is still the running numbers, never a
    // sentence about the target.
    f.widget.setFrequencyRange(f.widget.centerFrequency(), 192000.0);
    page.m_fftSizingCombo->setCurrentIndex(
        page.m_fftSizingCombo->findText(QStringLiteral("Hz/bin")));
    page.m_hzPerBinTargetSpin->setValue(1.0);
    QCOMPARE(line->text(), displayFftReadoutText({f.stream0->fftSize(), 192000.0, 1}));
    QVERIFY(line->text().endsWith(QStringLiteral("at 192 kHz")));
    page.m_fftSizingCombo->setCurrentIndex(
        page.m_fftSizingCombo->findText(QStringLiteral("Fixed")));
    QCOMPARE(line->text(), displayFftReadoutText({f.stream0->fftSize(), 192000.0, 1}));

    // With a window behind the page, the window's facts are the line, in
    // every sizing mode. 1.536 MHz zoomed to 35 kHz runs 262144 points.
    SetupFftHandlers handlers;
    handlers.readoutFacts = [] { return DisplayFftReadoutFacts{65536, 384000.0, 1}; };
    page.setFftHandlers(handlers);
    QCOMPARE(line->text(), displayFftReadoutText({65536, 384000.0, 1}));
    QCOMPARE(line->text(),
             QStringLiteral("5.86 Hz per bin · 0.17 s per transform at 384 kHz"));
    handlers.readoutFacts = [] { return DisplayFftReadoutFacts{262144, 1536000.0, 1}; };
    for (const char* mode : {"Auto", "Hz/bin", "Fixed"}) {
        page.m_fftSizingCombo->setCurrentIndex(
            page.m_fftSizingCombo->findText(QLatin1String(mode)));
        page.setFftHandlers(handlers);
        QCOMPARE(line->text(),
                 QStringLiteral("5.86 Hz per bin · 0.17 s per transform at 1536 kHz"));
    }
}

// A remote window's line is written from the size its Core granted the pan.
void TestDisplaySetupBinding::readoutLineInARemoteWindowUsesTheGrant()
{
    RadioModel remote(RadioModel::Role::Remote);
    SpectrumWidget widget;
    NereusSDR::FFTEngine idle(-1);  // a remote window's own engine does not run
    remote.setSpectrumWidget(&widget);
    remote.setFftEngine(&idle);
    const auto detach = qScopeGuard([&] {
        remote.setSpectrumWidget(nullptr);
        remote.setFftEngine(nullptr);
    });
    SpectrumDefaultsPage page(&remote);
    page.setStationSettingsAvailable(true, QString());
    // No grant yet: nothing to state.
    QCOMPARE(widget.remoteGrantedFftSize(), 0);
    QVERIFY(page.m_fftReadoutLine->text().isEmpty());

    SpectrumEndpointContext context;
    context.codec = {51, 1, -180, 0, 128, 128, 0};
    context.exactCentreHz = 14225000;
    context.exactSpanHz = 12000;
    context.targetFps = 25;
    widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000, 65536);
    QCOMPARE(widget.remoteGrantedFftSize(), 65536);
    QCOMPARE(page.m_fftReadoutLine->text(),
             displayFftReadoutText({65536, widget.sampleRate(), 1}));
    QCOMPARE(page.m_fftReadoutLine->text(),
             QStringLiteral("2.93 Hz per bin · 0.34 s per transform at 192 kHz"));
}

// ---------------------------------------------------------------------------
// Display pill plan Task 21: Setup's Display pages are the defaults
// ---------------------------------------------------------------------------

// The task's first acceptance. Pan 1 has an opacity of its own, 60. Setup's
// opacity goes to 90: the master holds 90 and both pans show it without
// keeping their previous override.
void TestDisplaySetupBinding::masterDefaultReachesEveryPan()
{
    AppSettings::instance().setValue(QStringLiteral("DisplayWfOpacity_1"), QStringLiteral("60"));
    TwoPanFixture f;
    QCOMPARE(f.pan0.wfOpacity(), 100);
    QCOMPARE(f.pan1.wfOpacity(), 60);
    WaterfallDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());
    QCOMPARE(page.m_opacitySlider->value(), 100);

    page.m_opacitySlider->setValue(90);

    QCOMPARE(stored(QStringLiteral("DisplayWfOpacity")), QStringLiteral("90"));
    QCOMPARE(f.pan0.wfOpacity(), 90);
    QCOMPARE(f.pan1.wfOpacity(), 90);
    f.pan0.saveSettings();
    f.pan1.saveSettings();
    QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayWfOpacity_0")));
    QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayWfOpacity_1")));
    QCOMPARE(stored(QStringLiteral("DisplayWfOpacity")), QStringLiteral("90"));

    // A pan made afterwards starts on the default.
    SpectrumWidget pan2;
    pan2.setPanIndex(2);
    pan2.loadSettings();
    QCOMPARE(pan2.wfOpacity(), 90);
}

namespace {

struct DefaultRow {
    const char* name;
    const char* page;     // "waterfall", "spectrum", "grid", "threeD", "tx", "colours"
    const char* keyBase;
    QString expected;     // the bare key's text
    std::function<void(QWidget*)> drive;
    std::function<bool(SpectrumWidget&)> moved;
};

QList<DefaultRow> defaultRows()
{
    const auto wf = [](QWidget* w) { return static_cast<WaterfallDefaultsPage*>(w); };
    const auto sp = [](QWidget* w) { return static_cast<SpectrumDefaultsPage*>(w); };
    const auto gr = [](QWidget* w) { return static_cast<GridScalesPage*>(w); };
    const auto td = [](QWidget* w) { return static_cast<Display3DSetupPage*>(w); };
    const auto cl = [](QWidget* w) { return static_cast<ColorsThemePage*>(w); };
    const auto tx = [](QWidget* w) { return static_cast<TxDisplayPage*>(w); };
    const QColor green(10, 200, 30);
    const QColor violet(120, 20, 220);
    return {
        {"waterfall opacity", "waterfall", "DisplayWfOpacity", QStringLiteral("35"),
         [wf](QWidget* w) { wf(w)->m_opacitySlider->setValue(35); },
         [](SpectrumWidget& pan) { return pan.wfOpacity() == 35; }},
        {"waterfall high level", "waterfall", "DisplayWfHighLevel", QStringLiteral("-50"),
         [wf](QWidget* w) { wf(w)->m_highThresholdSlider->setValue(-50); },
         [](SpectrumWidget& pan) { return qFuzzyCompare(pan.wfHighThreshold(), -50.0f); }},
        {"waterfall scheme", "waterfall", "DisplayWfColorScheme", QStringLiteral("3"),
         [wf](QWidget* w) { wf(w)->m_colorSchemeCombo->setCurrentIndex(3); },
         [](SpectrumWidget& pan) { return pan.displaySettings()->wfColorScheme() == 3; }},
        {"waterfall AGC", "waterfall", "DisplayWfAgc", QStringLiteral("False"),
         [wf](QWidget* w) { wf(w)->m_agcToggle->setChecked(false); },
         [](SpectrumWidget& pan) { return !pan.wfAgcEnabled(); }},
        {"waterfall NF-AGC", "waterfall", "WaterfallNFAGCEnabled", QStringLiteral("True"),
         [wf](QWidget* w) { wf(w)->m_wfNfAgcEnable->setChecked(true); },
         [](SpectrumWidget& pan) { return pan.waterfallNFAGCEnabled(); }},
        {"waterfall follows spectrum", "waterfall", "DisplayWfUseSpectrumMinMax",
         QStringLiteral("True"),
         [wf](QWidget* w) { wf(w)->m_useSpectrumMinMaxToggle->setChecked(true); },
         [](SpectrumWidget& pan) { return pan.wfUseSpectrumMinMax(); }},
        {"spectrum fill", "spectrum", "DisplayPanFill", QStringLiteral("False"),
         [sp](QWidget* w) { sp(w)->m_fillToggle->setChecked(false); },
         [](SpectrumWidget& pan) { return !pan.displaySettings()->panFill(); }},
        {"spectrum fill alpha", "spectrum", "DisplayFftFillAlpha", QStringLiteral("0.42"),
         [sp](QWidget* w) { sp(w)->m_fillAlphaSlider->setValue(42); },
         [](SpectrumWidget& pan) {
             return qFuzzyCompare(pan.displaySettings()->fillAlpha(), 0.42f);
         }},
        {"spectrum line width", "spectrum", "DisplayLineWidth", QStringLiteral("2.5"),
         [sp](QWidget* w) { sp(w)->m_lineWidthSlider->setValue(25); },
         [](SpectrumWidget& pan) { return qFuzzyCompare(pan.lineWidth(), 2.5f); }},
        {"spectrum gradient", "spectrum", "DisplayGradientEnabled", QString(),
         [sp](QWidget* w) {
             sp(w)->m_gradientToggle->setChecked(!sp(w)->m_gradientToggle->isChecked());
         },
         {}},
        {"spectrum calibration", "spectrum", "DisplayCalOffset", QStringLiteral("-3.5"),
         [sp](QWidget* w) { sp(w)->m_calOffsetSpin->setValue(-3.5); },
         [](SpectrumWidget& pan) { return qFuzzyCompare(pan.dbmCalOffset(), -3.5f); }},
        {"spectrum peak hold", "spectrum", "DisplayPeakHoldEnabled", QString(),
         [sp](QWidget* w) {
             sp(w)->m_peakHoldToggle->setChecked(!sp(w)->m_peakHoldToggle->isChecked());
         },
         {}},
        {"spectrum cursor frequency", "spectrum", "DisplayShowCursorFreq", QString(),
         [sp](QWidget* w) {
             sp(w)->m_showMHzOnCursorToggle->setChecked(
                 !sp(w)->m_showMHzOnCursorToggle->isChecked());
         },
         {}},
        {"grid lines", "grid", "DisplayGridEnabled", QString(),
         [gr](QWidget* w) { gr(w)->m_gridToggle->setChecked(!gr(w)->m_gridToggle->isChecked()); },
         {}},
        {"grid dBm scale", "grid", "DisplayDbmScaleVisible", QString(),
         [gr](QWidget* w) {
             gr(w)->m_dbmScaleVisibleToggle->setChecked(
                 !gr(w)->m_dbmScaleVisibleToggle->isChecked());
         },
         {}},
        {"grid FPS", "grid", "DisplayShowFps", QString(),
         [gr](QWidget* w) {
             gr(w)->m_showFpsToggle->setChecked(!gr(w)->m_showFpsToggle->isChecked());
         },
         {}},
        {"3D mode", "threeD", "DisplaySpectrumRenderMode", QStringLiteral("1"),
         [td](QWidget* w) { td(w)->m_modeCombo->setCurrentIndex(1); },
         [](SpectrumWidget& pan) { return pan.displaySettings()->spectrumRenderMode() == 1; }},
        {"3D gain", "threeD", "Display3DGain", QStringLiteral("44"),
         [td](QWidget* w) { td(w)->m_gainSlider->setValue(44); },
         [](SpectrumWidget& pan) { return pan.dssGain() == 44; }},
        {"3D span", "threeD", "Display3DSpan", QStringLiteral("60"),
         [td](QWidget* w) { td(w)->m_spanSlider->setValue(60); },
         [](SpectrumWidget& pan) { return pan.dssRowSpan() == 60; }},
        {"3D angle", "threeD", "Display3DAngle", QStringLiteral("15"),
         [td](QWidget* w) { td(w)->m_angleSlider->setValue(15); },
         [](SpectrumWidget& pan) { return pan.dssAngle() == 15; }},
        {"3D speed", "threeD", "Display3DSpeed", QStringLiteral("6"),
         [td](QWidget* w) { td(w)->m_speedSlider->setValue(6); },
         [](SpectrumWidget& pan) { return pan.displaySettings()->dssRowDivider() == 6; }},
        {"3D slice shadow", "threeD", "Display3DSliceShadow", QStringLiteral("True"),
         [td](QWidget* w) { td(w)->m_sliceShadowCheck->setChecked(true); },
         [](SpectrumWidget& pan) { return pan.threeDSliceDepth(); }},
        // Fix wave I8: the TX Display page's waterfall rows.
        {"tx waterfall low level", "tx", "DisplayTxWfLowLevel", QStringLiteral("-95"),
         [tx](QWidget* w) { tx(w)->m_txWfLowLevelSpin->setValue(-95); },
         [](SpectrumWidget& pan) { return pan.txWfLowLevel() == -95; }},
        {"tx waterfall high level", "tx", "DisplayTxWfHighLevel", QStringLiteral("45"),
         [tx](QWidget* w) { tx(w)->m_txWfHighLevelSpin->setValue(45); },
         [](SpectrumWidget& pan) { return pan.txWfHighLevel() == 45; }},
        {"tx waterfall palette", "tx", "DisplayTxWfPalette",
         QString::number(static_cast<int>(WfColorScheme::Spectran)),
         [tx](QWidget* w) { tx(w)->m_txWfPaletteCombo->setCurrentIndex(1); },
         [](SpectrumWidget& pan) { return pan.txWfPalette() == WfColorScheme::Spectran; }},
        {"tx waterfall low colour", "tx", "DisplayTxWfLowColor",
         green.name(QColor::HexArgb).toUpper(),
         [tx, green](QWidget* w) { tx(w)->m_txWfLowColorBtn->setColor(green); },
         [green](SpectrumWidget& pan) { return pan.txWfLowColor() == green; }},
        {"fill colour", "colours", "DisplayFillColor", green.name(QColor::HexArgb),
         [cl, green](QWidget* w) { cl(w)->m_traceFillColorBtn->setColor(green); },
         [green](SpectrumWidget& pan) { return pan.fillColor() == green; }},
        {"trace colour", "colours", "DisplayTraceColor", violet.name(QColor::HexArgb),
         [cl, violet](QWidget* w) { cl(w)->m_traceColorBtn->setColor(violet); },
         [violet](SpectrumWidget& pan) { return pan.traceColor() == violet; }},
        {"grid colour", "colours", "DisplayGridColor", green.name(QColor::HexArgb),
         [cl, green](QWidget* w) { cl(w)->m_gridColorBtn->setColor(green); },
         [green](SpectrumWidget& pan) { return pan.gridColor() == green; }},
    };
}

} // namespace

void TestDisplaySetupBinding::everyPageAppliesItsDefaultsToEveryPan_data()
{
    QTest::addColumn<int>("row");
    const QList<DefaultRow> rows = defaultRows();
    for (int i = 0; i < rows.size(); ++i) {
        QTest::newRow(rows.at(i).name) << i;
    }
}

// Each per-pan control on the four Display pages and on Appearance: the bare
// key is written, both pans move (neither has a value of its own), and
// neither stores a key of its own afterwards. A control that set only the
// active pan fails on pan 1.
void TestDisplaySetupBinding::everyPageAppliesItsDefaultsToEveryPan()
{
    QFETCH(int, row);
    const DefaultRow r = defaultRows().at(row);
    const QString base = QString::fromLatin1(r.keyBase);
    const QString pageName = QString::fromLatin1(r.page);
    TwoPanFixture f;
    std::unique_ptr<SetupPage> page;
    if (pageName == QStringLiteral("waterfall")) {
        page = std::make_unique<WaterfallDefaultsPage>(&f.model);
    } else if (pageName == QStringLiteral("spectrum")) {
        page = std::make_unique<SpectrumDefaultsPage>(&f.model);
    } else if (pageName == QStringLiteral("grid")) {
        page = std::make_unique<GridScalesPage>(&f.model);
    } else if (pageName == QStringLiteral("threeD")) {
        page = std::make_unique<Display3DSetupPage>(&f.pan0);
    } else if (pageName == QStringLiteral("tx")) {
        page = std::make_unique<TxDisplayPage>(&f.model);
    } else {
        page = std::make_unique<ColorsThemePage>(&f.model);
    }
    page->setDisplayProviders(f.providers());
    QVERIFY(stored(base).isEmpty());
    const QString before0 = f.pan0.perPanShipDefault(base);

    r.drive(page.get());

    const QString bare = stored(base);
    QVERIFY2(!bare.isEmpty(), r.keyBase);
    if (!r.expected.isEmpty()) {
        QCOMPARE(bare, r.expected);
    } else {
        // A box that was flipped: the opposite of the ship default.
        QVERIFY2(bare != before0, r.keyBase);
    }
    if (r.moved) {
        QVERIFY2(r.moved(f.pan0), "pan 0 did not take the default");
        QVERIFY2(r.moved(f.pan1), "pan 1 did not take the default");
    }
    f.pan0.saveSettings();
    f.pan1.saveSettings();
    QVERIFY2(!AppSettings::instance().contains(PanDisplayKeys::panKey(base, 0)), r.keyBase);
    QVERIFY2(!AppSettings::instance().contains(PanDisplayKeys::panKey(base, 1)), r.keyBase);
    QCOMPARE(stored(base), bare);
    // Both pans hold the default: a pan loaded from the keys agrees with them.
    for (int index : {0, 1}) {
        SpectrumWidget fresh;
        fresh.setPanIndex(index);
        fresh.loadSettings();
        if (r.moved) { QVERIFY2(r.moved(fresh), r.keyBase); }
    }
}

// Each page opens on the Setup default. The active pan has values of its
// own, stored; none of them is what the page shows.
void TestDisplaySetupBinding::pagesShowTheDefaultNotTheActivePansValue()
{
    auto& settings = AppSettings::instance();
    const QColor amber(250, 180, 10);
    const QColor teal(0, 140, 140);
    settings.setValue(QStringLiteral("DisplayWfOpacity"), QStringLiteral("80"));
    settings.setValue(QStringLiteral("DisplayWfOpacity_0"), QStringLiteral("40"));
    settings.setValue(QStringLiteral("DisplayLineWidth"), QStringLiteral("2.5"));
    settings.setValue(QStringLiteral("DisplayLineWidth_0"), QStringLiteral("1"));
    settings.setValue(QStringLiteral("DisplayGridEnabled"), QStringLiteral("False"));
    settings.setValue(QStringLiteral("DisplayGridEnabled_0"), QStringLiteral("True"));
    settings.setValue(QStringLiteral("Display3DGain"), QStringLiteral("30"));
    settings.setValue(QStringLiteral("Display3DGain_0"), QStringLiteral("90"));
    settings.setValue(QStringLiteral("DisplayFillColor"), amber.name(QColor::HexArgb));
    settings.setValue(QStringLiteral("DisplayFillColor_0"), teal.name(QColor::HexArgb));
    TwoPanFixture f;
    QCOMPARE(f.pan0.wfOpacity(), 40);
    QCOMPARE(f.pan0.lineWidth(), 1.0f);
    QVERIFY(f.pan0.gridEnabled());
    QCOMPARE(f.pan0.dssGain(), 90);
    QCOMPARE(f.pan0.fillColor(), teal);
    WriteCounter counter;

    WaterfallDefaultsPage waterfall(&f.model);
    QCOMPARE(waterfall.m_opacitySlider->value(), 80);
    SpectrumDefaultsPage spectrum(&f.model);
    QCOMPARE(spectrum.m_lineWidthSlider->value(), 25);
    GridScalesPage grid(&f.model);
    QVERIFY(!grid.m_gridToggle->isChecked());
    Display3DSetupPage threeD(&f.pan0);
    QCOMPARE(threeD.m_gainSlider->value(), 30);
    ColorsThemePage colours(&f.model);
    QCOMPARE(colours.m_traceFillColorBtn->color(), amber);
    // No trace colour chosen: the Trace button shows the fill default.
    QCOMPARE(colours.m_traceColorBtn->color(), amber);

    // Opening the pages wrote nothing and moved no pan.
    QCOMPARE(counter.writes, 0);
    QCOMPARE(f.pan0.wfOpacity(), 40);
    QCOMPARE(f.pan0.dssGain(), 90);
    QCOMPARE(f.pan0.fillColor(), teal);
}

// A default changed from another surface while the page is open: the pan
// that takes it says so, and the page shows it under a blocker. The page
// writes nothing back and its control emits nothing.
void TestDisplaySetupBinding::pageFollowsADefaultChangedElsewhereAndWritesNothing()
{
    TwoPanFixture f;
    WaterfallDefaultsPage waterfall(&f.model);
    waterfall.setDisplayProviders(f.providers());
    SpectrumDefaultsPage spectrum(&f.model);
    spectrum.setDisplayProviders(f.providers());
    Display3DSetupPage threeD(&f.pan0);
    threeD.setDisplayProviders(f.providers());
    ColorsThemePage colours(&f.model);
    colours.setDisplayProviders(f.providers());
    QSignalSpy opacityEchoes(waterfall.m_opacitySlider, &QSlider::valueChanged);
    QSignalSpy schemeEchoes(waterfall.m_colorSchemeCombo, &QComboBox::currentIndexChanged);
    QSignalSpy lineEchoes(spectrum.m_lineWidthSlider, &QSlider::valueChanged);
    QSignalSpy gainEchoes(threeD.m_gainSlider, &QSlider::valueChanged);
    QSignalSpy fillEchoes(colours.m_traceFillColorBtn, &ColorSwatchButton::colorChanged);
    const QColor rose(230, 60, 120);

    // The other surface: it stores the default, then moves the pan (pan 1,
    // not the active one).
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayWfOpacity"), QStringLiteral("55"));
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayWfColorScheme"), QStringLiteral("4"));
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayLineWidth"), QStringLiteral("0.5"));
    PanDisplayKeys::writeDefault(QStringLiteral("Display3DGain"), QStringLiteral("20"));
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayFillColor"),
                                 rose.name(QColor::HexArgb));
    WriteCounter counter;
    f.pan1.setWfOpacity(55);
    f.pan1.displaySettings()->setWfColorScheme(4);
    f.pan1.setLineWidth(0.5f);
    f.pan1.displaySettings()->setDssGain(20);
    f.pan1.setFillColor(rose);

    QCOMPARE(waterfall.m_opacitySlider->value(), 55);
    QCOMPARE(waterfall.m_colorSchemeCombo->currentIndex(), 4);
    QCOMPARE(spectrum.m_lineWidthSlider->value(), 5);
    QCOMPARE(spectrum.findChild<QDoubleSpinBox*>(QStringLiteral("specLineWidthSpin"))->value(),
             0.5);
    QCOMPARE(threeD.m_gainSlider->value(), 20);
    QCOMPARE(colours.m_traceFillColorBtn->color(), rose);
    QCOMPARE(colours.m_traceColorBtn->color(), rose);
    QCOMPARE(opacityEchoes.count(), 0);
    QCOMPARE(schemeEchoes.count(), 0);
    QCOMPARE(lineEchoes.count(), 0);
    QCOMPARE(gainEchoes.count(), 0);
    QCOMPARE(fillEchoes.count(), 0);
    QVERIFY2(counter.writes == 0, qPrintable(counter.keys.join(QLatin1Char(' '))));
    // The active pan was not moved by the page.
    QCOMPARE(f.pan0.wfOpacity(), 100);

    // A default set on one Setup page is shown on another that holds it.
    Display3DSetupPage second(&f.pan0);
    second.setDisplayProviders(f.providers());
    second.m_gainSlider->setValue(65);
    QCOMPARE(threeD.m_gainSlider->value(), 65);
    QCOMPARE(gainEchoes.count(), 0);
    QCOMPARE(f.pan0.dssGain(), 65);
    QCOMPARE(f.pan1.dssGain(), 65);
}

// Follow-up wave F4: the High and Low Threshold readouts show their whole
// value, down to the page's narrowest.
void TestDisplaySetupBinding::waterfallThresholdReadoutsShowTheirWholeValue()
{
    TwoPanFixture f;
    WaterfallDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());
    page.resize(page.minimumSizeHint().expandedTo(QSize(1, 1)));
    // Drawing the page lays it out without putting a window up.
    page.grab();

    for (QSpinBox* spin : {page.m_highThresholdSpin, page.m_lowThresholdSpin}) {
        QVERIFY(spin);
        QCOMPARE(spin->suffix(), QStringLiteral(" dBm"));
        auto* edit = spin->findChild<QLineEdit*>();
        QVERIFY(edit);
        for (const int value : {-122, -200}) {
            spin->setValue(value);
            const QString text = QStringLiteral("%1 dBm").arg(value);
            QCOMPARE(spin->text(), text);
            const int need = edit->fontMetrics().horizontalAdvance(text);
            const int have = edit->contentsRect().width();
            qInfo() << text << "needs" << need << "of" << have << "(spin" << spin->width() << ")";
            // 2 px each side is the line edit's own text margin.
            QVERIFY2(have >= need + 4,
                     qPrintable(QStringLiteral("%1 px for %2").arg(have).arg(need)));
        }
    }
    QCOMPARE(page.m_highThresholdSpin->width(), page.m_lowThresholdSpin->width());
    // A wide page leaves the readouts as they are: the sliders take the room.
    const int width = page.m_highThresholdSpin->width();
    page.resize(1200, page.height());
    page.grab();
    QCOMPARE(page.m_highThresholdSpin->width(), width);
}

// Waterfall Defaults' level rows: off with the reason while Clarity or the
// spectrum's range is setting the levels, live when neither is, and NF-AGC
// under AGC throughout. Low is also off while AGC is on, because AGC sets it.
void TestDisplaySetupBinding::waterfallLevelsGreyByWhoIsSettingThem()
{
    TwoPanFixture f;
    WaterfallDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());
    const QString clarityNote =
        QStringLiteral("Clarity is setting the levels. Turn it off to use these.");
    const QString minMaxNote =
        QStringLiteral("The spectrum's dB Max and Min are setting the levels.");
    const QString agcNote =
        QStringLiteral("AGC is setting Low. Turn AGC off to set it yourself.");
    const QList<QWidget*> levels{page.m_highThresholdSlider, page.m_highThresholdSpin,
                                 page.m_agcToggle};
    const QList<QWidget*> lowRows{page.m_lowThresholdSlider, page.m_lowThresholdSpin};
    const QList<QWidget*> nfRows{page.m_wfNfAgcEnable, page.m_wfAgcOffsetDb};
    const auto allEnabled = [](const QList<QWidget*>& rows, bool want) {
        for (QWidget* row : rows) {
            if (row->isEnabled() != want) { return false; }
        }
        return true;
    };
    const auto allSay = [](const QList<QWidget*>& rows, const QString& tip) {
        for (QWidget* row : rows) {
            if (row->toolTip() != tip) { return false; }
        }
        return true;
    };
    const QString highTip = page.m_highThresholdSlider->toolTip();
    const QString nfTip = page.m_wfNfAgcEnable->toolTip();
    const QString lowTip = QStringLiteral(
        "Waterfall Low Signal - Show Low Color below this value (gradient in between).");
    const int storedLow = page.m_lowThresholdSlider->value();
    QCOMPARE(page.m_levelOwnerNote->objectName(), QStringLiteral("wfLevelOwnerNote"));

    // Neither, AGC on (the ship default): AGC sets Low, so Low is off and
    // says why, under the rows too. High and the AGC box are live.
    QVERIFY(page.m_agcToggle->isChecked());
    QVERIFY(allEnabled(levels, true));
    QCOMPARE(page.m_highThresholdSlider->toolTip(), highTip);
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allSay(lowRows, agcNote));
    QVERIFY(allEnabled(nfRows, true));
    QCOMPARE(page.m_levelOwnerNote->text(), agcNote);
    // Disabled, never hidden, and still showing its stored value.
    QVERIFY(!page.m_lowThresholdSlider->isHidden());
    QVERIFY(!page.m_lowThresholdSpin->isHidden());
    QCOMPARE(page.m_lowThresholdSpin->value(), storedLow);

    // AGC off: Low is free at once, with its own tooltip back, and the line
    // is empty. NF-AGC and its offset are off, and say why.
    page.m_agcToggle->setChecked(false);
    QVERIFY(allEnabled(levels, true));
    QVERIFY(allEnabled(lowRows, true));
    QVERIFY(allSay(lowRows, lowTip));
    QCOMPARE(page.m_lowThresholdSlider->value(), storedLow);
    QVERIFY(allEnabled(nfRows, false));
    QVERIFY(allSay(nfRows, QStringLiteral("Turn on AGC to use NF-AGC.")));
    QVERIFY(page.m_levelOwnerNote->text().isEmpty());

    // Clarity takes the active pan's levels with AGC off: its reason.
    f.pan0.setClarityActive(true);
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allSay(lowRows, clarityNote));
    f.pan0.setClarityActive(false);
    QVERIFY(allEnabled(lowRows, true));
    QVERIFY(allSay(lowRows, lowTip));

    page.m_agcToggle->setChecked(true);
    QVERIFY(allEnabled(nfRows, true));
    QCOMPARE(page.m_wfNfAgcEnable->toolTip(), nfTip);
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allSay(lowRows, agcNote));
    QCOMPARE(page.m_levelOwnerNote->text(), agcNote);

    // Clarity takes the active pan's levels: its reason wins over AGC's.
    f.pan0.setClarityActive(true);
    QVERIFY(allEnabled(levels, false));
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allEnabled(nfRows, false));
    QVERIFY(allSay(levels, clarityNote));
    QVERIFY(allSay(lowRows, clarityNote));
    QVERIFY(allSay(nfRows, clarityNote));
    QCOMPARE(page.m_levelOwnerNote->text(), clarityNote);
    // Disabled, never hidden.
    QVERIFY(!page.m_highThresholdSlider->isHidden());
    QVERIFY(!page.m_wfNfAgcEnable->isHidden());
    // "Use spectrum min/max" is not a level row: it stays live.
    QVERIFY(page.m_useSpectrumMinMaxToggle->isEnabled());

    f.pan0.setClarityActive(false);
    QVERIFY(allEnabled(levels, true));
    QVERIFY(allEnabled(nfRows, true));
    QCOMPARE(page.m_highThresholdSlider->toolTip(), highTip);
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allSay(lowRows, agcNote));
    QCOMPARE(page.m_levelOwnerNote->text(), agcNote);

    // "Use spectrum min/max": its reason wins over AGC's too.
    page.m_useSpectrumMinMaxToggle->setChecked(true);
    QVERIFY(allEnabled(levels, false));
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allEnabled(nfRows, false));
    QVERIFY(allSay(levels, minMaxNote));
    QVERIFY(allSay(lowRows, minMaxNote));
    QCOMPARE(page.m_levelOwnerNote->text(), minMaxNote);
    // Clarity outranks it, as in the Display pill.
    f.pan0.setClarityActive(true);
    QCOMPARE(page.m_levelOwnerNote->text(), clarityNote);
    QVERIFY(allSay(lowRows, clarityNote));
    f.pan0.setClarityActive(false);
    QCOMPARE(page.m_levelOwnerNote->text(), minMaxNote);
    page.m_useSpectrumMinMaxToggle->setChecked(false);
    QVERIFY(allEnabled(levels, true));
    QVERIFY(allEnabled(nfRows, true));
    QCOMPARE(page.m_highThresholdSlider->toolTip(), highTip);
    QCOMPARE(page.m_wfNfAgcEnable->toolTip(), nfTip);
    QVERIFY(allEnabled(lowRows, false));
    QVERIFY(allSay(lowRows, agcNote));
    QCOMPARE(page.m_levelOwnerNote->text(), agcNote);

    // Unchecking AGC while the page is open frees Low at once.
    page.m_agcToggle->setChecked(false);
    QVERIFY(allEnabled(lowRows, true));
    QVERIFY(allSay(lowRows, lowTip));
    QVERIFY(page.m_levelOwnerNote->text().isEmpty());
}

void TestDisplaySetupBinding::waterfallSchemeListsTheEightSchemes()
{
    TwoPanFixture f;
    WaterfallDefaultsPage page(&f.model);
    QCOMPARE(page.m_colorSchemeCombo->count(), 8);
    for (int i = 0; i < 8; ++i) {
        QCOMPARE(page.m_colorSchemeCombo->itemText(i),
                 QString::fromLatin1(wfSchemeName(static_cast<WfColorScheme>(i))));
    }
}

// 0.1 to 5 px in tenths of a pixel; the 1.5 px ship default loads as 1.5
// (the row was whole pixels and showed it as 1). The slider and the number
// cell agree at every width.
// Final pass T2: the default is 1.0 px (it was 1.5); a stored 1.5 still
// opens as 1.5.
void TestDisplaySetupBinding::lineWidthIsTenthPixelSteps()
{
    TwoPanFixture f;
    QCOMPARE(f.pan0.lineWidth(), 1.0f);
    SpectrumDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());
    QSlider* slider = page.m_lineWidthSlider;
    auto* spin = page.findChild<QDoubleSpinBox*>(QStringLiteral("specLineWidthSpin"));
    QVERIFY(slider && spin);
    QCOMPARE(slider->minimum(), 1);
    QCOMPARE(slider->maximum(), 50);
    QCOMPARE(slider->singleStep(), 1);
    QCOMPARE(spin->minimum(), 0.1);
    QCOMPARE(spin->maximum(), 5.0);
    QCOMPARE(spin->singleStep(), 0.1);
    QCOMPARE(spin->decimals(), 1);
    QCOMPARE(spin->suffix(), QStringLiteral(" px"));
    QCOMPARE(slider->value(), 10);
    QCOMPARE(spin->value(), 1.0);
    QVERIFY(stored(QStringLiteral("DisplayLineWidth")).isEmpty());

    // The slider moves the number cell.
    slider->setValue(1);
    QCOMPARE(spin->value(), 0.1);
    QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QStringLiteral("0.1"));
    QCOMPARE(f.pan0.lineWidth(), 0.1f);
    QCOMPARE(f.pan1.lineWidth(), 0.1f);

    slider->setValue(3);
    QCOMPARE(spin->value(), 0.3);
    QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QStringLiteral("0.3"));
    QCOMPARE(f.pan0.lineWidth(), 0.3f);

    slider->setValue(50);
    QCOMPARE(spin->value(), 5.0);
    QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QStringLiteral("5"));
    QCOMPARE(f.pan1.lineWidth(), 5.0f);

    // The number cell moves the slider.
    spin->setValue(0.1);
    QCOMPARE(slider->value(), 1);
    QCOMPARE(f.pan1.lineWidth(), 0.1f);
    spin->setValue(0.3);
    QCOMPARE(slider->value(), 3);
    QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QStringLiteral("0.3"));
    spin->setValue(5.0);
    QCOMPARE(slider->value(), 50);
    QCOMPARE(f.pan0.lineWidth(), 5.0f);
    spin->setValue(1.5);
    QCOMPARE(slider->value(), 15);
    QCOMPARE(stored(QStringLiteral("DisplayLineWidth")), QStringLiteral("1.5"));
    QCOMPARE(f.pan0.lineWidth(), 1.5f);

    // The arrows step a tenth.
    spin->stepUp();
    QCOMPARE(spin->value(), 1.6);
    QCOMPARE(slider->value(), 16);
    spin->stepDown();
    QCOMPARE(slider->value(), 15);

    // A stored 1.5 opens as 1.5.
    SpectrumDefaultsPage again(&f.model);
    QCOMPARE(again.m_lineWidthSlider->value(), 15);
    QCOMPARE(again.findChild<QDoubleSpinBox*>(QStringLiteral("specLineWidthSpin"))->value(), 1.5);
    // And a pan opened on that settings file reads 1.5, not the new default.
    SpectrumWidget stored15;
    stored15.setPanIndex(1);
    stored15.loadSettings();
    QCOMPARE(stored15.lineWidth(), 1.5f);
}

// Appearance: a Trace and a Fill button. Each opens on the default and sets
// the default for every pan that has no colour of its own.
void TestDisplaySetupBinding::appearanceHasSeparateTraceAndFillColours()
{
    const QColor own(200, 100, 0);
    AppSettings::instance().setValue(QStringLiteral("DisplayFillColor_1"),
                                     own.name(QColor::HexArgb));
    TwoPanFixture f;
    QCOMPARE(f.pan1.fillColor(), own);
    ColorsThemePage page(&f.model);
    page.setDisplayProviders(f.providers());
    ColorSwatchButton* trace = page.m_traceColorBtn;
    ColorSwatchButton* fill = page.m_traceFillColorBtn;
    QVERIFY(trace && fill && trace != fill);
    QCOMPARE(page.findChild<ColorSwatchButton*>(QStringLiteral("appearanceTraceColorBtn")), trace);
    QCOMPARE(page.findChild<ColorSwatchButton*>(QStringLiteral("appearanceFillColorBtn")), fill);
    const QColor shipFill = f.pan0.fillColor();
    QCOMPARE(fill->color(), shipFill);
    QCOMPARE(trace->color(), shipFill);

    // Fill: the default, pan 0 and the Trace button that follows it. Pan 1
    // takes the master too.
    const QColor lime(120, 240, 20, 200);
    fill->setColor(lime);
    QCOMPARE(stored(QStringLiteral("DisplayFillColor")), lime.name(QColor::HexArgb));
    QCOMPARE(f.pan0.fillColor(), lime);
    QCOMPARE(f.pan0.traceColor(), lime);
    QCOMPARE(f.pan1.fillColor(), lime);
    QCOMPARE(trace->color(), lime);
    QVERIFY(stored(QStringLiteral("DisplayTraceColor")).isEmpty());

    // Trace: its own default; the fill is left alone.
    const QColor white(255, 255, 255);
    trace->setColor(white);
    QCOMPARE(stored(QStringLiteral("DisplayTraceColor")), white.name(QColor::HexArgb));
    QCOMPARE(f.pan0.traceColor(), white);
    QCOMPARE(f.pan1.traceColor(), white);
    QCOMPARE(f.pan0.fillColor(), lime);
    QCOMPARE(fill->color(), lime);
    // A chosen trace colour no longer follows the fill.
    fill->setColor(QColor(10, 20, 30));
    QCOMPARE(trace->color(), white);
    QCOMPARE(f.pan0.traceColor(), white);

    f.pan0.saveSettings();
    f.pan1.saveSettings();
    QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayFillColor_0")));
    QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayTraceColor_0")));
    QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayTraceColor_1")));
    QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayFillColor_1")));

    // Both open on the defaults.
    ColorsThemePage again(&f.model);
    QCOMPARE(again.m_traceFillColorBtn->color(), QColor(10, 20, 30));
    QCOMPARE(again.m_traceColorBtn->color(), white);

    // Appearance reset acts on its nine shared colours. The chosen picture
    // colours belong to Display and remain unchanged on both pans.
    page.resetColorsToDefaultsForTest();
    QCOMPARE(fill->color(), QColor(10, 20, 30));
    QCOMPARE(f.pan0.fillColor(), QColor(10, 20, 30));
    QCOMPARE(f.pan1.fillColor(), QColor(10, 20, 30));
}

// Grid & Scales' dB Max and dB Min write the band store of the pan the page
// is given, in that pan's band, and follow the store.
void TestDisplaySetupBinding::gridScalesWritesTheBandStoreOfThePanItIsGiven()
{
    TwoPanFixture f;
    PanadapterModel given;
    given.setCenterFrequency(7150000.0);   // 40m
    const Band band = given.band();
    PanadapterModel other;
    other.setCenterFrequency(14200000.0);  // 20m
    QVERIFY(other.band() != band);
    GridScalesPage page(&f.model);
    page.setDisplayProviders(f.providers(&given));
    page.setStationSettingsAvailable(true, QString());
    QVERIFY(page.m_dbMaxSpin->isEnabled());
    QVERIFY(page.m_dbMinSpin->isEnabled());
    QVERIFY(page.m_noPanNote->text().isEmpty());
    QCOMPARE(page.m_dbMaxSpin->value(), given.perBandGrid(band).dbMax);
    QCOMPARE(page.m_dbMinSpin->value(), given.perBandGrid(band).dbMin);
    const BandGridSettings otherBefore = other.perBandGrid(other.band());

    page.m_dbMaxSpin->setValue(-25);
    page.m_dbMinSpin->setValue(-125);
    QCOMPARE(given.perBandGrid(band).dbMax, -25);
    QCOMPARE(given.perBandGrid(band).dbMin, -125);
    // Another band's range is not touched.
    QCOMPARE(other.perBandGrid(other.band()).dbMax, otherBefore.dbMax);
    QCOMPARE(other.perBandGrid(other.band()).dbMin, otherBefore.dbMin);

    // A range stored from a pan (the Display pill) is shown, quietly.
    QSignalSpy echoes(page.m_dbMaxSpin, &QSpinBox::valueChanged);
    given.setPerBandDbMax(band, -33);
    QCOMPARE(page.m_dbMaxSpin->value(), -33);
    QCOMPARE(echoes.count(), 0);

    // The station out of reach: the rows are off with the station's reason.
    page.setStationSettingsAvailable(false, QStringLiteral("The Core is not connected."));
    QVERIFY(!page.m_dbMaxSpin->isEnabled());
    QCOMPARE(page.m_dbMaxSpin->toolTip(), QStringLiteral("The Core is not connected."));
    page.setStationSettingsAvailable(true, QString());
    QVERIFY(page.m_dbMaxSpin->isEnabled());
}

// With no pan the band rows are off, visible, and say what to do.
void TestDisplaySetupBinding::gridScalesWithNoPanGreysTheBandRowsWithANote()
{
    TwoPanFixture f;
    GridScalesPage page(&f.model);
    page.setDisplayProviders(f.providers(nullptr));
    page.setStationSettingsAvailable(true, QString());
    const QString note =
        QStringLiteral("Open a panadapter to set its band's dB Max and dB Min.");
    for (QWidget* row : QList<QWidget*>{page.m_dbMaxSpin, page.m_dbMinSpin,
                                        page.m_copyWfToSpecBtn}) {
        QVERIFY(!row->isEnabled());
        QVERIFY(!row->isHidden());
        QCOMPARE(row->toolTip(), note);
    }
    QCOMPARE(page.m_noPanNote->objectName(), QStringLiteral("gridNoPanNote"));
    QCOMPARE(page.m_noPanNote->text(), note);
    QVERIFY(!page.m_noPanNote->isHidden());
    // The rest of the page is live.
    QVERIFY(page.m_gridToggle->isEnabled());

    // A pan arrives: the rows come back with their own tooltips.
    PanadapterModel given;
    page.setDisplayProviders(f.providers(&given));
    QVERIFY(page.m_dbMaxSpin->isEnabled());
    QVERIFY(page.m_dbMaxSpin->toolTip() != note);
    QVERIFY(page.m_noPanNote->text().isEmpty());
}

// In the app: Setup is handed every pan and the active pan's band model.
void TestDisplaySetupBinding::setupInTheAppReachesEveryPan()
{
    Test::markAudioFirstRunDone();
    AppSettings::instance().setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                      Q_ARG(QString, QStringLiteral("2v"))));
    PanadapterStack* stack = window.findChild<PanadapterStack*>();
    QVERIFY(stack);
    SpectrumWidget* primary = stack->spectrum(QStringLiteral("pan-0"));
    SpectrumWidget* secondary = stack->spectrum(QStringLiteral("pan-1"));
    QVERIFY(primary && secondary);
    QTRY_VERIFY(secondary->vfoWidget(1));
    // Pan 1 has an opacity of its own, saved.
    secondary->setWfOpacity(60);
    secondary->saveSettings();
    const QString ownKey =
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfOpacity"), secondary->panIndex());
    QCOMPARE(stored(ownKey), QStringLiteral("60"));

    QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
    auto* dialog = window.findChild<SetupDialog*>();
    QVERIFY(dialog);
    const auto closeDialog = qScopeGuard([dialog] { delete dialog; });
    const auto pageNamed = [dialog](const QString& name) -> QWidget* {
        return dialog->realizePageForTest(name);
    };
    QWidget* realized = pageNamed(QStringLiteral("Waterfall"));
    QVERIFY(realized);
    auto* waterfall = qobject_cast<WaterfallDefaultsPage*>(realized);
    if (!waterfall) { waterfall = dialog->findChild<WaterfallDefaultsPage*>(); }
    if (waterfall) { QVERIFY(realized->isAncestorOf(waterfall->m_opacitySlider)); }
    QVERIFY(waterfall);
    QCOMPARE(waterfall->m_opacitySlider->value(), 100);

    waterfall->m_opacitySlider->setValue(90);
    QCOMPARE(stored(QStringLiteral("DisplayWfOpacity")), QStringLiteral("90"));
    QCOMPARE(primary->wfOpacity(), 90);
    QCOMPARE(secondary->wfOpacity(), 90);
    primary->saveSettings();
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfOpacity"), primary->panIndex())));

    // A row with no value of its own on either pan reaches both.
    waterfall->m_wfStopOnTx->setChecked(!waterfall->m_wfStopOnTx->isChecked());
    QCOMPARE(primary->perPanShipDefault(QStringLiteral("WaterfallStopOnTx"))
                 != stored(QStringLiteral("WaterfallStopOnTx")), true);

    // Grid & Scales: the active pan's band store, and the pan follows it.
    realized = pageNamed(QStringLiteral("Levels"));
    QVERIFY(realized);
    auto* grid = qobject_cast<GridScalesPage*>(realized);
    if (!grid) { grid = dialog->findChild<GridScalesPage*>(); }
    if (grid) { QVERIFY(realized->isAncestorOf(grid->m_dbMaxSpin)); }
    QVERIFY(grid);
    PanadapterModel* active = window.panModels()->model(stack->activePanId());
    QVERIFY(active);
    SpectrumWidget* activeWidget = stack->spectrum(stack->activePanId());
    QVERIFY(activeWidget);
    QVERIFY(grid->m_dbMaxSpin->isEnabled());
    QVERIFY(grid->m_noPanNote->text().isEmpty());
    const Band band = active->band();
    QCOMPARE(grid->m_dbMaxSpin->value(), active->perBandGrid(band).dbMax);
    grid->m_dbMaxSpin->setValue(-27);
    QCOMPARE(active->perBandGrid(band).dbMax, -27);
    QTRY_COMPARE(activeWidget->refLevel(), -27.0f);
    // Every pan on that band has the range.
    for (PanadapterApplet* applet : stack->allApplets()) {
        PanadapterModel* model = window.panModels()->model(applet->panId());
        QVERIFY(model);
        if (model->band() == band) {
            QCOMPARE(model->perBandGrid(band).dbMax, -27);
            QTRY_COMPARE(stack->spectrum(applet->panId())->refLevel(), -27.0f);
        }
    }

    // dB Step: the box sets the spacing of every pan's horizontal dB lines.
    QCOMPARE(grid->m_dbStepSpin->value(), 10);
    QCOMPARE(primary->dbGridStep(), 10);
    QCOMPARE(secondary->dbGridStep(), 10);
    grid->m_dbStepSpin->setValue(5);
    QCOMPARE(stored(QStringLiteral("DisplayGridStep")), QStringLiteral("5"));
    QCOMPARE(primary->dbGridStep(), 5);
    QCOMPARE(secondary->dbGridStep(), 5);
}

// Fix wave M1. A pan's own change is stored up to half a second after it is
// made. A Setup default for the same key inside that time must not take the
// pan for one that follows Setup: the pan keeps what it was given, and its
// own key is stored against the default it was changed from.
void TestDisplaySetupBinding::masterDefaultReplacesAPansUnsavedChange()
{
    TwoPanFixture f;
    WaterfallDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());

    // A key the widget stores itself.
    f.pan1.setWfOpacity(60);
    QVERIFY(f.pan1.settingsSavePendingForTest());
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfOpacity"), 1)));
    page.m_opacitySlider->setValue(90);
    QCOMPARE(stored(QStringLiteral("DisplayWfOpacity")), QStringLiteral("90"));
    QCOMPARE(f.pan0.wfOpacity(), 90);
    QCOMPARE(f.pan1.wfOpacity(), 90);
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfOpacity"), 1)));

    // A key the pan's settings model stores.
    f.pan1.displaySettings()->setWfColorScheme(2);
    QVERIFY(f.pan1.settingsSavePendingForTest());
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfColorScheme"), 1)));
    page.m_colorSchemeCombo->setCurrentIndex(5);
    QCOMPARE(stored(QStringLiteral("DisplayWfColorScheme")), QStringLiteral("5"));
    QCOMPARE(f.pan0.displaySettings()->wfColorScheme(), 5);
    QCOMPARE(f.pan1.displaySettings()->wfColorScheme(), 5);
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfColorScheme"), 1)));

    // The pan that follows Setup still has no key of its own.
    f.pan0.saveSettings();
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfOpacity"), 0)));
    QVERIFY(!AppSettings::instance().contains(
        PanDisplayKeys::panKey(QStringLiteral("DisplayWfColorScheme"), 0)));
}

// The averaging time: Setup's number box and the pill's slider and number
// box show the same time for the same setting, from 1 ms in steps of 1. A
// time past the pill slider's top (500 ms) is shown and written nowhere,
// whoever shows it.
void TestDisplaySetupBinding::avgTimeAgreesInSetupAndThePill_data()
{
    QTest::addColumn<bool>("waterfall");
    QTest::newRow("Spectrum Avg Time") << false;
    QTest::newRow("WF Avg Time") << true;
}

void TestDisplaySetupBinding::avgTimeAgreesInSetupAndThePill()
{
    QFETCH(bool, waterfall);
    const QString key = waterfall ? QStringLiteral("DisplayWaterfallAverageTimeMs")
                                  : QStringLiteral("DisplaySpectrumAverageTimeMs");
    const QString panKey = PanDisplayKeys::panKey(key, 1);
    // This regression exercises editing, so the waterfall averaging
    // parent must remain active through the pan resets below.
    if (waterfall) {
        AppSettings::instance().setValue(QStringLiteral("DisplayWaterfallAveraging"),
            QString::number(static_cast<int>(SpectrumAveraging::Recursive)));
    }
    TwoPanFixture f;
    SpectrumDefaultsPage spectrumPage(&f.model);
    WaterfallDefaultsPage waterfallPage(&f.model);
    spectrumPage.setDisplayProviders(f.providers());
    waterfallPage.setDisplayProviders(f.providers());
    QSpinBox* spin = waterfall ? waterfallPage.m_waterfallAvgTimeSpin
                               : spectrumPage.m_averagingTimeSpin;
    const auto ms = [waterfall](const SpectrumWidget& pan) {
        return waterfall ? pan.waterfallAverageTimeMs() : pan.spectrumAverageTimeMs();
    };

    f.pan1.resize(900, 600);
    auto* panel = new SpectrumOverlayPanel(&f.pan1);
    panel->openDisplayPill();
    QWidget* pill = f.pan1.findChild<QWidget*>(QStringLiteral("displayPill"));
    QVERIFY(pill);
    auto* slider = pill->findChild<QSlider*>(QStringLiteral("pill") + key);
    auto* box = pill->findChild<QSpinBox*>(QStringLiteral("pill") + key + QStringLiteral("Box"));
    QVERIFY(slider);
    QVERIFY(box);

    // Setup's box and the pill's box have one range and one step.
    QCOMPARE(spin->minimum(), 1);
    QCOMPARE(spin->maximum(), 9999);
    QCOMPARE(spin->singleStep(), 1);
    QCOMPARE(box->minimum(), spin->minimum());
    QCOMPARE(box->maximum(), spin->maximum());
    QCOMPARE(box->singleStep(), spin->singleStep());
    QCOMPARE(box->suffix(), spin->suffix());

    // From Setup: a time the old 10 ms steps could not reach, then one
    // step of the box, then the floor.
    spin->setValue(35);
    QCOMPARE(stored(key), QStringLiteral("35"));
    QCOMPARE(ms(f.pan1), 35);
    QCOMPARE(slider->value(), 35);
    QCOMPARE(box->value(), 35);
    spin->stepUp();
    QCOMPARE(stored(key), QStringLiteral("36"));
    QCOMPARE(ms(f.pan1), 36);
    QCOMPARE(slider->value(), 36);
    QCOMPARE(box->value(), 36);
    spin->setValue(1);
    QCOMPARE(stored(key), QStringLiteral("1"));
    QCOMPARE(ms(f.pan0), 1);
    QCOMPARE(ms(f.pan1), 1);
    QCOMPARE(slider->value(), 1);
    QCOMPARE(box->value(), 1);

    // From Setup, past the slider's top: the pill shows it and writes
    // nothing of its own.
    {
        WriteCounter counter;
        spin->setValue(900);
        QCOMPARE(counter.keys, QStringList{key});
    }
    QCOMPARE(stored(key), QStringLiteral("900"));
    QCOMPARE(ms(f.pan1), 900);
    QCOMPARE(box->value(), 900);
    QCOMPARE(slider->value(), 500);
    QCOMPARE(spin->value(), 900);
    f.pan1.saveSettings();
    QVERIFY(!AppSettings::instance().contains(panKey));
    QCOMPARE(stored(key), QStringLiteral("900"));

    // "Reset this pan" on the other pan, and then on this one: the time is
    // still 900 everywhere and the pill has written nothing.
    f.pan0.resetPanDisplayOverrides();
    QCoreApplication::processEvents();
    QCOMPARE(stored(key), QStringLiteral("900"));
    QVERIFY(!AppSettings::instance().contains(panKey));
    QCOMPARE(ms(f.pan0), 900);
    QCOMPARE(ms(f.pan1), 900);
    QCOMPARE(box->value(), 900);
    QCOMPARE(slider->value(), 500);
    f.pan1.resetPanDisplayOverrides();
    QCoreApplication::processEvents();
    QCOMPARE(stored(key), QStringLiteral("900"));
    QVERIFY(!AppSettings::instance().contains(panKey));
    QCOMPARE(ms(f.pan1), 900);
    QCOMPARE(box->value(), 900);
    QCOMPARE(slider->value(), 500);
    QCOMPARE(spin->value(), 900);

    // From the pill: the slider is moved, and takes over from its top. The
    // pan now has a time of its own; Setup's box is the default and keeps
    // its 900, and the other pan still follows Setup.
    QTest::keyClick(slider, Qt::Key_Left);
    QCOMPARE(ms(f.pan1), 499);
    QCOMPARE(box->value(), 499);
    f.pan1.saveSettings();
    QCOMPARE(stored(panKey), QStringLiteral("499"));
    QCOMPARE(stored(key), QStringLiteral("900"));
    QCOMPARE(spin->value(), 900);
    QCOMPARE(ms(f.pan0), 900);

    // One arrow press from 30 ms is 31 ms, and 31 is what is stored.
    box->setValue(30);
    QCOMPARE(ms(f.pan1), 30);
    QCOMPARE(slider->value(), 30);
    QTest::keyClick(slider, Qt::Key_Right);
    QCOMPARE(ms(f.pan1), 31);
    QCOMPARE(box->value(), 31);
    f.pan1.saveSettings();
    QCOMPARE(stored(panKey), QStringLiteral("31"));

    // The pill's box at the floor.
    box->setValue(1);
    QCOMPARE(ms(f.pan1), 1);
    f.pan1.saveSettings();
    QCOMPARE(stored(panKey), QStringLiteral("1"));
    // Read back from the settings, 1 ms is still 1 ms.
    f.pan1.loadSettings();
    QCOMPARE(ms(f.pan1), 1);
}

// Fix wave I7. "Reset to Smooth Defaults" sets Setup defaults: a pan with a
// value of its own takes the reset too, and both pans follow the master.
void TestDisplaySetupBinding::smoothDefaultsAreSetupDefaultsForEveryPan()
{
    auto& settings = AppSettings::instance();
    settings.setValue(QStringLiteral("DisplayWfColorScheme_0"), QStringLiteral("3"));
    settings.setValue(QStringLiteral("DisplaySpectrumAverageTimeMs_0"), QStringLiteral("200"));
    // Both pans request Clarity and remember an inactive AGC flag. Smooth
    // preserves that tuple; only a requested Manual mode becomes Auto.
    settings.setValue(QStringLiteral("DisplayWfAgc"), QStringLiteral("False"));
    TwoPanFixture f;
    QCOMPARE(f.pan0.displaySettings()->wfColorScheme(), 3);
    QCOMPARE(f.pan0.spectrumAverageTimeMs(), 200);
    QVERIFY(!f.pan0.wfAgcEnabled());
    QVERIFY(!f.pan1.wfAgcEnabled());

    SpectrumDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());
    QPushButton* reset = nullptr;
    for (QPushButton* button : page.findChildren<QPushButton*>()) {
        if (button->property("nereusSetupId").toString()
            == QStringLiteral("display.spectrumDefaults.smoothDefaults")) {
            reset = button;
        }
    }
    QVERIFY(reset);
    // The button asks first; the question is answered Yes.
    bool answered = false;
    QTimer::singleShot(0, &page, [&answered]() {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) { return; }
        answered = true;
        box->button(QMessageBox::Yes)->click();
    });
    reset->click();
    QVERIFY(answered);

    const int clarityBlue = static_cast<int>(WfColorScheme::ClarityBlue);
    const QColor white(0xff, 0xff, 0xff, 230);
    // The defaults.
    QCOMPARE(stored(QStringLiteral("DisplayWfColorScheme")), QString::number(clarityBlue));
    QCOMPARE(stored(QStringLiteral("DisplaySpectrumAveraging")),
             QString::number(static_cast<int>(SpectrumAveraging::LogRecursive)));
    QCOMPARE(stored(QStringLiteral("DisplaySpectrumAverageTimeMs")), QStringLiteral("650"));
    QCOMPARE(QColor::fromString(stored(QStringLiteral("DisplayFillColor"))), white);
    QCOMPARE(stored(QStringLiteral("DisplayPanFill")), QStringLiteral("False"));
    QCOMPARE(stored(QStringLiteral("DisplayWfAgc")), QStringLiteral("False"));
    QVERIFY(f.pan0.clarityEnabled());
    QVERIFY(f.pan1.clarityEnabled());
    QVERIFY(!f.pan0.wfAgcEnabled());
    QVERIFY(!f.pan1.wfAgcEnabled());
    QCOMPARE(stored(QStringLiteral("DisplayWfUpdatePeriodMs")), QStringLiteral("30"));
    QCOMPARE(stored(QStringLiteral("DisplayProfileApplied")), QStringLiteral("True"));
    // Pan 1 has no value of its own: it takes all of them.
    QCOMPARE(f.pan1.displaySettings()->wfColorScheme(), clarityBlue);
    QCOMPARE(f.pan1.spectrumAveraging(), SpectrumAveraging::LogRecursive);
    QCOMPARE(f.pan1.spectrumAverageTimeMs(), 650);
    QCOMPARE(f.pan1.fillColor(), white);
    // No trace color was chosen: the trace is white, as it was when it
    // followed the fill, and it is now stored as the default.
    QCOMPARE(QColor::fromString(stored(QStringLiteral("DisplayTraceColor"))), white);
    QCOMPARE(f.pan1.traceColor(), white);
    QCOMPARE(f.pan0.traceColor(), white);
    QVERIFY(!f.pan1.displaySettings()->panFill());
    QVERIFY(!f.pan1.wfAgcEnabled());
    QCOMPARE(f.pan1.wfUpdatePeriodMs(), 30);
    // The explicit master reset replaces Pan 0's own values too.
    QCOMPARE(f.pan0.displaySettings()->wfColorScheme(), clarityBlue);
    QCOMPARE(f.pan0.spectrumAverageTimeMs(), 650);
    QCOMPARE(f.pan0.fillColor(), white);
    QVERIFY(!f.pan0.wfAgcEnabled());
    // Both pans now follow the reset master, without these overrides.
    f.pan0.saveSettings();
    f.pan1.saveSettings();
    QVERIFY(!settings.contains(QStringLiteral("DisplayWfColorScheme_0")));
    QVERIFY(!settings.contains(QStringLiteral("DisplaySpectrumAverageTimeMs_0")));
    for (const char* base : {"DisplayWfColorScheme", "DisplaySpectrumAveraging",
                             "DisplaySpectrumAverageTimeMs", "DisplayFillColor",
                             "DisplayTraceColor",
                             "DisplayPanFill", "DisplayWfAgc", "DisplayWfUpdatePeriodMs"}) {
        QVERIFY2(!settings.contains(PanDisplayKeys::panKey(QString::fromLatin1(base), 1)), base);
    }
    for (const char* base : {"DisplaySpectrumAveraging", "DisplayFillColor", "DisplayTraceColor",
                             "DisplayPanFill", "DisplayWfAgc", "DisplayWfUpdatePeriodMs"}) {
        QVERIFY2(!settings.contains(PanDisplayKeys::panKey(QString::fromLatin1(base), 0)), base);
    }
}

// Final pass T1. "Reset to Smooth Defaults" sets the trace color to the
// profile's white as well as the fill: a trace color chosen in Setup
// (here red) does not outlive the button, on the default and on every pan
// that follows it. A pan given a trace color of its own from its Display
// panel takes the master reset too, as for its other values.
void TestDisplaySetupBinding::smoothDefaultsSetTheTraceColorToWhite()
{
    auto& settings = AppSettings::instance();
    const QColor red(QStringLiteral("#FF0000"));
    const QColor green(QStringLiteral("#00FF00"));
    const QColor white(0xff, 0xff, 0xff, 230);
    PanDisplayKeys::writeDefault(QStringLiteral("DisplayTraceColor"), red.name(QColor::HexArgb));
    TwoPanFixture f;
    QCOMPARE(f.pan0.traceColor(), red);
    QCOMPARE(f.pan1.traceColor(), red);
    // Pan 0 is adjusted from its Display panel.
    f.pan0.setTraceColor(green);
    f.pan0.saveSettings();
    QVERIFY(settings.contains(QStringLiteral("DisplayTraceColor_0")));

    SpectrumDefaultsPage page(&f.model);
    page.setDisplayProviders(f.providers());
    QPushButton* reset = nullptr;
    for (QPushButton* button : page.findChildren<QPushButton*>()) {
        if (button->property("nereusSetupId").toString()
            == QStringLiteral("display.spectrumDefaults.smoothDefaults")) {
            reset = button;
        }
    }
    QVERIFY(reset);
    bool answered = false;
    QTimer::singleShot(0, &page, [&answered]() {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) { return; }
        answered = true;
        box->button(QMessageBox::Yes)->click();
    });
    reset->click();
    QVERIFY(answered);

    // The default is the profile's white, the fill's.
    QCOMPARE(QColor::fromString(stored(QStringLiteral("DisplayTraceColor"))), white);
    QCOMPARE(QColor::fromString(stored(QStringLiteral("DisplayFillColor"))), white);
    // Pan 1 follows the default: white trace, no key of its own.
    QCOMPARE(f.pan1.traceColor(), white);
    QCOMPARE(f.pan1.fillColor(), white);
    // The master reset replaces Pan 0's own trace color too.
    QCOMPARE(f.pan0.traceColor(), white);
    QCOMPARE(f.pan0.fillColor(), white);
    f.pan0.saveSettings();
    f.pan1.saveSettings();
    QVERIFY(!settings.contains(QStringLiteral("DisplayTraceColor_0")));
    QVERIFY(!settings.contains(QStringLiteral("DisplayTraceColor_1")));
    // A pan opened afterwards reads the white trace.
    SpectrumWidget later;
    later.setPanIndex(1);
    later.loadSettings();
    QCOMPARE(later.traceColor(), white);
}

// Fix wave I9. In the app, with Setup open on Spectrum Defaults: the
// station's FFT settings changed the way a pan's Display pill changes them.
// The five rows show the new values, no control says it changed, and each
// key is written once (by the window, never again by the page).
void TestDisplaySetupBinding::fftRowsFollowAChangeMadeOutsideSetup()
{
    Test::markAudioFirstRunDone();
    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    FftEnginePool* pool = window.fftEnginePoolForTest();
    QVERIFY(pool);
    QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
    auto* dialog = window.findChild<SetupDialog*>();
    QVERIFY(dialog);
    const auto closeDialog = qScopeGuard([dialog] { delete dialog; });
    QWidget* realized = dialog->realizePageForTest(QStringLiteral("FFT & Engine"));
    QVERIFY(realized);
    auto* page = qobject_cast<SpectrumDefaultsPage*>(realized);
    if (!page) { page = dialog->findChild<SpectrumDefaultsPage*>(); }
    if (page) { QVERIFY(realized->isAncestorOf(page->m_fftSizeSlider)); }
    QVERIFY(page);
    QCOMPARE(page->m_fftSizeSlider->value(), 0);
    QCOMPARE(page->m_decimationSpin->value(), 1);

    QSignalSpy sizeEchoes(page->m_fftSizeSlider, &QSlider::valueChanged);
    QSignalSpy windowEchoes(page->m_windowCombo, &QComboBox::currentIndexChanged);
    QSignalSpy targetEchoes(page->m_hzPerBinTargetSpin, &QDoubleSpinBox::valueChanged);
    QSignalSpy fpsEchoes(page->m_fpSlider, &QSlider::valueChanged);
    QSignalSpy fpsSpinEchoes(page->m_fpSpin, &QSpinBox::valueChanged);
    QSignalSpy decimationEchoes(page->m_decimationSpin, &QSpinBox::valueChanged);
    WriteCounter counter;

    window.applyFftBaseline(16384);
    window.applyDisplayFftWindow(2);
    window.applyDisplayHzPerBinTarget(2.5);
    window.applyDisplayFps(20);
    window.applyDisplayDecimation(2);

    // The target made the sizing mode Hz/bin, so the page's Sizing shows it
    // and its grayed size slider is on the size that runs, not the stored
    // 16384 (design D38).
    const QList<SpectrumOverlayPanel*> pills = window.findChildren<SpectrumOverlayPanel*>();
    QVERIFY(!pills.isEmpty());
    const int runningSize = pills.first()->fftState().runningSize;
    QVERIFY(runningSize > 0);
    QCOMPARE(page->m_fftSizingCombo->currentText(), QStringLiteral("Hz/bin"));
    QVERIFY(!page->m_fftSizeSlider->isEnabled());
    QCOMPARE(page->m_fftSizeSlider->value(), ControlRanges::displayFftSizePosition(runningSize));
    QCOMPARE(page->m_fftSizeReadout->text(), QString::number(runningSize));
    QCOMPARE(page->m_windowCombo->currentIndex(), 2);
    QVERIFY(page->m_hzPerBinTargetSpin->isEnabled());
    QCOMPARE(page->m_hzPerBinTargetSpin->value(), 2.5);
    QCOMPARE(page->m_fpSlider->value(), 20);
    QCOMPARE(page->m_fpSpin->value(), 20);
    QCOMPARE(page->m_decimationSpin->value(), 2);
    QCOMPARE(sizeEchoes.size(), 0);
    QCOMPARE(windowEchoes.size(), 0);
    QCOMPARE(targetEchoes.size(), 0);
    QCOMPARE(fpsEchoes.size(), 0);
    QCOMPARE(fpsSpinEchoes.size(), 0);
    QCOMPARE(decimationEchoes.size(), 0);
    QCOMPARE(counter.keys.count(QLatin1String(ControlRanges::kDisplayFftSizeKey)), 1);
    QCOMPARE(counter.keys.count(QLatin1String(ControlRanges::kDisplayFftWindowKey)), 1);
    QCOMPARE(counter.keys.count(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey)), 1);
    QCOMPARE(counter.keys.count(QLatin1String(ControlRanges::kDisplaySpectrumFpsKey)), 1);
    // The target made the sizing mode Hz/bin, where the stored 16384 plays
    // no part: the engines' baseline is 4096 (design D38).
    QCOMPARE(counter.keys.count(QLatin1String(ControlRanges::kDisplayFftSizingModeKey)), 1);
    QCOMPARE(pool->config().sizingMode, ControlRanges::DisplayFftSizingMode::HzPerBin);
    QCOMPARE(pool->config().fftSize, 4096);
    QCOMPARE(storedFftSize(), QStringLiteral("16384"));

    // Fixed chosen elsewhere (a pan's Display pill): the page's Sizing
    // follows, and its slider is live on the stored size.
    QSignalSpy sizingEchoes(page->m_fftSizingCombo, &QComboBox::currentIndexChanged);
    window.applyDisplayFftSizingMode(ControlRanges::DisplayFftSizingMode::Fixed, 0.0);
    QCOMPARE(sizingEchoes.size(), 0);
    QCOMPARE(page->m_fftSizingCombo->currentText(), QStringLiteral("Fixed"));
    QVERIFY(page->m_fftSizeSlider->isEnabled());
    QCOMPARE(page->m_fftSizeSlider->value(), 2);
    QCOMPARE(page->m_fftSizeReadout->text(), QStringLiteral("16384"));
    QVERIFY(!page->m_hzPerBinTargetSpin->isEnabled());
    QCOMPARE(page->m_hzPerBinTargetSpin->value(), 2.5);
    QCOMPARE(pool->config().fftSize, 16384);

    // A size the operator is still choosing is left alone: the handle is
    // held at 65536 when the frame rate changes elsewhere, and letting go
    // still makes 65536 the size.
    QSlider* size = page->m_fftSizeSlider;
    size->setSliderDown(true);
    size->setSliderPosition(4);
    window.applyDisplayFps(25);
    QCOMPARE(page->m_fpSlider->value(), 25);
    QCOMPARE(size->sliderPosition(), 4);
    size->setSliderDown(false);
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));
    QCOMPARE(pool->config().fftSize, 65536);
    QCOMPARE(page->m_fftSizeSlider->value(), 4);
}

// Rulings wave R2. An FFT size change from a pan's Display pill, from Setup
// and from a station setting change each leave the FFT size offset at the
// size's slider step times 2 dB (Thetis setup.cs:16154 [v2.10.3.13]) and
// every noise-floor tracker in fast attack (setup.cs:16156), and one change
// says so once.
void TestDisplaySetupBinding::everyFftSizeChangeSetsTheOffsetAndRestartsTheNoiseFloor()
{
    Test::markAudioFirstRunDone();
    // The Fixed sizing mode, where the FFT Size slider sets the size every
    // stream runs (design D38). In Auto and Hz/bin the slider's size is
    // stored and plays no part.
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    RadioModel* model = window.radioModel();
    FftEnginePool* pool = window.fftEnginePoolForTest();
    QVERIFY(pool);
    NereusSDR::FFTEngine* engine = model->fftEngine();
    QVERIFY(engine);
    NoiseFloorTracker* shared = model->noiseFloorTracker();
    QVERIFY(shared);
    NoiseFloorTracker* streams = model->noiseFloorTrackerForSlice(model->sliceById(0));
    QVERIFY(streams);
    QVERIFY(streams != shared);
    QCOMPARE(engine->fftSizeBaseline(), 4096);
    QCOMPARE(engine->fftSizeOffsetDb(), 0.0);

    QSignalSpy changes(engine, &NereusSDR::FFTEngine::fftSizeBaselineChanged);
    // A frame takes each tracker out of the fast attack it starts in.
    const QVector<float> frame(64, -120.0f);
    const auto settle = [&]() {
        shared->feed(frame, 33.0f);
        streams->feed(frame, 33.0f);
        QVERIFY(!shared->fastAttackPending());
        QVERIFY(!streams->fastAttackPending());
        changes.clear();
    };
    const auto inFastAttack = [&]() {
        return shared->fastAttackPending() && streams->fastAttackPending();
    };

    // A pan's Display pill.
    settle();
    if (QTest::currentTestFailed()) { return; }
    auto* pill = window.findChild<SpectrumOverlayPanel*>();
    QVERIFY(pill);
    emit pill->fftBaselineRequested(16384);
    QCOMPARE(storedFftSize(), QStringLiteral("16384"));
    QCOMPARE(engine->fftSizeBaseline(), 16384);
    QCOMPARE(engine->fftSizeOffsetDb(), 4.0);
    QVERIFY(inFastAttack());
    QCOMPARE(changes.size(), 1);
    // The same size again changes nothing.
    settle();
    if (QTest::currentTestFailed()) { return; }
    emit pill->fftBaselineRequested(16384);
    QCOMPARE(engine->fftSizeOffsetDb(), 4.0);
    QVERIFY(!inFastAttack());
    QCOMPARE(changes.size(), 0);

    // Setup's slider, let go at 65536.
    QVERIFY(QMetaObject::invokeMethod(&window, "createSetupDialog"));
    auto* dialog = window.findChild<SetupDialog*>();
    QVERIFY(dialog);
    const auto closeDialog = qScopeGuard([dialog] { delete dialog; });
    QWidget* realized = dialog->realizePageForTest(QStringLiteral("FFT & Engine"));
    QVERIFY(realized);
    auto* page = qobject_cast<SpectrumDefaultsPage*>(realized);
    if (!page) { page = dialog->findChild<SpectrumDefaultsPage*>(); }
    if (page) { QVERIFY(realized->isAncestorOf(page->m_fftSizeSlider)); }
    QVERIFY(page);
    settle();
    if (QTest::currentTestFailed()) { return; }
    QSlider* size = page->m_fftSizeSlider;
    size->setSliderDown(true);
    size->setSliderPosition(4);
    size->setSliderDown(false);
    QCOMPARE(storedFftSize(), QStringLiteral("65536"));
    QCOMPARE(engine->fftSizeBaseline(), 65536);
    QCOMPARE(engine->fftSizeOffsetDb(), 8.0);
    QVERIFY(inFastAttack());
    QCOMPARE(changes.size(), 1);

    // A station setting change: the key arrives, then the model says so.
    settle();
    if (QTest::currentTestFailed()) { return; }
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                     QStringLiteral("262144"));
    emit model->stationSettingChanged(QLatin1String(ControlRanges::kDisplayFftSizeKey));
    QCOMPARE(pool->config().fftSize, 262144);
    QCOMPARE(engine->fftSizeBaseline(), 262144);
    QCOMPARE(engine->fftSizeOffsetDb(), 12.0);
    QVERIFY(inFastAttack());
    QCOMPARE(changes.size(), 1);
    // The whole snapshot arriving with the same size changes nothing.
    settle();
    if (QTest::currentTestFailed()) { return; }
    emit model->stationSettingChanged(QString());
    QCOMPARE(engine->fftSizeOffsetDb(), 12.0);
    QVERIFY(!inFastAttack());
    QCOMPARE(changes.size(), 0);
}

// Design D43 (R-DSP-67). A sizing mode change, like a size change, leaves
// the FFT size offset at the step of the size the mode's control chose
// (the stored size in Fixed, 4096 in Auto and Hz/bin) and every noise-floor
// tracker in fast attack, and says so exactly once: when the baseline moves
// with the mode, and when it does not (Auto to Hz/bin and back). The same
// mode again says nothing.
void TestDisplaySetupBinding::aSizingModeChangeSetsTheOffsetAndRestartsTheNoiseFloorOnce()
{
    Test::markAudioFirstRunDone();
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey),
                                     QStringLiteral("16384"));
    MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
    startOneReceiver(window);
    if (QTest::currentTestFailed()) { return; }
    RadioModel* model = window.radioModel();
    NereusSDR::FFTEngine* engine = model->fftEngine();
    QVERIFY(engine);
    NoiseFloorTracker* shared = model->noiseFloorTracker();
    QVERIFY(shared);
    NoiseFloorTracker* streams = model->noiseFloorTrackerForSlice(model->sliceById(0));
    QVERIFY(streams);
    // Auto: the stored 16384 plays no part.
    QCOMPARE(engine->fftSizeBaseline(), 4096);
    QCOMPARE(engine->fftSizeOffsetDb(), 0.0);

    QSignalSpy changes(engine, &NereusSDR::FFTEngine::fftSizeBaselineChanged);
    const QVector<float> frame(64, -120.0f);
    const auto settle = [&]() {
        shared->feed(frame, 33.0f);
        streams->feed(frame, 33.0f);
        QVERIFY(!shared->fastAttackPending());
        QVERIFY(!streams->fastAttackPending());
        changes.clear();
    };
    const auto inFastAttack = [&]() {
        return shared->fastAttackPending() && streams->fastAttackPending();
    };
    using ControlRanges::DisplayFftSizingMode;
    struct Step {
        DisplayFftSizingMode mode;
        int baseline;
        double offsetDb;
        int said;
    };
    // Auto to Fixed and Fixed to Hz/bin move the baseline; Hz/bin to Auto
    // does not, and is said all the same.
    const Step steps[] = {
        {DisplayFftSizingMode::Fixed, 16384, 4.0, 1},
        {DisplayFftSizingMode::Fixed, 16384, 4.0, 0},
        {DisplayFftSizingMode::HzPerBin, 4096, 0.0, 1},
        {DisplayFftSizingMode::Auto, 4096, 0.0, 1},
        {DisplayFftSizingMode::Auto, 4096, 0.0, 0},
        {DisplayFftSizingMode::HzPerBin, 4096, 0.0, 1},
        {DisplayFftSizingMode::Fixed, 16384, 4.0, 1},
        {DisplayFftSizingMode::Auto, 4096, 0.0, 1},
    };
    for (const Step& step : steps) {
        settle();
        if (QTest::currentTestFailed()) { return; }
        window.applyDisplayFftSizingMode(step.mode);
        QCOMPARE(engine->fftSizeBaseline(), step.baseline);
        QCOMPARE(engine->fftSizeOffsetDb(), step.offsetDb);
        QCOMPARE(changes.size(), step.said);
        QCOMPARE(inFastAttack(), step.said == 1);
    }

    // The mode arriving as a station setting (the Core's, in a remote
    // window) goes the same way.
    settle();
    if (QTest::currentTestFailed()) { return; }
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
    emit model->stationSettingChanged(QLatin1String(ControlRanges::kDisplayFftSizingModeKey));
    QCOMPARE(engine->fftSizeBaseline(), 16384);
    QCOMPARE(engine->fftSizeOffsetDb(), 4.0);
    QVERIFY(inFastAttack());
    QCOMPARE(changes.size(), 1);
    // An older client's Hz/bin target arriving with the mode the Core gave
    // it: one change, said once, though two keys arrived.
    settle();
    if (QTest::currentTestFailed()) { return; }
    AppSettings::instance().setValue(
        QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
        QLatin1String(ControlRanges::kDisplayFftSizingModeHzPerBinToken));
    AppSettings::instance().setValue(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey),
                                     QStringLiteral("2.5"));
    emit model->stationSettingChanged(QLatin1String(ControlRanges::kDisplayFftSizingModeKey));
    emit model->stationSettingChanged(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey));
    QCOMPARE(engine->fftSizeBaseline(), 4096);
    QCOMPARE(engine->hzPerBinTarget(), 2.5);
    QCOMPARE(engine->fftSizeOffsetDb(), 0.0);
    QVERIFY(inFastAttack());
    QCOMPARE(changes.size(), 1);
}

// Fix wave I9, a remote window: the Core's FFT settings arrive in the
// station keys, and the open page reads them again without writing.
void TestDisplaySetupBinding::fftRowsInARemoteWindowFollowTheStationKeys()
{
    RadioModel model(RadioModel::Role::Remote);
    SpectrumDefaultsPage page(&model);
    auto& settings = AppSettings::instance();
    settings.setValue(QLatin1String(ControlRanges::kDisplayFftSizeKey), QStringLiteral("32768"));
    settings.setValue(QLatin1String(ControlRanges::kDisplayFftWindowKey), QStringLiteral("3"));
    settings.setValue(QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey),
                      QStringLiteral("1.5"));
    settings.setValue(QLatin1String(ControlRanges::kDisplaySpectrumFpsKey), QStringLiteral("45"));
    QSignalSpy sizeEchoes(page.m_fftSizeSlider, &QSlider::valueChanged);
    QSignalSpy windowEchoes(page.m_windowCombo, &QComboBox::currentIndexChanged);
    QSignalSpy targetEchoes(page.m_hzPerBinTargetSpin, &QDoubleSpinBox::valueChanged);
    QSignalSpy fpsEchoes(page.m_fpSlider, &QSlider::valueChanged);
    WriteCounter counter;

    page.stationFftSettingsChanged();

    QCOMPARE(page.m_fftSizeSlider->value(), 3);
    QCOMPARE(page.m_fftSizeReadout->text(), QStringLiteral("32768"));
    QCOMPARE(page.m_windowCombo->currentIndex(), 3);
    QCOMPARE(page.m_hzPerBinTargetSpin->value(), 1.5);
    QCOMPARE(page.m_fpSlider->value(), 45);
    QCOMPARE(page.m_fpSpin->value(), 45);
    QCOMPARE(sizeEchoes.size(), 0);
    QCOMPARE(windowEchoes.size(), 0);
    QCOMPARE(targetEchoes.size(), 0);
    QCOMPARE(fpsEchoes.size(), 0);
    QCOMPARE(counter.writes, 0);
}

QTEST_MAIN(TestDisplaySetupBinding)
#include "tst_display_setup_binding.moc"
