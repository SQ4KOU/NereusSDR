// NereusSDR-original infrastructure — no Thetis source ported here.
// No upstream attribution required (NereusSDR rebuild round-trip test).
//
// Design note (Task 1.2):
//   Tests exercise captureState/applyState with a channel that is NOT opened
//   in WDSP (channel 99, same pattern as tst_rxchannel_agc_advanced).
//
//   WDSP-wired setters (setMode, setFilterFreqs, setAgcMode, setAgcHang, etc.)
//   have early-return guards that skip the WDSP call when the new value equals
//   the current value. Tests for those setters only pass same-as-RxChannel-
//   default values so the early-return fires and no WDSP call is made.
//
//   Carry-only setters (setEqEnabled, setSquelchEnabled, setRitOffset,
//   setAntennaIndex, setNbEnabled, setNrMode, setEqBand, setEqPreamp) make
//   no WDSP calls, so they can safely be set to non-default values to exercise
//   the full round-trip path.
//
//   Note: RxChannelState default values intentionally differ from RxChannel
//   defaults (e.g. agcAttackMs=5 vs m_agcAttack=2) to reflect the Thetis
//   UI-visible defaults. Tests that call applyState must therefore explicitly
//   set any WDSP-wired fields to match the RxChannel constructor defaults,
//   otherwise the early-return guard doesn't fire and WDSP is called on the
//   unopened test channel.
//
// Design note (Task 1.3 — rebuild):
//   RxChannel does NOT own its WDSP channel — WdspEngine does.
//   RxChannel::rebuild(engine, cfg) delegates to engine.rebuildRxChannel()
//   which owns the destroy/recreate/reapply cycle.
//
//   In the CI unit-test build (no WDSP), WdspEngine is never initialized, so
//   createRxChannel() returns nullptr and rebuildRxChannel() returns -1 for
//   channels not in its map. Tests verify the API contracts under these
//   conditions rather than requiring a live WDSP session.
//
//   The state-preservation contract (carry-only fields survive rebuild) is
//   verified by constructing an RxChannel directly and exercising the
//   captureState/applyState round-trip that rebuildRxChannel() relies on
//   internally — the same mechanism tested in Task 1.2.

#include <QtTest/QtTest>
#include "core/RxChannel.h"
#include "core/WdspEngine.h"
#include "core/dsp/ChannelConfig.h"
#include "core/dsp/RxChannelState.h"

using namespace NereusSDR;

static constexpr int kTestChannel  = 99;  // Never opened via OpenChannel
static constexpr int kTestBufSize  = 64;
static constexpr int kTestRate     = 48000;

class TestRxChannelRebuild : public QObject {
    Q_OBJECT

private slots:
    void capture_returns_current_state()
    {
        // Use carry-only filter setters (no WDSP call) to set non-default values.
        // mode stays at the RxChannel default (LSB) so no WDSP SetRXAMode call.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);

        // setFilterLow/setFilterHigh are carry-only — no WDSP call.
        ch.setFilterLow(400);
        ch.setFilterHigh(800);

        const RxChannelState state = ch.captureState();

        QCOMPARE(state.mode, SliceModel::Mode::LSB);  // RxChannel default
        QCOMPARE(state.filterLowHz, 400);
        QCOMPARE(state.filterHighHz, 800);
    }

