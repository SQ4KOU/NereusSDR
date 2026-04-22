// =================================================================
// src/core/NbFamily.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/ChannelMaster/cmaster.c, original licence from Thetis source is included below
//   Project Files/Source/Console/HPSDR/specHPSDR.cs (upstream has no top-of-file header — project-level LICENSE applies)
//   Project Files/Source/Console/console.cs, original licence from Thetis source is included below
//   Project Files/Source/Console/setup.cs, original licence from Thetis source is included below
//
// See also src/core/NbFamily.h for the nob.h / nobII.h / snb.h upstream
// attribution blocks (Warren Pratt, NR0V) that cover the WDSP objects this
// file creates and destroys.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-22 — NbFamily lifecycle + tuning implementation.
//                Authored by J.J. Boyd (KG4VCF), with AI-assisted
//                transformation via Anthropic Claude Code.
// =================================================================

// --- From cmaster.c ---
/*  cmaster.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2014-2019 Warren Pratt, NR0V

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

warren@wpratt.com

*/

//=================================================================
// console.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
// Credit is given to Sizenko Alexander of Style-7 (http://www.styleseven.com/) for the Digital-7 font.
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
// Modifications to support the Behringer Midi controllers
// by Chris Codella, W2PA, May 2017.  Indicated by //-W2PA comment lines.
// Modifications for using the new database import function.  W2PA, 29 May 2017
// Support QSK, possible with Protocol-2 firmware v1.7 (Orion-MkI and Orion-MkII), and later.  W2PA, 5 April 2019
// Modfied heavily - Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
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

//=================================================================
// setup.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
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
// Continual modifications Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
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

#include "NbFamily.h"

#include "wdsp_api.h"

