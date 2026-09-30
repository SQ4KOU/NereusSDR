#pragma once

// no-port-check: NereusSDR-original selection helper. It generalises the
// two-receiver rules ported in P1RadioConnection.cpp and
// P2RadioConnection.cpp (which carry the Thetis console.cs and mi0bot
// Penny.cs headers) to every slice counted on one receiver input. The
// Thetis and mi0bot file names below point at where those rules are
// quoted; no upstream code is reproduced here.

// =================================================================
// src/core/SharedInputLowPass.h  (NereusSDR)
// =================================================================
//
// Shared-input filters, ruling (c) 2026-09-30: which receiver the receive
// low-pass on a shared input follows. The connections call this to pick the
// filter, and RadioModel calls it to name the slice in the low-pass reason
// (AlexAdcState::lowPassSlice), so the reason always names the receiver the
// filter follows.
//
// One frequency source per filter, the one Thetis uses for it:
//
//  - The Alex receive low-pass (every Alex board, Protocol 1 and 2) is
//    chosen from the receiver's DDS frequency, Thetis's rx1_dds_freq_mhz /
//    rx2_dds_freq_mhz (console.cs:15487-15498 UpdateAlexTXFilter
//    [v2.10.3.15]). Both protocols go through the same setAlexLPF. Under
//    click-tune that is the DDC centre, not the VFO: RX1's DDS frequency
//    takes the VFO only with click-tune off, and the centre frequency
//    otherwise (console.cs:31894-31910 [v2.10.3.15]). Here: the receiver's
//    DDC centre.
//  - The HL2's receive filter is the N2ADR board, chosen by the OC pins of
//    a band, and that band is the VFO's (console.cs:29101-29106
//    [v2.10.3.15] BandByFreq(VFOAFreq) into UpdateExtCtrl; mi0bot
//    Penny.cs:183-189 [@c26a8a4] takes the higher band index when RX2 is
//    on, in the Band enum's order, OcMatrix::extCtrlBandIndex). Here: the VFO of
//    the slice the connection knows the receiver by, its centre when no VFO
//    was told.
//
// Candidates are one per hardware receiver slot, in slot order. On a tie
// the earlier candidate keeps it, as mi0bot's strict compare keeps RX1.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-30 - Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                AI-assisted implementation via Anthropic Claude Code.
//   2026-09-30 - Review fix: the notes describe the upstream rules and
//                cite them, with no upstream text quoted. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QList>
#include <QtGlobal>

#include "core/OcMatrix.h"
#include "models/Band.h"

namespace NereusSDR::SharedInputLowPass {

enum class Rule {
    HighestCentre,     // Alex receive low-pass: the DDC centre (DDS frequency)
    HighestVfoBand,    // HL2 N2ADR receive pins: the VFO's band, mi0bot's order
};

struct Candidate {
    int     slot {-1};       // hardware receiver slot (AlexRxBpf::countedSlotsAdc0 bit)
    quint64 centreHz {0};    // the DDC centre commanded to that slot
    quint64 vfoHz {0};       // the VFO of the slice the slot is known by, 0 if none
};

// The frequency the rule reads for one candidate. 0 means "not tuned".
inline quint64 ruleHz(Rule rule, const Candidate& c) noexcept
{
    if (rule == Rule::HighestVfoBand) {
        return (c.vfoHz != 0) ? c.vfoHz : c.centreHz;
    }
    return c.centreHz;
}

// Index into `candidates` of the receiver the low-pass follows, or -1 when
// none is tuned.
inline int highest(Rule rule, const QList<Candidate>& candidates) noexcept
{
    int best = -1;
    quint64 bestHz = 0;
    int bestBand = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        const quint64 hz = ruleHz(rule, candidates.at(i));
        if (hz == 0) { continue; }
        if (rule == Rule::HighestVfoBand) {
            const int band = OcMatrix::extCtrlBandIndex(
                bandFromFrequency(static_cast<double>(hz)));
            if (best < 0 || band > bestBand) {
                best = i;
                bestBand = band;
                bestHz = hz;
            }
        } else if (best < 0 || hz > bestHz) {
            best = i;
            bestHz = hz;
        }
    }
    return best;
}

} // namespace NereusSDR::SharedInputLowPass
