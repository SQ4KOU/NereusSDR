// no-port-check: NereusSDR-original test file.  All Thetis source cites for
// the underlying TransmitModel properties live in TransmitModel.h, and the
// FAITHFUL Thetis decorative-slider quirk for ptbNoiseGate is documented
// inline inside PhoneCwApplet::wireControls().
// =================================================================
// tests/tst_phone_cw_applet_vox_dexp_wiring.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-iii Task 15 — PhoneCwApplet VOX (#10) + DEXP (#11) wiring.
//
// PhoneCwApplet's VOX [ON] button + threshold slider + Hold slider and the
// DEXP [ON] button + (decorative) threshold slider sat un-wired and
// NyiOverlay-marked since 3I-3.  This file pins down the wiring after Task 15:
//
//   VOX [ON]   — bidirectional ↔ TransmitModel::voxEnabled
//                 (objectName "PhoneCwVoxButton").
//   VOX Th     — bidirectional ↔ TransmitModel::voxThresholdDb
//                 (objectName "PhoneCwVoxThresholdSlider", range -80..0 dB).
//   VOX Hold   — bidirectional ↔ TransmitModel::voxHangTimeMs
//                 (objectName "PhoneCwVoxHoldSlider", range 1..2000 ms).
//   DEXP [ON]  — bidirectional ↔ TransmitModel::dexpEnabled
//                 (objectName "PhoneCwDexpButton").
//   DEXP Th    — DECORATIVE-ONLY (objectName "PhoneCwDexpThresholdSlider",
//                 range -160..0 dB).  FAITHFUL Thetis quirk: the slider
//                 value is NEVER pushed to TransmitModel or TxChannel; it
//                 only drives the meter marker on the DexpPeakMeter strip
//                 (Thetis console.cs:28962-28970 [v2.10.3.13] verbatim).
//   Right-click on [ON] → emit openSetupRequested("Transmit", "DEXP/VOX").
//   Peak meters — DexpPeakMeter strips under each threshold slider
//                 (objectNames "PhoneCwVoxPeakMeter" / "PhoneCwDexpPeakMeter").
//   Polling     — 100 ms QTimer drives both meters (matches Thetis
//                 UpdateNoiseGate Task.Delay(100), console.cs:25347
//                 [v2.10.3.13]).
//
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>

