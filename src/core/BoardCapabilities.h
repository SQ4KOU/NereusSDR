// src/core/BoardCapabilities.h
//
// Pure-data capability registry. One entry per HPSDRHW value.
// Drives both protocol connections and the Hardware setup UI.
//
// Source: mi0bot/Thetis-HL2 clsHardwareSpecific.cs, Setup.cs, console.cs,
//         HPSDR/specHPSDR.cs, ChannelMaster/network.h

#pragma once

#include "HpsdrModel.h"
#include "RadioDiscovery.h"  // for ProtocolVersion
#include <array>
#include <span>

namespace NereusSDR {

struct BoardCapabilities {
    HPSDRHW         board;
    ProtocolVersion protocol;

    int  adcCount;
    int  maxReceivers;
    std::array<int, 4> sampleRates;  // zero-pad unused slots
    int  maxSampleRate;

    struct Atten {
        int minDb;
        int maxDb;
        int stepDb;
        bool present;
    } attenuator;

    struct Preamp {
        bool present;
        bool hasBypassAndPreamp;
    } preamp;

    int  ocOutputCount;
    bool hasAlexFilters;
    bool hasAlexTxRouting;
    int  xvtrJackCount;
    int  antennaInputCount;

    bool hasPureSignal;
    bool hasDiversityReceiver;
    bool hasStepAttenuatorCal;
    bool hasPaProfile;

    bool hasBandwidthMonitor;
    bool hasIoBoardHl2;
    bool hasSidetoneGenerator;

    int  minFirmwareVersion;
    int  knownGoodFirmware;

    const char* displayName;
    const char* sourceCitation;
};

namespace BoardCapsTable {
    const BoardCapabilities& forBoard(HPSDRHW hw) noexcept;
    const BoardCapabilities& forModel(HPSDRModel m) noexcept;
    std::span<const BoardCapabilities> all() noexcept;
}

} // namespace NereusSDR
