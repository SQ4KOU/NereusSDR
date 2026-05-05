// no-port-check: NereusSDR-original test file.  Cite comments below point
// at Thetis lines that the asserted wiring mirrors; no Thetis logic is
// ported in this test file.
// =================================================================
// tests/tst_tx_applet_rf_power_per_band.cpp  (NereusSDR)
// =================================================================
//
// Regression tests for RF Power per-band persistence wiring.
//
// Bug pre-fix: moving the RF Power slider only wrote to TransmitModel::
// m_power (the runtime PWR scalar) and emitted powerChanged.  The per-band
// store m_powerByBand[currentBand] only updated indirectly via the
// setPowerUsingTargetDbm txMode-0 side-effect (TransmitModel.cpp:825),
// which is gated on connected radio + loaded PA profile + !TUNE.  And
// TxApplet::setCurrentBand only repainted the *Tune* Power slider — it
// never recalled the per-band value into the *RF* Power slider.  Result:
// app restart on a different band showed the wrong slider position, and
// disconnected slider moves never persisted.
//
// Fix: TxApplet's RF Power slider lambda now calls setPowerForBand
// directly (mirrors Thetis ptbPWR_Scroll at console.cs:28642), and
// setCurrentBand routes the per-band value through tx.setPower so the
// existing reverse-binding paints the slider (mirrors Thetis TXBand
// setter at console.cs:17513).
//
// Source references (cite comments only — no Thetis logic in tests):
//   console.cs:28642 [v2.10.3.13] — power_by_band[(int)_tx_band] = ptbPWR.Value
//   console.cs:17513 [v2.10.3.13] — PWR = power_by_band[(int)value]
//   console.cs:1813-1814 [v2.10.3.13] — power_by_band default 50 W
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QSlider>

#include "core/AppSettings.h"
#include "gui/applets/TxApplet.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {
constexpr const char* kTestMac = "AA:BB:CC:DD:EE:FF";
}  // namespace

