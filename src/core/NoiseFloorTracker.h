// =================================================================
// src/core/NoiseFloorTracker.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-16 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
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

#pragma once

#include <QVector>

namespace NereusSDR {

// Lerp-based noise floor tracker ported from Thetis display.cs.
// From Thetis v2.10.3.13 display.cs:4628-4693 — noise floor estimation
class NoiseFloorTracker {
public:
    NoiseFloorTracker();

    void feed(const QVector<float>& binsDbm, float frameIntervalMs);
    float noiseFloor() const noexcept { return m_lerpAverage; }
    bool isGood() const noexcept { return m_isGood; }
    void triggerFastAttack();
    float attackTimeMs() const noexcept { return m_attackTimeMs; }
    void setAttackTimeMs(float ms) { m_attackTimeMs = ms; }

private:
    float m_lerpAverage{-200.0f};      // From Thetis v2.10.3.13 display.cs:4628
    float m_attackTimeMs{2000.0f};     // From Thetis v2.10.3.13 display.cs:4638
    bool m_isGood{false};              // From Thetis v2.10.3.13 display.cs:4630
    bool m_fastAttackPending{true};
    float m_elapsedMs{0.0f};
};

}  // namespace NereusSDR
