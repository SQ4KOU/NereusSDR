// no-port-check: unit tests for SpectrumWidget MOX overlay boolean state.
// Phase 3M-1a H.1.
//
// Tests verify:
//   1. Fresh widget has MOX overlay off.
//   2. setMoxOverlay(true) stores state.
//   3. setMoxOverlay(false) clears state.
//   4. setMoxOverlay is idempotent (same-value calls don't crash).
//   5. setTxAttenuatorOffsetDb stores value.
//   6. setTxFilterVisible stores state.
//   7. Parity Task 31: display duplex is off by default; keyed with it on
//      the transmit grid loads but the receive span stays (and is what a
//      keyed save writes); turned off while keyed it loads the transmit span.
//
// No pixel-diff — visual verification is deferred until the GPU-path render
// loop is unit-testable.  State accessors confirm the slot set the field.
//
// Display pill plan Task 1 (2026-10-10): a pan's own value lives in
// "<base>_<N>", pan 0 included, the bare key is the Setup default, and a
// save writes a pan's key only when the value differs from what the pan
// inherits. Three cases read pan 0's value back from the bare key and were
// rewritten to read "<base>_0": txGrid_changes_share_the_computer_pair,
// keyed_save_leaves_the_receive_bandwidth_alone (which also saved the ship
// default span, now not written at all, so it uses another span) and
// duplex_keeps_the_receive_span_under_the_transmit_grid. What each one
// guards is unchanged. The store is cleared around every case, since one
// case's "_0" key would otherwise be the next case's override.

#include <QtTest/QtTest>
#include <memory>

#include "gui/SpectrumWidget.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestSpectrumWidgetMoxOverlay : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }
    void overlay_disabledByDefault();
    void setMoxOverlay_trueStoresState();
    void setMoxOverlay_falseStoresState();
    void setMoxOverlay_idempotent();
    void setTxAttenuatorOffsetDb_storesValue();
    void setTxFilterVisible_storesState();
    void grids_are_independent_across_mox();
    void txGrid_changes_share_the_computer_pair();
    void transmitScaleMigratesHistoricMasterAndRetiresCanonicalPanKeys();
    void keyedScaleUpdatesEveryPanAndSurvivesAnInactivePansSave();
    void sharedTransmitApiPreservesReceiveAndNativeStorageBounds();
    void newerTransmitEditWinsReentrantNotificationAndRetiredPeer();
    void no_pointer_path_tunes_while_keyed();
    void keyed_save_leaves_the_receive_bandwidth_alone();
    void txGrid_load_accepts_what_the_drag_can_produce();
    void tx_low_colour_is_used_below_the_floor();
    void duplex_keeps_the_receive_span_under_the_transmit_grid();
};

// 1. Fresh widget has MOX overlay off
void TestSpectrumWidgetMoxOverlay::overlay_disabledByDefault()
{
    SpectrumWidget w;
    QVERIFY(!w.isMoxOverlayActive());
    QCOMPARE(w.txAttenuatorOffsetDb(), 0.0f);
    QVERIFY(!w.txFilterVisible());
}

// 2. setMoxOverlay(true) stores state
void TestSpectrumWidgetMoxOverlay::setMoxOverlay_trueStoresState()
{
    SpectrumWidget w;
    w.setMoxOverlay(true);
    QVERIFY(w.isMoxOverlayActive());
}

// 3. setMoxOverlay(false) clears state
void TestSpectrumWidgetMoxOverlay::setMoxOverlay_falseStoresState()
{
    SpectrumWidget w;
    w.setMoxOverlay(true);
    QVERIFY(w.isMoxOverlayActive());
    w.setMoxOverlay(false);
    QVERIFY(!w.isMoxOverlayActive());
}

// 4. setMoxOverlay is idempotent (calling with same value must not crash)
void TestSpectrumWidgetMoxOverlay::setMoxOverlay_idempotent()
{
    SpectrumWidget w;
    w.setMoxOverlay(false);  // default, should be no-op
    QVERIFY(!w.isMoxOverlayActive());

    w.setMoxOverlay(true);
    w.setMoxOverlay(true);   // second call: idempotent
    QVERIFY(w.isMoxOverlayActive());

    w.setMoxOverlay(false);
    w.setMoxOverlay(false);  // second call: idempotent
    QVERIFY(!w.isMoxOverlayActive());
}

