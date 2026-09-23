// no-port-check: NereusSDR-original WDSP scheduling glue. Not a port of
// Thetis or WDSP logic; it schedules the existing per-channel csDSP lock so
// control calls are not starved by a busy DSP worker.

/*  dsplock.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2026 J.J. Boyd, KG4VCF (NereusSDR-original scheduling glue)

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

boydsoftprez@gmail.com

*/

// =================================================================
// third_party/wdsp/src/dsplock.h (NereusSDR)
// =================================================================
//
// Fair turn-taking for each channel's csDSP lock. comm.h redirects every
// WDSP EnterCriticalSection call to WdspEnterCS. A thread entering a
// channel's csDSP announces itself while it waits; before the channel's
// worker retakes csDSP for its next block it holds off, for a bounded time,
// while announced waiters exist. Every other lock goes straight to the
// platform call.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23 - Created by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code (R-R3-39).
// =================================================================

#ifndef _dsplock_h
#define _dsplock_h

#include "comm.h"

// Target of comm.h's EnterCriticalSection redirect.
void WdspEnterCS (LPCRITICAL_SECTION cs);

// The channel worker's csDSP acquire and release for one block (main.c).
void WdspWorkerEnter (int channel);
void WdspWorkerLeave (int channel);

// Test-only: busy-wait this many microseconds inside the worker's locked
// section on every block, to simulate an overloaded DSP chain. Default 0
// (off); when off the worker pays one relaxed load per block. Not for
// production use.
PORT void WDSPSetTestBlockDelayUs (int channel, int microseconds);

#endif
