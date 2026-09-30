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
//  - The HL2's receive filter is the N2ADR board, a bank of low-pass
//    filters chosen by the OC pins of a band, and that band is the VFO's
//    (console.cs:29101-29106 [v2.10.3.15] BandByFreq(VFOAFreq) into
//    UpdateExtCtrl). Which receiver's band: the one with the highest
//    frequency, read as for the Alex low-pass (the DDC centre). A low-pass
//    set for the highest counted receiver passes every lower one.
//    Divergence (maintainer ruling, 2026-09-30): NereusSDR orders by
//    frequency; mi0bot compares band enum values (Penny.cs:158-159 and
//    183-189 [@c26a8a4], idxb > idx over enums.cs:280-322), which rank WWV
//    and the SWL bands above 10 m, so enum order can pick a lower filter.
//    A receiver whose band is outside mi0bot's range (GEN, XVTR;
//    OcMatrix::extCtrlBandIndex -1, where mi0bot sends no pins,
//    Penny.cs:162-165) ranks below every receiver inside it.
//
// One ordering frequency serves both filters (the DDC centre); they differ
// only in what the chosen receiver sets: the Alex row for its centre, or the
// N2ADR pins for its VFO's band (ruleHz).
//
// Candidates are one per hardware receiver slot, in slot order, RX1 first.
// On a frequency tie the earlier candidate keeps it, as mi0bot's strict
// compare keeps RX1.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-30 - Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                AI-assisted implementation via Anthropic Claude Code.
//   2026-09-30 - Review fix: the notes describe the upstream rules and
//                cite them, with no upstream text quoted. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-30 - HL2 pins ordered by frequency, not mi0bot's band enum
//                (maintainer ruling on review I-2). J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QList>
#include <QtGlobal>

#include "core/OcMatrix.h"
#include "models/Band.h"

namespace NereusSDR::SharedInputLowPass {

enum class Rule {
    HighestCentre,     // Alex receive low-pass: the row for the DDC centre
    HighestCentrePins, // HL2 N2ADR receive pins: the pins for the VFO's band
};

struct Candidate {
    int     slot {-1};       // hardware receiver slot (AlexRxBpf::countedSlotsAdc0 bit)
    quint64 centreHz {0};    // the DDC centre commanded to that slot
    quint64 vfoHz {0};       // the VFO of the slice the slot is known by, 0 if none
};

// The frequency the chosen receiver's filter is set from: the centre for
// the Alex row, the VFO's band for the HL2 pins (its centre when no VFO was
// told). 0 means "not tuned".
inline quint64 ruleHz(Rule rule, const Candidate& c) noexcept
{
    if (rule == Rule::HighestCentrePins) {
        return (c.vfoHz != 0) ? c.vfoHz : c.centreHz;
    }
    return c.centreHz;
}

// The frequency receivers are ordered by: the DDC centre, for both rules
// (on the HL2 the VFO when no centre was told).
inline quint64 orderHz(Rule rule, const Candidate& c) noexcept
{
    if (rule == Rule::HighestCentrePins && c.centreHz == 0) {
        return c.vfoHz;
    }
    return c.centreHz;
}

// Index into `candidates` of the receiver the low-pass follows, or -1 when
// none is tuned. Highest orderHz wins; on the HL2 a receiver whose band
// has no place in mi0bot's range ranks below every one that has. A tie
// keeps the earlier candidate.
inline int highest(Rule rule, const QList<Candidate>& candidates) noexcept
{
    int best = -1;
    quint64 bestHz = 0;
    bool bestInRange = false;
    for (int i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates.at(i);
        const quint64 hz = orderHz(rule, c);
        if (hz == 0) { continue; }
        bool inRange = true;
        if (rule == Rule::HighestCentrePins) {
            inRange = OcMatrix::extCtrlBandIndex(
                          bandFromFrequency(static_cast<double>(ruleHz(rule, c)))) >= 0;
        }
        const bool better = (best < 0)
            || (inRange && !bestInRange)
            || (inRange == bestInRange && hz > bestHz);
        if (better) {
            best = i;
            bestHz = hz;
            bestInRange = inRange;
        }
    }
    return best;
}

} // namespace NereusSDR::SharedInputLowPass
