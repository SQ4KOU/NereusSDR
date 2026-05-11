// no-port-check: NereusSDR-original unit-test file.  RADE channel-swap
// is a NereusSDR-native extension; no Thetis equivalent.
// =================================================================
// tests/tst_slice_model_rade_swap.cpp  (NereusSDR)
// =================================================================
//
// Phase 3R Task J3 unit tests: SliceModel::setDspMode mode-aware
// channel swap between RxChannel (WDSP) and RadeChannel (third_party/rade
// neural codec).
//
// On the transition into DSPMode::RADE, setDspMode:
//   1. Destroys the slice's existing RxChannel via WdspEngine.
//   2. Creates a new RadeChannel via WdspEngine.
//   3. Calls RadioModel::wireRadeChannel(sliceId, channel, slice) to
//      plumb the channel's signals into the per-slice slot graph.
//   4. Starts the RadeChannel with the configured model path (or the
//      "dummy" librade sentinel if no path is set).
//
// On the reverse transition out of DSPMode::RADE, setDspMode:
//   1. Destroys the slice's RadeChannel.
//   2. Recreates the RxChannel with default args.
//
// The tests below reach the WdspEngine via the friend-access trick
// (NEREUS_BUILD_TESTS) to set m_initialized = true synchronously so
// createRxChannel does not error out on its !m_initialized guard.
// createRadeChannel does NOT require m_initialized (it is pure C++
// object management, no WDSP-side state).
//
// Test cases (5):
//   1. switchToRadeDestroysRxChannel
//   2. switchFromRadeReinstatesRxChannel
//   3. switchToRadeWiresChannelToRadioModel
//   4. switchToRadeStartsTheChannel
//   5. switchToSameModeIsNoOp
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-11 - New test file for Phase 3R Task J3.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/RadeChannel.h"
#include "core/WdspEngine.h"
#include "core/WdspTypes.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestSliceModelRadeSwap : public QObject {
    Q_OBJECT

    // RAII helper that wraps a stack-allocated RadioModel + primed
    // WdspEngine + seeded RxChannel.  Each test method constructs one,
    // exercises the swap, then the destructor unwinds the engine on
    // scope exit.  Stack-allocation keeps the engine lifetime
    // strictly within one test method so the global WDSP impulse-cache
    // tear-down (~WdspEngine -> shutdown -> destroy_impulse_cache)
    // runs at most once per method instead of accumulating.
    struct RadioFixture {
        RadioModel  radio;
        WdspEngine* engine{nullptr};
        SliceModel* slice{nullptr};

        RadioFixture()
        {
            engine = radio.wdspEngine();
            engine->m_initialized = true;  // friend access (NEREUS_BUILD_TESTS)

            // Seed slice 0 with a default RxChannel so the from-USB
            // transition has something to tear down.
            engine->createRxChannel(0);

            const int sliceIdx = radio.addSlice();
            Q_ASSERT(sliceIdx == 0);
            slice = radio.sliceAt(0);
            Q_ASSERT(slice != nullptr);
        }
    };

private slots:

    // ── Test 1: switchToRadeDestroysRxChannel ──────────────────────────
    //
    // After setDspMode(RADE), the slice's RxChannel is gone and a
    // RadeChannel exists in its place.
    void switchToRadeDestroysRxChannel() {
        RadioFixture fx;

        QVERIFY(fx.engine->rxChannel(0) != nullptr);
        QVERIFY(fx.engine->radeChannel(0) == nullptr);

        fx.slice->setDspMode(DSPMode::RADE);

        QVERIFY2(fx.engine->rxChannel(0) == nullptr,
                 "setDspMode(RADE) must destroy the slice's RxChannel");
        QVERIFY2(fx.engine->radeChannel(0) != nullptr,
                 "setDspMode(RADE) must create a RadeChannel for the slice");
    }

    // ── Test 2: switchFromRadeReinstatesRxChannel ──────────────────────
    //
    // After a round trip into RADE and back to a WDSP mode (USB), the
    // RadeChannel is gone and the RxChannel is back.
    void switchFromRadeReinstatesRxChannel() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE);
        QVERIFY(fx.engine->radeChannel(0) != nullptr);
        QVERIFY(fx.engine->rxChannel(0) == nullptr);

        fx.slice->setDspMode(DSPMode::USB);
        QVERIFY2(fx.engine->radeChannel(0) == nullptr,
                 "setDspMode(<non-RADE>) must destroy the slice's RadeChannel");
        QVERIFY2(fx.engine->rxChannel(0) != nullptr,
                 "setDspMode(<non-RADE>) must recreate the slice's RxChannel");
    }

    // ── Test 3: switchToRadeWiresChannelToRadioModel ───────────────────
    //
    // After setDspMode(RADE), the created RadeChannel's signals are
    // wired into RadioModel's slot graph.  The end-to-end signal
    // forwarding is covered by tst_rade_channel_model_wiring (I5);
    // here the load-bearing property is that the channel is reachable
    // through the engine lookup after the swap and that the swap did
    // not produce a spurious snrDb emission.
    void switchToRadeWiresChannelToRadioModel() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE);

        RadeChannel* ch = fx.engine->radeChannel(0);
        QVERIFY(ch != nullptr);
        QVERIFY(qIsNaN(fx.slice->snrDb()));  // no spurious emission yet
    }

    // ── Test 4: switchToRadeStartsTheChannel ───────────────────────────
    //
    // After setDspMode(RADE), the RadeChannel was started with the
    // "dummy" librade sentinel (no external model path persisted in
    // AppSettings).  isActive() returns true on a started channel.
    void switchToRadeStartsTheChannel() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE);

        RadeChannel* ch = fx.engine->radeChannel(0);
        QVERIFY(ch != nullptr);
        QVERIFY2(ch->isActive(),
                 "setDspMode(RADE) must call channel.start(\"dummy\") "
                 "so the channel is active and ready for I/Q + speech");
    }

    // ── Test 5: switchToSameModeIsNoOp ─────────────────────────────────
    //
    // Calling setDspMode with the current mode is a no-op: no channel
    // teardown / construction, no signal emission.
    void switchToSameModeIsNoOp() {
        RadioFixture fx;

        // Slice starts at USB (SliceModel::m_dspMode default).
        // Capture baseline RxChannel pointer.
        RxChannel* before = fx.engine->rxChannel(0);
        QVERIFY(before != nullptr);

        QSignalSpy modeSpy(fx.slice, &SliceModel::dspModeChanged);
        fx.slice->setDspMode(DSPMode::USB);  // same mode

        QCOMPARE(modeSpy.count(), 0);
        QCOMPARE(fx.engine->rxChannel(0), before);
        QVERIFY(fx.engine->radeChannel(0) == nullptr);
    }
};

QTEST_APPLESS_MAIN(TestSliceModelRadeSwap)
#include "tst_slice_model_rade_swap.moc"
