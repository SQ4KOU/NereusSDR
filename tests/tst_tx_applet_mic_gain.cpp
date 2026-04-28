// no-port-check: test-only — exercises NereusSDR-native TxApplet Mic Gain wiring.
// Phase 3M-1b J.1.
//
// TxApplet inserts a Mic Gain slider between RF Power (Row 3) and Tune Power
// (Row 4). The slider is bidirectional with TransmitModel::micGainDb and
// reflects mic-mute state (greyed when micMute == false).
//
// Test cases:
//  1. Default value  — micGainDb() default is -6 (plan §0 row 11).
//  2. Slider range from board caps — Hermes: -40/+10; Unknown: -50/+70.
//  3. UI → Model  — setMicGainDb(-10) → getter returns -10.
//  4. Model → UI  — micGainDbChanged fires with correct value.
//  5. Mic mute state — micMuteChanged(false) disables slider visual.
//                       micMuteChanged(true) re-enables it.
//  6. No feedback loop — setting micGainDb twice same value emits signal once.
//  7. Idempotency — repeat setMicGainDb(same) → only one micGainDbChanged.
//  8. Clamping — values outside [-50,70] (global range) are clamped.
//
// These tests exercise the model layer contracts that TxApplet::wireControls()
// depends on. TxApplet itself is a QWidget and requires a display server, so
// widget construction is not performed here (same pattern as
// tst_tx_applet_tune_wiring.cpp).

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "models/TransmitModel.h"
#include "core/BoardCapabilities.h"

using namespace NereusSDR;

class TestTxAppletMicGain : public QObject
{
    Q_OBJECT

private slots:
    void micGainDb_defaultIsMinusSix();
    void micGainRange_hermesBoard();
    void micGainRange_unknownBoard();
    void setMicGainDb_roundtrip();
    void micGainDbChanged_firesOnChange();
    void micMuteChanged_firesOnChange();
    void micMute_defaultTrue();
    void noFeedbackLoop_idempotentSameValue();
    void setMicGainDb_clampsToRange();
};

// 1. Default value — micGainDb() == -6 (NereusSDR safety default, plan §0 row 11).
void TestTxAppletMicGain::micGainDb_defaultIsMinusSix()
{
    TransmitModel tx;
    QCOMPARE(tx.micGainDb(), -6);
}

// 2a. Slider range for a known board (Hermes) uses Thetis defaults -40/+10.
// Porting from Thetis console.cs:19151-19171 [v2.10.3.13]:
//   private int mic_gain_min = -40;
//   private int mic_gain_max = 10;
void TestTxAppletMicGain::micGainRange_hermesBoard()
{
    // BoardCapabilities for HERMES board uses the Thetis defaults.
    // Default-constructed BoardCapabilities represents the known-board defaults.
    BoardCapabilities caps;
    QCOMPARE(caps.micGainMinDb, -40);
    QCOMPARE(caps.micGainMaxDb, +10);
}

// 2b. Unknown board uses the TransmitModel global fallback range -50/+70.
// From TransmitModel.h: kMicGainDbMin = -50, kMicGainDbMax = 70.
void TestTxAppletMicGain::micGainRange_unknownBoard()
{
    QCOMPARE(TransmitModel::kMicGainDbMin, -50);
    QCOMPARE(TransmitModel::kMicGainDbMax,  70);
}

// 3. UI → Model: setMicGainDb → getter returns the clamped value.
void TestTxAppletMicGain::setMicGainDb_roundtrip()
{
    TransmitModel tx;

    tx.setMicGainDb(-10);
    QCOMPARE(tx.micGainDb(), -10);

    tx.setMicGainDb(0);
    QCOMPARE(tx.micGainDb(), 0);

    tx.setMicGainDb(5);
    QCOMPARE(tx.micGainDb(), 5);
}

// 4. Model → UI: micGainDbChanged fires with the correct clamped dB value.
void TestTxAppletMicGain::micGainDbChanged_firesOnChange()
{
    TransmitModel tx;
    QSignalSpy spy(&tx, &TransmitModel::micGainDbChanged);

    tx.setMicGainDb(-20);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), -20);

    tx.setMicGainDb(3);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 3);
}

// 5. Mic mute state: micMuteChanged fires with correct bool.
// NOTE: micMute == true means mic IS in use (Thetis counter-intuitive naming,
//       console.cs:28752 [v2.10.3.13]: "although called MicMute, true = mic in use").
void TestTxAppletMicGain::micMuteChanged_firesOnChange()
{
    TransmitModel tx;
    // Default is true (mic in use).
    QVERIFY(tx.micMute());

    QSignalSpy spy(&tx, &TransmitModel::micMuteChanged);

    // Setting to false (muted): slider should be disabled in TxApplet.
    tx.setMicMute(false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(0).toBool());

    // Setting back to true (in use): slider should be re-enabled.
    tx.setMicMute(true);
    QCOMPARE(spy.count(), 2);
    QVERIFY(spy.at(1).at(0).toBool());
}

// 6. micMute default is true (mic in use — Thetis console.designer.cs:2029-2030).
void TestTxAppletMicGain::micMute_defaultTrue()
{
    TransmitModel tx;
    QVERIFY(tx.micMute());
}

// 7. No feedback loop / idempotency: setting same micGainDb value twice emits
//    the signal exactly once (second call is a no-op).
void TestTxAppletMicGain::noFeedbackLoop_idempotentSameValue()
{
    TransmitModel tx;
    tx.setMicGainDb(-10);  // first set — leaves model at -10

    QSignalSpy spy(&tx, &TransmitModel::micGainDbChanged);

    // Same value again — should be a no-op.
    tx.setMicGainDb(-10);
    QCOMPARE(spy.count(), 0);

    // Different value — should emit exactly once.
    tx.setMicGainDb(-5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), -5);
}

// 8. Clamping: values outside TransmitModel::kMicGainDbMin/Max are clamped.
void TestTxAppletMicGain::setMicGainDb_clampsToRange()
{
    TransmitModel tx;

    // Below minimum (-50).
    tx.setMicGainDb(-200);
    QCOMPARE(tx.micGainDb(), TransmitModel::kMicGainDbMin);

    // Above maximum (+70).
    tx.setMicGainDb(200);
    QCOMPARE(tx.micGainDb(), TransmitModel::kMicGainDbMax);

    // Exact bounds are accepted as-is.
    tx.setMicGainDb(TransmitModel::kMicGainDbMin);
    QCOMPARE(tx.micGainDb(), TransmitModel::kMicGainDbMin);

    tx.setMicGainDb(TransmitModel::kMicGainDbMax);
    QCOMPARE(tx.micGainDb(), TransmitModel::kMicGainDbMax);
}

QTEST_MAIN(TestTxAppletMicGain)
#include "tst_tx_applet_mic_gain.moc"
