// no-port-check: test fixture asserts UI behavior, no Thetis port
//
// Phase 3P-A Task 15: verify S-ATT spinbox max reads from
// BoardCapabilities::attenuator.maxDb (single source of truth).
//   HL2    → 0–63 dB  (mi0bot 6-bit LNA range, [@c26a8a4])
//   Hermes → 0–31 dB  (Thetis setup.cs:15765 [v2.10.3.13] base)
//
// RxApplet sets the spinbox range in buildUi() using
// m_model->boardCapabilities().attenuator.maxDb so the correct ceiling
// is in place from widget creation, before any radio connection occurs.

#include <QtTest/QtTest>
#include <QApplication>

#include "gui/applets/RxApplet.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestRxAppletAttRange : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!qApp) {
            static int   argc = 0;
            static char* argv = nullptr;
            new QApplication(argc, &argv);
        }
    }

    // HL2 S-ATT slider uses signed −28..+32 dB range (mi0bot
    // setup.cs:16085-16086 [v2.10.3.13-beta2] udHermesStepAttenuatorData).
    void hl2_slider_max_is_signed_range()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        RxApplet applet(nullptr, &model);
        QCOMPARE(applet.stepAttMaxForTest(), 32);
    }

    // All standard-attenuator boards stay at 0–31 dB (no Alex present at
    // construction time; Alex extension happens in connectSlice() on connect).
    void hermes_slider_max_is_31()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Hermes);
        RxApplet applet(nullptr, &model);
        QCOMPARE(applet.stepAttMaxForTest(), 31);
    }
};

QTEST_MAIN(TestRxAppletAttRange)
#include "tst_rxapplet_att_range.moc"
