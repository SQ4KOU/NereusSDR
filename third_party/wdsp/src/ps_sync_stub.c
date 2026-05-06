// no-port-check: NereusSDR-original glue stub. Not a port of Thetis logic;
// provides the SetPSRxIdx / SetPSTxIdx symbols so the bundled wdsp_static
// library exposes the API surface that Thetis's ChannelMaster module
// exports. The real Thetis implementation at
// `Project Files/Source/ChannelMaster/sync.c:69-79 [v2.10.3.13]` writes
// `psyn->xmtr[id].ps_tx_idx / ps_rx_idx` via _InterlockedExchange; that
// pcm-backed storage is part of the wider ChannelMaster module, which
// NereusSDR has not bundled yet.
//
// Until the ChannelMaster sync structure is ported, this stub stores the
// most-recent indices into a per-id static so the symbol resolves and
// downstream callers can be unit-tested. The C++ wrapper at
// src/core/TxChannel.cpp `setPSRxIdx` / `setPSTxIdx` already idempotency-
// suppresses duplicate calls before reaching this entry, so the stub is hit
// once per distinct value.
//
// Mirrors the existing txgain_stub.c convention used for the other
// ChannelMaster-exported symbols NereusSDR consumes (`SetTXFixedGain` /
// `SetTXFixedGainRun`) — see third_party/wdsp/src/txgain_stub.c.

/*  ps_sync_stub.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2026 J.J. Boyd, KG4VCF (NereusSDR-original glue stub)

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
// third_party/wdsp/src/ps_sync_stub.c (NereusSDR)
// =================================================================
//
// Upstream source: Thetis Project Files/Source/ChannelMaster/sync.c:69-79
// [v2.10.3.13] (Warren Pratt, NR0V) supplies the `SetPSRxIdx` / `SetPSTxIdx`
// API surface that NereusSDR provides via this glue. NereusSDR does NOT
// reproduce the Thetis implementation here — only a minimum storage stub
// that satisfies the linker. A future phase that ports the ChannelMaster
// sync pcm structure proper can drop this file and replace it with the
// real `Project Files/Source/ChannelMaster/sync.c` byte-for-byte port.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — NereusSDR-original glue stub created by J.J. Boyd
//                 (KG4VCF) during Phase 3M-4 Task 3, with AI-assisted
//                 implementation via Anthropic Claude Code.  Implements
//                 `SetPSRxIdx` / `SetPSTxIdx` minimum surface; per-id
//                 static array stores the most-recent indices so
//                 downstream callers can be unit-tested.
// =================================================================

// NereusSDR uses a small fixed transmitter cap (txid 0 is the only one
// used by current OpenHPSDR boards; 8 is generous and matches the
// kMaxXmtrs-style sentinel used in other NereusSDR callsites).
#define NEREUS_PS_SYNC_STUB_MAX_XMTRS 8

static struct {
    int ps_rx_idx;
    int ps_tx_idx;
} s_ps_sync_state[NEREUS_PS_SYNC_STUB_MAX_XMTRS];

// Mirrors the Thetis ChannelMaster API surface — see
// `Project Files/Source/ChannelMaster/sync.c:69-79 [v2.10.3.13]`.
// The Thetis impl issues an _InterlockedExchange against
// psyn->xmtr[id].ps_tx_idx / ps_rx_idx; NereusSDR's stub stores into a
// flat per-id array instead, since the psyn structure is not yet ported.
// Callers (cmaster.cs:533-534 [v2.10.3.13]) only invoke these once at PS
// init with txid = 0, so a simple non-atomic store is sufficient until
// real ChannelMaster wiring lands.

void SetPSTxIdx(int id, int idx)
{
    if (id < 0 || id >= NEREUS_PS_SYNC_STUB_MAX_XMTRS) return;
    s_ps_sync_state[id].ps_tx_idx = idx;
}

void SetPSRxIdx(int id, int idx)
{
    if (id < 0 || id >= NEREUS_PS_SYNC_STUB_MAX_XMTRS) return;
    s_ps_sync_state[id].ps_rx_idx = idx;
}
