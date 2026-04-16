// src/core/HardwareProfile.h
//
// Maps an HPSDRModel enum to hardware overrides (ADC count, BPF style,
// supply voltage, audio swap, effective board identity).
//
// Source: mi0bot/Thetis clsHardwareSpecific.cs:85-184

#pragma once

#include "HpsdrModel.h"
#include "BoardCapabilities.h"
#include <QList>

namespace NereusSDR {

struct HardwareProfile {
    HPSDRModel               model{HPSDRModel::HERMES};
    HPSDRHW                  effectiveBoard{HPSDRHW::Hermes};
    const BoardCapabilities* caps{nullptr};
    int                      adcCount{1};
    bool                     mkiiBpf{false};
    int                      adcSupplyVoltage{33};
    bool                     lrAudioSwap{true};
};

// Compute a HardwareProfile for the given model.
// From Thetis clsHardwareSpecific.cs:85-184
HardwareProfile profileForModel(HPSDRModel model);

// Return the default (auto-guessed) HPSDRModel for a discovered board byte.
HPSDRModel defaultModelForBoard(HPSDRHW board);

// Return the list of HPSDRModel values compatible with a discovered board byte.
// From Thetis NetworkIO.cs:164-171
QList<HPSDRModel> compatibleModels(HPSDRHW board);

} // namespace NereusSDR
