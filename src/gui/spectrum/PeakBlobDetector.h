#pragma once

// =================================================================
// src/gui/spectrum/PeakBlobDetector.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from
//   Thetis source is included below.
//
// NereusSDR C++ translation of the top-N peak detector and hold/decay
// state machine described in Thetis Display.cs:4395-4714, 5453-5508
// [v2.10.3.13].
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
//                 Structural pattern follows AetherSDR (ten9876/
//                 AetherSDR, GPLv3).
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

#include <QVector>
#include <QtGlobal>

namespace NereusSDR {

/// One detected peak blob entry.
/// From Thetis display.cs:5453-5508 [v2.10.3.13] m_RX1Maximums array.
struct PeakBlob {
    int    binIndex   = 0;
    float  max_dBm    = 0.0f;
    qint64 timeMs     = 0;    // wall-clock ms when this blob was last refreshed
};

/// Top-N peak detector with optional hold + decay state machine.
///
/// Per-frame update sequence (mirrors Thetis Display.cs:5453-5508 [v2.10.3.13]):
///   1. Find local maxima (bins[i] > bins[i-1] && bins[i] > bins[i+1])
///   2. Sort descending by dBm
///   3. Keep top N (count())
///   4. Optionally constrain to filter-passband bins
///   5. Merge with prior blobs: preserve held max_dBm / timestamp for
///      matching bin indices
///   6. tickFrame(): apply hold + decay per Thetis display.cs:5483 [v2.10.3.13]:
///        entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
class PeakBlobDetector {
public:
    // ---- Configuration setters ----

    void setEnabled(bool e)            { m_enabled = e; if (!e) { m_blobs.clear(); } }
    /// From Thetis Display.cs:4407 [v2.10.3.13] m_nNumberOfMaximums = 3; max 20.
    void setCount(int n)               { m_count = qMax(1, n); }
    /// From Thetis Display.cs:4401 [v2.10.3.13] m_bInsideFilterOnly = false.
    void setInsideFilterOnly(bool i)   { m_insideOnly = i; }
    /// From Thetis Display.cs:4593 [v2.10.3.13] m_bBlobPeakHold = false.
    void setHoldEnabled(bool h)        { m_holdEnabled = h; }
    /// From Thetis Display.cs:4599 [v2.10.3.13] m_fBlobPeakHoldMS = 500.
    void setHoldMs(int ms)             { m_holdMs = ms; }
    /// From Thetis Display.cs:4605 [v2.10.3.13] m_bBlobPeakHoldDrop = false.
    void setHoldDrop(bool d)           { m_holdDrop = d; }
    /// From Thetis Display.cs:4697 [v2.10.3.13] m_dBmPerSecondPeakBlobFall = 6.0f.
    void setFallDbPerSec(double r)     { m_fallDbPerSec = r; }

    // ---- Per-frame update ----

    /// Detect local maxima in `bins`, keep top N, merge with held state.
    /// filterLowBin / filterHighBin are inclusive; used only when
    /// insideFilterOnly() is true.
    /// From Thetis display.cs:5453-5508 [v2.10.3.13].
    void update(const QVector<float>& bins, int filterLowBin, int filterHighBin);

    /// Apply decay to blobs that have exceeded the hold window.
    /// elapsedMs is the wall-clock time since the previous call — used to
    /// advance m_currentTimeMs. fps is the display frame rate used in the
    /// Thetis decay formula (display.cs:5483 [v2.10.3.13]).
    void tickFrame(int fps, int elapsedMs);

    // ---- Accessors ----

    bool enabled()       const { return m_enabled; }
    int  count()         const { return m_count; }
    bool insideOnly()    const { return m_insideOnly; }
    bool holdEnabled()   const { return m_holdEnabled; }
    int  holdMs()        const { return m_holdMs; }
    bool holdDrop()      const { return m_holdDrop; }
    double fallDbPerSec() const { return m_fallDbPerSec; }

    /// Current live blob list (sorted descending by max_dBm).
    const QVector<PeakBlob>& blobs() const { return m_blobs; }

private:
    bool   m_enabled       = false;
    // From Thetis Display.cs:4407 [v2.10.3.13] m_nNumberOfMaximums = 3
    int    m_count         = 3;
    // From Thetis Display.cs:4401 [v2.10.3.13] m_bInsideFilterOnly = false
    bool   m_insideOnly    = false;
    // From Thetis Display.cs:4593 [v2.10.3.13] m_bBlobPeakHold = false
    bool   m_holdEnabled   = false;
    // From Thetis Display.cs:4599 [v2.10.3.13] m_fBlobPeakHoldMS = 500
    int    m_holdMs        = 500;
    // From Thetis Display.cs:4605 [v2.10.3.13] m_bBlobPeakHoldDrop = false
    bool   m_holdDrop      = false;
    // From Thetis Display.cs:4697 [v2.10.3.13] m_dBmPerSecondPeakBlobFall = 6.0f
    double m_fallDbPerSec  = 6.0;

    QVector<PeakBlob> m_blobs;
    // Monotonically increasing wall-clock counter advanced by tickFrame().
    qint64 m_currentTimeMs = 0;
};

}  // namespace NereusSDR