// 5. setTxAttenuatorOffsetDb stores value
void TestSpectrumWidgetMoxOverlay::setTxAttenuatorOffsetDb_storesValue()
{
    SpectrumWidget w;
    w.setTxAttenuatorOffsetDb(31.0f);
    QCOMPARE(w.txAttenuatorOffsetDb(), 31.0f);

    w.setTxAttenuatorOffsetDb(0.0f);
    QCOMPARE(w.txAttenuatorOffsetDb(), 0.0f);
}

// 6. setTxFilterVisible stores state
void TestSpectrumWidgetMoxOverlay::setTxFilterVisible_storesState()
{
    SpectrumWidget w;
    QVERIFY(!w.txFilterVisible());

    w.setTxFilterVisible(true);
    QVERIFY(w.txFilterVisible());

    w.setTxFilterVisible(false);
    QVERIFY(!w.txFilterVisible());
}

// 7. Receive and transmit keep independent dBm grids.
//
// From Thetis display.cs:1782-1804 [v2.10.3.15] — SpectrumGridMaxMoxModified
// returns tx_spectrum_grid_max while MOX is asserted and spectrum_grid_max
// otherwise. Two stores, one selector, nothing saved and restored.
//
// The version this replaced saved the receive grid on the MOX rise edge and
// captured whatever was live on the fall edge. That made the transmit range
// equal to "whatever the widget happened to be showing at un-key", so ANY
// writer between the edges silently redefined it -- the receive noise-floor
// tracker did exactly that on an ORION-class radio, whose receiver keeps
// running through transmit, and the next key-up came up unusable. Bench
// 2026-08-05.
void TestSpectrumWidgetMoxOverlay::grids_are_independent_across_mox()
{
    SpectrumWidget w;

    w.setDbmRange(-120.0f, -40.0f);          // a receive range
    const float rxRef   = w.refLevel();
    const float rxRange = w.dynamicRange();

    w.setMoxOverlay(true);
    // Transmit comes up on its own grid, not the receive one.
    QVERIFY2(!qFuzzyCompare(w.refLevel(), rxRef)
             || !qFuzzyCompare(w.dynamicRange(), rxRange),
             "transmit inherited the receive grid instead of its own");

    w.setDbmRange(-80.0f, 20.0f);            // adjust while transmitting
    const float txRef   = w.refLevel();
    const float txRange = w.dynamicRange();

    w.setMoxOverlay(false);
    // Receive is exactly what it was. The transmit edit did not leak.
    QCOMPARE(w.refLevel(), rxRef);
    QCOMPARE(w.dynamicRange(), rxRange);

    // And a receive-side change cannot reach the transmit grid -- this is
    // the property the save/restore version could not hold.
    w.setDbmRange(-130.0f, -30.0f);

    w.setMoxOverlay(true);
    QCOMPARE(w.refLevel(), txRef);
    QCOMPARE(w.dynamicRange(), txRange);
}

// The transmit grid keys are per pan, like every other display setting.
//
// They shipped as bare globals, which every pan read at startup AND wrote on
// every save while holding its own copy. Only the transmitting pan's copy
// ever changes, so the next save from any OTHER pan put the startup value
// back and the operator's drag silently reverted. Fails on the bare-key
// version: pan 1's save lands on pan 0's key. Pan 0's own key is
// "DisplayTxGridRefLevel_0"; the bare key is the Setup default and no pan's
// save writes it.
void TestSpectrumWidgetMoxOverlay::transmitScaleMigratesHistoricMasterAndRetiresCanonicalPanKeys()
{
    auto& settings = AppSettings::instance();
    settings.setValue(QStringLiteral("DisplayTxGridRefLevel"), QStringLiteral("12"));
    settings.setValue(QStringLiteral("DisplayTxGridDynamicRange"), QStringLiteral("80"));
    settings.setValue(QStringLiteral("DisplayTxGridRefLevel_0"), QStringLiteral("-10"));
    settings.setValue(QStringLiteral("DisplayTxGridDynamicRange_0"), QStringLiteral("150"));
    settings.setValue(QStringLiteral("DisplayTxGridRefLevel_7"), QStringLiteral("-20"));
    settings.setValue(QStringLiteral("DisplayTxGridRefLevel_backup"), QStringLiteral("17"));
    SpectrumWidget pan;
    pan.loadSettings();
    QCOMPARE(pan.transmitRefLevel(), 12.0f);
    QCOMPARE(pan.transmitDynamicRange(), 80.0f);
    QVERIFY(!settings.contains(QStringLiteral("DisplayTxGridRefLevel_0")));
    QVERIFY(!settings.contains(QStringLiteral("DisplayTxGridDynamicRange_0")));
    QVERIFY(!settings.contains(QStringLiteral("DisplayTxGridRefLevel_7")));
    QCOMPARE(settings.value(QStringLiteral("DisplayTxGridRefLevel_backup")).toString(), QStringLiteral("17"));
}

