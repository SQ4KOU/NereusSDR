// no-port-check: NereusSDR-original test. Setup's All pans controls write
// the display master and reach every open pan, while a direct pan edit is
// a sparse override and must not leak to its neighbours.
//
// Modification history (NereusSDR):
//   2026-09-28  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"
#include "gui/ColorSwatchButton.h"
#include "gui/SpectrumWidget.h"
#include "gui/setup/DisplaySetupPages.h"
#include "models/Band.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

#include <memory>

using namespace NereusSDR;

namespace {

template <typename T>
T* byId(QWidget& page, const char* id)
{
    for (T* widget : page.findChildren<T*>()) {
        if (widget->property("nereusSetupId").toString() == QLatin1String(id)) {
            return widget;
        }
    }
    return nullptr;
}

const char* const kSharedKeys[] = {
    "DisplayShowBinWidth", "DisplayShowNoiseFloor", "DisplayShowNoiseFloorPosition",
    "DisplayNoiseFloorColor", "DisplayNoiseFloorTextColor", "DisplayNoiseFloorFastColor",
    "DisplayNoiseFloorLineWidth", "DisplayNoiseFloorShiftDb", "DisplayDispNormalize",
    "DisplayShowPeakValueOverlay", "DisplayPeakValuePosition", "DisplayPeakTextDelayMs",
    "DisplayPeakValueColor", "DisplayAdjustGridMinToNoiseFloor", "DisplayNFOffsetGridFollow",
    "DisplayMaintainNFAdjustDelta", "BandPlanFontSize", "DisplayGridStep",
    "DisplayGridMax_20m", "DisplayGridMin_20m", "Display3DFloorDepth_20m"};

} // namespace

