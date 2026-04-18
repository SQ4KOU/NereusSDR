// =================================================================
// src/core/SampleRateCatalog.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/setup.cs, original licence from Thetis source is included below
//
// Cited line ranges:
//   setup.cs:847-852 — per-protocol rate lists (P1 standard, RedPitaya P1, P2)
//   setup.cs:866     — default sample rate (192000)
//
// The buffer-size formula `64 * rate / 48000` (kBufferBaseSize and
// kBufferBaseRate below) ensures WDSP receives the 48 kHz output chunks
// it expects; the constants are facts about WDSP's internal rate, not
// ported expression, and are documented inline at use rather than under
// a separate cmsetup.c port-citation.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// setup.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to:
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Continual modifications Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
//=================================================================
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

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

// Buffer-size constants. WDSP processes audio in 48 kHz output chunks, so
// the input buffer scales with the input sample rate to keep WDSP's per-
// chunk work constant: buffer_size = kBufferBaseSize * rate / kBufferBaseRate.
// These values are facts about WDSP's internal rate, not ported expression.
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
