// =================================================================
// src/core/codec/P1CodecStandard.h  (NereusSDR)
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

#pragma once

#include "IP1Codec.h"

namespace NereusSDR {

// ramdor WriteMainLoop port. Banks 0-16. Used for every non-HL2 P1
// board by default; AnvelinaPro3 and RedPitaya extend it with bank-17
// or bank-12 overrides (see those subclasses).
class P1CodecStandard : public IP1Codec {
public:
    void composeCcForBank(int bank, const CodecContext& ctx, quint8 out[5]) const override;
    int  maxBank() const override { return 16; }

protected:
    // Helpers exposed so subclasses can call into specific banks they
    // don't override.
    void bank0(const CodecContext& ctx, quint8 out[5]) const;
    void bank10(const CodecContext& ctx, quint8 out[5]) const;
    void bank11(const CodecContext& ctx, quint8 out[5]) const;
    virtual void bank12(const CodecContext& ctx, quint8 out[5]) const;  // RedPitaya overrides
};

} // namespace NereusSDR