void TestSpectrumWidgetMoxOverlay::keyedScaleUpdatesEveryPanAndSurvivesAnInactivePansSave()
{
    SpectrumWidget first;
    SpectrumWidget second;
    second.setPanIndex(1);
    first.loadSettings();
    second.loadSettings();
    const float receiveMax = first.refLevel();
    const float receiveRange = first.dynamicRange();
    first.setMoxOverlay(true);
    first.setDbmRange(-68.0f, 12.0f);
    first.saveSettingsForTest();
    QCOMPARE(second.transmitRefLevel(), 12.0f);
    QCOMPARE(second.transmitDynamicRange(), 80.0f);
    second.saveSettingsForTest();
    QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayTxGridRefLevel")).toFloat(), 12.0f);
    QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayTxGridDynamicRange")).toFloat(), 80.0f);
    first.setMoxOverlay(false);
    QCOMPARE(first.refLevel(), receiveMax);
    QCOMPARE(first.dynamicRange(), receiveRange);
    SpectrumWidget restarted;
    restarted.setPanIndex(7);
    restarted.loadSettings();
    QCOMPARE(restarted.transmitRefLevel(), 12.0f);
    QCOMPARE(restarted.transmitDynamicRange(), 80.0f);
}

void TestSpectrumWidgetMoxOverlay::sharedTransmitApiPreservesReceiveAndNativeStorageBounds()
{
    SpectrumWidget first;
    SpectrumWidget second;
    first.setDbmRange(-120.0f, -40.0f);
    second.setDbmRange(-110.0f, -30.0f);
    first.setTransmitDbmRange(-200.0f, 200.0f);
    QCOMPARE(first.transmitRefLevel(), 200.0f);
    QCOMPARE(first.transmitDynamicRange(), 200.0f); // no 400 dB storage range
    QCOMPARE(second.transmitRefLevel(), 200.0f);
    QCOMPARE(second.transmitDynamicRange(), 200.0f);
    QCOMPARE(first.refLevel(), -40.0f);
    QCOMPARE(first.dynamicRange(), 80.0f);
    QCOMPARE(second.refLevel(), -30.0f);
    QCOMPARE(second.dynamicRange(), 80.0f);
    second.setMoxOverlay(true);
    QCOMPARE(second.refLevel(), 200.0f);
    QCOMPARE(second.dynamicRange(), 200.0f);
    second.setMoxOverlay(false);
    QCOMPARE(second.refLevel(), -30.0f);
    QCOMPARE(second.dynamicRange(), 80.0f);
    first.setTransmitDbmRange(20.0f, 20.0f);
    QCOMPARE(first.transmitDynamicRange(), 10.0f);
    QCOMPARE(second.transmitDynamicRange(), 10.0f);
    QVERIFY(!first.perPanDisplayKeyBases().contains(QStringLiteral("DisplayTxGridRefLevel")));
    QVERIFY(!first.resettablePanDisplayKeyBases().contains(QStringLiteral("DisplayTxGridDynamicRange")));
}

void TestSpectrumWidgetMoxOverlay::newerTransmitEditWinsReentrantNotificationAndRetiredPeer()
{
    SpectrumWidget first;
    auto retired = std::make_unique<SpectrumWidget>();
    SpectrumWidget survivor;
    bool reentered = false;
    connect(&first, &SpectrumWidget::displaySettingChanged, &first,
        [&](const QString& key) {
            if (key != QStringLiteral("DisplayTxGridRefLevel") || reentered) { return; }
            reentered = true;
            retired.reset(); // a remaining fan-out recipient can disappear
            survivor.setTransmitDbmRange(-90.0f, 10.0f);
        });
    first.setTransmitDbmRange(-68.0f, 12.0f);
    QVERIFY(reentered);
    QVERIFY(!retired);
    QCOMPARE(first.transmitRefLevel(), 10.0f);
    QCOMPARE(first.transmitDynamicRange(), 100.0f);
    QCOMPARE(survivor.transmitRefLevel(), 10.0f);
    QCOMPARE(survivor.transmitDynamicRange(), 100.0f);
    QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayTxGridRefLevel")).toFloat(), 10.0f);
    QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayTxGridDynamicRange")).toFloat(), 100.0f);
}

