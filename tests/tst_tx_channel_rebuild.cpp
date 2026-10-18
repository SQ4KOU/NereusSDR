// NereusSDR-original infrastructure — no Thetis source ported here.
// No upstream attribution required (NereusSDR rebuild round-trip test).
//
// Design note (Task 1.4):
//   Tests exercise captureState/applyState with a TxChannel that is NOT opened
//   in WDSP (channel 98, similar to the RxChannel rebuild test pattern).
//
//   WDSP-wired setters (setTxMode, setTxBandpass, setTxLevelerOn, etc.) have
//   WDSP null-guards (rsmpin.p == nullptr early return) that make them safe on
//   an unopened channel. The carry fields are always updated regardless.
//
//   Carry-only fields (micGainDb, eqEnabled, eqPreampDb, eqBandsDb,
//   pureSignalEnabled) make no WDSP calls, so they can be set to non-default
//   values to exercise the full round-trip path.
//
//   Design note (Task 1.4 — rebuild):
//   TxChannel does NOT own its WDSP channel — WdspEngine does.
//   TxChannel::rebuild(engine, cfg) delegates to engine.rebuildTxChannel()
//   which owns the destroy/recreate/reapply cycle.
//
//   In the CI unit-test build (no WDSP), WdspEngine is never initialized, so
//   createTxChannel() returns nullptr and rebuildTxChannel() returns -1 for
//   channels not in its map. Tests verify the API contracts under these
//   conditions rather than requiring a live WDSP session.
//
//   The state-preservation contract (carry-only fields survive rebuild) is
//   verified by constructing a TxChannel directly and exercising the
//   captureState/applyState round-trip that rebuildTxChannel() relies on
//   internally — the same mechanism tested in the capture/apply tests.

#include <QtTest/QtTest>
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/dsp/ChannelConfig.h"
#include "core/dsp/TxChannelState.h"

using namespace NereusSDR;

// Use channel 3 / 4 to avoid collision with production channel 1 (WdspEngine TX)
// and to stay within the WDSP MAX_CHANNELS=32 bound (txa[] array size).
// RxChannel rebuild tests use 98/99 (RX channels live in rxa[], no txa[] access).
// WDSP TX setters null-guard via txa[channelId].rsmpin.p — out-of-bounds access
// on channel IDs >= 32 causes SIGBUS on macOS, so we keep IDs small here.
static constexpr int kTestChannel    = 3;
static constexpr int kAltTestChannel = 4;  // "rebuilt" channel (avoid double-init on same ID)
static constexpr int kTestBufSize    = 64;

class TestTxChannelRebuild : public QObject {
    Q_OBJECT

private slots:

    // ── Task 1.4: captureState() reads back the current carry state ───────────

    void capture_returns_current_state()
    {
        TxChannel ch(kTestChannel, kTestBufSize, kTestBufSize);

        // Carry-only setters — safe to call with non-default values.
        // micGainDb, eqEnabled, eqPreampDb, eqBandsDb, pureSignalEnabled.
        // setTxBandpass is WDSP-wired but also updates carry fields; the
        // rsmpin null-guard fires on the unopened test channel so no WDSP call.
        ch.setTxBandpass(300, 2500);
        ch.setTxLevelerOn(true);
        ch.setTxLevelerTopDb(10.0);
        ch.setTxLevelerDecayMs(200);
        ch.setTxCfcRunning(true);
        ch.setTxCpdrOn(true);
        ch.setTxCpdrGainDb(12.0);
        ch.setTxCessbOn(true);

        const TxChannelState state = ch.captureState();

        QCOMPARE(state.filterLowHz,   300);
        QCOMPARE(state.filterHighHz,  2500);
        QCOMPARE(state.levelerOn,     true);
        QCOMPARE(state.levelerMaxGainDb, 10.0);
        QCOMPARE(state.levelerDecayMs,   200);
        QCOMPARE(state.cfcOn,         true);
        QCOMPARE(state.cpdrOn,        true);
        QCOMPARE(state.cpdrLevelDb,   12.0);
        QCOMPARE(state.cessbOn,       true);
    }

    // ── Task 1.4: applyState/captureState round-trip ─────────────────────────

