// src/core/SampleRateCatalog.h
//
// Sample-rate catalog and resolvers for Hardware Config.
//
// Source: Thetis Project Files/Source/Console/setup.cs:847-852 (rate lists),
//         setup.cs:866 (default selection), ChannelMaster/cmsetup.c:104-111
//         (buffer-size formula — getbuffsize).

#pragma once

#include "BoardCapabilities.h"
#include "HpsdrModel.h"
#include "RadioDiscovery.h" // ProtocolVersion

#include <vector>

class QVariant;
class QString;

namespace NereusSDR {

class AppSettings;

// Thetis-source constants.
//
// From setup.cs:849 — P1 base list (every non-RedPitaya board).
inline constexpr int kP1RatesBase[] = {48000, 96000, 192000};

// From setup.cs:847-849 — P1 list when include_extra_p1_rate is true
// (only HPSDRModel::REDPITAYA).
inline constexpr int kP1RatesRedPitaya[] = {48000, 96000, 192000, 384000};

// From setup.cs:850 — P2 list, every ETH board.
inline constexpr int kP2Rates[] = {48000, 96000, 192000, 384000, 768000, 1536000};

// From setup.cs:866 — default selected rate when nothing is persisted.
inline constexpr int kDefaultSampleRate = 192000;

// From ChannelMaster/cmsetup.c:108-110 — base constants for getbuffsize.
// buffer_size = kBufferBaseSize * rate / kBufferBaseRate.
inline constexpr int kBufferBaseRate = 48000;
inline constexpr int kBufferBaseSize = 64;

// Return the allowed sample-rate list for a given protocol + board + model.
// Intersects the protocol-appropriate master list with caps.sampleRates
// (skipping zero sentinels). HPSDRModel distinguishes RedPitaya (which
// shares HPSDRHW::OrionMKII with real OrionMKII but gets the extra 384k
// on P1, per setup.cs:847).
std::vector<int> allowedSampleRates(ProtocolVersion proto,
                                     const BoardCapabilities& caps,
                                     HPSDRModel model);

// Return the default rate: kDefaultSampleRate if present in allowed,
// otherwise the first allowed entry (paranoia fallback).
int defaultSampleRate(ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model);

// Compute WDSP in_size for a given rate. From cmsetup.c:104-111.
// Precondition: rate > 0 (callers must validate).
constexpr int bufferSizeForRate(int rate) noexcept
{
    return kBufferBaseSize * rate / kBufferBaseRate;
}

// Read the persisted sample rate for the given MAC, validate it against
// allowedSampleRates, and return a valid rate. If the persisted value is
// missing, zero, or not in the allowed list, returns defaultSampleRate
// and logs a warning via qCWarning(lcConnection) for the not-in-list case.
int resolveSampleRate(const AppSettings& settings,
                      const QString& mac,
                      ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model);

// Read the persisted active-RX count for the given MAC, clamp to
// [1, caps.maxReceivers]. If the persisted value is missing or < 1,
// returns 1.
int resolveActiveRxCount(const AppSettings& settings,
                         const QString& mac,
                         const BoardCapabilities& caps);

} // namespace NereusSDR