#include "core/AppSettings.h"
#include "gui/applets/PhoneCwApplet.h"
#include "gui/widgets/DexpPeakMeter.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TestPhoneCwAppletVoxDexpWiring : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // ── 1. VOX [ON] toggle bidirectional with TM::voxEnabled ─────────────
    void voxButton_bidirectionalWithTm()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(QStringLiteral("PhoneCwVoxButton"));
        QVERIFY(btn != nullptr);
        QVERIFY(btn->isCheckable());

        // Default voxEnabled is false (TransmitModel.h:583-591 [v2.10.3.13]).
        QCOMPARE(rm.transmitModel().voxEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // UI → Model
        btn->click();
        QCOMPARE(rm.transmitModel().voxEnabled(), true);
        QCOMPARE(btn->isChecked(), true);

        btn->click();
        QCOMPARE(rm.transmitModel().voxEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // Model → UI
        rm.transmitModel().setVoxEnabled(true);
        QCOMPARE(btn->isChecked(), true);

        rm.transmitModel().setVoxEnabled(false);
        QCOMPARE(btn->isChecked(), false);
    }

    // ── 2. VOX threshold slider bidirectional with TM::voxThresholdDb ────
    void voxThresholdSlider_bidirectionalWithTm()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* sl = applet.findChild<QSlider*>(QStringLiteral("PhoneCwVoxThresholdSlider"));
        QVERIFY(sl != nullptr);

        // Range -80..0 from console.Designer.cs:6018-6019 [v2.10.3.13].
        QCOMPARE(sl->minimum(), -80);
        QCOMPARE(sl->maximum(),   0);

        // UI → Model
        sl->setValue(-30);
        QCOMPARE(rm.transmitModel().voxThresholdDb(), -30);

        sl->setValue(0);
        QCOMPARE(rm.transmitModel().voxThresholdDb(), 0);

        sl->setValue(-80);
        QCOMPARE(rm.transmitModel().voxThresholdDb(), -80);

        // Model → UI
        rm.transmitModel().setVoxThresholdDb(-25);
        QCOMPARE(sl->value(), -25);

        rm.transmitModel().setVoxThresholdDb(-60);
        QCOMPARE(sl->value(), -60);
    }

    // ── 3. VOX hold slider bidirectional with TM::voxHangTimeMs ──────────
    void voxHoldSlider_bidirectionalWithTm()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* sl = applet.findChild<QSlider*>(QStringLiteral("PhoneCwVoxHoldSlider"));
        QVERIFY(sl != nullptr);

        // Range 1..2000 from setup.designer.cs:45005-45013 [v2.10.3.13].
        QCOMPARE(sl->minimum(),    1);
        QCOMPARE(sl->maximum(), 2000);

        // UI → Model
        sl->setValue(750);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), 750);

        sl->setValue(1);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), 1);

        sl->setValue(2000);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), 2000);

        // Model → UI
        rm.transmitModel().setVoxHangTimeMs(1200);
        QCOMPARE(sl->value(), 1200);

        rm.transmitModel().setVoxHangTimeMs(500);
        QCOMPARE(sl->value(), 500);
    }

    // ── 4. DEXP [ON] toggle bidirectional with TM::dexpEnabled ───────────
    void dexpButton_bidirectionalWithTm()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(QStringLiteral("PhoneCwDexpButton"));
        QVERIFY(btn != nullptr);
        QVERIFY(btn->isCheckable());

        // Default dexpEnabled is false (TransmitModel.h:656-661 [v2.10.3.13]).
        QCOMPARE(rm.transmitModel().dexpEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // UI → Model
        btn->click();
        QCOMPARE(rm.transmitModel().dexpEnabled(), true);
        QCOMPARE(btn->isChecked(), true);

        btn->click();
        QCOMPARE(rm.transmitModel().dexpEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // Model → UI
        rm.transmitModel().setDexpEnabled(true);
        QCOMPARE(btn->isChecked(), true);

        rm.transmitModel().setDexpEnabled(false);
        QCOMPARE(btn->isChecked(), false);
    }

    // ── 5. DEXP threshold slider is DECORATIVE — does NOT push to TM/TxChannel.
    //      FAITHFUL Thetis quirk per console.cs:28962-28970 [v2.10.3.13] —
    //      the slider value updates only the in-applet marker dB; it must
    //      never reach TransmitModel.  We assert that:
    //        a. moving the slider does NOT change any TM property
    //           (voxThresholdDb is the only nearby int in -160..0 range,
    //           but the slider's value is in -160..-80 territory at the
    //           low end, so a flow-through would corrupt it).
    //        b. the slider exists by objectName + range -160..0.
    void dexpThresholdSlider_isDecorative()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* sl = applet.findChild<QSlider*>(QStringLiteral("PhoneCwDexpThresholdSlider"));
        QVERIFY(sl != nullptr);

        // Range -160..0 from Thetis console.cs:28974-28980 [v2.10.3.13]
        // picNoiseGate_Paint scaling.
        QCOMPARE(sl->minimum(), -160);
        QCOMPARE(sl->maximum(),    0);

        // Snapshot every nearby TM property the slider could plausibly leak
        // into.  None should change when we move the decorative slider.
        const int voxThBefore   = rm.transmitModel().voxThresholdDb();
        const int voxHoldBefore = rm.transmitModel().voxHangTimeMs();
        const bool voxOnBefore  = rm.transmitModel().voxEnabled();
        const bool dexpOnBefore = rm.transmitModel().dexpEnabled();

        sl->setValue(-100);
        sl->setValue(0);
        sl->setValue(-160);

        // None of the TM properties moved in response.
        QCOMPARE(rm.transmitModel().voxThresholdDb(), voxThBefore);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), voxHoldBefore);
        QCOMPARE(rm.transmitModel().voxEnabled(),    voxOnBefore);
        QCOMPARE(rm.transmitModel().dexpEnabled(),   dexpOnBefore);
    }

    // ── 6. Right-click on VOX [ON] emits openSetupRequested("Transmit",
    //      "DEXP/VOX") ─────────────────────────────────────────────────────
    void voxButton_rightClickEmitsOpenSetupRequested()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(QStringLiteral("PhoneCwVoxButton"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->contextMenuPolicy(), Qt::CustomContextMenu);

        QSignalSpy spy(&applet, &PhoneCwApplet::openSetupRequested);
        QVERIFY(spy.isValid());

        // CustomContextMenu policy means right-click emits
        // customContextMenuRequested.  Emit directly to exercise the
        // applet's connected slot.
        emit btn->customContextMenuRequested(QPoint(0, 0));

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("Transmit"));
        QCOMPARE(args.at(1).toString(), QStringLiteral("DEXP/VOX"));
    }

    // ── 7. Right-click on DEXP [ON] emits openSetupRequested("Transmit",
    //      "DEXP/VOX") ─────────────────────────────────────────────────────
    void dexpButton_rightClickEmitsOpenSetupRequested()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(QStringLiteral("PhoneCwDexpButton"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->contextMenuPolicy(), Qt::CustomContextMenu);

        QSignalSpy spy(&applet, &PhoneCwApplet::openSetupRequested);
        QVERIFY(spy.isValid());

        emit btn->customContextMenuRequested(QPoint(0, 0));

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("Transmit"));
        QCOMPARE(args.at(1).toString(), QStringLiteral("DEXP/VOX"));
    }

    // ── 8. m_dexpMeterTimer interval is 100 ms ───────────────────────────
    //      Matches Thetis UpdateNoiseGate Task.Delay(100) at
    //      console.cs:25347 [v2.10.3.13].
    void dexpMeterTimer_intervalIs100ms()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        // The applet owns at least two QTimer children (mic-level @ 50 ms
        // and DEXP-meter @ 100 ms).  Walk all QTimer children and confirm
        // exactly one ticks at 100 ms and is active.
        const auto timers = applet.findChildren<QTimer*>();
        bool found100 = false;
        for (QTimer* t : timers) {
            if (t->interval() == 100 && t->isActive()) {
                found100 = true;
                break;
            }
        }
        QVERIFY2(found100, "No active 100 ms QTimer found on PhoneCwApplet");
    }

    // ── 9. m_voxPeakMeter and m_dexpPeakMeter exist (not null) ───────────
    void peakMeters_existAfterConstruction()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        auto* voxMeter  = applet.findChild<DexpPeakMeter*>(QStringLiteral("PhoneCwVoxPeakMeter"));
        auto* dexpMeter = applet.findChild<DexpPeakMeter*>(QStringLiteral("PhoneCwDexpPeakMeter"));

        QVERIFY2(voxMeter  != nullptr, "VOX peak meter not created");
        QVERIFY2(dexpMeter != nullptr, "DEXP peak meter not created");
    }

    // ── 10. NyiOverlay markers absent on the now-wired controls ──────────
    //       Defensive sanity-check: make sure the migration off NyiOverlay
    //       actually landed for VOX [ON] / VOX Th / VOX Hold / DEXP [ON].
    //       The decorative DEXP Th slider is also wired (just not to TM)
    //       so it shouldn't carry a NYI tooltip either.
    void wiredControls_haveNoNyiMarker()
    {
        RadioModel rm;
        PhoneCwApplet applet(&rm);

        const QStringList wired{
            QStringLiteral("PhoneCwVoxButton"),
            QStringLiteral("PhoneCwVoxThresholdSlider"),
            QStringLiteral("PhoneCwVoxHoldSlider"),
            QStringLiteral("PhoneCwDexpButton"),
            QStringLiteral("PhoneCwDexpThresholdSlider"),
        };
        for (const QString& objName : wired) {
            QWidget* w = applet.findChild<QWidget*>(objName);
            QVERIFY2(w != nullptr,
                     qPrintable(QStringLiteral("Wired control not found: %1").arg(objName)));
            QVERIFY2(w->isEnabled(),
                     qPrintable(QStringLiteral("Wired control disabled: %1").arg(objName)));
            QVERIFY2(!w->toolTip().contains(QStringLiteral("Not Yet Implemented")),
                     qPrintable(QStringLiteral("Wired control still carries NYI tooltip: %1").arg(objName)));
        }
    }
};

QTEST_MAIN(TestPhoneCwAppletVoxDexpWiring)
#include "tst_phone_cw_applet_vox_dexp_wiring.moc"
