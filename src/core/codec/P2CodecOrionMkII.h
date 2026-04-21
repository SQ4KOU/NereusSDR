/*
 * network.c
 * Copyright (C) 2015-2020 Doug Wigley (W5WC)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

// =================================================================
// src/core/codec/P2CodecOrionMkII.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/ChannelMaster/network.c:821-1248 (CmdGeneral,
//   CmdHighPriority, CmdRx, CmdTx packet builders)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-20 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted transformation via Anthropic
//                Claude Code. Lifted from P2RadioConnection inline compose
//                helpers (extracted in Phase 3P-B Task 1); now delegates
//                here once Task 7 cutover lands.
// =================================================================

#pragma once

#include "IP2Codec.h"
#include <QtGlobal>

namespace NereusSDR {

// Base P2 codec for the Orion-MKII / 7000D / 8000D / AnvelinaPro3 family.
// Saturn (ANAN-G2 / G2-1K) extends this with Saturn BPF1 band-edge
// override (Phase B Task 5, P2CodecSaturn).
//
// Source: network.c:821-1248 [@501e3f5]
class P2CodecOrionMkII : public IP2Codec {
public:
    void composeCmdGeneral     (const CodecContext& ctx, quint8 buf[60])   const override;
    void composeCmdHighPriority(const CodecContext& ctx, quint8 buf[1444]) const override;
    void composeCmdRx          (const CodecContext& ctx, quint8 buf[1444]) const override;
    void composeCmdTx          (const CodecContext& ctx, quint8 buf[60])   const override;

protected:
    // Build Alex0 (bytes 1432-1435) and Alex1 (bytes 1428-1431) 32-bit registers.
    // Protected so P2CodecSaturn can override Alex1 with Saturn BPF1 band-edge bits.
    // Source: network.c:1040-1050 [@501e3f5]
    virtual quint32 buildAlex0(const CodecContext& ctx) const;
    virtual quint32 buildAlex1(const CodecContext& ctx) const;

    // Write a 32-bit value big-endian at offset into buf.
    static void writeBE32(quint8* buf, int offset, quint32 value);

    // Convert frequency in Hz to NCO phase word (2^32 / 122880000 * freqHz).
    // Source: network.c:936-1005 [@501e3f5]
    static quint32 hzToPhaseWord(quint64 freqHz);

    static constexpr int kMaxDdc = 7;
    static constexpr int kBufLen = 1444;
};

} // namespace NereusSDR
