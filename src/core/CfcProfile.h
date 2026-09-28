// CFC paired-profile codec (NereusSDR).
// Ported from Thetis frmCFCConfig.cs:333-392,492-557 and
// ucParametricEq.cs:1353-1452 [v2.10.3.15].
// Modification history (NereusSDR): 2026-09-27 J.J. Boyd (KG4VCF),
// AI-assisted via OpenAI Codex: bounded Core codec for the existing
// CFCParaEQData format.
/*  frmCFCConfig.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2020-2026 Richard Samphire MW0LGE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

mw0lge@grange-lane.co.uk
*/
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
#include <vector>

namespace NereusSDR::CfcProfile {

struct Profile {
    std::vector<double> f;
    std::vector<double> postF;
    std::vector<double> g;
    std::vector<double> e;
    std::vector<double> qg;
    std::vector<double> qe;
    double minHz = 0.0;
    double maxHz = 4000.0;
    double postMinHz = 0.0;
    double postMaxHz = 4000.0;
    double precompDb = 0.0;
    double postEqGainDb = 0.0;
    bool compParametric = true;
    bool eqParametric = true;

    bool usesQ() const noexcept { return compParametric && eqParametric; }
};

// CFCParaEQData is gzip+base64url of two widget JSON objects separated by
// literal <SEP>. Thetis frmCFCConfig.cs:492-557 [v2.10.3.15].
// Decode is transactional: out is unchanged on failure. Empty/opaque saved
// values are handled by their caller's legacy ten-band fallback.
bool decode(const QString& blob, Profile& out);
QString encode(const Profile& profile);

} // namespace NereusSDR::CfcProfile
