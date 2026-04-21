// =================================================================
// src/core/accessories/AlexController.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/HPSDR/Alex.cs:30-106
//   (TxAnt[]/RxAnt[]/RxOnlyAnt[] per-band antenna arrays,
//    LimitTXRXAntenna / SetAntennasTo1 external-ATU compat mode,
//    setRxAnt / setRxOnlyAnt / setTxAnt per-band setters)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-20 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted transformation via Anthropic
//                Claude Code. Replaces Phase 3I Alex stubs. Per-MAC
//                persistence via AppSettings. NereusSDR spin: 14 bands
//                (Band160m–XVTR) vs Thetis's 12 (B160M–B6M); extra
//                GEN/WWV/XVTR slots default to Ant 1. Block-TX safety
//                (blockTxAnt2/3) added as NereusSDR-native UI contract
//                on top of the core Thetis model.
// =================================================================
//
// === Verbatim Thetis Console/HPSDR/Alex.cs header (lines 1-23) ===
/*
*
* Copyright (C) 2008 Bill Tracey, KD5TFD, bill@ewjt.com
* Copyright (C) 2010-2020  Doug Wigley
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
//
// this module contains code to support the Alex Filter and Antenna Selection board
//
//
// =================================================================

#pragma once

#include <QObject>
#include <QString>
#include <array>
#include "models/Band.h"

namespace NereusSDR {

// Per-band antenna assignment (TX / RX1 / RX-only) + Block-TX safety.
//
// Source: HPSDR/Alex.cs:30-106 [@501e3f5]
//
// Thetis stores 12-band arrays indexed as (int)band - (int)Band.B160M.
// NereusSDR uses Band::Count=14 bands (adds GEN/WWV/XVTR); those slots
// default to Ant 1 like all bands.
//
// Block-TX toggles (blockTxAnt2 / blockTxAnt3) are a NereusSDR addition —
// safety guards for the Antenna Control UI: antenna ports wired RX-only
// should not accept TX assignments.
class AlexController : public QObject {
    Q_OBJECT

public:
    explicit AlexController(QObject* parent = nullptr);

    // ── Per-band antenna selection (1, 2, or 3) ──────────────────────────────
    // Source: HPSDR/Alex.cs:56-58 TxAnt/RxAnt/RxOnlyAnt fields [@501e3f5]
    int  txAnt(Band band) const;
    int  rxAnt(Band band) const;
    int  rxOnlyAnt(Band band) const;  // 1 = rx1, 2 = rx2, 3 = xv, 0 = none selected [Alex.cs:58]
    void setTxAnt(Band band, int ant);     // clamps to [1, 3]; rejected if blockTxAntN
    void setRxAnt(Band band, int ant);
    void setRxOnlyAnt(Band band, int ant);

    // ── Block-TX safety ──────────────────────────────────────────────────────
    // NereusSDR UI contract: when true, setTxAnt() rejects assignment to that port.
    bool blockTxAnt2() const;
    bool blockTxAnt3() const;
    void setBlockTxAnt2(bool on);
    void setBlockTxAnt3(bool on);

    // Forces every band's TX/RX antenna to port 1 (external-ATU compat).
    // Source: HPSDR/Alex.cs SetAntennasTo1(bool IsSetTo1) [@501e3f5]
    // "SetAntennasTo1 causes RX, TX antennas to be set to 1 — the various RX bypass unaffected"
    void setAntennasTo1(bool force);

    // ── Persistence ──────────────────────────────────────────────────────────
    void setMacAddress(const QString& mac);
    void load();   // hydrate from AppSettings under hardware/<mac>/alex/antenna/...
    void save();   // persist current state to AppSettings

signals:
    void antennaChanged(Band band);  // fires on any per-band assignment mutation
    void blockTxChanged();           // fires when blockTxAnt2 or blockTxAnt3 changes

private:
    // From Thetis HPSDR/Alex.cs:56-58 [@501e3f5] — Thetis uses 12 bands
    // (B160M..B6M); NereusSDR uses Band::Count=14 (adds GEN/WWV/XVTR).
    static constexpr int kBandCount = int(Band::Count);  // 14

    std::array<int, kBandCount> m_txAnt{};      // TxAnt[12] in Thetis
    std::array<int, kBandCount> m_rxAnt{};      // RxAnt[12] in Thetis
    std::array<int, kBandCount> m_rxOnlyAnt{};  // RxOnlyAnt[12] in Thetis
    bool m_blockTxAnt2{false};
    bool m_blockTxAnt3{false};

    QString m_mac;

    QString persistenceKey() const;  // "hardware/<mac>/alex/antenna"
    static int clampAnt(int v) { return v < 1 ? 1 : (v > 3 ? 3 : v); }
};

} // namespace NereusSDR
