// SPDX-License-Identifier: GPL-2.0-or-later
//
// tst_puresignal_caps.cpp (NereusSDR)
//
// Phase 3M-4 PureSignal port — Task 1: BoardCapabilities (psDefaultPeak +
// psSampleRate).  Verifies the per-board PureSignal hardware-default peak
// and feedback-channel sample rate against Thetis sources:
//   - ramdor clsHardwareSpecific.cs:285-310 [v2.10.3.13]
//   - mi0bot clsHardwareSpecific.cs:300-330 [v2.10.3.13-beta2] (HL2 override)
//
// Per-protocol values from the verbatim Thetis source (verified at task time):
//
//   ramdor clsHardwareSpecific.cs:285-310 [v2.10.3.13]:
//     P1 (USB protocol, default):    return 0.4072
//     P2 (UDP protocol, default):    return 0.2899
//     P2 case HPSDRHW.Saturn:        return 0.6121
//
//   mi0bot clsHardwareSpecific.cs:300-330 [v2.10.3.13-beta2] (HL2 override):
//     P1 case HPSDRHW.HermesLite:    return 0.233
//     P2 case HPSDRHW.HermesLite:    return 0.233
//
// Note on ANAN-G2: HPSDRModel::ANAN_G2 → HPSDRHW::Saturn (boardForModel in
// HpsdrModel.h:149).  The Thetis switch is keyed on _hardware (HPSDRHW), so
// G2/G2-1K → Saturn HW → 0.6121, NOT the 0.2899 P2-default.  This test
// asserts the actual upstream-faithful value.
//
// psSampleRate:
//   192000 for G2-class boards (cmaster.cs:424 ps_rate=192000 [v2.10.3.13]).
//   0 sentinel = "use rx1_rate" for HL2 (mi0bot console.cs:8472-8488
//   [v2.10.3.13-beta2]: "HL2 can work at a high sample rate", uses
//   Rate[0]=rx1_rate during PS).
//
// HL2 hasPureSignal: currently false in NereusSDR's caps table (Phase 3M-4
// PureSignal coordinator wires HL2 in a follow-up task).  This test only
// asserts the cap-field values (forward-compat populated), not the gate.
//
// no-port-check: this is a NereusSDR-original test fixture asserting cap
// fields against Thetis source rules; not itself a port of any Thetis file.

#include <QtTest>

#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TstPureSignalCaps : public QObject {
    Q_OBJECT
private slots:
    void g2HasPureSignalAnd192kAnd0_6121Default();
    void hl2PsDefaultPeak0_233AndPsRate0();
    void atlasHasNoPureSignal();
};

void TstPureSignalCaps::g2HasPureSignalAnd192kAnd0_6121Default() {
    const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
    QVERIFY(caps.hasPureSignal);
    QCOMPARE(caps.psSampleRate, 192000);
    // From Thetis clsHardwareSpecific.cs:304-305 [v2.10.3.13]
    //   else // protocol 2
    //   { case HPSDRHW.Saturn: return 0.6121; ... }
    // ANAN_G2 → HPSDRHW::Saturn (boardForModel HpsdrModel.h:149).
    QCOMPARE(caps.psDefaultPeak, 0.6121);
}

void TstPureSignalCaps::hl2PsDefaultPeak0_233AndPsRate0() {
    const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMESLITE);
    // From mi0bot-Thetis clsHardwareSpecific.cs:311-312 [v2.10.3.13-beta2]
    //   if (NetworkIO.CurrentRadioProtocol == RadioProtocol.USB) // protocol 1
    //   { case HPSDRHW.HermesLite: return 0.233; ... }
    QCOMPARE(caps.psDefaultPeak, 0.233);
    // For HL2, psSampleRate = 0 sentinel meaning "use rx1_rate" per
    // mi0bot console.cs:8472-8488 [v2.10.3.13-beta2]
    //   "HL2 can work at a high sample rate"
    QCOMPARE(caps.psSampleRate, 0);
}

void TstPureSignalCaps::atlasHasNoPureSignal() {
    const auto& caps = BoardCapsTable::forModel(HPSDRModel::HPSDR);
    QVERIFY(!caps.hasPureSignal);
}

QTEST_MAIN(TstPureSignalCaps)
#include "tst_puresignal_caps.moc"
