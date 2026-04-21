// =================================================================
// src/core/codec/P1CodecHl2.cpp  (NereusSDR)
// =================================================================
//
// Ported from mi0bot-Thetis sources:
//   Project Files/Source/ChannelMaster/networkproto1.c:869-1201
//   (WriteMainLoop_HL2)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-20 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted transformation via Anthropic
//                Claude Code. HL2-only codec; mirrors mi0bot's
//                literal WriteMainLoop_HL2 vs WriteMainLoop split.
//                Fixes reported HL2 S-ATT bug at the wire layer.
// =================================================================
//
// === Verbatim mi0bot networkproto1.c header (lines 1-19) ===
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

#include "P1CodecHl2.h"

namespace NereusSDR {

void P1CodecHl2::composeCcForBank(int bank, const CodecContext& ctx,
                                  quint8 out[5]) const
{
    // C0 low bit = XmitBit (MOX)
    // Source: mi0bot networkproto1.c:899 [@c26a8a4]
    const quint8 C0base = ctx.mox ? 0x01 : 0x00;
    for (int i = 0; i < 5; ++i) { out[i] = 0; }

    switch (bank) {
        // Bank 0 — General settings
        // Source: mi0bot networkproto1.c:938-978 [@c26a8a4 / matches @501e3f5]
        case 0:
            out[0] = C0base | 0x00;
            out[1] = quint8(ctx.sampleRateCode & 0x03);
            out[2] = quint8((ctx.ocByte << 1) & 0xFE);
            // C3: rxPreamp[0] + dither + random + RX_1_In select (default 0x20)
            out[3] = quint8((ctx.rxPreamp[0] ? 0x04 : 0)
                          | (ctx.dither[0]   ? 0x08 : 0)
                          | (ctx.random[0]   ? 0x10 : 0)
                          | 0x20);
            out[4] = quint8((ctx.antennaIdx & 0x03)
                          | (ctx.duplex ? 0x04 : 0)
                          | (((ctx.activeRxCount - 1) & 0x0F) << 3)
                          | (ctx.diversity ? 0x80 : 0));
            return;

        // Bank 1 — TX VFO
        // Source: mi0bot networkproto1.c:980-984 [@c26a8a4 / matches @501e3f5]
        case 1:
            out[0] = C0base | 0x02;
            out[1] = quint8((ctx.txFreqHz >> 24) & 0xFF);
            out[2] = quint8((ctx.txFreqHz >> 16) & 0xFF);
            out[3] = quint8((ctx.txFreqHz >>  8) & 0xFF);
            out[4] = quint8( ctx.txFreqHz        & 0xFF);
            return;

        // Banks 2-9 — RX VFOs
        // Source: mi0bot networkproto1.c:985-1058 [@c26a8a4 / matches @501e3f5]
        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: {
            out[0] = C0base | quint8(0x04 + (bank - 2) * 2);
            const int rxIdx = bank - 2;
            const quint64 freq = (rxIdx < ctx.activeRxCount)
                                  ? ctx.rxFreqHz[rxIdx]
                                  : ctx.txFreqHz;
            out[1] = quint8((freq >> 24) & 0xFF);
            out[2] = quint8((freq >> 16) & 0xFF);
            out[3] = quint8((freq >>  8) & 0xFF);
            out[4] = quint8( freq        & 0xFF);
            return;
        }

        // Bank 10 — TX drive, mic boost, Alex HPF/LPF, PA
        // Source: mi0bot networkproto1.c:1060-1090 [@c26a8a4 / matches @501e3f5]
        case 10:
            out[0] = C0base | 0x12;
            out[1] = quint8(ctx.txDrive & 0xFF);
            out[2] = 0x40;
            out[3] = quint8(ctx.alexHpfBits | (ctx.paEnabled ? 0x80 : 0));
            out[4] = quint8(ctx.alexLpfBits);
            return;

        // Bank 11 — Preamp + RX/TX step ATT ADC0 (HL2 6-bit encoding + MOX branch)
        // **THIS IS THE BUG FIX** — HL2 needs 6-bit mask (0x3F) + 0x40 enable,
        // and MOX branch selects txStepAttn[0] instead of rxStepAttn[0].
        // Standard codec keeps ramdor's 5-bit (0x1F) + 0x20 encoding.
        // Source: mi0bot networkproto1.c:1091-1104 [@c26a8a4]
        case 11: {
            out[0] = C0base | 0x14;
            out[1] = quint8((ctx.rxPreamp[0] ? 0x01 : 0)
                          | (ctx.rxPreamp[1] ? 0x02 : 0)
                          | (ctx.rxPreamp[2] ? 0x04 : 0)
                          | (ctx.rxPreamp[0] ? 0x08 : 0));
            out[2] = 0;
            out[3] = 0;
            // MI0BOT: Different read loop for HL2 — Larger range for the HL2 attenuator
            // [original inline comment from networkproto1.c:1100,1102]
            if (ctx.mox) {
                out[4] = quint8((ctx.txStepAttn[0] & 0b00111111) | 0b01000000);  // Larger range for the HL2 attenuator
            } else {
                out[4] = quint8((ctx.rxStepAttn[0] & 0b00111111) | 0b01000000);  // Larger range for the HL2 attenuator
            }
            return;
        }

        // Bank 12 — Step ATT ADC1/2 + CW keyer
        // HL2 behavior: identical to Standard (forces ADC1=0x1F under MOX).
        // No RedPitaya-special-case needed — HL2 is known hardware.
        // Source: mi0bot networkproto1.c:1106-1125 [@c26a8a4]
        case 12: {
            out[0] = C0base | 0x16;
            if (ctx.mox) {
                out[1] = 0x1F | 0x20;  // forced under MOX (same as Standard)
            } else {
                out[1] = quint8(ctx.rxStepAttn[1]) | 0x20;
            }
            out[2] = quint8((ctx.rxStepAttn[2] & 0x1F) | 0x20);
            out[3] = 0;
            out[4] = 0;
            return;
        }

        // Banks 13-16 — CW / EER / BPF2 (identical to Standard)
        // Source: mi0bot networkproto1.c:1126-1159 [@c26a8a4 / matches @501e3f5]
        case 13: out[0] = C0base | 0x1E; return;
        case 14: out[0] = C0base | 0x20; return;
        case 15: out[0] = C0base | 0x22; return;
        case 16: out[0] = C0base | 0x24; return;

        // Bank 17 — TX latency + PTT hang (HL2-only, NOT AnvelinaPro3 extra OC)
        // Source: mi0bot networkproto1.c:1162-1168 [@c26a8a4]
        case 17:
            out[0] = C0base | 0x2E;
            out[1] = 0;
            out[2] = 0;
            out[3] = quint8(ctx.hl2PttHang & 0b00011111);
            out[4] = quint8(ctx.hl2TxLatency & 0b01111111);
            return;

        // Bank 18 — Reset on disconnect (HL2-only firmware feature)
        // Source: mi0bot networkproto1.c:1170-1176 [@c26a8a4]
        case 18:
            out[0] = C0base | 0x74;
            out[1] = 0;
            out[2] = 0;
            out[3] = 0;
            out[4] = quint8(ctx.hl2ResetOnDisconnect ? 0x01 : 0x00);
            return;

        default:
            out[0] = C0base;
            return;
    }
}

} // namespace NereusSDR
