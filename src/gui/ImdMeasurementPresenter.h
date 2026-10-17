// Ported from Thetis Project Files/Source/Console/display.cs.
// The paired implementation carries the sourced trigger constant;
// the GUI presenter interface and frame acceptance policy are NereusSDR-original.
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
// Transitions to directX and continual modifications Copyright (C) 2020-2026 Richard Samphire (MW0LGE)
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
// no-port-check: NereusSDR-original GUI frame acceptance and validity policy.
// Modification history (NereusSDR):
//   2026-10-13: J.J. Boyd (KG4VCF), OpenAI Codex; analyze once per fresh frame.
#include "gui/ImdMeasurementFrame.h"
#include <QVector>
namespace NereusSDR {
class ImdMeasurementPresenter {
public:
    const ImdMeasurementFrame& acceptVisibleSpectrum(const QVector<float>& pixelsDbm,
                                                     const ImdInputContext& context);
    const ImdMeasurementFrame& frame() const { return m_frame; }
    void invalidate(ImdStatus status, const ImdInputContext& context);
private:
    friend class SpectrumWidget;
    ImdMeasurementFrame m_frame;
    ImdOverlay m_overlay;
    bool m_hasAcceptedFrame{false};
    ImdInputContext m_lastAcceptedContext;
};
} // namespace NereusSDR