    void apply_round_trips_through_capture()
    {
        TxChannel ch(kTestChannel, kTestBufSize, kTestBufSize);
        TxChannelState state;

        // Set all carry-only fields to non-default values so the round-trip
        // is actually exercising something.
        state.mode         = SliceModel::Mode::LSB;
        state.filterLowHz  = 400;
        state.filterHighHz = 2800;

        // Mic / EQ — carry only
        state.micGainDb  = 3;
        state.eqEnabled  = true;
        state.eqPreampDb = 6;
        state.eqBandsDb[0] = -3;
        state.eqBandsDb[4] =  2;
        state.eqBandsDb[9] =  5;

        // Leveler — WDSP-wired but carry-tracked; rsmpin guard makes WDSP a no-op
        state.levelerOn        = true;
        state.levelerMaxGainDb = 12.0;
        state.levelerDecayMs   = 300;

        // ALC — WDSP-wired but carry-tracked
        state.alcMaxGainDb = 6.0;
        state.alcDecayMs   = 20;

        // CFC — WDSP-wired but carry-tracked
        state.cfcOn        = true;
        state.cfcPostEqOn  = true;
        state.cfcPrecompDb = 3.0;
        state.cfcPostEqGainDb = 1.5;

        // Phase rotator — WDSP-wired but carry-tracked
        state.phaseRotatorOn      = true;
        state.phaseRotatorFreqHz  = 500.0;
        state.phaseRotatorStages  = 4;
        state.phaseRotatorReverse = true;

        // CESSB / CPDR — WDSP-wired but carry-tracked
        state.cessbOn     = true;
        state.cpdrOn      = true;
        state.cpdrLevelDb = 9.0;

        // PureSignal — carry only
        state.pureSignalEnabled = true;

        ch.applyState(state);

        const TxChannelState verify = ch.captureState();

        // Mode carry
        QCOMPARE(verify.mode,         SliceModel::Mode::LSB);

        // Filter carry
        QCOMPARE(verify.filterLowHz,  400);
        QCOMPARE(verify.filterHighHz, 2800);

        // Mic / EQ carry
        QCOMPARE(verify.micGainDb,    3);
        QCOMPARE(verify.eqEnabled,    true);
        QCOMPARE(verify.eqPreampDb,   6);
        QCOMPARE(verify.eqBandsDb[0], -3);
        QCOMPARE(verify.eqBandsDb[4],  2);
        QCOMPARE(verify.eqBandsDb[9],  5);

        // Leveler carry
        QCOMPARE(verify.levelerOn,        true);
        QCOMPARE(verify.levelerMaxGainDb, 12.0);
        QCOMPARE(verify.levelerDecayMs,   300);

        // ALC carry
        QCOMPARE(verify.alcMaxGainDb, 6.0);
        QCOMPARE(verify.alcDecayMs,   20);

        // CFC carry
        QCOMPARE(verify.cfcOn,           true);
        QCOMPARE(verify.cfcPostEqOn,     true);
        QCOMPARE(verify.cfcPrecompDb,    3.0);
        QCOMPARE(verify.cfcPostEqGainDb, 1.5);

        // Phase rotator carry
        QCOMPARE(verify.phaseRotatorOn,      true);
        QCOMPARE(verify.phaseRotatorFreqHz,  500.0);
        QCOMPARE(verify.phaseRotatorStages,  4);
        QCOMPARE(verify.phaseRotatorReverse, true);

        // CESSB / CPDR carry
        QCOMPARE(verify.cessbOn,     true);
        QCOMPARE(verify.cpdrOn,      true);
        QCOMPARE(verify.cpdrLevelDb, 9.0);

        // PureSignal carry
        QCOMPARE(verify.pureSignalEnabled, true);
    }

    // ── Task 1.4: WdspEngine::rebuildTxChannel API contracts ─────────────────

