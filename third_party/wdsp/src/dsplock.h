// no-port-check: NereusSDR-original WDSP scheduling glue. Not a port of
// Thetis or WDSP logic; it schedules the existing per-channel csDSP lock so
// control calls are not starved by a busy DSP worker, and lets channel
// teardown wait for the worker to leave its loop.

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
// Channel teardown (pre_main_destroy) waits for the channel's worker to
// signal that it has left its loop before any buffer is freed.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23 - Created by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code (R-R3-39).
//   2026-09-23 - Worker-exit signal and WdspWaitWorkerExit added by
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code (R-R3-39).
// =================================================================

#ifndef _dsplock_h
#define _dsplock_h

#include "comm.h"

// Target of comm.h's EnterCriticalSection redirect.
void WdspEnterCS (LPCRITICAL_SECTION cs);

// The channel worker's csDSP acquire and release for one block (main.c).
void WdspWorkerEnter (int channel);
void WdspWorkerLeave (int channel);

// The channel worker calls this once, after its loop ends (main.c).
void WdspWorkerExited (int channel);

// Channel teardown (pre_main_destroy): returns once the channel's worker has
// left its loop. Never gives up while the worker is still inside a block;
// writes a dprintf line for every kWorkerExitLogIntervalMs of waiting.
void WdspWaitWorkerExit (int channel);

// Test-only: busy-wait this many microseconds inside the worker's locked
// section on every block, to simulate an overloaded DSP chain. Default 0
// (off); when off the worker pays one relaxed load per block. Not for
// production use.
PORT void WDSPSetTestBlockDelayUs (int channel, int microseconds);

// Test-only: how many times this channel's worker has left its loop in this
// process. Not for production use.
PORT int WDSPGetTestWorkerExitCount (int channel);

#endif
