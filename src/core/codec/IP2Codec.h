// =================================================================
// src/core/codec/IP2Codec.h  (NereusSDR)
// =================================================================
//
// Per-board codec interface for the Protocol 2 command-packet compose
// layer. Subclasses model board variants:
// P2CodecOrionMkII handles the Orion-MKII / 7000D / 8000D / AnvelinaPro3
// family; P2CodecSaturn extends it with the G8NJJ Saturn BPF1 band-edge
// override for ANAN-G2 / G2-1K.
//
// P2RadioConnection owns std::unique_ptr<IP2Codec> chosen at connect time
// from m_hardwareProfile.model (see applyBoardQuirks()).
//
// Parallel to IP1Codec (which composes 5-byte C&C banks for Protocol 1);
// P2 uses four fixed-size command packets instead — CmdGeneral (60),
// CmdHighPriority (1444), CmdRx (1444), CmdTx (60).
//
// NereusSDR-original. Independently implemented from IP1Codec.h interface.
// No Thetis port; no PROVENANCE row.
// =================================================================

#pragma once

#include <QtGlobal>
#include "CodecContext.h"
#include "../HpsdrModel.h"

namespace NereusSDR {

class IP2Codec {
public:
    virtual ~IP2Codec() = default;

    // Each composer fills the buffer with the post-Phase-B byte layout.
    // Buffers are caller-allocated and zero-initialized by the caller.
    // Sequence numbers (bytes 0-3 of each packet) are NOT stamped here —
    // the caller (P2RadioConnection::sendCmd*) stamps them just before
    // UDP transmission so they remain monotonic across retransmits.
    virtual void composeCmdGeneral     (const CodecContext& ctx, quint8 buf[60])   const = 0;
    virtual void composeCmdHighPriority(const CodecContext& ctx, quint8 buf[1444]) const = 0;
    virtual void composeCmdRx          (const CodecContext& ctx, quint8 buf[1444]) const = 0;
    virtual void composeCmdTx          (const CodecContext& ctx, quint8 buf[60])   const = 0;

    // PureSignal DDC config emission. Phase 3M-4 Task 5.
    // Returns the wire-byte map describing how this board configures its
    // DDCs when PureSignal is active. Called from
    // ReceiverManager::updateDdcAssignment() (Task 6) to drive the PS
    // branches of per-board UpdateDDCs.
    //
    // The `model` parameter is included for symmetry with IP1Codec
    // (where P1CodecStandard handles multiple HpsdrModel values).  P2
    // codec subclasses currently map 1:1 to single board families —
    // P2CodecOrionMkII covers ANAN-100D/200D/ORIONMKII/7000D/8000D/
    // ANVELINAPRO3, P2CodecSaturn covers ANAN-G2/G2-1K — and the Thetis
    // switch lumps both into the same G2-class branch
    // (console.cs:8211-8295 [v2.10.3.13]).  Reserved for future
    // per-model divergence.
    //
    // The `adcCtrl1` / `adcCtrl2` parameters carry the live ADC control
    // bytes (Thetis console.cs `rx_adc_ctrl1` / `rx_adc_ctrl2` members
    // [v2.10.3.13]) into the masked emission formulas.
    //
    // Source: ports the per-board branches in Thetis console.cs UpdateDDCs()
    // (lines 8186-8538) [v2.10.3.13].
    virtual PsDdcConfig applyPureSignalDdcConfig(
        HPSDRModel model,
        bool psEnabled,
        bool diversityEnabled,
        bool moxState,
        int rx1Rate,
        int rx2Rate,
        bool rx2Enabled,
        quint8 adcCtrl1,
        quint8 adcCtrl2
    ) const = 0;
};

} // namespace NereusSDR
