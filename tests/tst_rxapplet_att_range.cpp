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
//
// R-R3-46 / R-R3-21 (2026-09-23): in a remote window the range is the one
// the Core reports for its own radio, once the Core offers its attenuator.
//
// Level Cal fix wave (2026-09-30): a slice on the other ADC shows and sets
// RX2's own preamp mode from RX2's list.

#include <QtTest/QtTest>
#include <QApplication>
#include <QSpinBox>
#include "core/AppSettings.h"
#include "core/RadioConnection.h"
#include "core/ConnectionState.h"
#include "core/BoardCapabilities.h"

#include "core/StepAttenuatorController.h"
#include "core/session/IStationLink.h"
#include "core/StepAttenuatorFacade.h"
#include "models/Band.h"
#include "models/SliceModel.h"
#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include "gui/applets/RxApplet.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class AttenuationConnection final : public RadioConnection {
    Q_OBJECT
public:
    explicit AttenuationConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
    {
        setState(ConnectionState::Connected);
    }

    QList<QPair<int, int>> rxCommands;
    QList<int> txCommands;

    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int dB) override { rxCommands.append({0, dB}); }
    void setAttenuatorForAdc(int adc, int dB) override { rxCommands.append({adc, dB}); }
    void setTxStepAttenuation(int dB) override { txCommands.append(dB); }
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void sendTxIq(const float*, int) override {}
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
};


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

    // The widget shares RX1's position with the keyed TX readout. An edit
    // must change that displayed TX lane, preserving RX's band memory.
    void keyedRx1EditWritesTxAndUnkeyRestoresRx()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::OrionMKII);
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setBoardIdentity(HPSDRHW::OrionMKII, HPSDRModel::ANAN7000D, true);
        ctrl.setStepAttEnabled(true);
        ctrl.setRx2StepAttEnabled(true);
        model.setStepAttController(&ctrl);
        AttenuationConnection radio;
        ctrl.setRadioConnection(&radio);
        const QString mac = QStringLiteral("02:00:00:00:A7:01");
        AppSettings::instance().clearHardwareValues(mac);
        ctrl.loadSettings(mac);
        ctrl.setBand(Band::Band20m);
        ctrl.setTxBand(Band::Band40m);
        ctrl.setPsActive(true);
        ctrl.setAttOnTxEnabled(true);
        ctrl.setAttenuation(5);
        ctrl.setAttOnTxValue(31);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        RxApplet applet(nullptr, &model);
        applet.setSlice(slice);
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(stack);
        auto* spin = stack->findChild<QSpinBox*>();
        QVERIFY(spin);
        QCOMPARE(spin->value(), 5);

        radio.rxCommands.clear();
        radio.txCommands.clear();
        QSignalSpy txChanges(&ctrl, &StepAttenuatorController::attOnTxValueChanged);
        ctrl.onMoxHardwareFlipped(true);
        QCOMPARE(spin->value(), 31);
        QCOMPARE(radio.txCommands, QList<int>{31});
        QVERIFY(radio.rxCommands.isEmpty());
        QCOMPARE(txChanges.count(), 0); // model echo must not dispatch
        radio.txCommands.clear();

        spin->setValue(20);
        QCOMPARE(ctrl.attOnTxValue(), 20);
        QCOMPARE(ctrl.txAttByBandForTest(Band::Band40m), 20);
        QCOMPARE(ctrl.txAttByBandForTest(Band::Band20m), 0);
        QCOMPARE(radio.txCommands, QList<int>{20});
        QVERIFY(radio.rxCommands.isEmpty());
        QCOMPARE(ctrl.attenuatorDb(), 20);
        QCOMPARE(spin->value(), 20);
        QCOMPARE(txChanges.count(), 1);

        ctrl.onMoxHardwareFlipped(false);
        QCOMPARE(ctrl.attenuatorDb(), 5);
        QCOMPARE(spin->value(), 5);
        ctrl.saveSettings(mac);
        QCOMPARE(AppSettings::instance().hardwareValue(mac,
                     QStringLiteral("options/stepAtt/rx1Band/") + bandKeyName(Band::Band20m)).toInt(), 5);
        QCOMPARE(txChanges.count(), 1); // RX restore does not write TX
        ctrl.setRadioConnection(nullptr);
        model.setStepAttController(nullptr);
    }

    void keyedAlexSpinboxReturnsToUnchangedClampedTx_data()
    {
        QTest::addColumn<int>("asked");
        QTest::newRow("increment-above-tx-ceiling") << 32;
        QTest::newRow("rx-ceiling") << 61;
    }

    void keyedAlexSpinboxReturnsToUnchangedClampedTx()
    {
        QFETCH(int, asked);
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Hermes);
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setBoardIdentity(HPSDRHW::Hermes, HPSDRModel::ANAN100, true);
        // Use the same board/Alex ceiling as local connect propagation.
        ctrl.setMaxAttenuation(BoardCapsTable::stepAttMaxDb(HPSDRHW::Hermes, true));
        QCOMPARE(ctrl.maxAttenuation(), 61);
        model.setStepAttController(&ctrl);
        AttenuationConnection radio;
        ctrl.setRadioConnection(&radio);
        const QString mac = QStringLiteral("02:00:00:00:A7:02");
        AppSettings::instance().clearHardwareValues(mac);
        ctrl.loadSettings(mac);
        ctrl.setBand(Band::Band20m);
        ctrl.setTxBand(Band::Band40m);
        ctrl.setPsActive(true);
        ctrl.setAttOnTxEnabled(true);
        ctrl.setStepAttEnabled(true);
        ctrl.setAttenuation(5);
        ctrl.setAttOnTxValue(31);
        ctrl.saveSettings(mac);
        const QString rxBandKey = QStringLiteral("options/stepAtt/rx1Band/")
            + bandKeyName(Band::Band20m);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        RxApplet applet(nullptr, &model);
        applet.setSlice(slice);
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(stack);
        auto* spin = stack->findChild<QSpinBox*>();
        QVERIFY(spin);
        QCOMPARE(spin->maximum(), 61);
        ctrl.onMoxHardwareFlipped(true);
        QCOMPARE(spin->value(), 31);
        radio.rxCommands.clear();
        radio.txCommands.clear();
        QSignalSpy displayChanges(&ctrl, &StepAttenuatorController::attenuationChanged);

        spin->setValue(asked);
        QCOMPARE(spin->value(), 31);
        QCOMPARE(ctrl.attenuatorDb(), 31);
        QCOMPARE(ctrl.attOnTxValue(), 31);
        QCOMPARE(ctrl.txAttByBandForTest(Band::Band40m), 31);
        QCOMPARE(radio.txCommands, QList<int>{31});
        QVERIFY(radio.rxCommands.isEmpty());
        QCOMPARE(AppSettings::instance().hardwareValue(mac, rxBandKey).toInt(), 5);
        QCOMPARE(displayChanges.count(), 1);

        // A second invalid edit is corrected too; the valid equality is quiet.
        spin->setValue(asked);
        QCOMPARE(spin->value(), 31);
        QCOMPARE(displayChanges.count(), 2);
        QCOMPARE(radio.txCommands, (QList<int>{31, 31}));
        ctrl.setDisplayedAttenuation(31);
        QCOMPARE(displayChanges.count(), 2);
        QVERIFY(radio.rxCommands.isEmpty());
        ctrl.onMoxHardwareFlipped(false);
        QCOMPARE(spin->value(), 5);
        QCOMPARE(ctrl.attenuatorDb(), 5);
        ctrl.saveSettings(mac);
        QCOMPARE(AppSettings::instance().hardwareValue(mac, rxBandKey).toInt(), 5);
        ctrl.setRadioConnection(nullptr);
        model.setStepAttController(nullptr);
    }

    void keyedDisplaySetterReflectsClampedTxValue_data()
    {
        QTest::addColumn<int>("minimum");
        QTest::newRow("standard") << 0;
        QTest::newRow("signed-hl2") << -28;
    }

    void keyedDisplaySetterReflectsClampedTxValue()
    {
        QFETCH(int, minimum);
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setMinAttenuation(minimum);
        ctrl.setPsActive(true);
        ctrl.setAttenuation(5);
        ctrl.setAttOnTxValue(15);
        AttenuationConnection radio;
        ctrl.setRadioConnection(&radio);
        ctrl.onMoxHardwareFlipped(true);
        radio.rxCommands.clear();
        radio.txCommands.clear();
        QSignalSpy displayChanges(&ctrl, &StepAttenuatorController::attenuationChanged);
        ctrl.setDisplayedAttenuation(63);
        QCOMPARE(ctrl.attOnTxValue(), 31);
        QCOMPARE(ctrl.attenuatorDb(), 31);
        QCOMPARE(radio.txCommands, QList<int>{31});
        QCOMPARE(displayChanges.count(), 1);
        ctrl.setDisplayedAttenuation(63);
        QCOMPARE(displayChanges.count(), 2); // correct repeated invalid request
        ctrl.setDisplayedAttenuation(31);
        QCOMPARE(displayChanges.count(), 2); // valid unchanged value has no echo
        ctrl.setDisplayedAttenuation(-50);
        QCOMPARE(ctrl.attOnTxValue(), minimum);
        QCOMPARE(ctrl.attenuatorDb(), minimum);
        QCOMPARE(displayChanges.count(), 3);
        QVERIFY(radio.rxCommands.isEmpty());
        ctrl.onMoxHardwareFlipped(false);
        QCOMPARE(ctrl.attenuatorDb(), 5);
        ctrl.setRadioConnection(nullptr);
    }

    void rxDisplayEditWritesRx_data()
    {
        QTest::addColumn<bool>("keyed");
        QTest::addColumn<bool>("attOnTx");
        QTest::addColumn<bool>("hpsdr");
        QTest::newRow("unkeyed") << false << true << false;
        QTest::newRow("att-on-tx-disabled") << true << false << false;
        QTest::newRow("hpsdr-board") << true << true << true;
    }

    void rxDisplayEditWritesRx()
    {
        QFETCH(bool, keyed);
        QFETCH(bool, attOnTx);
        QFETCH(bool, hpsdr);
        RadioModel model;
        model.setBoardForTest(HPSDRHW::OrionMKII);
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setIsHpsdrBoard(hpsdr);
        ctrl.setAttOnTxEnabled(attOnTx);
        ctrl.setAttenuation(5);
        ctrl.setAttOnTxValue(31);
        model.setStepAttController(&ctrl);
        AttenuationConnection radio;
        ctrl.setRadioConnection(&radio);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        RxApplet applet(nullptr, &model);
        applet.setSlice(slice);
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(stack);
        auto* spin = stack->findChild<QSpinBox*>();
        QVERIFY(spin);
        if (keyed) {
            ctrl.onMoxHardwareFlipped(true);
        }
        radio.rxCommands.clear();
        radio.txCommands.clear();
        spin->setValue(20);
        QCOMPARE(ctrl.attenuatorDb(), 20);
        QCOMPARE(ctrl.attOnTxValue(), 31);
        QCOMPARE(radio.rxCommands, (QList<QPair<int, int>>{{0, 20}}));
        QVERIFY(radio.txCommands.isEmpty());
        ctrl.setRadioConnection(nullptr);
        model.setStepAttController(nullptr);
    }

    void keyedOtherAdcEditStillWritesRx2()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::OrionMKII);
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setBoardIdentity(HPSDRHW::OrionMKII, HPSDRModel::ANAN7000D, true);
        ctrl.setStepAttEnabled(true);
        ctrl.setRx2StepAttEnabled(true);
        model.setStepAttController(&ctrl);
        SliceModel* a = model.sliceById(model.addSlice());
        SliceModel* b = model.sliceById(model.addSlice());
        QVERIFY(a && b);
        ctrl.setAdcRouting(0, 1, Band::Band20m, false, 1u << b->sliceIndex());
        ctrl.setAttenuation(5);
        ctrl.setRx2Attenuation(7);
        ctrl.setPsActive(true);
        ctrl.setAttOnTxValue(31);
        AttenuationConnection radio;
        ctrl.setRadioConnection(&radio);
        RxApplet applet(nullptr, &model);
        applet.setSlice(b);
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(stack);
        auto* spin = stack->findChild<QSpinBox*>();
        QVERIFY(spin);
        ctrl.onMoxHardwareFlipped(true);
        QCOMPARE(spin->value(), 7);
        radio.rxCommands.clear();
        radio.txCommands.clear();
        spin->setValue(20);
        QCOMPARE(ctrl.rx2AttenuatorDb(), 20);
        QCOMPARE(ctrl.attOnTxValue(), 31);
        QCOMPARE(ctrl.attenuatorDb(), 31);
        QCOMPARE(radio.rxCommands, (QList<QPair<int, int>>{{1, 20}}));
        QVERIFY(radio.txCommands.isEmpty());
        ctrl.setRadioConnection(nullptr);
        model.setStepAttController(nullptr);
    }

    void remoteSpinboxEditRetainsFacadeRefusal()
    {
        RadioModel remote(RadioModel::Role::Remote);
        remote.setBoardForTest(HPSDRHW::OrionMKII);
        RxApplet applet(nullptr, &remote);
        StepAttenuatorFacade* facade = remote.stepAttFacade();
        facade->setWindowAvailability(true, QString());
        facade->setEditGate([](QString*) { return true; });
        facade->setAttenuationDb(5);
        facade->setAttOnTxValue(31);
        facade->setEditGate([](QString* reason) {
            *reason = QStringLiteral("Test Core refuses this edit");
            return false;
        });
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(stack);
        auto* spin = stack->findChild<QSpinBox*>();
        QVERIFY(spin);
        QSignalSpy refusals(facade, &StepAttenuatorFacade::editRejected);
        spin->setValue(20);
        QCOMPARE(refusals.count(), 1);
        QCOMPARE(facade->attenuationDb(), 5);
        QCOMPARE(facade->attOnTxValue(), 31);
    }

    // HL2 S-ATT slider uses signed −28..+31 dB range.  mi0bot widens
    // upper bound to +32 at console.cs:11043 [v2.10.3.13-beta2] but that
    // is an off-by-one upstream bug (wire encoding `31 - userDb` produces
    // wire = -1 at userDb=32 → 6-bit-masks to LNA-gain wraparound region).
    // NereusSDR caps at +31 per maintainer approval (issue #175).
    void hl2_slider_max_is_signed_range()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        RxApplet applet(nullptr, &model);
        QCOMPARE(applet.stepAttMaxForTest(), 31);
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

    // A remote window: the board table stands until the Core offers its
    // attenuator, then the range is the Core's (Hermes with Alex: 0..61,
    // Thetis setup.cs:15773-15786 [v2.10.3.13] via stepAttMaxDb on the Core).
    void remote_slider_range_is_the_cores()
    {
        RadioModel remote(RadioModel::Role::Remote);
        remote.setBoardForTest(HPSDRHW::Hermes);
        RxApplet applet(nullptr, &remote);
        StepAttenuatorFacade* stepAtt = remote.stepAttFacade();
        stepAtt->applyRemoteProperty("minDb", 0);
        stepAtt->applyRemoteProperty("maxDb", 61);
        QCOMPARE(applet.stepAttMaxForTest(), 31);
        stepAtt->setWindowAvailability(true, QString());
        QCOMPARE(applet.stepAttMinForTest(), 0);
        QCOMPARE(applet.stepAttMaxForTest(), 61);

        // An HL2 Core reports its signed range.
        stepAtt->applyRemoteProperty("minDb", -28);
        stepAtt->applyRemoteProperty("maxDb", 31);
        QCOMPARE(applet.stepAttMinForTest(), -28);
        QCOMPARE(applet.stepAttMaxForTest(), 31);
    }

    // R-R3-46 / R-R3-11: the ATT / S-ATT / A-ATT label and which control
    // shows follow the step attenuator of the slice's own ADC (Thetis RX2's
    // enable and auto-attenuate for a slice on the other ADC). The preamp
    // choice there is RX2's own (Thetis comboRX2Preamp, RX2PreampMode).
    void localLabelFollowsTheSlicesAdc()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        auto* ctrl = new StepAttenuatorController(&model);
        ctrl->setTickTimerEnabled(false);
        model.setStepAttController(ctrl);
        SliceModel* a = model.sliceById(model.addSlice());
        SliceModel* b = model.sliceById(model.addSlice());
        QVERIFY(a && b);
        ctrl->setAdcRouting(0, 1, Band::Band20m, false, 1u << b->sliceIndex());
        ctrl->setStepAttEnabled(true);
        ctrl->setRx2StepAttEnabled(false);
        ctrl->setPreampMode(PreampMode::SaMinus20);
        RxApplet applet(nullptr, &model);
        auto* label = applet.findChild<QLabel*>(QStringLiteral("RxAttLabel"));
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        auto* combo = applet.findChild<QComboBox*>(QStringLiteral("RxPreampCombo"));
        QVERIFY(label && stack && combo);

        applet.setSlice(a);
        QCOMPARE(label->text(), QStringLiteral("S-ATT"));
        QCOMPARE(stack->currentIndex(), 1);

        applet.setSlice(b);
        QCOMPARE(label->text(), QStringLiteral("ATT"));
        QCOMPARE(stack->currentIndex(), 0);
        QVERIFY(combo->isEnabled());
        QVERIFY(combo->toolTip().isEmpty());
        // RX2's list and RX2's mode; a choice sets RX2's mode only.
        QStringList labels;
        for (int i = 0; i < combo->count(); ++i) {
            labels.append(combo->itemText(i));
        }
        QCOMPARE(labels, (QStringList{QStringLiteral("0dB"), QStringLiteral("-10dB"),
                                      QStringLiteral("-20dB"), QStringLiteral("-30dB")}));
        const PreampMode rx1Mode = ctrl->preampMode();
        ctrl->setRx2PreampMode(PreampMode::SaMinus30);
        QCOMPARE(combo->currentData().toInt(), static_cast<int>(PreampMode::SaMinus30));
        combo->setCurrentIndex(combo->findData(static_cast<int>(PreampMode::SaMinus10)));
        QCOMPARE(ctrl->rx2PreampMode(), PreampMode::SaMinus10);
        QCOMPARE(ctrl->preampMode(), rx1Mode);

        ctrl->setRx2StepAttEnabled(true);
        QCOMPARE(label->text(), QStringLiteral("S-ATT"));
        QCOMPARE(stack->currentIndex(), 1);
        ctrl->setRx2AutoAttEnabled(true);
        QCOMPARE(label->text(), QStringLiteral("A-ATT"));

        applet.setSlice(a);
        QCOMPARE(label->text(), QStringLiteral("S-ATT"));
        QVERIFY(combo->isEnabled());
        QCOMPARE(combo->currentData().toInt(), static_cast<int>(ctrl->preampMode()));
    }

    void remoteLabelFollowsTheSlicesAdc()
    {
        RadioModel remote(RadioModel::Role::Remote);
        remote.setBoardForTest(HPSDRHW::Saturn);
        RxApplet applet(nullptr, &remote);
        StepAttenuatorFacade* stepAtt = remote.stepAttFacade();
        stepAtt->setWindowAvailability(true, QString());
        stepAtt->applyRemoteProperty("rx2SliceMask", 1 << 1);
        auto* label = applet.findChild<QLabel*>(QStringLiteral("RxAttLabel"));
        auto* stack = applet.findChild<QStackedWidget*>(QStringLiteral("RxAttenuatorStack"));
        QVERIFY(label && stack);
        SliceModel b(1);
        applet.setSlice(&b);
        QCOMPARE(label->text(), QStringLiteral("ATT"));
        // No Core that carries RX2's own preamp mode: disabled with the reason.
        auto* combo = applet.findChild<QComboBox*>(QStringLiteral("RxPreampCombo"));
        QVERIFY(combo);
        QVERIFY(!combo->isEnabled());
        QCOMPARE(combo->toolTip(), IStationLink::rx2PreampModeUnavailableReason());
        stepAtt->setRx2StepAttEnabled(true);
        QCOMPARE(label->text(), QStringLiteral("S-ATT"));
        QCOMPARE(stack->currentIndex(), 1);
        stepAtt->setRx2AutoAttEnabled(true);
        QCOMPARE(label->text(), QStringLiteral("A-ATT"));
        applet.setSlice(nullptr);
    }
};

QTEST_MAIN(TestRxAppletAttRange)
#include "tst_rxapplet_att_range.moc"
