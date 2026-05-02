#pragma once

#include "models/SliceModel.h"   // for Mode enum (alias of DSPMode)

namespace NereusSDR {

/// Snapshot of all per-channel DSP state that survives a rebuild.
/// Captured before WDSP channel destroy; reapplied after recreate.
struct RxChannelState {
    SliceModel::Mode mode               = SliceModel::Mode::USB;
    int    filterLowHz              = 200;
    int    filterHighHz             = 2700;

    // AGC
    int    agcMode                  = 0;
    int    agcAttackMs              = 5;
    int    agcDecayMs               = 500;
    int    agcHangMs                = 100;
    int    agcSlope                 = 5;
    int    agcMaxGainDb             = 90;
    int    agcFixedGainDb           = 60;
    int    agcHangThresholdPct      = 0;

    // Noise blanker / noise reduction / ANF
    bool   nbEnabled                = false;
    int    nbMode                   = 0;
    bool   nrEnabled                = false;
    int    nrMode                   = 0;
    bool   anfEnabled               = false;

    // EQ
    bool   eqEnabled                = false;
    int    eqPreampDb               = 0;
    int    eqBandsDb[10]            = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Squelch
    bool   squelchEnabled           = false;
    int    squelchThresholdDb       = -150;

    // RIT, antenna, shift offset
    int    ritOffsetHz              = 0;
    int    antennaIndex             = 0;
    double shiftOffsetHz            = 0.0;
};

}  // namespace NereusSDR