    // Without WDSP initialized, rebuildTxChannel must return -1 (channel not
    // in map — can never have been created without an initialized engine).
    void rebuildTxChannel_returns_minus_one_when_channel_not_found()
    {
        WdspEngine engine;  // uninitialized — no WDSP
        ChannelConfig cfg;
        cfg.sampleRate = 48000;
        cfg.bufferSize = 64;
        cfg.filterSize = 2048;
        cfg.filterType = 0;

        const qint64 result = engine.rebuildTxChannel(kTestChannel, cfg);
        QCOMPARE(result, qint64(-1));
    }

    // rebuildTxChannel is idempotent on a non-existent channel ID (no crash,
    // no state change in the engine map).
    void rebuildTxChannel_is_safe_with_unknown_channel()
    {
        WdspEngine engine;
        ChannelConfig cfg;
        // Two calls — neither must crash.
        engine.rebuildTxChannel(0, cfg);
        engine.rebuildTxChannel(0, cfg);
        // Engine txChannel lookup still returns nullptr (nothing was created).
        QVERIFY(engine.txChannel(0) == nullptr);
    }

    // ── Task 1.4: TxChannel::rebuild() forwarding ────────────────────────────

    // rebuild(engine, cfg) must delegate to engine.rebuildTxChannel() and
    // forward its return value.
    //
    // Uses a directly-constructed TxChannel (not owned by WdspEngine), so
    // engine.rebuildTxChannel(kTestChannel, cfg) returns -1 (not in map) —
    // which is the expected result for this channel ID in the engine.
    void rebuild_forwards_to_engine_and_returns_its_result()
    {
        TxChannel ch(kTestChannel, kTestBufSize, kTestBufSize);

        WdspEngine engine;  // uninitialized — kTestChannel not in its map
        ChannelConfig cfg;
        cfg.sampleRate = 48000;
        cfg.bufferSize = 64;
        cfg.filterSize = 2048;
        cfg.filterType = 0;

        const qint64 result = ch.rebuild(engine, cfg);
        // Channel not in engine map → engine returns -1 → rebuild returns -1.
        QCOMPARE(result, qint64(-1));
    }

    // ── Task 1.4: state preservation contract ───────────────────────────────

    // Verifies that the captureState/applyState round-trip that
    // WdspEngine::rebuildTxChannel() relies on works correctly for carry-only
    // fields set to non-default values.
    //
    // This is not a full end-to-end rebuild test (that requires a live WDSP
    // session) but it pins the mechanism that rebuild correctness depends on.
    void rebuild_preserves_state_with_new_buffer_size()
    {
        // Channel kTestChannel: the "original" — set carry-only fields, capture.
        TxChannel ch(kTestChannel, kTestBufSize, kTestBufSize);
        ch.setTxLevelerOn(true);
        ch.setTxLevelerTopDb(8.0);
        ch.setTxCfcRunning(true);
        ch.setTxCpdrOn(true);
        ch.setTxCessbOn(true);

        // 1. Capture state (as rebuildTxChannel does before CloseChannel).
        TxChannelState captured = ch.captureState();

        // Also set a carry-only field directly on the captured struct
        // (simulates a value that wasn't set via setter but was captured).
        captured.eqEnabled    = true;
        captured.eqPreampDb   = 3;
        captured.pureSignalEnabled = true;

        // 2. Construct a fresh channel with a different buffer size (distinct ID
        //    to keep the two TxChannel objects cleanly separate — same rationale
        //    as the RxChannel rebuild test using kAltTestChannel=98).
        TxChannel rebuilt(kAltTestChannel, 256 /* new buf */, 256);

        // 3. Reapply captured state to the new channel.
        rebuilt.applyState(captured);

        // 4. Verify all carry-only fields survived the round-trip.
        const TxChannelState verify = rebuilt.captureState();
        QCOMPARE(verify.levelerOn,         true);
        QCOMPARE(verify.levelerMaxGainDb,  8.0);
        QCOMPARE(verify.cfcOn,             true);
        QCOMPARE(verify.cpdrOn,            true);
        QCOMPARE(verify.cessbOn,           true);
        QCOMPARE(verify.eqEnabled,         true);
        QCOMPARE(verify.eqPreampDb,        3);
        QCOMPARE(verify.pureSignalEnabled, true);
    }
};

QTEST_MAIN(TestTxChannelRebuild)
#include "tst_tx_channel_rebuild.moc"
