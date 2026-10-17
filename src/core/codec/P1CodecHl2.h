// =================================================================
// src/core/codec/P1CodecHl2.h  (NereusSDR)
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
//   2026-10-14 - The HL2 I2C OUT debug line is kept (I2cOutLine) and
//                printed by the caller once P1RadioConnection's frame lock
//                is released, as the frame is now composed on the EP2
//                sender thread. J.J. Boyd (KG4VCF), with AI-assisted
//                implementation via Anthropic Claude Code.
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

#pragma once

#include "IP1Codec.h"

#include <array>

namespace NereusSDR { class IoBoardHl2; }  // forward decl — full header in .cpp

namespace NereusSDR {

// mi0bot WriteMainLoop_HL2 port. Banks 0-18.
//
// Key divergences from Standard (ramdor WriteMainLoop):
//   - Bank 11 C4: 6-bit ATT mask (0x3F) + 0x40 enable (vs 0x1F + 0x20).
//                 Under MOX, sends txStepAttn[0] instead of rxStepAttn[0].
//                 This is the load-bearing bug fix for the reported S-ATT issue.
//                 Source: mi0bot networkproto1.c:1099-1102 [@c26a8a4]
//   - Bank 12:    Identical to Standard behavior (forces 0x1F under MOX).
//                 No RedPitaya-special-case needed — HL2 is never confused with
//                 RedPitaya, so the bank12() override from P1CodecRedPitaya does
//                 not apply. Uses direct IP1Codec implementation.
//                 Source: mi0bot networkproto1.c:1107-1122 [@c26a8a4]
//   - Bank 17:    TX latency + PTT hang (vs AnvelinaPro3 extra OC byte).
//                 Source: mi0bot networkproto1.c:1162-1168 [@c26a8a4]
//   - Bank 18:    Reset-on-disconnect flag (HL2-only firmware feature).
//                 Source: mi0bot networkproto1.c:1170-1176 [@c26a8a4]
//   - usesI2cIntercept() returns true — Phase E will use this hook to
//     swap in I2C TLV bytes when the IoBoardHl2 queue is non-empty.
//
// Note: banks 0-10, 13-16 are byte-for-byte identical to ramdor WriteMainLoop
// (mi0bot inherits them unchanged).
// Source: mi0bot networkproto1.c:869-1098 [@c26a8a4 / matches @501e3f5]
class P1CodecHl2 : public IP1Codec {
public:
    void composeCcForBank(int bank, const CodecContext& ctx, quint8 out[5]) const override;
    int  maxBank() const override { return 18; }
    bool usesI2cIntercept() const override { return true; }

    // Phase 3M-4 Task 5: PureSignal DDC config emission for HL2.
    // Source: mi0bot console.cs:8409-8488 [v2.10.3.13-beta2] (HL2 grouped
    // with HERMES/ANAN10/ANAN100 case; mi0bot adds HL2-specific rate
    // override at lines 8476-8485).
    //
    // Inline tags preserved per CLAUDE.md "Inline comment preservation":
    //MI0BOT   [console.cs:8409 `case HPSDRModel.HERMESLITE: // MI0BOT: HL2`
    //          + 8476 `if (hpsdr_model == HPSDRModel.HERMESLITE) // MI0BOT:
    //          HL2 can work at a high sample rate` — HL2 case-statement
    //          marker + high-rate branch attribution]
    PsDdcConfig applyPureSignalDdcConfig(
        HPSDRModel model,
        bool psEnabled,
        bool diversityEnabled,
        bool moxState,
        int rx1Rate,
        int rx2Rate,
        bool rx2Enabled,
        quint8 adcCtrl1,
        quint8 adcCtrl2
    ) const override;

    DdcAssignment applyDdcAssignment(
        const CodecContext& ctx,
        const std::array<SliceConfig, 5>& slices) const override;

    // Phase 3P-E Task 2: wire to IoBoardHl2 for I2C intercept.
    // Called by P1RadioConnection::setIoBoard() at connect time.
    void setIoBoard(IoBoardHl2* io) { m_io = io; }

    // Compose the next I2C TLV frame (5 bytes) for the oldest pending
    // I2C transaction in the IoBoardHl2 queue.  Returns true if a frame
    // was emitted (txn dequeued); false if queue is empty / no IoBoard wired.
    // mox: current MOX state (written into C0 bit 0, matching XmitBit).
    //
    // Source: mi0bot networkproto1.c:898-943 [@c26a8a4]
    bool tryComposeI2cFrame(quint8 out[5], bool mox) const;

    // The "HL2 I2C OUT" debug line of an I2C frame (2026-10-14). The frame
    // is composed with P1RadioConnection's frame lock held, on the EP2
    // sender thread, so tryComposeI2cFrame keeps the line here instead of
    // printing it there. The caller takes the kept lines while it still
    // holds the lock (takeI2cOutLog) and prints them (printI2cOutLine)
    // once the lock is released. If nobody takes them (a compose outside
    // the sender, a test), the lines are printed at once when the store
    // is full, so none is lost.
    struct I2cOutLine {
        std::array<quint8, 5> cc{};
        quint8 bus{0};
        quint8 address{0};
        quint8 control{0};
        bool   isRead{false};
        bool   needsResponse{false};
    };
    // One EP2 frame carries two subframes, and an I2C frame goes at most
    // every third subframe, so a frame keeps one line at most.
    static constexpr int kI2cOutLogLines = 4;
    using I2cOutLog = std::array<I2cOutLine, kI2cOutLogLines>;
    // Moves the kept lines to out, oldest first, and returns how many.
    int takeI2cOutLog(I2cOutLog& out) const;
    static void printI2cOutLine(const I2cOutLine& line);

private:
    IoBoardHl2* m_io{nullptr};
    // Lines kept by tryComposeI2cFrame; read and written with the frame
    // lock held, as the rest of the compose path is.
    mutable I2cOutLog m_i2cOutLog{};
    mutable int m_i2cOutLogCount{0};
};

} // namespace NereusSDR
