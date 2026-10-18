// no-port-check: NereusSDR-original struct; the doc comment below names Thetis
// source files only to explain the default-value provenance, not to port code.
#pragma once

#include "models/SliceModel.h"   // for Mode enum (alias of DSPMode)

namespace NereusSDR {

/// Snapshot of all per-channel TX DSP state that survives a rebuild.
/// Captured before WDSP channel destroy; reapplied after recreate.
///
/// **Default values:** These are the Thetis-visible "first-launch UI defaults"
/// from `radio.cs` and `setup.cs` (e.g. `mode = USB`, `levelerOn = false` for
/// the initial UI state Thetis shows on fresh installation). They do NOT
/// necessarily match `TxChannel`'s constructor or WdspEngine boot defaults
/// (which reflect WDSP wire-level defaults from `cmaster.c`, `wcpAGC.c`, etc.).
/// Always populate this struct via `TxChannel::captureState()` before calling
/// `applyState()` — default-constructing a `TxChannelState` and applying it
/// will push UI defaults to WDSP, which is rarely what you want.
///
/// Fields marked "carry-only" are stored locally in `TxChannel` private
/// members and reapplied without making WDSP calls (the WDSP wiring lands in
/// a later task). Fields marked "WDSP-wired" invoke real WDSP API calls when
/// `TxChannel::applyState` dispatches them.
struct TxChannelState {
    // ── Mode + filter ─────────────────────────────────────────────────────────
    // mode: carry-only until TransmitModel cutover wires SetTXAMode at connect.
    // filterLow/High: carry-only (WDSP-wired path is setTxBandpass which is
    //   called by the mode-config logic; the standalone int carry mirrors what
    //   RxChannelState does for filterLowHz/filterHighHz).
    SliceModel::Mode mode               = SliceModel::Mode::USB;
    int    filterLowHz                  = 200;
    int    filterHighHz                 = 2700;

    // ── Mic / TX EQ ──────────────────────────────────────────────────────────
    // All carry-only — wired to WDSP in the EQ task (3M-3a-i Batch 2+).
    int    micGainDb                    = 0;
    bool   eqEnabled                    = false;
    int    eqPreampDb                   = 0;
    int    eqBandsDb[10]                = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // ── Leveler ───────────────────────────────────────────────────────────────
    // WDSP-wired via setTxLevelerOn / setTxLevelerTopDb / setTxLevelerDecayMs.
    // Defaults from Thetis radio.cs:3019/2979/2999 [v2.10.3.13]:
    //   tx_leveler_on = true, tx_leveler_max_gain = 15.0 dB, tx_leveler_decay = 100 ms.
    bool   levelerOn                    = false;
    double levelerMaxGainDb             = 15.0;
    int    levelerDecayMs               = 100;

    // ── ALC ───────────────────────────────────────────────────────────────────
    // WDSP-wired via setTxAlcMaxGainDb / setTxAlcDecayMs.
    // Defaults from Thetis database.cs:4592 [v2.10.3.13]:
    //   TXProfile ALC_MaximumGain = 3, ALC_Decay = 10 ms.
    double alcMaxGainDb                 = 3.0;
    int    alcDecayMs                   = 10;

    // ── CFC + Phase Rotator + CESSB + CPDR ───────────────────────────────────
    // WDSP-wired via setTxCfcRunning / setTxCfcPostEqRunning /
    //   setTxCfcPrecompDb / setTxCfcPrePeqDb / setTxPhrotCornerHz /
    //   setTxPhrotNstages / setTxPhrotReverse / setTxCessbOn /
    //   setTxCpdrOn / setTxCpdrGainDb.
    // Defaults reflect the WDSP boot-off state; users enable these explicitly.
    bool   cfcOn                        = false;
    bool   cfcPostEqOn                  = false;
    double cfcPrecompDb                 = 0.0;
    double cfcPostEqGainDb              = 0.0;
    bool   phaseRotatorOn               = false;
    double phaseRotatorFreqHz           = 338.0;
    int    phaseRotatorStages           = 8;
    bool   phaseRotatorReverse          = false;
    bool   cessbOn                      = false;
    bool   cpdrOn                       = false;
    double cpdrLevelDb                  = 0.0;

    // ── PureSignal placeholder ────────────────────────────────────────────────
    // Carry-only — 3M-4 work. Captured and reapplied without WDSP call so
    // the state survives a rebuild even though PureSignal is not yet wired.
    bool   pureSignalEnabled            = false;
};

}  // namespace NereusSDR