    void apply_round_trips_through_capture()
    {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        RxChannelState state;

        // WDSP-wired fields — must match the RxChannel constructor defaults so
        // each setter's early-return guard fires (no WDSP call on channel 99).
        //   m_mode     default: DSPMode::LSB (= SliceModel::Mode::LSB)
        //   m_agcMode  default: AGCMode::Med (= 3)
        //   m_agcAttack  default: 2 ms
        //   m_agcDecay   default: 250 ms
        //   m_agcHang    default: 250 ms
        //   m_agcSlope   default: 0
        //   m_agcMaxGain default: 90 dB
        //   m_agcFixedGain default: 20 dB
        //   m_agcHangThreshold default: 0
        state.mode             = SliceModel::Mode::LSB;
        state.agcMode          = static_cast<int>(AGCMode::Med);  // 3
        state.agcAttackMs      = 2;
        state.agcDecayMs       = 250;
        state.agcHangMs        = 250;
        state.agcSlope         = 0;
        state.agcMaxGainDb     = 90;
        state.agcFixedGainDb   = 20;
        state.agcHangThresholdPct = 0;

        // Filter: applyState() calls setFilterFreqs() directly (the WDSP path).
        // To avoid calling WDSP on the unopened test channel, use values that
        // match the RxChannel constructor defaults so setFilterFreqs' equality
        // guard fires and returns early.  The carry-only round-trip for
        // non-default filter values is covered by capture_returns_current_state.
        state.filterLowHz       = 150;   // RxChannel default m_filterLow
        state.filterHighHz      = 2850;  // RxChannel default m_filterHigh
        state.eqEnabled         = true;
        state.eqPreampDb        = 6;
        state.eqBandsDb[0]      = -3;
        state.eqBandsDb[9]      = 5;
        state.squelchEnabled    = true;
        state.squelchThresholdDb = -80;
        state.ritOffsetHz       = 300;
        state.antennaIndex      = 2;
        state.nbEnabled         = true;
        state.nrMode            = 1;

        ch.applyState(state);

        const RxChannelState verify = ch.captureState();

        // WDSP-wired fields round-trip via atomic read
        QCOMPARE(verify.mode, SliceModel::Mode::LSB);
        QCOMPARE(verify.agcMode, static_cast<int>(AGCMode::Med));

        // Filter values round-trip via setFilterFreqs' int carry update
        QCOMPARE(verify.filterLowHz,        150);
        QCOMPARE(verify.filterHighHz,       2850);
        QCOMPARE(verify.eqEnabled,          true);
        QCOMPARE(verify.eqPreampDb,         6);
        QCOMPARE(verify.eqBandsDb[0],       -3);
        QCOMPARE(verify.eqBandsDb[9],       5);
        QCOMPARE(verify.squelchEnabled,     true);
        QCOMPARE(verify.squelchThresholdDb, -80);
        QCOMPARE(verify.ritOffsetHz,        300);
        QCOMPARE(verify.antennaIndex,       2);
        QCOMPARE(verify.nbEnabled,          true);
        QCOMPARE(verify.nrMode,             1);
    }

    // ── Task 1.3: WdspEngine::rebuildRxChannel API contracts ─────────────────

    // Without WDSP initialized, rebuildRxChannel must return -1 (channel not
    // in map — can never have been created without an initialized engine).
    void rebuildRxChannel_returns_minus_one_when_channel_not_found()
    {
        WdspEngine engine;  // uninitialized — no WDSP
        ChannelConfig cfg;
        cfg.sampleRate  = 48000;
        cfg.bufferSize  = 512;
        cfg.filterSize  = 4096;
        cfg.filterType  = 0;

        const qint64 result = engine.rebuildRxChannel(99, cfg);
        QCOMPARE(result, qint64(-1));
    }

    // rebuildRxChannel is idempotent on a non-existent channel ID (no crash,
    // no state change in the engine map).
    void rebuildRxChannel_is_safe_with_unknown_channel()
    {
        WdspEngine engine;
        ChannelConfig cfg;
        // Two calls — neither must crash.
        engine.rebuildRxChannel(0, cfg);
        engine.rebuildRxChannel(0, cfg);
        // Engine rxChannel lookup still returns nullptr (nothing was created).
        QVERIFY(engine.rxChannel(0) == nullptr);
    }

    // ── Task 1.3: RxChannel::rebuild() forwarding ────────────────────────────
    //
    // rebuild(engine, cfg) must delegate to engine.rebuildRxChannel() and
    // forward its return value. Verifies the forwarding is wired correctly.
    //
    // Uses a directly-constructed RxChannel (not owned by WdspEngine), so
    // engine.rebuildRxChannel(kTestChannel, cfg) returns -1 (not in map) —
    // which is the expected result for this channel ID in the engine.
    void rebuild_forwards_to_engine_and_returns_its_result()
    {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);