// Approved rebuild: one computer TX pair replaces the old per-pan workaround.
void TestSpectrumWidgetMoxOverlay::txGrid_changes_share_the_computer_pair()
{
    auto& settings = AppSettings::instance();
    SpectrumWidget pan0;
    pan0.setPanIndex(0);
    pan0.setMoxOverlay(true);
    pan0.setDbmRange(-70.0f, 10.0f);
    pan0.saveSettingsForTest();
    QCOMPARE(settings.value(QStringLiteral("DisplayTxGridRefLevel")).toFloat(), 10.0f);
    QCOMPARE(settings.value(QStringLiteral("DisplayTxGridDynamicRange")).toFloat(), 80.0f);

    SpectrumWidget pan1;
    pan1.setPanIndex(1);
    pan1.setMoxOverlay(true);
    QCOMPARE(pan1.refLevel(), 10.0f); // newly opened pan reads the same pair
    pan1.setDbmRange(-100.0f, -20.0f);
    pan1.saveSettingsForTest();
    QCOMPARE(settings.value(QStringLiteral("DisplayTxGridRefLevel")).toFloat(), -20.0f);
    QCOMPARE(pan0.transmitRefLevel(), -20.0f);
    QCOMPARE(pan0.transmitDynamicRange(), 80.0f);
    pan0.saveSettingsForTest();
    QCOMPARE(settings.value(QStringLiteral("DisplayTxGridRefLevel")).toFloat(), -20.0f);
    for (const QString& base : {QStringLiteral("DisplayTxGridRefLevel"),
                                QStringLiteral("DisplayTxGridDynamicRange")}) {
        QVERIFY(!settings.contains(base + QStringLiteral("_0")));
        QVERIFY(!settings.contains(base + QStringLiteral("_1")));
    }
}

// Not one pointer path may move the radio while this pan is keyed.
//
// The original guard went on the short-pan-release branch alone, and that was
// not the only way in: a press inside the passband sets m_draggingVfo and
// mouseMoveEvent tunes on every move, a spot label tunes on press, and the
// wheel tunes on every notch. Codex found all three on PR #317.
//
// Asserted through requestTune's public effect rather than by synthesising
// mouse events: the contract being pinned is that frequencyClicked cannot
// escape while m_moxOverlay is set, whatever calls it. Every emitter is
// routed through that one function, which is checkable by grep and is what
// makes this test cover the paths it cannot drive.
void TestSpectrumWidgetMoxOverlay::no_pointer_path_tunes_while_keyed()
{
    SpectrumWidget w;
    QSignalSpy spy(&w, &SpectrumWidget::frequencyClicked);

    w.requestTuneForTest(14200000.0);
    QCOMPARE(spy.count(), 1);       // receive: tunes, as always

    w.setMoxOverlay(true);
    w.requestTuneForTest(14250000.0);
    QCOMPARE(spy.count(), 1);       // keyed: withheld

    w.setMoxOverlay(false);
    w.requestTuneForTest(14300000.0);
    QCOMPARE(spy.count(), 2);       // un-keyed: tunes again
    QCOMPARE(spy.at(1).at(0).toDouble(), 14300000.0);
}

// A save that lands mid-transmission must not write the transmit span into
// the receive bandwidth key.
//
// The direction-aware save protected the dBm pair and left DisplayBandwidth
// reading the live m_bandwidthHz, which IS the transmit span while keyed. Any
// setting change during a transmission put the transmit zoom in the receive
// key and the next launch came up with the receive pan at a transmit width.
// Found by Codex on PR #317.
void TestSpectrumWidgetMoxOverlay::keyed_save_leaves_the_receive_bandwidth_alone()
{
    auto& s = AppSettings::instance();

    SpectrumWidget w;
    w.setPanIndex(0);
    w.setFrequencyRange(14200000.0, 96000.0);    // the receive span
    w.saveSettingsForTest();
    const QString rxStored =
        s.value(QStringLiteral("DisplayBandwidth_0"), QString()).toString();
    QCOMPARE(rxStored.toFloat(), 96000.0f);

    w.setMoxOverlay(true);
    w.setFrequencyRange(14200000.0, 8000.0);     // a transmit span
    w.saveSettingsForTest();

    QCOMPARE(s.value(QStringLiteral("DisplayBandwidth_0"), QString())
                 .toString().toFloat(),
             96000.0f);
}

