// no-port-check: NereusSDR-original WDSP scheduling glue. Not a port of
// Thetis or WDSP logic; it schedules the existing per-channel csDSP lock so
// control calls are not starved by a busy DSP worker, and lets channel
// teardown wait for the worker to leave its loop, and times each worker block
// so the application can see how loaded each channel is.

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
// Each worker block (csDSP acquired to csDSP released) is timed with a
// monotonic clock; per-channel cumulative counters are written only by the
// worker and read, without csDSP, through GetChannelDspLoad.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23 - Created by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code (R-R3-39).
//   2026-09-23 - Worker-exit signal and WdspWaitWorkerExit added by
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code (R-R3-39).
//   2026-09-23 - Per-channel block timing and GetChannelDspLoad added by
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code (R-R3-40).
//   2026-09-23 - Test-only process delay (WdspWorkerTestProcessDelay,
//                 WDSPSetTestProcessDelayUs) added by J.J. Boyd (KG4VCF),
//                 with AI-assisted implementation via Anthropic Claude Code
//                 (R-R3-39).
//   2026-09-23 - Block in progress (currentBlockNs), block period published
//                 at block start, and the per-interval longest block
//                 (TakeChannelDspIntervalMaxBlockUs) added by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code (R-R3-40).
//   2026-09-23 - Worker start result (WdspWorkerStarted): teardown of a
//                 channel whose worker never started logs once and does not
//                 wait, by J.J. Boyd (KG4VCF), with AI-assisted
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

// The channel worker calls this once, after its loop ends (main.c).
void WdspWorkerExited (int channel);

// start_thread (channel.c) reports whether it started the channel's worker
// (started != 0). A teardown of a channel whose worker never started logs
// one line and returns without waiting, since no exit will ever come.
void WdspWorkerStarted (int channel, int started);

// The channel worker calls this at the start of a block it processes, after
// its exec_bypass check (main.c). It runs the test-only process delay set by
// WDSPSetTestProcessDelayUs and otherwise returns after one relaxed load.
void WdspWorkerTestProcessDelay (int channel);

// Channel teardown (pre_main_destroy): returns once the channel's worker has
// left its loop. Never gives up while the worker is still inside a block;
// writes a dprintf line for every kWorkerExitLogIntervalMs of waiting.
// Returns at once, after one dprintf line, if the worker never started.
void WdspWaitWorkerExit (int channel);

// One channel's worker load since the process started. Every field only
// grows except blockPeriodUs, which is the block period (dsp_size / dsp_rate)
// of the worker's latest or current block (0 before its first block), and
// currentBlockNs.
//   blocks         - worker blocks completed
//   busyNs         - total time the worker held csDSP for those blocks
//   lateBlocks     - blocks that took longer than their block period
//   maxBlockUs     - longest single block
//   currentBlockNs - how long the block in progress has run so far, 0 when
//                    the worker is not inside a block
// src/core/wdsp_api.h declares the same struct; the guard lets a file include
// both headers.
#ifndef NEREUS_WDSP_CHANNEL_LOAD_DEFINED
#define NEREUS_WDSP_CHANNEL_LOAD_DEFINED
typedef struct
{
	long long blocks;
	long long busyNs;
	long long lateBlocks;
	long long maxBlockUs;
	int blockPeriodUs;
	long long currentBlockNs;
} WdspChannelLoad;
#endif

// Copies the channel's load counters into *out without taking csDSP, so it
// never waits for the worker. Returns 0 on success, -1 for an invalid
// channel or a null out. The fields are read one by one, so a block that
// completes during the read may be counted in some fields and not others.
PORT int GetChannelDspLoad (int channel, WdspChannelLoad* out);

// Returns the longest block (microseconds) the channel's worker completed
// since the previous call, and starts the next interval (0 if no block
// completed). One periodic reader owns this (NereusSDR's RadioModel load
// sampler); a second caller would split its intervals. Returns -1 for an
// invalid channel. Never takes csDSP.
PORT long long TakeChannelDspIntervalMaxBlockUs (int channel);

// Test-only: busy-wait this many microseconds inside the worker's locked
// section on every block, to simulate an overloaded DSP chain. Default 0
// (off); when off the worker pays one relaxed load per block. Not for
// production use.
PORT void WDSPSetTestBlockDelayUs (int channel, int microseconds);

// Test-only: like WDSPSetTestBlockDelayUs, but the busy-wait runs inside a
// block the worker processes, after its exec_bypass check and before
// dexchange, so a teardown that lands during the delay meets real DSP work.
// Default 0 (off); when off the worker pays one relaxed load per processed
// block. Not for production use.
PORT void WDSPSetTestProcessDelayUs (int channel, int microseconds);

// Test-only: how many times this channel's worker has left its loop in this
// process. Not for production use.
PORT int WDSPGetTestWorkerExitCount (int channel);

#endif