class TestTxAppletRfPowerPerBand : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
    }

    void init()
    {
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // ── 1. Slider valueChanged writes per-band ──────────────────────────────
    //
    // Mirrors Thetis ptbPWR_Scroll at console.cs:28642 [v2.10.3.13]:
    //   power_by_band[(int)_tx_band] = ptbPWR.Value;
    //
    // The handler must update m_powerByBand[currentBand] directly — not
    // wait on the setPowerUsingTargetDbm gating.
    void slider_valueChanged_writesPerBand()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        // Default m_currentBand is Band20m.
        auto* slider = applet.findChild<QSlider*>(
            QStringLiteral("TxRfPowerSlider"));
        QVERIFY(slider != nullptr);

        // Pre-condition: Band20m is at the default 50 W.
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band20m), 50);

        // Drive a slider event — same path the user takes.
        slider->setValue(75);

        // Per-band slot for current band must now reflect the slider value.
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band20m), 75);
        // m_power (current-PWR runtime scalar) tracks too.
        QCOMPARE(rm.transmitModel().power(), 75);
    }

    // ── 2. setCurrentBand recalls per-band into the RF slider ───────────────
    //
    // Mirrors Thetis TXBand setter at console.cs:17513 [v2.10.3.13]:
    //   PWR = power_by_band[(int)value];
    void setCurrentBand_recallsPerBandIntoSlider()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        // Pre-seed three bands with distinct values.
        rm.transmitModel().setPowerForBand(Band::Band20m, 20);
        rm.transmitModel().setPowerForBand(Band::Band40m, 80);
        rm.transmitModel().setPowerForBand(Band::Band10m, 5);

        auto* slider = applet.findChild<QSlider*>(
            QStringLiteral("TxRfPowerSlider"));
        QVERIFY(slider != nullptr);

        applet.setCurrentBand(Band::Band40m);
        QCOMPARE(slider->value(), 80);

        applet.setCurrentBand(Band::Band10m);
        QCOMPARE(slider->value(), 5);

        applet.setCurrentBand(Band::Band20m);
        QCOMPARE(slider->value(), 20);
    }

    // ── 3. Bootstrap call with default-band still recalls ───────────────────
    //
    // Regression test for Gap 3: TxApplet::m_currentBand defaults to
    // Band20m.  When MainWindow.cpp:1578 calls
    //   txApplet->setCurrentBand(pan0->band())
    // and pan0->band() == Band20m, the previous early-return guard caused
    // the call to no-op, so the loaded m_powerByBand[Band20m] never made
    // it into the slider — slider stayed at construct-time default 100 W.
    void setCurrentBand_sameAsDefault_stillRecallsSlider()
    {
        RadioModel rm;
        // Pre-seed Band20m to a non-default value BEFORE the applet exists.
        rm.transmitModel().setPowerForBand(Band::Band20m, 35);

        TxApplet applet(&rm);
        auto* slider = applet.findChild<QSlider*>(
            QStringLiteral("TxRfPowerSlider"));
        QVERIFY(slider != nullptr);
        // Construct-time default is 100 (TxApplet.cpp:281).
        QCOMPARE(slider->value(), 100);

        // Bootstrap call — same as MainWindow.cpp:1578 with pan0 on Band20m.
        applet.setCurrentBand(Band::Band20m);

        // Slider must have pulled the per-band stored value, not stayed at
        // the construct-time default.
        QCOMPARE(slider->value(), 35);
    }

    // ── 4. Per-band isolation across slider moves ───────────────────────────
    //
    // Moving the slider on band A must not bleed into band B's stored value.
    void slider_movesOnDifferentBands_isolatedPerBand()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* slider = applet.findChild<QSlider*>(
            QStringLiteral("TxRfPowerSlider"));
        QVERIFY(slider != nullptr);

        applet.setCurrentBand(Band::Band20m);
        slider->setValue(40);
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band20m), 40);

        applet.setCurrentBand(Band::Band40m);
        slider->setValue(90);
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band40m), 90);

        applet.setCurrentBand(Band::Band10m);
        slider->setValue(15);
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band10m), 15);

        // Earlier bands must still hold their original assignments.
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band20m), 40);
        QCOMPARE(rm.transmitModel().powerForBand(Band::Band40m), 90);

        // Slider visually reflects the active-band value when re-selected.
        applet.setCurrentBand(Band::Band20m);
        QCOMPARE(slider->value(), 40);

        applet.setCurrentBand(Band::Band40m);
        QCOMPARE(slider->value(), 90);
    }

    // ── 5. Persistence round-trip across TransmitModel reload ───────────────
    //
    // Canonical bug repro: with a per-MAC scope active, slider moves on
    // distinct bands must survive TransmitModel reconstruction +
    // loadFromSettings(mac).
    void slider_writes_roundtripAcrossReload()
    {
        // Phase A — write through the applet → model → AppSettings chain.
        {
            RadioModel rm;
            // Activate per-MAC auto-persist.  After this call,
            // setPowerForBand writes to AppSettings on every change.
            rm.transmitModel().loadFromSettings(QString::fromLatin1(kTestMac));

            TxApplet applet(&rm);
            auto* slider = applet.findChild<QSlider*>(
                QStringLiteral("TxRfPowerSlider"));
            QVERIFY(slider != nullptr);

            applet.setCurrentBand(Band::Band20m);
            slider->setValue(72);

            applet.setCurrentBand(Band::Band40m);
            slider->setValue(33);

            applet.setCurrentBand(Band::Band6m);
            slider->setValue(8);
        }

        // Phase B — fresh TransmitModel reads the saved per-band values.
        TransmitModel reloaded;
        reloaded.loadFromSettings(QString::fromLatin1(kTestMac));

        QCOMPARE(reloaded.powerForBand(Band::Band20m), 72);
        QCOMPARE(reloaded.powerForBand(Band::Band40m), 33);
        QCOMPARE(reloaded.powerForBand(Band::Band6m),  8);

        // Untouched bands remain at the default.
        QCOMPARE(reloaded.powerForBand(Band::Band80m), 50);
        QCOMPARE(reloaded.powerForBand(Band::Band10m), 50);
    }

    // ── 6. powerByBandChanged signal fires on slider events ─────────────────
    //
    // Confirms the slider lambda routes through setPowerForBand (which
    // emits powerByBandChanged), not through some side-channel that would
    // break listeners (e.g. PA-cal UI watching the per-band store).
    void slider_emitsPowerByBandChanged()
    {
        RadioModel rm;
        TxApplet applet(&rm);
        applet.setCurrentBand(Band::Band40m);

        QSignalSpy spy(&rm.transmitModel(),
                       &TransmitModel::powerByBandChanged);

        auto* slider = applet.findChild<QSlider*>(
            QStringLiteral("TxRfPowerSlider"));
        QVERIFY(slider != nullptr);

        slider->setValue(60);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<Band>(), Band::Band40m);
        QCOMPARE(spy.at(0).at(1).toInt(), 60);
    }
};

QTEST_MAIN(TestTxAppletRfPowerPerBand)
#include "tst_tx_applet_rf_power_per_band.moc"
