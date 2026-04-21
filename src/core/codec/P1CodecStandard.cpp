// =================================================================
// src/core/codec/P1CodecStandard.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/ChannelMaster/networkproto1.c:419-698 (WriteMainLoop)
//   original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-20 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted transformation via Anthropic
//                Claude Code. Lifted from P1RadioConnection::composeCcForBank
//                which previously held the inline ramdor port; now
//                delegates here.
// =================================================================
//
// === Verbatim Thetis ChannelMaster/networkproto1.c header (lines 1-45) ===
// /*
//  * networkprot1.c
//  * Copyright (C) 2020 Doug Wigley (W5WC)
//  *
//  * This library is free software; you can redistribute it and/or
//  * modify it under the terms of the GNU Lesser General Public
//  * License as published by the Free Software Foundation; either
//  * version 2 of the License, or (at your option) any later version.
//  *
//  * This library is distributed in the hope that it will be useful,
//  * but WITHOUT ANY WARRANTY; without even the implied warranty of
//  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  * Lesser General Public License for more details.
//  *
//  * You should have received a copy of the GNU Lesser General Public
//  * License along with this library; if not, write to the Free Software
//  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//  *
//  */
// =================================================================

#include "P1CodecStandard.h"

namespace NereusSDR {

void P1CodecStandard::composeCcForBank(int bank, const CodecContext& ctx,
                                       quint8 out[5]) const
{
    // C0 low bit = MOX
    const quint8 C0base = ctx.mox ? 0x01 : 0x00;
    // Zero-init out[1..4] before the per-bank switch fills them.
    for (int i = 1; i < 5; ++i) { out[i] = 0; }

    switch (bank) {
        case 0:  bank0(ctx, out);  return;
        case 10: bank10(ctx, out); return;
        case 11: bank11(ctx, out); return;
        case 12: bank12(ctx, out); return;

        // Bank 1 — TX VFO
        // Source: networkproto1.c:477-481 [@501e3f5]
        case 1:
            out[0] = C0base | 0x02;
            out[1] = quint8((ctx.txFreqHz >> 24) & 0xFF);
            out[2] = quint8((ctx.txFreqHz >> 16) & 0xFF);
            out[3] = quint8((ctx.txFreqHz >>  8) & 0xFF);
            out[4] = quint8( ctx.txFreqHz        & 0xFF);
            return;

        // Banks 2-9 — RX VFOs (0..7), one per bank
        // Source: networkproto1.c:485-576 [@501e3f5]
        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: {
            out[0] = C0base | quint8(0x04 + (bank - 2) * 2);
            const int rxIdx = bank - 2;
            const quint64 freq = (rxIdx < ctx.activeRxCount)
                                  ? ctx.rxFreqHz[rxIdx]
                                  : ctx.txFreqHz;  // unused DDCs default to TX freq
            out[1] = quint8((freq >> 24) & 0xFF);
            out[2] = quint8((freq >> 16) & 0xFF);
            out[3] = quint8((freq >>  8) & 0xFF);
            out[4] = quint8( freq        & 0xFF);
            return;
        }

        // Bank 13 — CW enable / sidetone level
        // Source: networkproto1.c:634-638 [@501e3f5]
        case 13: out[0] = C0base | 0x1E; return;

        // Bank 14 — CW hang / sidetone freq
        // Source: networkproto1.c:642-646 [@501e3f5]
        case 14: out[0] = C0base | 0x20; return;

        // Bank 15 — EER PWM
        // Source: networkproto1.c:650-654 [@501e3f5]
        case 15: out[0] = C0base | 0x22; return;

        // Bank 16 — BPF2
        // Source: networkproto1.c:658-665 [@501e3f5]
        case 16: out[0] = C0base | 0x24; return;

        default:
            // Standard codec only emits banks 0-16; subclasses (AP3, HL2)
            // extend the range and override composeCcForBank to handle
            // banks 17/18 before delegating here.
            out[0] = C0base;
            return;
    }
}

// Bank 0 — General settings: sample rate, OC, preamp/dither/random,
// antenna, duplex, NDDC, diversity.
// Source: networkproto1.c:446-471 [@501e3f5/@c26a8a4 — identical]
void P1CodecStandard::bank0(const CodecContext& ctx, quint8 out[5]) const
{
    out[0] = (ctx.mox ? 0x01 : 0x00) | 0x00;
    out[1] = quint8(ctx.sampleRateCode & 0x03);
    out[2] = quint8((ctx.ocByte << 1) & 0xFE);
    // C3: rxPreamp + dither + random + RX input select.
    // Source: networkproto1.c:453-461 + 458 (RX input select default 0x20)
    out[3] = quint8((ctx.rxPreamp[0] ? 0x04 : 0)
                  | (ctx.dither[0]   ? 0x08 : 0)
                  | (ctx.random[0]   ? 0x10 : 0)
                  | 0x20);  // RX_1_In select default
    // C4: antenna, duplex, NDDC-1, diversity (networkproto1.c:463-471)
    out[4] = quint8((ctx.antennaIdx & 0x03)
                  | (ctx.duplex ? 0x04 : 0)
                  | (((ctx.activeRxCount - 1) & 0x0F) << 3)
                  | (ctx.diversity ? 0x80 : 0));
}

// Bank 10 — TX drive, mic, Alex HPF/LPF, PA enable
// Source: networkproto1.c:579-590 [@501e3f5]
void P1CodecStandard::bank10(const CodecContext& ctx, quint8 out[5]) const
{
    out[0] = (ctx.mox ? 0x01 : 0x00) | 0x12;
    out[1] = quint8(ctx.txDrive & 0xFF);
    out[2] = 0x40;  // line_in=0, mic_boost=0 defaults; bit 6 set per upstream
    out[3] = quint8(ctx.alexHpfBits | (ctx.paEnabled ? 0x80 : 0));
    out[4] = quint8(ctx.alexLpfBits);
}

// Bank 11 — Preamp + RX step ATT ADC0 (5-bit mask + 0x20 enable)
// Source: networkproto1.c:594-601 [@501e3f5]
void P1CodecStandard::bank11(const CodecContext& ctx, quint8 out[5]) const
{
    out[0] = (ctx.mox ? 0x01 : 0x00) | 0x14;
    out[1] = quint8((ctx.rxPreamp[0] ? 0x01 : 0)
                  | (ctx.rxPreamp[1] ? 0x02 : 0)
                  | (ctx.rxPreamp[2] ? 0x04 : 0)
                  | (ctx.rxPreamp[0] ? 0x08 : 0));  // bit3 = rx0 again (Thetis quirk)
    out[2] = 0;
    out[3] = 0;
    // canonical 5-bit ramdor encoding
    out[4] = quint8((ctx.rxStepAttn[0] & 0x1F) | 0x20);
}

// Bank 12 — Step ATT ADC1/2 + CW keyer
// Source: networkproto1.c:606-628 [@501e3f5]
//
// ADC1 carve-out: during MOX, force 0x1F UNLESS RedPitaya. Standard
// codec is non-RedPitaya; RedPitaya subclass overrides this method.
void P1CodecStandard::bank12(const CodecContext& ctx, quint8 out[5]) const
{
    out[0] = (ctx.mox ? 0x01 : 0x00) | 0x16;
    if (ctx.mox) {
        out[1] = 0x1F | 0x20;  // forced max
    } else {
        out[1] = quint8(ctx.rxStepAttn[1] & 0xFF) | 0x20;
    }
    out[2] = quint8((ctx.rxStepAttn[2] & 0x1F) | 0x20);
    // C3 / C4 = CW keyer defaults — zero for Phase A; Phase 3M-2 wires CW.
    out[3] = 0;
    out[4] = 0;
}

} // namespace NereusSDR