class TestDisplaySharedSettingsAllPans : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        auto& s = AppSettings::instance();
        for (const char* key : kSharedKeys) {
            s.remove(QLatin1String(key));
            for (int pan = 0; pan < 4; ++pan) {
                s.remove(PanDisplayKeys::panKey(QString::fromLatin1(key), pan));
            }
        }
    }

    void setupMasterReachesEveryPanWhileDirectPanEditStaysLocal()
    {
        RadioModel model;
        SpectrumWidget first;
        SpectrumWidget second;
        auto third = std::make_unique<SpectrumWidget>();
        first.setPanIndex(0);
        second.setPanIndex(1);
        third->setPanIndex(2);
        for (SpectrumWidget* pan : {&first, &second, third.get()}) {
            pan->loadSettingsForTest();
        }
        model.setSpectrumWidget(&first);
        SpectrumDefaultsPage spectrum(&model);
        GridScalesPage grid(&model);
        SetupDisplayProviders providers;
        providers.panWidgets = [&first, &second, &third]() {
            return QList<SpectrumWidget*>{&first, &second, third.get()};
        };
        spectrum.setDisplayProviders(providers);
        grid.setDisplayProviders(providers);

        byId<QCheckBox>(spectrum, "display.spectrumDefaults.showBinWidth")->setChecked(true);
        byId<QCheckBox>(spectrum, "display.spectrumDefaults.showNoiseFloor")->setChecked(true);
        byId<QDoubleSpinBox>(spectrum, "display.spectrumDefaults.noiseFloorShift")->setValue(3.5);
        byId<QDoubleSpinBox>(spectrum, "display.spectrumDefaults.noiseFloorLineWidth")->setValue(2.5);
        byId<ColorSwatchButton>(spectrum, "display.spectrumDefaults.noiseFloorColor")
            ->setColor(QColor(0x10, 0x20, 0x30));
        byId<ColorSwatchButton>(spectrum, "display.spectrumDefaults.noiseFloorTextColor")
            ->setColor(QColor(0x40, 0x50, 0x60));
        byId<QCheckBox>(spectrum, "display.spectrumDefaults.normalize")->setChecked(true);
        byId<QCheckBox>(spectrum, "display.spectrumDefaults.showPeakValue")->setChecked(true);
        byId<QComboBox>(spectrum, "display.spectrumDefaults.peakValuePosition")->setCurrentIndex(3);
        byId<QSpinBox>(spectrum, "display.spectrumDefaults.peakTextDelay")->setValue(900);
        byId<QCheckBox>(grid, "display.gridScales.adjustGridMinToNoiseFloor")->setChecked(true);
        byId<QSpinBox>(grid, "display.gridScales.noiseFloorOffset")->setValue(-7);
        byId<QCheckBox>(grid, "display.gridScales.maintainGridRange")->setChecked(true);

        for (SpectrumWidget* pan : {&first, &second, third.get()}) {
            QVERIFY(pan->showBinWidth());
            QVERIFY(pan->showNoiseFloor());
            QCOMPARE(pan->nfShiftDbm(), 3.5f);
            QCOMPARE(pan->noiseFloorLineWidth(), 2.5f);
            QCOMPARE(pan->noiseFloorColor(), QColor(0x10, 0x20, 0x30));
            QCOMPARE(pan->noiseFloorTextColor(), QColor(0x40, 0x50, 0x60));
            QVERIFY(pan->dispNormalize());
            QVERIFY(pan->showPeakValueOverlay());
            QCOMPARE(pan->peakValuePosition(), SpectrumWidget::OverlayPosition::BottomRight);
            QCOMPARE(pan->peakTextDelayMs(), 900);
            QVERIFY(pan->adjustGridMinToNoiseFloor());
            QCOMPARE(pan->nfOffsetGridFollow(), -7);
            QVERIFY(pan->maintainNFAdjustDelta());
        }

        // A direct edit belongs only to pan 0 and becomes its sparse
        // override when that pan saves.
        first.setShowBinWidth(false);
        first.setNoiseFloorFastColor(QColor(0x70, 0x80, 0x90));
        first.setPeakValueColor(QColor(0xA0, 0xB0, 0xC0));
        first.setShowNoiseFloorPosition(SpectrumWidget::OverlayPosition::TopLeft);
        first.saveSettingsForTest();
        QVERIFY(!first.showBinWidth());
        QVERIFY(second.showBinWidth());
        QVERIFY(third->showBinWidth());
        QCOMPARE(first.noiseFloorFastColor(), QColor(0x70, 0x80, 0x90));
        QVERIFY(second.noiseFloorFastColor() != first.noiseFloorFastColor());
        QCOMPARE(first.peakValueColor(), QColor(0xA0, 0xB0, 0xC0));
        QVERIFY(second.peakValueColor() != first.peakValueColor());
        QCOMPARE(first.showNoiseFloorPosition(), SpectrumWidget::OverlayPosition::TopLeft);
        QVERIFY(second.showNoiseFloorPosition() != first.showNoiseFloorPosition());

        // Neighbour saves leave both the master and pan 0's override intact.
        second.saveSettingsForTest();
        third->saveSettingsForTest();
        auto& s = AppSettings::instance();
        QCOMPARE(s.value(QStringLiteral("DisplayShowBinWidth")).toString(), QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplayShowBinWidth_0")).toString(), QStringLiteral("False"));
        QVERIFY(!s.contains(QStringLiteral("DisplayShowBinWidth_1")));
        QVERIFY(!s.contains(QStringLiteral("DisplayShowBinWidth_2")));
        QCOMPARE(s.value(QStringLiteral("DisplayShowNoiseFloor")).toString(), QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplayNoiseFloorShiftDb")).toString(), QStringLiteral("3.5"));
        QCOMPARE(s.value(QStringLiteral("DisplayDispNormalize")).toString(), QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplayPeakTextDelayMs")).toString(), QStringLiteral("900"));
        QCOMPARE(s.value(QStringLiteral("DisplayAdjustGridMinToNoiseFloor")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplayNFOffsetGridFollow")).toString(), QStringLiteral("-7"));

        // A later pan has no override and therefore takes the master.
        SpectrumWidget later;
        later.setPanIndex(3);
        later.loadSettingsForTest();
        QVERIFY(later.showBinWidth());
        QVERIFY(later.showNoiseFloor());
        QCOMPARE(later.nfOffsetGridFollow(), -7);
    }

    // The per-band grid, the global dB step and the per-band 3D floor are
    // one store for every pan's model: a change through one reaches the
    // others' slots, so a later band change there shows it.
    void perBandGridAndStepReachEveryPanModel()
    {
        PanadapterModel a;
        PanadapterModel b;
        a.setPerBandDbMax(Band::Band20m, -55);
        a.setPerBandDbMin(Band::Band20m, -125);
        a.setGridStep(6);
        a.setDss3DFloorDepthForBand(Band::Band20m, 9);
        QCOMPARE(b.perBandGrid(Band::Band20m).dbMax, -55);
        QCOMPARE(b.perBandGrid(Band::Band20m).dbMin, -125);
        QCOMPARE(b.gridStep(), 6);
        QCOMPARE(b.dss3DFloorDepthForBand(Band::Band20m), 9);
        PanadapterModel later;
        QCOMPARE(later.perBandGrid(Band::Band20m).dbMax, -55);
        QCOMPARE(later.gridStep(), 6);
    }
};

QTEST_MAIN(TestDisplaySharedSettingsAllPans)
#include "tst_display_shared_settings_all_pans.moc"
