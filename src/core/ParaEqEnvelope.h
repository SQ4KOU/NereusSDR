// =================================================================
// src/core/ParaEqEnvelope.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/Common.cs, original licence from
//   Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-30 — Reimplemented in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 Phase 3M-3a-ii follow-up Batch 6 — gzip+base64url
//                 envelope helper mirroring Thetis Common.cs
//                 Compress_gzip / Decompress_gzip [v2.10.3.13],
//                 byte-identical so parametric-EQ JSON blobs
//                 round-trip across Thetis <-> NereusSDR profile
//                 storage.
// =================================================================

//=================================================================
// common.cs
//=================================================================
// PowerSDR is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2012  FlexRadio Systems
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
// You may contact us via email at: gpl@flexradio.com.
// Paper mail may be sent to:
//    FlexRadio Systems
//    4616 W. Howard Lane  Suite 1-150
//    Austin, TX 78728
//    USA
//
//=================================================================
// Continual modifications Copyright (C) 2019-2025 Richard Samphire (MW0LGE)
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

#include <QString>
#include <optional>

namespace NereusSDR {

// ParaEqEnvelope — gzip+base64url codec mirroring Thetis Common.cs
// Compress_gzip / Decompress_gzip [v2.10.3.13].
//
// The Thetis ucParametricEq UserControl serializes its band/preamp
// state as Newtonsoft JSON, then wraps that JSON in a gzip+base64url
// envelope before stuffing it into the TXProfile CFCParaEQData /
// TXParaEQData columns.  NereusSDR's ParametricEqWidget produces
// Thetis-compatible JSON via saveToJson(); this helper applies the
// same envelope so:
//
//   1. NereusSDR can round-trip its own blobs through MicProfileManager.
//   2. Thetis-saved profile blobs decode cleanly when imported.
//   3. NereusSDR-saved blobs decode cleanly if exported back into Thetis.
//
// The output is byte-identical to Thetis's encoder for the same input
// (modulo the gzip header mtime field — gzip permits mtime=0 and
// Thetis's GZipStream typically writes 0; either way the inflate side
// is unaffected).
namespace ParaEqEnvelope {

// Encode: utf8(payload) -> gzip (deflate level 9, windowBits=31) ->
// base64url (Qt's Base64UrlEncoding | OmitTrailingEquals).
//
// Returns an empty QString for empty/null input, mirroring Thetis's
// "if (string.IsNullOrEmpty(...)) return null;" guard.
//
// From Thetis Common.cs:1745-1762 [v2.10.3.13].
QString encode(const QString& payload);

// Decode: reverse base64url -> inflate (windowBits=31) -> utf8 -> string.
//
// Returns std::nullopt on any failure (corrupt base64, bad gzip header,
// truncated stream, inflate data error, etc.) — Thetis's version raises
// on bad-length base64 and on GZipStream errors; NereusSDR collapses
// all failure modes to nullopt so callers don't need to distinguish.
//
// From Thetis Common.cs:1764-1790 [v2.10.3.13].
std::optional<QString> decode(const QString& blob);

}  // namespace ParaEqEnvelope

}  // namespace NereusSDR
