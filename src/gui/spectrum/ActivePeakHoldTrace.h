#pragma once

// =================================================================
// src/gui/spectrum/ActivePeakHoldTrace.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs
//   original licence from Thetis source is included below
//
// NereusSDR-original class structure. Constants and logic reference
// Thetis display.cs: m_bActivePeakHold and associated peak-hold
// rendering.  Thetis stores the trace as a per-bin max array
// (display.cs:~4750) and decays it in the display timer; NereusSDR
// mirrors that pattern here with an explicit tickFrame() step.
//
// Design decision Q14.1 (locked): rendered as a separate pass on
// SpectrumWidget, not composited with the main trace, for
// architectural flexibility.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
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
#include <limits>

namespace NereusSDR {

/// Per-bin peak-hold trace with configurable decay and TX-state gating.
///
/// State machine:
///   - update(currentBins): if !txActive || onTx, raise peak[i] = max(peak[i], current[i])
///   - tickFrame(fps): peak[i] -= dropDbPerSec / fps  (linear decay)
///   - clear(): reset peaks to -∞
///
/// Rendered as a separate trace on SpectrumWidget (Q14.1: separate pass for
/// architectural flexibility, not composited with main spectrum trace).
///
/// From Thetis display.cs m_bActivePeakHold [v2.10.3.13] — per-bin maximum
/// tracking with decay, mirrored to C++20/Qt6.
class ActivePeakHoldTrace {
public:
    explicit ActivePeakHoldTrace(int nBins = 0);
    void resize(int nBins);

    // ---- Configuration ----

    void setEnabled(bool e)             { m_enabled = e; }
    // Hold duration before decay begins (ms). Currently stored for UI
    // round-trip; actual hold timer deferred to per-bin age tracking
    // (Task 2.5 follow-up). For now, decay is immediate (constant rate).
    void setDurationMs(int ms)          { m_durationMs = ms; }
    // From Thetis Display.cs:4697 [v2.10.3.13] m_dBmPerSecondPeakBlobFall = 6.0f
    // — reused as the default decay rate for the Active Peak Hold trace.
    void setDropDbPerSec(double r)      { m_dropDbPerSec = r; }
    void setFill(bool f)                { m_fill = f; }
    // When true, the trace continues to update while TX is active.
    // When false (default), peaks are frozen during TX.
    // From Thetis display.cs m_bActivePeakHold / MOX interaction [v2.10.3.13].
    void setOnTx(bool o)                { m_onTx = o; }
    void setTxActive(bool t)            { m_txActive = t; }
    bool txActive() const               { return m_txActive; }

    // ---- Per-frame operations ----

    /// Raise each peak bin to max(peak[i], currentBins[i]).
    /// No-op when disabled or when TX is active and !onTx.
    void update(const QVector<float>& currentBins);

    /// Decay all finite peaks by (dropDbPerSec / fps) dB.
    /// No-op when disabled or fps <= 0.
    void tickFrame(int fps);

    /// Reset all peaks to -infinity.
    void clear();

    // ---- Accessors ----

    bool   enabled() const              { return m_enabled; }
    bool   fill() const                 { return m_fill; }
    int    durationMs() const           { return m_durationMs; }
    double dropDbPerSec() const         { return m_dropDbPerSec; }
    float  peak(int bin) const          {
        return (bin >= 0 && bin < m_peaks.size())
               ? m_peaks[bin]
               : -std::numeric_limits<float>::infinity();
    }
    const QVector<float>& peaks() const { return m_peaks; }
    int    size() const                 { return m_peaks.size(); }

private:
    bool   m_enabled       = false;
    // From Thetis Display.cs:4599 [v2.10.3.13] m_fBlobPeakHoldMS = 500 — similar
    // hold pattern; Active Peak Hold defaults to 2000 ms per SpectrumPeaksPage.
    int    m_durationMs    = 2000;
    // From Thetis Display.cs:4697 [v2.10.3.13] m_dBmPerSecondPeakBlobFall = 6.0f
    double m_dropDbPerSec  = 6.0;
    bool   m_fill          = false;
    bool   m_onTx          = false;
    bool   m_txActive      = false;
    QVector<float> m_peaks;
};

}  // namespace NereusSDR
