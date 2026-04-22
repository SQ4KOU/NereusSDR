// =================================================================
// src/core/NbFamily.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/ChannelMaster/cmaster.c (NB/NB2 create defaults)
//   Project Files/Source/Console/HPSDR/specHPSDR.cs (P/Invoke signatures)
//   Project Files/Source/Console/console.cs (chkNB tri-state cycle)
//   Project Files/Source/Console/setup.cs (NB1 UI scaling factors)
//   Project Files/Source/wdsp/nob.h, nobII.h, snb.h (Warren Pratt, NR0V)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-22 — New NereusSDR facade class for the NB / NB2 / SNB
//                family. The WDSP C functions it wraps retain their
//                Warren Pratt / Richard Samphire copyrights verbatim
//                in third_party/wdsp/src/*.c headers.
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

// --- From nob.h ---
/*  nob.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2014 Warren Pratt, NR0V

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

// --- From nobII.h ---
/*  nobII.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2014 Warren Pratt, NR0V

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

// --- From snb.h ---
/*  snb.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2015, 2016 Warren Pratt, NR0V

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

#pragma once

#include <atomic>

#include "WdspTypes.h"

namespace NereusSDR {

// NB1/NB2 tuning knobs. Field units chosen to match Thetis setup.cs
// user-visible units (milliseconds for times, linear for threshold)
// so UI sliders round-trip without conversion; WDSP calls are converted
// to seconds at the wdsp_api.h boundary.
//
// Defaults below are Thetis cmaster.c:43-68 [@501e3f5] byte-for-byte.
struct NbTuning
{
    // NB1 — applied via create_anbEXT + SetEXTANB* post-create setters.
    double nbTauMs       = 0.1;    // cmaster.c:49   tau=0.0001 s
    double nbHangMs      = 0.1;    // cmaster.c:50   hangtime=0.0001 s
    double nbAdvMs       = 0.1;    // cmaster.c:51   advtime=0.0001 s
    double nbBacktau     = 0.05;   // cmaster.c:52   backtau=0.05 s (stored in s, not ms)
    double nbThreshold   = 30.0;   // cmaster.c:53   threshold=30.0

    // NB2 — applied via create_nobEXT + SetEXTNOB* post-create setters.
    int    nb2Mode       = 0;      // cmaster.c:61   mode=0 (zero-fill)
    double nb2SlewMs     = 0.1;    // cmaster.c:62/64 advslewtime=hangslewtime=0.0001 s
    double nb2AdvMs      = 0.1;    // cmaster.c:63   advtime=0.0001 s
    double nb2HangMs     = 0.1;    // cmaster.c:65   hangtime=0.0001 s
    double nb2MaxImpMs   = 25.0;   // cmaster.c:66   max_imp_seq_time=0.025 s
    double nb2Backtau    = 0.05;   // cmaster.c:67   backtau=0.05 s
    double nb2Threshold  = 30.0;   // cmaster.c:68   threshold=30.0
};

// Cycles the NbMode on user toggle: Off → NB → NB2 → Off.
// From Thetis console.cs:42476-42482 [@501e3f5] — space-bar increment
// and console.cs:43513-43560 [@501e3f5] — chkNB CheckState transition.
NbMode cycleNbMode(NbMode current);

// Wrapper facade. One instance per RxChannel. Holds no WDSP-side ownership —
// the underlying anb/nob objects are owned by the channel-master inside WDSP
// and keyed by channel id. This class centralizes the API surface and the
// audio-thread-safe mode flag.
class NbFamily
{
public:
    // channelId is the WDSP channel (== RxChannel::m_channelId).
    // sampleRate is the input sample rate handed to create_anbEXT/nobEXT.
    // bufferSize is the number of COMPLEX samples per xanbEXTF/xnobEXTF call
    // (matches fexchange2 input buffer size).
    NbFamily(int channelId, int sampleRate, int bufferSize);
    ~NbFamily();

    NbFamily(const NbFamily&)            = delete;
    NbFamily& operator=(const NbFamily&)  = delete;

    // Mode/SNB setters — safe to call from any thread. processIq() reads
    // m_mode via load(std::memory_order_acquire).
    void setMode(NbMode mode);
    NbMode mode() const { return m_mode.load(std::memory_order_acquire); }

    void setSnbEnabled(bool enabled);
    bool snbEnabled() const { return m_snbEnabled.load(std::memory_order_acquire); }

    // Tuning setter — pushes every field through to WDSP. Also stores
    // the struct locally so subsequent partial setters (e.g. setNbThreshold
    // from a slider) can mutate in place.
    void setTuning(const NbTuning& tuning);
    const NbTuning& tuning() const { return m_tuning; }

    // Per-knob convenience setters (used by RxApplet tuning sliders).
    // Each pushes only its one WDSP call; the local m_tuning is updated too.
    void setNbThreshold(double threshold);
    void setNbTauMs(double ms);
    void setNbLeadMs(double advMs);    // "lead" ≡ advtime
    void setNbLagMs(double hangMs);    // "lag"  ≡ hangtime

private:
    const int m_channelId;
    const int m_sampleRate;
    const int m_bufferSize;

    std::atomic<NbMode> m_mode{NbMode::Off};
    std::atomic<bool>   m_snbEnabled{false};
    NbTuning            m_tuning{};

    // Pushes all tuning fields through WDSP post-create setters for both
    // NB1 and NB2. Called from ctor (after create_*EXT) and from setTuning.
    void pushAllTuning();
};

} // namespace NereusSDR
