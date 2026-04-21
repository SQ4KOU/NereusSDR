// =================================================================
// src/core/codec/IP1Codec.h  (NereusSDR)
// =================================================================
//
// Per-board codec interface for the Protocol 1 EP2 C&C bank compose
// layer. Subclasses model Thetis's behavioral splits literally:
// P1CodecStandard mirrors ramdor's WriteMainLoop; P1CodecHl2
// mirrors mi0bot's WriteMainLoop_HL2; P1CodecAnvelinaPro3 and
// P1CodecRedPitaya extend Standard with the per-board carve-outs
// ramdor's main loop already gates with `if (HPSDRModel == ...)`.
//
// P1RadioConnection owns std::unique_ptr<IP1Codec> chosen at connect
// time from m_hardwareProfile.model (see applyBoardQuirks()).
//
// NereusSDR-original. No Thetis port; no PROVENANCE row.
// Independently implemented from Protocol 1 interface design.
// =================================================================

#pragma once

#include <QtGlobal>
#include "CodecContext.h"

namespace NereusSDR {

class IP1Codec {
public:
    virtual ~IP1Codec() = default;

    // Emit the 5-byte C&C payload for the given bank into out[0..4].
    // bank ∈ [0, maxBank()].
    virtual void composeCcForBank(int bank, const CodecContext& ctx,
                                  quint8 out[5]) const = 0;

    // Highest bank index this codec emits. Standard = 16, AnvelinaPro3 = 17,
    // Hl2 = 18. P1RadioConnection cycles 0..maxBank() round-robin.
    virtual int  maxBank() const = 0;

    // True for HL2 — when the I2C queue (Phase E) is non-empty, frame
    // compose is overridden to carry I2C TLV bytes instead of normal
    // C&C. False for Standard / AnvelinaPro3 / RedPitaya.
    virtual bool usesI2cIntercept() const { return false; }
};

} // namespace NereusSDR
