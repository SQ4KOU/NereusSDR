// =================================================================
// src/gui/spectrum/PeakBlobDetector.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from
//   Thetis source is included below.
//
// Port covers:
//   display.cs:4395-4714  public API + state variables [v2.10.3.13]
//   display.cs:5453-5508  render path peak detection [v2.10.3.13]
//   display.cs:5483       decay math verbatim [v2.10.3.13]:
//       entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
//                 Ported from Thetis display.cs v2.10.3.13
//                 (commit 501e3f5).
// =================================================================

//=================================================================
// display.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley (W5WC)
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to:
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Waterfall AGC Modifications Copyright (C) 2013 Phil Harman (VK6APH)
// Transitions to directX and continual modifications Copyright (C) 2020-2025 Richard Samphire (MW0LGE)
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

#include "PeakBlobDetector.h"
#include <algorithm>

namespace NereusSDR {

// From Thetis display.cs:5453-5508 [v2.10.3.13]
// C# loop searched m_RX1Maximums for local maxima and kept top-N by magnitude.
void PeakBlobDetector::update(const QVector<float>& bins,
                              int filterLowBin, int filterHighBin)
{
    if (!m_enabled || bins.isEmpty()) {
        m_blobs.clear();
        return;
    }

    // Clamp search range to valid interior (i ± 1 must be valid).
    const int low  = m_insideOnly ? qMax(1, filterLowBin)              : 1;
    const int high = m_insideOnly ? qMin(bins.size() - 1, filterHighBin)
                                  : bins.size() - 1;

    // Find local maxima — must exceed both immediate neighbours.
    // From Thetis display.cs:5455-5468 [v2.10.3.13] peak search loop.
    QVector<PeakBlob> found;
    found.reserve(qMin(m_count * 4, high - low));  // reasonable pre-alloc
    for (int i = low; i < high; ++i) {
        if (bins[i] > bins[i - 1] && bins[i] > bins[i + 1]) {
            found.append({i, bins[i], m_currentTimeMs});
        }
    }

    // Sort descending by dBm.
    std::sort(found.begin(), found.end(),
              [](const PeakBlob& a, const PeakBlob& b) {
                  return a.max_dBm > b.max_dBm;
              });

    // Keep top N.
    if (found.size() > m_count) {
        found.resize(m_count);
    }

    // Hold merge: if a prior blob for the same bin index had a higher dBm,
    // preserve that maximum and its original timestamp so the hold window
    // continues from the time the maximum was last seen.
    // From Thetis display.cs:5475-5484 [v2.10.3.13] hold logic.
    if (m_holdEnabled) {
        for (auto& nb : found) {
            for (const auto& ob : m_blobs) {
                if (ob.binIndex == nb.binIndex && ob.max_dBm > nb.max_dBm) {
                    nb.max_dBm = ob.max_dBm;
                    nb.timeMs  = ob.timeMs;
                    break;
                }
            }
        }
    }

    m_blobs = std::move(found);
}

// From Thetis display.cs:5483 [v2.10.3.13]:
//   entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
void PeakBlobDetector::tickFrame(int fps, int elapsedMs)
{
    m_currentTimeMs += elapsedMs;

    if (!m_enabled || !m_holdEnabled || fps <= 0) {
        return;
    }

    for (auto& b : m_blobs) {
        const qint64 ageMs = m_currentTimeMs - b.timeMs;
        if (ageMs > static_cast<qint64>(m_holdMs) && m_holdDrop) {
            // From Thetis Display.cs:5483 [v2.10.3.13]
            //   entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
            b.max_dBm -= static_cast<float>(m_fallDbPerSec / fps);
        }
    }
}

}  // namespace NereusSDR
