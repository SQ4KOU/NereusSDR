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
// On the transition into either RADE sideband (DSPMode::RADE_U or
// DSPMode::RADE_L), setDspMode:
//   1. Destroys the slice's existing RxChannel via WdspEngine.
//   2. Creates a new RadeChannel via WdspEngine.
//   3. Calls RadeChannel::setSideband(true) for RADE_U or
//      setSideband(false) for RADE_L so the channel knows which
//      sideband it serves.
//   4. Calls RadioModel::wireRadeChannel(sliceId, channel, slice) to
//      plumb the channel's signals into the per-slice slot graph.
//   5. Starts the RadeChannel with the configured model path (or the
//      "dummy" librade sentinel if no path is set).
//
// On the reverse transition out of either RADE sideband, setDspMode:
//   1. Destroys the slice's RadeChannel.
//   2. Recreates the RxChannel with default args.
//
// On a RADE_U <-> RADE_L transition (sideband flip), setDspMode:
//   1. Destroys the slice's RadeChannel.
//   2. Creates a fresh RadeChannel with the new sideband flag.
//   This matches the standing J3 "mode change implies channel swap"
//   pattern; it is simpler than reusing the channel and re-calling
//   setSideband, and keeps the lifecycle bookkeeping uniform.
//
// The tests below reach the WdspEngine via the friend-access trick
// (NEREUS_BUILD_TESTS) to set m_initialized = true synchronously so
// createRxChannel does not error out on its !m_initialized guard.
// createRadeChannel does NOT require m_initialized (it is pure C++
// object management, no WDSP-side state).
//
// Test cases (8):
//   1. switchToRadeUpperDestroysRxChannel
//   2. switchToRadeLowerDestroysRxChannel
//   3. switchFromRadeUpperReinstatesRxChannel
//   4. switchFromRadeLowerReinstatesRxChannel
//   5. switchToRadeUpperSetsSidebandFlag
//   6. switchToRadeLowerSetsSidebandFlag
//   7. switchRadeUpperToLowerFlipsSideband  (recreate path verified
//      via post-state, not pointer identity)
//   8. switchToSameModeIsNoOp
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-11 - New test file for Phase 3R Task J3.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-05-11 - Split RADE into RADE_U / RADE_L; covered both
//                 sidebands + the U <-> L recreate path.  J.J. Boyd
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

    // ── Test 1: switchToRadeUpperDestroysRxChannel ─────────────────────
    //
    // After setDspMode(RADE_U), the slice's RxChannel is gone and a
    // RadeChannel exists in its place.
    void switchToRadeUpperDestroysRxChannel() {
        RadioFixture fx;

        QVERIFY(fx.engine->rxChannel(0) != nullptr);
        QVERIFY(fx.engine->radeChannel(0) == nullptr);

        fx.slice->setDspMode(DSPMode::RADE_U);

        QVERIFY2(fx.engine->rxChannel(0) == nullptr,
                 "setDspMode(RADE_U) must destroy the slice's RxChannel");
        QVERIFY2(fx.engine->radeChannel(0) != nullptr,
                 "setDspMode(RADE_U) must create a RadeChannel for the slice");
    }

    // ── Test 2: switchToRadeLowerDestroysRxChannel ─────────────────────
    //
    // Same as Test 1 but for the lower-sideband variant.
    void switchToRadeLowerDestroysRxChannel() {
        RadioFixture fx;

        QVERIFY(fx.engine->rxChannel(0) != nullptr);
        QVERIFY(fx.engine->radeChannel(0) == nullptr);

        fx.slice->setDspMode(DSPMode::RADE_L);

        QVERIFY2(fx.engine->rxChannel(0) == nullptr,
                 "setDspMode(RADE_L) must destroy the slice's RxChannel");
        QVERIFY2(fx.engine->radeChannel(0) != nullptr,
                 "setDspMode(RADE_L) must create a RadeChannel for the slice");
    }

    // ── Test 3: switchFromRadeUpperReinstatesRxChannel ─────────────────
    //
    // After a round trip into RADE_U and back to a WDSP mode (USB), the
    // RadeChannel is gone and the RxChannel is back.
    void switchFromRadeUpperReinstatesRxChannel() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE_U);
        QVERIFY(fx.engine->radeChannel(0) != nullptr);
        QVERIFY(fx.engine->rxChannel(0) == nullptr);

        fx.slice->setDspMode(DSPMode::USB);
        QVERIFY2(fx.engine->radeChannel(0) == nullptr,
                 "setDspMode(<non-RADE>) must destroy the slice's RadeChannel");
        QVERIFY2(fx.engine->rxChannel(0) != nullptr,
                 "setDspMode(<non-RADE>) must recreate the slice's RxChannel");
    }

    // ── Test 4: switchFromRadeLowerReinstatesRxChannel ─────────────────
    //
    // Same as Test 3 but for the lower-sideband variant.
    void switchFromRadeLowerReinstatesRxChannel() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE_L);
        QVERIFY(fx.engine->radeChannel(0) != nullptr);
        QVERIFY(fx.engine->rxChannel(0) == nullptr);

        fx.slice->setDspMode(DSPMode::LSB);
        QVERIFY2(fx.engine->radeChannel(0) == nullptr,
                 "setDspMode(<non-RADE>) must destroy the slice's RadeChannel");
        QVERIFY2(fx.engine->rxChannel(0) != nullptr,
                 "setDspMode(<non-RADE>) must recreate the slice's RxChannel");
    }

    // ── Test 5: switchToRadeUpperSetsSidebandFlag ──────────────────────
    //
    // After setDspMode(RADE_U), the created RadeChannel's sideband flag
    // is set to upper (true).
    void switchToRadeUpperSetsSidebandFlag() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE_U);

        RadeChannel* ch = fx.engine->radeChannel(0);
        QVERIFY(ch != nullptr);
        QVERIFY2(ch->sidebandUpper(),
                 "setDspMode(RADE_U) must set RadeChannel::sidebandUpper()=true");
    }

    // ── Test 6: switchToRadeLowerSetsSidebandFlag ──────────────────────
    //
    // After setDspMode(RADE_L), the created RadeChannel's sideband flag
    // is set to lower (false).
    void switchToRadeLowerSetsSidebandFlag() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE_L);

        RadeChannel* ch = fx.engine->radeChannel(0);
        QVERIFY(ch != nullptr);
        QVERIFY2(!ch->sidebandUpper(),
                 "setDspMode(RADE_L) must set RadeChannel::sidebandUpper()=false");
    }

    // ── Test 7: switchRadeUpperToLowerFlipsSideband ────────────────────
    //
    // RADE_U <-> RADE_L is a destroy-and-recreate (the standing J3
    // pattern "mode change implies channel swap").  We verify the
    // recreate via the post-condition: after the flip, the channel's
    // sidebandUpper() must reflect the new mode.  Pointer-identity
    // is not a reliable witness because the allocator may reuse the
    // same memory location after the unique_ptr destructor runs.
    void switchRadeUpperToLowerFlipsSideband() {
        RadioFixture fx;

        fx.slice->setDspMode(DSPMode::RADE_U);
        QVERIFY(fx.engine->radeChannel(0) != nullptr);
        QVERIFY(fx.engine->radeChannel(0)->sidebandUpper());

        // RADE_U -> RADE_L: sideband flag must flip to lower.  This
        // implies the channel was destroyed and recreated (or the
        // setSideband path was re-invoked); either way the new state
        // is the contract that matters to the caller.
        fx.slice->setDspMode(DSPMode::RADE_L);
        RadeChannel* afterDown = fx.engine->radeChannel(0);
        QVERIFY(afterDown != nullptr);
        QVERIFY2(!afterDown->sidebandUpper(),
                 "RADE_U -> RADE_L must flip the sideband flag to lower");
        // The recreate path also restarts the channel (start("dummy")):
        // the post-swap channel should report isActive() == true.
        QVERIFY2(afterDown->isActive(),
                 "After RADE_U -> RADE_L the new RadeChannel must be active");

        // And back the other way: RADE_L -> RADE_U flips sideband to
        // upper.  Same recreate contract.
        fx.slice->setDspMode(DSPMode::RADE_U);
        RadeChannel* afterUp = fx.engine->radeChannel(0);
        QVERIFY(afterUp != nullptr);
        QVERIFY2(afterUp->sidebandUpper(),
                 "RADE_L -> RADE_U must flip the sideband flag to upper");
        QVERIFY2(afterUp->isActive(),
                 "After RADE_L -> RADE_U the new RadeChannel must be active");
    }

    // ── Test 8: switchToSameModeIsNoOp ─────────────────────────────────
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