// The loader's bounds are the drag's bounds.
//
// They were [-160, +20] while the drag clamps to Thetis's [-200, +200], and
// +20 is exactly the transmit grid's default reference level. So an operator
// who dragged a pinned transmit scale UP saved the fix and had it silently
// discarded on the next launch. Found by Codex on PR #317.
void TestSpectrumWidgetMoxOverlay::txGrid_load_accepts_what_the_drag_can_produce()
{
    auto& s = AppSettings::instance();
    s.setValue(QStringLiteral("DisplayTxGridRefLevel"),
               QStringLiteral("60"));            // above the old +20 ceiling
    s.setValue(QStringLiteral("DisplayTxGridDynamicRange"),
               QStringLiteral("100"));

    SpectrumWidget w;
    w.setPanIndex(0);
    w.loadSettingsForTest();
    w.setMoxOverlay(true);                       // swap the transmit grid in

    QCOMPARE(w.refLevel(), 60.0f);
}

// TX Low Color is a control the operator can set, so it has to reach a pixel.
//
// It was persisted and read back and never consulted in dbmToRgb: a bin at or
// below m_txWfLowLevel produced adjusted == 0 and fell into the palette's
// first gradient stop. Thetis pairs the two in its TX branch
// (display.cs:6424-6427 [v2.10.3.15], low_threshold and low_color set
// together) and NereusSDR ported only the threshold. Found by Codex on
// PR #317.
void TestSpectrumWidgetMoxOverlay::tx_low_colour_is_used_below_the_floor()
{
    SpectrumWidget w;
    w.setTxWfLowLevel(-100);
    w.setTxWfHighLevel(0);
    w.setTxWfLowColor(QColor(17, 34, 51));      // nothing a palette would pick

    // Receive is untouched: it has no separate low-colour control, so the
    // floor there is still the palette's own first stop.
    const QRgb rxFloor = w.dbmToRgbForTest(-140.0f);
    QVERIFY2(rxFloor != qRgb(17, 34, 51),
             "the TX low colour leaked into the receive path");

    w.setMoxOverlay(true);
    QCOMPARE(w.dbmToRgbForTest(-140.0f), qRgb(17, 34, 51));  // under the floor
    QCOMPARE(w.dbmToRgbForTest(-100.0f), qRgb(17, 34, 51));  // exactly on it

    // And a bin above the floor still comes from the gradient.
    QVERIFY2(w.dbmToRgbForTest(-20.0f) != qRgb(17, 34, 51),
             "the low colour swallowed a bin above the floor");
}

// 7. Parity Task 31 (A11): display duplex.
void TestSpectrumWidgetMoxOverlay::duplex_keeps_the_receive_span_under_the_transmit_grid()
{
    auto& s = AppSettings::instance();
    SpectrumWidget w;
    QVERIFY(!w.displayDuplex());   // Thetis _display_duplex = false
    w.setPanIndex(0);
    w.setFrequencyRange(14200000.0, 192000.0);
    w.setDisplayDuplex(true);
    w.setMoxOverlay(true);
    QVERIFY(w.isMoxOverlayActive());
    QVERIFY(!w.showsTransmitView());
    QCOMPARE(w.bandwidth(), 192000.0);
    QCOMPARE(w.centerFrequency(), 14200000.0);
    // A zoom while keyed with DUP on is the receive view's, and saved as it.
    w.setFrequencyRange(14200000.0, 96000.0);
    w.saveSettingsForTest();
    QCOMPARE(s.value(QStringLiteral("DisplayBandwidth_0"), QString()).toString().toFloat(),
             96000.0f);
    // DUP off while keyed: the transmit span loads; on again: the receive
    // span comes back.
    w.setDisplayDuplex(false);
    QVERIFY(w.showsTransmitView());
    QVERIFY(w.bandwidth() != 96000.0);
    w.setDisplayDuplex(true);
    QCOMPARE(w.bandwidth(), 96000.0);
    w.setMoxOverlay(false);
    QCOMPARE(w.bandwidth(), 96000.0);
    QVERIFY(!w.isMoxOverlayActive());
}

QTEST_MAIN(TestSpectrumWidgetMoxOverlay)
#include "tst_spectrum_widget_mox_overlay.moc"
