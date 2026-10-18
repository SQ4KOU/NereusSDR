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


// Ported from Thetis Project Files/Source/Console/display.cs (trigger constant).
// Modification history (NereusSDR):
//   2026-10-13: J.J. Boyd (KG4VCF), OpenAI Codex; GUI acceptance/partial state
//               uses ImdOverlay's sourced search and unchanged EMA/formulas.
#include "gui/ImdMeasurementPresenter.h"
#include <algorithm>
#include <cmath>
namespace NereusSDR {
namespace {
bool sameContext(const ImdInputContext& a, const ImdInputContext& b)
{
    return a.contextRevision == b.contextRevision
        && a.firstSampleHz == b.firstSampleHz && a.sampleStepHz == b.sampleStepHz
        && a.mox == b.mox && a.testing == b.testing
        && a.measurementsEnabled == b.measurementsEnabled
        && a.duplex == b.duplex && a.sourceEligible == b.sourceEligible;
}
}
void ImdMeasurementPresenter::invalidate(ImdStatus status, const ImdInputContext& context)
{
    m_overlay.reset();
    m_frame = {};
    m_frame.status = status;
    m_frame.context = context;
    // Keep the accepted identity so a replay cannot revive cleared numbers.
}
const ImdMeasurementFrame& ImdMeasurementPresenter::acceptVisibleSpectrum(
    const QVector<float>& pixelsDbm, const ImdInputContext& context)
{
    const bool unchangedContext = sameContext(context, m_frame.context);
    if (m_hasAcceptedFrame && sameContext(context, m_lastAcceptedContext)
        && context.frameSequence <= m_lastAcceptedContext.frameSequence) {
        return m_frame;
    }
    if (!unchangedContext) { m_overlay.reset(); }
    m_frame = {};
    m_frame.context = context;
    m_hasAcceptedFrame = true;
    m_lastAcceptedContext = context;
    const auto invalid = [this](ImdStatus status) -> const ImdMeasurementFrame& {
        m_frame.status = status;
        m_overlay.reset();
        return m_frame;
    };
    if (!context.measurementsEnabled) { return invalid(ImdStatus::Disabled); }
    if (!context.mox || !context.testing) { return invalid(ImdStatus::WaitingForTx); }
    if (!context.duplex) { return invalid(ImdStatus::DuplexRequired); }
    if (!context.sourceEligible || pixelsDbm.size() < 2
        || !std::isfinite(context.firstSampleHz) || !std::isfinite(context.sampleStepHz)
        || context.sampleStepHz <= 0.0
        || !std::isfinite(context.firstSampleHz + (pixelsDbm.size() - 1) * context.sampleStepHz)
        || !std::all_of(pixelsDbm.cbegin(), pixelsDbm.cend(),
                       [](float value) { return std::isfinite(value); })) {
        return invalid(ImdStatus::WaitingForSpectrum);
    }
    // From Thetis display.cs:5223 [v2.10.3.15] — trigger_delta = 10 dB.
    constexpr float kTriggerDelta = 10.0f; //db
    const std::vector<float> pixels(pixelsDbm.cbegin(), pixelsDbm.cend());
    const ImdOverlay::ProductMatches matches =
        ImdOverlay::matchImdProducts(ImdOverlay::detectPeaks(pixels, kTriggerDelta));
    if (!matches.fundamentalsResolved) {
        return invalid(ImdStatus::FundamentalsUnresolved);
    }
    // The existing readout's dbc is the stronger measured fundamental.
    const float reference = std::max(matches.peaks[0]->dBm, matches.peaks[1]->dBm);
    bool outside = false;
    bool missing = false;
    for (size_t i = 0; i < matches.peaks.size(); ++i) {
        if (matches.expectedSampleIndex[i]) {
            const int index = *matches.expectedSampleIndex[i];
            m_frame.expectedFrequencyHz[i] = context.firstSampleHz + index * context.sampleStepHz;
            outside = outside || index < 0 || index >= pixelsDbm.size();
        }
        if (matches.peaks[i]) {
            const Maximum& peak = *matches.peaks[i];
            m_frame.peaks[i] = ImdPeakReading{peak.x,
                context.firstSampleHz + peak.x * context.sampleStepHz,
                peak.dBm, peak.dBm - reference};
        } else { missing = true; }
    }
    if (outside) { return invalid(ImdStatus::ProductsOutsideView); }
    if (missing) { return invalid(ImdStatus::ProductsMissing); }
    m_overlay.updateReadout(*matches.peaks[0], *matches.peaks[1], *matches.peaks[2],
                            *matches.peaks[3], *matches.peaks[4], *matches.peaks[5]);
    m_frame.summary = m_overlay.readout();
    m_frame.status = ImdStatus::Valid;
    return m_frame;
}
} // namespace NereusSDR
