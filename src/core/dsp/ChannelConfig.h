#pragma once

namespace NereusSDR {

/// Inputs for RxChannel/TxChannel rebuild. All values authoritative for the
/// new channel; rebuild captures current state, destroys, recreates with this
/// config, then reapplies state.
struct ChannelConfig {
    int  sampleRate                 = 48000;
    int  bufferSize                 = 256;
    int  filterSize                 = 4096;
    int  filterType                 = 0;     // 0=LowLatency, 1=LinearPhase
    bool cacheImpulse               = false;
    bool cacheImpulseSaveRestore    = false;
    bool highResFilterCharacteristics = false;
};

}  // namespace NereusSDR