namespace NereusSDR {

namespace {
constexpr double kMsToSec = 0.001;
}

NbMode cycleNbMode(NbMode current)
{
    // From Thetis console.cs:42476-42482 [v2.10.3.13] — space-bar cycle:
    //   Unchecked/Indeterminate → Checked
    //   Checked                 → Unchecked
    //   (second press path handles Unchecked/Checked → Indeterminate)
    // Our simpler Off→NB→NB2→Off chain matches the user-visible three-state
    // rotation from console.cs:43513-43560 without Thetis's two-switch
    // hidden-state complexity.
    switch (current) {
        case NbMode::Off:  return NbMode::NB;
        case NbMode::NB:   return NbMode::NB2;
        case NbMode::NB2:  return NbMode::Off;
    }
    return NbMode::Off;
}

NbFamily::NbFamily(int channelId, int sampleRate, int bufferSize)
    : m_channelId(channelId)
    , m_sampleRate(sampleRate)
    , m_bufferSize(bufferSize)
{
#ifdef HAVE_WDSP
    // From Thetis cmaster.c:43-53 [v2.10.3.13] — NB (anb) create call.
    // Run flag starts at 0; processIq() gates xanbEXTF on m_mode separately.
    create_anbEXT(
        m_channelId,
        /*run=*/0,
        m_bufferSize,
        static_cast<double>(m_sampleRate),
        m_tuning.nbTauMs    * kMsToSec,
        m_tuning.nbHangMs   * kMsToSec,
        m_tuning.nbAdvMs    * kMsToSec,
        m_tuning.nbBacktau,
        m_tuning.nbThreshold);

    // From Thetis specHPSDR.cs:896-907 [v2.10.3.13] — NB2 P/Invoke 10-arg form:
    //   (id, run, mode, buffsize, samplerate, tau, hangtime, advtime, backtau, threshold)
    // This is a reduction of the nobII.c 13-arg C signature — advslewtime,
    // hangslewtime, and max_imp_seq_time are NOT post-create settable via the
    // EXT interface; they come from nobII.c internal defaults. `nb2SlewMs`
    // and `nb2MaxImpMs` fields in NbTuning are reserved for a future direct
    // C-API wrapper and are unused in this path.
    create_nobEXT(
        m_channelId,
        /*run=*/0,
        m_tuning.nb2Mode,
        m_bufferSize,
        static_cast<double>(m_sampleRate),
        m_tuning.nb2SlewMs     * kMsToSec,       // tau  (cmaster.c:62/64 default 0.0001 s)
        m_tuning.nb2HangMs     * kMsToSec,       // hangtime
        m_tuning.nb2AdvMs      * kMsToSec,       // advtime
        m_tuning.nb2Backtau,
        m_tuning.nb2Threshold);
#endif
}

NbFamily::~NbFamily()
{
#ifdef HAVE_WDSP
    // From Thetis cmaster.c:104-105 [v2.10.3.13] — destroy in reverse of create.
    destroy_nobEXT(m_channelId);
    destroy_anbEXT(m_channelId);
#endif
}

void NbFamily::setMode(NbMode mode)
{
    m_mode.store(mode, std::memory_order_release);
    // The per-buffer xanbEXTF / xnobEXTF dispatch in RxChannel::processIq()
    // reads this atomic; no WDSP "run" toggle needed.
}

void NbFamily::setSnbEnabled(bool enabled)
{
    m_snbEnabled.store(enabled, std::memory_order_release);
#ifdef HAVE_WDSP
    // From Thetis console.cs:36347 [v2.10.3.13]
    //   WDSP.SetRXASNBARun(WDSP.id(0, 0), chkDSPNB2.Checked)
    // WDSP: third_party/wdsp/src/snb.c
    SetRXASNBARun(m_channelId, enabled ? 1 : 0);
#endif
}

void NbFamily::setTuning(const NbTuning& tuning)
{
    m_tuning = tuning;
    pushAllTuning();
}

void NbFamily::setNbThreshold(double threshold)
{
    m_tuning.nbThreshold = threshold;
#ifdef HAVE_WDSP
    SetEXTANBThreshold(m_channelId, threshold);
#endif
}

void NbFamily::setNbTauMs(double ms)
{
    m_tuning.nbTauMs = ms;
#ifdef HAVE_WDSP
    // From Thetis setup.cs:16222 [v2.10.3.13]
    //   NBTau = 0.001 * (double)udDSPNBTransition.Value
    SetEXTANBTau(m_channelId, ms * kMsToSec);
#endif
}

void NbFamily::setNbLeadMs(double advMs)
{
    m_tuning.nbAdvMs = advMs;
#ifdef HAVE_WDSP
    // From Thetis setup.cs:16229 [v2.10.3.13]
    //   NBAdvTime = 0.001 * (double)udDSPNBLead.Value
    SetEXTANBAdvtime(m_channelId, advMs * kMsToSec);
#endif
}

void NbFamily::setNbLagMs(double hangMs)
{
    m_tuning.nbHangMs = hangMs;
#ifdef HAVE_WDSP
    // From Thetis setup.cs:16236 [v2.10.3.13]
    //   NBHangTime = 0.001 * (double)udDSPNBLag.Value
    SetEXTANBHangtime(m_channelId, hangMs * kMsToSec);
#endif
}

void NbFamily::pushAllTuning()
{
#ifdef HAVE_WDSP
    // NB1
    SetEXTANBTau      (m_channelId, m_tuning.nbTauMs  * kMsToSec);
    SetEXTANBHangtime (m_channelId, m_tuning.nbHangMs * kMsToSec);
    SetEXTANBAdvtime  (m_channelId, m_tuning.nbAdvMs  * kMsToSec);
    SetEXTANBBacktau  (m_channelId, m_tuning.nbBacktau);
    SetEXTANBThreshold(m_channelId, m_tuning.nbThreshold);

    // NB2
    SetEXTNOBMode     (m_channelId, m_tuning.nb2Mode);
    SetEXTNOBTau      (m_channelId, m_tuning.nb2SlewMs   * kMsToSec);
    SetEXTNOBHangtime (m_channelId, m_tuning.nb2HangMs   * kMsToSec);
    SetEXTNOBAdvtime  (m_channelId, m_tuning.nb2AdvMs    * kMsToSec);
    SetEXTNOBBacktau  (m_channelId, m_tuning.nb2Backtau);
    SetEXTNOBThreshold(m_channelId, m_tuning.nb2Threshold);
    // max_imp_seq_time has no post-create setter in Thetis's spec — it is
    // fixed at create time per cmaster.c:66. Changes require channel re-create.
#endif
}

} // namespace NereusSDR
