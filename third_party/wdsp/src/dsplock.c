// no-port-check: NereusSDR-original WDSP scheduling glue. Not a port of
// Thetis or WDSP logic; it schedules the existing per-channel csDSP lock so
// control calls are not starved by a busy DSP worker, and lets channel
// teardown wait for the worker to leave its loop, and times each worker block
// so the application can see how loaded each channel is.

/*  dsplock.c

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
// third_party/wdsp/src/dsplock.c (NereusSDR)
// =================================================================
//
// Why: the platform locks behind csDSP (a recursive pthread mutex on
// Linux/macOS, a CRITICAL_SECTION on Windows) do not hand ownership to a
// waiter. A channel worker that is slower than real time always has its next
// block ready, releases csDSP at the end of a block and retakes it within
// microseconds, so a control call (a getter, a setter, NNR configuration)
// can wait for many blocks. On an overloaded Core that starved the event loop
// and dropped the station link.
//
// How: every WDSP EnterCriticalSection call arrives here through comm.h's
// redirect. A csDSP lock is recognised by its address inside ch[]; its caller
// counts itself as a waiter until the platform call returns. Before the
// worker takes csDSP for a block it checks that count with one atomic read.
// If waiters exist it pauses briefly between checks while they do, keeps
// waiting for a short grace after the last one so a burst of calls stays
// together, and never waits longer than a per-block budget. The worker holds
// no lock while it waits, so no lock-order edge is added.
//
// Teardown: pre_main_destroy used to sleep a fixed 25 ms after telling the
// worker to stop, then free the channel's buffers and locks. A block slower
// than that (a neural noise reduction block on a slow computer) was still
// running when its memory went away. The worker now counts its exit after its
// loop ends, and WdspWaitWorkerExit waits for that count. Each channel build
// starts exactly one worker and each teardown stops exactly one, so the Nth
// teardown of a channel waits for the Nth exit; a worker that has not yet
// been scheduled still sees run cleared and exits at once.
//
// Load: WdspWorkerEnter reads a monotonic clock right after the worker
// acquires csDSP and WdspWorkerLeave reads it again just before the release,
// so the block time is the whole locked section (including the test block
// delay). The worker alone adds that time to per-channel 64-bit atomic
// counters; GetChannelDspLoad reads them without csDSP, so reading never
// makes the worker wait. The cost to the worker is two clock reads and a few
// atomic stores per block.
//
// Portability: Windows uses only Interlocked*, QueryPerformanceCounter and
// SwitchToThread, which WDSP already relies on (the load counters use
// InterlockedExchangeAdd64, InterlockedExchange64 and
// InterlockedCompareExchange64); POSIX uses GCC/Clang atomic
// builtins, clock_gettime and nanosleep, as linux_port.c does.
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
// =================================================================

#include "comm.h"

// This file calls the platform lock directly.
#undef EnterCriticalSection

// Longest the worker holds off for waiters before one block, in microseconds.
static const int64_t kDspWorkerMaxDeferUs = 20000;
// The per-block budget is also at most this fraction (1/N) of the block period.
static const int64_t kDspWorkerDeferPeriodDivisor = 4;
// After the last announced waiter the worker keeps holding off this long, so
// back-to-back control calls are served as one burst.
static const int64_t kDspWorkerBurstGraceUs = 1000;
// Pause between waiter checks while the worker holds off.
static const long kDspWorkerPauseNs = 50000;

// Teardown writes one log line per this much time spent waiting for a worker.
static const int64_t kWorkerExitLogIntervalMs = 2000;
// Teardown polls finely for this long (a worker that is idle or between
// blocks exits well within it), then once per millisecond.
static const int64_t kWorkerExitFastPollUs = 5000;

// Threads currently blocked entering each channel's csDSP.
static volatile long dsp_waiters[MAX_CHANNELS];

// Test-only per-block busy-wait, microseconds; 0 = off.
static volatile long test_block_delay_us[MAX_CHANNELS];

// Times each channel's worker has left its loop.
static volatile long worker_exits[MAX_CHANNELS];
// Times teardown has waited for each channel's worker (control thread only).
static volatile long worker_exit_waits[MAX_CHANNELS];

// Per-channel load counters (see WdspChannelLoad). Written only by the
// channel's worker, read by GetChannelDspLoad from any thread.
static volatile long long load_blocks[MAX_CHANNELS];
static volatile long long load_busy_ns[MAX_CHANNELS];
static volatile long long load_late_blocks[MAX_CHANNELS];
static volatile long long load_max_block_us[MAX_CHANNELS];
static volatile long load_block_period_us[MAX_CHANNELS];
// When the worker acquired csDSP for its current block (worker only).
static int64_t block_start_ns[MAX_CHANNELS];

static int64_t dsplock_now_us (void)
{
#ifdef _WIN32
	LARGE_INTEGER frequency, counter;
	QueryPerformanceFrequency (&frequency);
	QueryPerformanceCounter (&counter);
	return (int64_t)(counter.QuadPart / frequency.QuadPart) * 1000000
		+ (int64_t)(counter.QuadPart % frequency.QuadPart) * 1000000 / frequency.QuadPart;
#else
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000 + (int64_t)now.tv_nsec / 1000;
#endif
}

static int64_t dsplock_now_ns (void)
{
#ifdef _WIN32
	LARGE_INTEGER frequency, counter;
	QueryPerformanceFrequency (&frequency);
	QueryPerformanceCounter (&counter);
	return (int64_t)(counter.QuadPart / frequency.QuadPart) * 1000000000
		+ (int64_t)(counter.QuadPart % frequency.QuadPart) * 1000000000 / frequency.QuadPart;
#else
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000000 + (int64_t)now.tv_nsec;
#endif
}

static void load_add64 (volatile long long* target, long long value)
{
#ifdef _WIN32
	InterlockedExchangeAdd64 (target, value);
#else
	__atomic_fetch_add (target, value, __ATOMIC_RELEASE);
#endif
}

static void load_store64 (volatile long long* target, long long value)
{
#ifdef _WIN32
	InterlockedExchange64 (target, value);
#else
	__atomic_store_n (target, value, __ATOMIC_RELEASE);
#endif
}

static long long load_read64 (volatile long long* source)
{
#ifdef _WIN32
	return InterlockedCompareExchange64 (source, 0, 0);
#else
	return __atomic_load_n (source, __ATOMIC_ACQUIRE);
#endif
}

static long load_read32 (volatile long* source)
{
#ifdef _WIN32
	return InterlockedCompareExchange (source, 0, 0);
#else
	return __atomic_load_n (source, __ATOMIC_ACQUIRE);
#endif
}

static void dsplock_pause (void)
{
#ifdef _WIN32
	// Sleep(1) is too coarse on Windows; yield the rest of the time slice.
	SwitchToThread ();
#else
	const struct timespec pause = { 0, kDspWorkerPauseNs };
	nanosleep (&pause, 0);
#endif
}

static long load_waiters (int channel)
{
#ifdef _WIN32
	return InterlockedCompareExchange (&dsp_waiters[channel], 0, 0);
#else
	return __atomic_load_n (&dsp_waiters[channel], __ATOMIC_ACQUIRE);
#endif
}

static long load_worker_exits (int channel)
{
#ifdef _WIN32
	return InterlockedCompareExchange (&worker_exits[channel], 0, 0);
#else
	return __atomic_load_n (&worker_exits[channel], __ATOMIC_ACQUIRE);
#endif
}

static long load_test_block_delay (int channel)
{
#ifdef _WIN32
	return test_block_delay_us[channel];
#else
	return __atomic_load_n (&test_block_delay_us[channel], __ATOMIC_RELAXED);
#endif
}

// Returns the channel whose csDSP lives at cs, or -1 for any other lock.
static int dsp_channel_of (const void* cs)
{
	const uintptr_t base = (uintptr_t)&ch[0].csDSP;
	const uintptr_t address = (uintptr_t)cs;
	uintptr_t offset, index;
	if (address < base)
	{
		return -1;
	}
	offset = address - base;
	if (offset % sizeof (ch[0]) != 0)
	{
		return -1;
	}
	index = offset / sizeof (ch[0]);
	return index < MAX_CHANNELS ? (int)index : -1;
}

static int valid_channel (int channel)
{
	return channel >= 0 && channel < MAX_CHANNELS;
}

static int64_t worker_defer_budget_us (int channel)
{
	const int64_t size = ch[channel].dsp_size;
	const int64_t rate = ch[channel].dsp_rate;
	int64_t budget = kDspWorkerMaxDeferUs;
	if (size > 0 && rate > 0)
	{
		const int64_t share = size * 1000000 / rate / kDspWorkerDeferPeriodDivisor;
		if (share < budget)
		{
			budget = share;
		}
	}
	return budget;
}

// Called only when waiters were seen. Holds off while they exist, plus the
// burst grace, within the per-block budget.
static void worker_defer (int channel)
{
	const int64_t budget = worker_defer_budget_us (channel);
	const int64_t start = dsplock_now_us ();
	int64_t last_seen = start;
	for (;;)
	{
		int64_t now;
		dsplock_pause ();
		now = dsplock_now_us ();
		if (now - start >= budget)
		{
			break;
		}
		if (load_waiters (channel) != 0)
		{
			last_seen = now;
		}
		else if (now - last_seen >= kDspWorkerBurstGraceUs)
		{
			break;
		}
	}
}

static void test_block_delay (int channel)
{
	const long delay = load_test_block_delay (channel);
	if (delay > 0)
	{
		const int64_t until = dsplock_now_us () + delay;
		while (dsplock_now_us () < until)
		{
			// busy-wait: simulates DSP work while holding csDSP
		}
	}
}

void WdspEnterCS (LPCRITICAL_SECTION cs)
{
	const int channel = dsp_channel_of (cs);
	if (channel < 0)
	{
		EnterCriticalSection (cs);
		return;
	}
	InterlockedIncrement (&dsp_waiters[channel]);
	EnterCriticalSection (cs);
	InterlockedDecrement (&dsp_waiters[channel]);
}

void WdspWorkerEnter (int channel)
{
	if (valid_channel (channel) && load_waiters (channel) != 0)
	{
		worker_defer (channel);
	}
	EnterCriticalSection (&ch[channel].csDSP);
	if (valid_channel (channel))
	{
		block_start_ns[channel] = dsplock_now_ns ();
		test_block_delay (channel);
	}
}

// Worker only, csDSP held: adds the block that just ended to the counters.
static void record_block (int channel)
{
	const int64_t elapsed_ns = dsplock_now_ns () - block_start_ns[channel];
	const long long elapsed_us = (long long)(elapsed_ns / 1000);
	const int64_t size = ch[channel].dsp_size;
	const int64_t rate = ch[channel].dsp_rate;
	const long period_us = (size > 0 && rate > 0) ? (long)(size * 1000000 / rate) : 0L;
	load_add64 (&load_busy_ns[channel], (long long)elapsed_ns);
	if (period_us > 0 && elapsed_us > period_us)
	{
		load_add64 (&load_late_blocks[channel], 1);
	}
	if (elapsed_us > load_max_block_us[channel])
	{
		load_store64 (&load_max_block_us[channel], elapsed_us);
	}
	if (period_us != load_block_period_us[channel])
	{
		InterlockedExchange (&load_block_period_us[channel], period_us);
	}
	load_add64 (&load_blocks[channel], 1);
}

void WdspWorkerLeave (int channel)
{
	if (valid_channel (channel))
	{
		record_block (channel);
	}
	LeaveCriticalSection (&ch[channel].csDSP);
}

void WdspWorkerExited (int channel)
{
	if (valid_channel (channel))
	{
		InterlockedIncrement (&worker_exits[channel]);
	}
}

void WdspWaitWorkerExit (int channel)
{
	long expected;
	int64_t start, next_log;
	if (!valid_channel (channel))
	{
		return;
	}
	expected = InterlockedIncrement (&worker_exit_waits[channel]);
	start = dsplock_now_us ();
	next_log = start + kWorkerExitLogIntervalMs * 1000;
	while (load_worker_exits (channel) < expected)
	{
		int64_t now = dsplock_now_us ();
		if (now - start < kWorkerExitFastPollUs)
		{
			dsplock_pause ();
		}
		else
		{
			Sleep (1);
		}
		now = dsplock_now_us ();
		if (now >= next_log)
		{
			dprintf ("wdsp: channel %d teardown still waiting for its DSP worker "
				"to finish a block (%d ms)\n", channel, (int)((now - start) / 1000));
			next_log += kWorkerExitLogIntervalMs * 1000;
		}
	}
}

PORT
void WDSPSetTestBlockDelayUs (int channel, int microseconds)
{
	if (!valid_channel (channel))
	{
		return;
	}
	InterlockedExchange (&test_block_delay_us[channel], microseconds > 0 ? (long)microseconds : 0L);
}

PORT
int GetChannelDspLoad (int channel, WdspChannelLoad* out)
{
	if (!valid_channel (channel) || out == 0)
	{
		return -1;
	}
	// blocks first: the worker bumps it last, so every block it counts has
	// already been added to the other fields.
	out->blocks = load_read64 (&load_blocks[channel]);
	out->busyNs = load_read64 (&load_busy_ns[channel]);
	out->lateBlocks = load_read64 (&load_late_blocks[channel]);
	out->maxBlockUs = load_read64 (&load_max_block_us[channel]);
	out->blockPeriodUs = (int)load_read32 (&load_block_period_us[channel]);
	return 0;
}

PORT
int WDSPGetTestWorkerExitCount (int channel)
{
	return valid_channel (channel) ? (int)load_worker_exits (channel) : 0;
}
