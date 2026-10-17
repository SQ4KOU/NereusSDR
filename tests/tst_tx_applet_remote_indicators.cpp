// no-port-check: NereusSDR-original test; no upstream code ported.
// Modification history (NereusSDR):
//   2026-10-12: Remote indicator initialization/resync regression.
//               J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest/QtTest>
#include <QPushButton>
#include <QSignalSpy>
#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/TwoToneController.h"
#include "core/session/PureSignalSessionFacade.h"
#include "gui/applets/TxApplet.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TestTxAppletRemoteIndicators : public QObject
{
    Q_OBJECT
private:
    static void mirror(RadioModel& model, bool transmitting, bool tune, bool twoTone)
    {
        QCOMPARE(model.applyMirroredValue("transmitting", transmitting), QString());
        model.transmitModel().setTune(tune);
        QVERIFY(model.pureSignalFacade()->applyRemoteProperty("twoToneOn", twoTone));
    }
    static void check(TxApplet& applet, bool transmitting, bool tune, bool twoTone)
    {
        // Keep checking all lamps after one mismatch, so RED diagnoses
        // Tune and two-tone initialization independently of the MOX lamp.
        QTest::qCompare(applet.moxButton()->isChecked(), transmitting,
                        "MOX checked", "Core transmitting", __FILE__, __LINE__);
        QTest::qCompare(applet.tuneButton()->isChecked(), tune,
                        "TUNE checked", "Core tune", __FILE__, __LINE__);
        QTest::qCompare(applet.tuneButton()->text(),
                        tune ? QStringLiteral("TUNING...") : QStringLiteral("TUNE"),
                        "TUNE text", "Core tune text", __FILE__, __LINE__);
        QTest::qCompare(applet.twoToneButton()->isChecked(), twoTone,
                        "2-TONE checked", "Core twoToneOn", __FILE__, __LINE__);
    }
private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }
    void preexistingMirrors_data()
    {
        QTest::addColumn<bool>("transmitting");
        QTest::addColumn<bool>("tune");
        QTest::addColumn<bool>("twoTone");
        QTest::newRow("idle") << false << false << false;
        QTest::newRow("mox") << true << false << false;
        QTest::newRow("tune") << true << true << false;
        QTest::newRow("twoTone") << true << false << true;
    }
    void preexistingMirrors()
    {
        QFETCH(bool, transmitting);
        QFETCH(bool, tune);
        QFETCH(bool, twoTone);
        RadioModel model(RadioModel::Role::Remote);
        mirror(model, transmitting, tune, twoTone);
        QSignalSpy txChanges(&model, &RadioModel::transmittingChanged);
        QSignalSpy tuneChanges(&model.transmitModel(), &TransmitModel::tuneChanged);
        QSignalSpy psChanges(model.pureSignalFacade(), &PureSignalSessionFacade::statusChanged);
        QSignalSpy localKeys(model.moxController(), &MoxController::moxChanged);
        QSignalSpy localManualKeys(model.moxController(), &MoxController::manualMoxChanged);
        QSignalSpy refusedRequests(&model, &RadioModel::remoteTransmitRefused);
        QSignalSpy localTwoTone(model.twoToneController(), &TwoToneController::twoToneActiveChanged);
        TxApplet applet(&model);
        check(applet, transmitting, tune, twoTone);
        applet.syncFromModel();
        check(applet, transmitting, tune, twoTone);
        applet.setTwoToneController(model.twoToneController());
        check(applet, transmitting, tune, twoTone);
        QCOMPARE(txChanges.count(), 0);
        QCOMPARE(tuneChanges.count(), 0);
        QCOMPARE(psChanges.count(), 0);
        QCOMPARE(localKeys.count(), 0);
        QCOMPARE(localManualKeys.count(), 0);
        QCOMPARE(refusedRequests.count(), 0);
        QCOMPARE(localTwoTone.count(), 0);
        QVERIFY(!model.moxController()->isMox());
        QVERIFY(!model.twoToneController()->isActive());
    }
    void explicitSyncRetainsObservedState()
    {
        RadioModel model(RadioModel::Role::Remote);
        TxApplet applet(&model);
        mirror(model, true, true, true);
        check(applet, true, true, true);
        applet.syncFromModel();
        check(applet, true, true, true);
        mirror(model, false, false, false);
        check(applet, false, false, false);
    }
    void refusalRestoresObservedIndicators()
    {
        RadioModel model(RadioModel::Role::Remote);
        TxApplet applet(&model);
        // A pending press is allowed to show intent until Core feedback.
        // Block button signals so this offdevice fixture asks for no key.
        {
            QSignalBlocker mox(applet.moxButton());
            QSignalBlocker tune(applet.tuneButton());
            QSignalBlocker twoTone(applet.twoToneButton());
            applet.moxButton()->setChecked(true);
            applet.tuneButton()->setChecked(true);
            applet.tuneButton()->setText(QStringLiteral("TUNING..."));
            applet.twoToneButton()->setChecked(true);
        }
        check(applet, true, true, true);
        model.reportRemoteTransmitRefused(QStringLiteral("test refusal"));
        check(applet, false, false, false);
        QVERIFY(!model.moxController()->isMox());
    }
    void idleLocalControllerBindingRetainsCoreTwoTone()
    {
        RadioModel model(RadioModel::Role::Remote);
        TxApplet applet(&model);
        mirror(model, true, false, true);
        check(applet, true, false, true);
        applet.setTwoToneController(model.twoToneController());
        check(applet, true, false, true);
        // Even a local controller notification has no remote authority.
        model.twoToneController()->twoToneActiveChanged(false);
        check(applet, true, false, true);
        QVERIFY(!model.moxController()->isMox());
        QVERIFY(!model.twoToneController()->isActive());
    }
};
QTEST_MAIN(TestTxAppletRemoteIndicators)
#include "tst_tx_applet_remote_indicators.moc"
