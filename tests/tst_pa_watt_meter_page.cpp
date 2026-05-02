// tests/tst_pa_watt_meter_page.cpp  (NereusSDR)
//
// Setup IA reshape Phase 3A — PaCalibrationGroup migrated from Hardware →
// Calibration tab to PA → Watt Meter page.
// no-port-check: test fixture — no Thetis attribution required.
//
// Verifies the live wiring of PaCalibrationGroup as a child of
// PaWattMeterPage, and the page's response to CalibrationController
// paCalProfileChanged signals (radio swap rebuild path).
//
// Source attribution (lives in production headers, not the test):
//   Thetis tpWattMeter   setup.designer.cs:49304-49309 [v2.10.3.13 @501e3f51]
//   Thetis CalibratedPAPower  console.cs:6691-6724      [v2.10.3.13 @501e3f51]

#include <QtTest/QtTest>

#include "core/CalibrationController.h"
#include "core/PaCalProfile.h"
#include "gui/setup/PaSetupPages.h"
#include "gui/setup/hardware/PaCalibrationGroup.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TstPaWattMeterPage : public QObject {
    Q_OBJECT

private slots:
    void page_constructs_with_PaCalibrationGroup_child();
    void group_populated_from_controller_on_construction();
    void page_repopulates_on_paCalProfileChanged();
};

// ---------------------------------------------------------------------------
// Constructing the page must instantiate exactly one PaCalibrationGroup
// child (the migrated cal-spinbox group).
// ---------------------------------------------------------------------------
void TstPaWattMeterPage::page_constructs_with_PaCalibrationGroup_child()
{
    RadioModel model;
    PaWattMeterPage page(&model);

    auto* grp = page.findChild<PaCalibrationGroup*>();
    QVERIFY2(grp != nullptr,
             "PaWattMeterPage must host a PaCalibrationGroup child after Phase 3A migration");
}

// ---------------------------------------------------------------------------
// Pre-seeding the controller with an Anan100 profile must yield a fully
// populated 10-spinbox group at construction time (10 W .. 100 W labels).
// ---------------------------------------------------------------------------
void TstPaWattMeterPage::group_populated_from_controller_on_construction()
{
    RadioModel model;
    auto& ctrl = model.calibrationControllerMutable();
    ctrl.setPaCalProfile(PaCalProfile::defaults(PaCalBoardClass::Anan100));

    PaWattMeterPage page(&model);

    auto* grp = page.findChild<PaCalibrationGroup*>();
    QVERIFY(grp != nullptr);
    QCOMPARE(grp->spinBoxCountForTest(), 10);
    QCOMPARE(grp->labelTextForTest(1), QString("10 W"));
    QCOMPARE(grp->labelTextForTest(10), QString("100 W"));
}

// ---------------------------------------------------------------------------
// After construction with default (None) profile, switching the controller
// to Anan10 must rebuild the embedded group with 1 W .. 10 W labels.
// ---------------------------------------------------------------------------
void TstPaWattMeterPage::page_repopulates_on_paCalProfileChanged()
{
    RadioModel model;
    PaWattMeterPage page(&model);

    auto* grp = page.findChild<PaCalibrationGroup*>();
    QVERIFY(grp != nullptr);
    // Default-constructed RadioModel has no profile yet → None → 0 spinboxes.
    QCOMPARE(grp->spinBoxCountForTest(), 0);

    auto& ctrl = model.calibrationControllerMutable();
    ctrl.setPaCalProfile(PaCalProfile::defaults(PaCalBoardClass::Anan10));

    // The page's lambda subscribed to paCalProfileChanged should have rebuilt.
    QCOMPARE(grp->spinBoxCountForTest(), 10);
    QCOMPARE(grp->labelTextForTest(1), QString("1 W"));
    QCOMPARE(grp->labelTextForTest(10), QString("10 W"));
}

QTEST_MAIN(TstPaWattMeterPage)
#include "tst_pa_watt_meter_page.moc"