        WdspEngine engine;  // uninitialized — kTestChannel not in its map
        ChannelConfig cfg;
        cfg.sampleRate  = 48000;
        cfg.bufferSize  = 512;
        cfg.filterSize  = 4096;
        cfg.filterType  = 0;

        const qint64 result = ch.rebuild(engine, cfg);
        // Channel not in engine map → engine returns -1 → rebuild returns -1.
        QCOMPARE(result, qint64(-1));
    }

    // ── Task 1.3: state preservation contract ───────────────────────────────
    //
    // This test verifies that the captureState/applyState round-trip that
    // WdspEngine::rebuildRxChannel() relies on works correctly for carry-only
    // fields set to non-default values.
    //
    // This is not a full end-to-end rebuild test (that requires a live WDSP
    // session) but it pins the mechanism that rebuild correctness depends on.
    //
    // Design constraint: In a WDSP-enabled build, the RxChannel constructor
    // calls create_anbEXT/create_nobEXT on the channel ID. Two RxChannel
    // objects with the SAME channel ID would double-create and then
    // double-destroy the WDSP noise blanker — a use-after-free. Use a
    // distinct channel ID (kAltTestChannel = 98) for the "rebuilt" object
    // to avoid this.
    void rebuild_preserves_state_with_new_buffer_size()
    {
        // Channel 99: the "original" — set carry-only fields, capture state.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        ch.setFilterLow(200);
        ch.setFilterHigh(2700);
        ch.setEqEnabled(true);
        ch.setEqPreamp(6);
        ch.setRitOffset(300);
        ch.setAntennaIndex(2);
        ch.setNbEnabled(true);
        ch.setNrMode(1);

        // 1. Capture state (as rebuildRxChannel does before CloseChannel).
        const RxChannelState captured = ch.captureState();

        // 2. Construct a fresh channel with a new buffer size (distinct ID to
        //    avoid WDSP double-create of anbEXT/nobEXT on channel 99).
        //    Channel 98 = "rebuilt" — only used for the applyState call.
        static constexpr int kAltTestChannel = 98;
        RxChannel rebuilt(kAltTestChannel, 512 /* new buf */, kTestRate);

        // 3. Reapply state to the new channel.
        //    Use WDSP-wired fields at their RxChannel constructor defaults to
        //    avoid WDSP calls on the never-Opened test channels (same pattern
        //    as apply_round_trips_through_capture above).
        RxChannelState toApply = captured;
        toApply.mode                = SliceModel::Mode::LSB;   // RxChannel ctor default
        toApply.agcMode             = static_cast<int>(AGCMode::Med);
        toApply.agcAttackMs         = 2;
        toApply.agcDecayMs          = 250;
        toApply.agcHangMs           = 250;
        toApply.agcSlope            = 0;
        toApply.agcMaxGainDb        = 90;
        toApply.agcFixedGainDb      = 20;
        toApply.agcHangThresholdPct = 0;
        toApply.filterLowHz         = 150;   // RxChannel ctor default
        toApply.filterHighHz        = 2850;  // RxChannel ctor default
        rebuilt.applyState(toApply);

        // 4. Verify carry-only fields survived the round-trip.
        const RxChannelState verify = rebuilt.captureState();
        QCOMPARE(verify.eqEnabled,        true);
        QCOMPARE(verify.eqPreampDb,       6);
        QCOMPARE(verify.ritOffsetHz,      300);
        QCOMPARE(verify.antennaIndex,     2);
        QCOMPARE(verify.nbEnabled,        true);
        QCOMPARE(verify.nrMode,           1);
        // Filter values match what we applied (ctor defaults in this test).
        QCOMPARE(verify.filterLowHz,      150);
        QCOMPARE(verify.filterHighHz,     2850);
    }
};

QTEST_MAIN(TestRxChannelRebuild)
#include "tst_rx_channel_rebuild.moc"
