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

#include <QtTest/QtTest>
#include "core/RxChannel.h"
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
};

QTEST_MAIN(TestRxChannelRebuild)
#include "tst_rx_channel_rebuild.moc"
