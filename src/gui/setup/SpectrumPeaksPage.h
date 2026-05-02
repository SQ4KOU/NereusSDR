#pragma once

// =================================================================
// src/gui/setup/SpectrumPeaksPage.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from Thetis source is included below
//
// NereusSDR-original page structure.  Constants and property defaults
// reference Thetis display.cs:4395-4714 (peak blob / spectral peak hold).
// Rendering logic deferred to Tasks 2.5 / 2.6.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Skeleton created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
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

#include "gui/SetupPage.h"

class QCheckBox;
class QSpinBox;
class QPushButton;
class QGroupBox;

namespace NereusSDR {

class ColorSwatchButton;

// ---------------------------------------------------------------------------
// Display > Spectrum Peaks
// Hosts Active Peak Hold (5 controls, Task 2.5) and Peak Blobs
// (7 controls + 2 colors, Task 2.6).  Task 2.4 wires persistence only;
// rendering / decay logic lands in Tasks 2.5 and 2.6.
// ---------------------------------------------------------------------------
class SpectrumPeaksPage : public SetupPage {
    Q_OBJECT
public:
    explicit SpectrumPeaksPage(RadioModel* model, QWidget* parent = nullptr);

signals:
    /// Emitted when the user clicks the "← Spectrum defaults" cross-link.
    /// SetupDialog connects this to selectPage("Spectrum Defaults").
    void backToSpectrumDefaultsRequested();

private:
    void buildUI();

    // Active Peak Hold group (5 ctrls — full impl in Task 2.5)
    QGroupBox* m_aphGroup{nullptr};
    QCheckBox* m_aphEnable{nullptr};
    QSpinBox*  m_aphDurationMs{nullptr};
    QSpinBox*  m_aphDropDbPerSec{nullptr};
    QCheckBox* m_aphFill{nullptr};
    QCheckBox* m_aphOnTx{nullptr};

    // Peak Blobs group (7 ctrls + 2 colors — full impl in Task 2.6)
    // From Thetis Display.cs:4395-4714 [v2.10.3.13]
    QGroupBox*         m_blobGroup{nullptr};
    QCheckBox*         m_blobEnable{nullptr};
    QSpinBox*          m_blobCount{nullptr};
    QCheckBox*         m_blobInsideFilter{nullptr};
    QCheckBox*         m_blobHoldEnable{nullptr};
    QSpinBox*          m_blobHoldMs{nullptr};
    QCheckBox*         m_blobHoldDrop{nullptr};
    QSpinBox*          m_blobFallDbPerSec{nullptr};
    ColorSwatchButton* m_blobColor{nullptr};
    ColorSwatchButton* m_blobTextColor{nullptr};

    // Cross-link
    QPushButton* m_backBtn{nullptr};
};

}  // namespace NereusSDR
