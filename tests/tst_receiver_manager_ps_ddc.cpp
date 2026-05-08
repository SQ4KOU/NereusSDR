// SPDX-License-Identifier: GPL-2.0-or-later
//
// no-port-check: test fixture cites Thetis source for expected wire-byte
// values, not a port itself.
//
// Phase 3M-4 Task 6: ReceiverManager UpdateDDCs PS branch test.
//
// Verifies that ReceiverManager:
//   1. Stores PS / MOX / diversity / RX rate / RX2 enable / ADC ctrl shadow
//      state via the new setters.
//   2. Calls the injected per-board codec's applyPureSignalDdcConfig() on
//      every relevant state change.
//   3. Re-emits the resulting PsDdcConfig via the new ddcConfigChanged
//      signal so RadioConnection (a follow-up task) can consume it.
//   4. Is idempotent — same-state setters do not re-emit.
//   5. With no codec injected, no PsDdcConfig is emitted (graceful fallback).
//
// Codec layer (Task 5) is the source-of-truth for wire bytes; this file
// only confirms ReceiverManager dispatches to it correctly and that the
// PsDdcConfig round-trips through QSignalSpy unchanged.
//
// Source: ports orchestration of Thetis console.cs:8186-8538 UpdateDDCs()
// [v2.10.3.13]; the per-board switch lives in the codec layer.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/ReceiverManager.h"
#include "core/codec/CodecContext.h"
#include "core/codec/IP1Codec.h"
#include "core/codec/IP2Codec.h"
#include "core/codec/P1CodecHl2.h"
#include "core/codec/P1CodecStandard.h"
#include "core/codec/P2CodecOrionMkII.h"
#include "core/codec/P2CodecSaturn.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

namespace {

// Bit constants from Thetis console.cs:8190 [v2.10.3.13]:
//   int DDC0 = 1, DDC1 = 2, DDC2 = 4, DDC3 = 8;
constexpr uint8_t DDC0 = 1, DDC1 = 2, DDC2 = 4, DDC3 = 8;

// From Thetis cmaster.cs:424 [v2.10.3.13]: private static int ps_rate = 192000;
constexpr int kPsRate = 192000;

} // namespace

class TstReceiverManagerPsDdc : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // Signal-fires-on-state-change
    void setPureSignalEnabledTriggersUpdateDdcAssignment();
    void setMoxTriggersUpdateDdcAssignment();
    void setDiversityEnabledTriggersUpdateDdcAssignment();

    // Wire-byte correctness via codec layer (round-trip through QSignalSpy)
    void g2PsOnMoxEmitsExpectedPsDdcConfig();
    void hl2PsOnMoxEmitsExpectedPsDdcConfig();
    void standardHermesPsOffMoxEmitsExpectedPsDdcConfig();

    // Idempotence
    void setSameStateDoesNotEmit();

    // No codec injected = no emit
    void noCodecInjectedDoesNotEmit();

    // Both codec setters: P2 takes precedence when both non-null (defensive
    // — RadioModel only ever sets one based on protocol)
    void p2CodecTakesPrecedenceOverP1();
};

void TstReceiverManagerPsDdc::initTestCase()
{
    // Q_DECLARE_METATYPE in CodecContext.h covers the static type, but
    // QSignalSpy needs runtime registration too.
    qRegisterMetaType<PsDdcConfig>("NereusSDR::PsDdcConfig");
    qRegisterMetaType<PsDdcConfig>("PsDdcConfig");
}

void TstReceiverManagerPsDdc::setPureSignalEnabledTriggersUpdateDdcAssignment()
{
    P2CodecSaturn codec;
    ReceiverManager mgr;
    mgr.setP2Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);

    mgr.setPureSignalEnabled(true);
    QVERIFY2(spy.count() >= 1,
             "ddcConfigChanged should fire on setPureSignalEnabled");
}

void TstReceiverManagerPsDdc::setMoxTriggersUpdateDdcAssignment()
{
    P2CodecSaturn codec;
    ReceiverManager mgr;
    mgr.setP2Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);

    mgr.setMox(true);
    QVERIFY2(spy.count() >= 1,
             "ddcConfigChanged should fire on setMox");
}

void TstReceiverManagerPsDdc::setDiversityEnabledTriggersUpdateDdcAssignment()
{
    P2CodecSaturn codec;
    ReceiverManager mgr;
    mgr.setP2Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);

    mgr.setDiversityEnabled(true);
    QVERIFY2(spy.count() >= 1,
             "ddcConfigChanged should fire on setDiversityEnabled");
}

void TstReceiverManagerPsDdc::g2PsOnMoxEmitsExpectedPsDdcConfig()
{
    // G2-class PS-on no-diversity MOX expected wire bytes
    // Source: console.cs:8256-8266 [v2.10.3.13]
    // Inline tag preservation (CLAUDE.md §"Inline comment preservation"):
    //   console.cs:8251 — // [2.10.3.13]MW0LGE p1 !  (Hermes-style fallback
    //                       above the G2 branch)
    //   DDCEnable = DDC0 + DDC2 = 5
    //   SyncEnable = DDC1 = 2
    //   Rate[0] = ps_rate = 192000
    //   Rate[1] = ps_rate = 192000
    //   Rate[2] = rx1_rate = 48000
    //   cntrl1 = (rx_adc_ctrl1 & 0xf3) | 0x08
    //   P1_DDCConfig = 3
    P2CodecSaturn codec;
    ReceiverManager mgr;
    mgr.setP2Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    // Seed everything except the last toggle that fires the change.
    mgr.setRx1Rate(48000);
    mgr.setRx2Rate(0);
    mgr.setRx2Enabled(false);
    mgr.setRxAdcCtrl1(0x00);
    mgr.setRxAdcCtrl2(0x00);
    mgr.setDiversityEnabled(false);
    mgr.setMox(true);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);
    mgr.setPureSignalEnabled(true);

    QCOMPARE(spy.count(), 1);
    auto config = spy.last().first().value<PsDdcConfig>();
    QCOMPARE(int(config.ddcEnable),  int(DDC0 + DDC2));
    QCOMPARE(int(config.syncEnable), int(DDC1));
    QCOMPARE(int(config.rate[0]),    kPsRate);
    QCOMPARE(int(config.rate[1]),    kPsRate);
    QCOMPARE(int(config.rate[2]),    48000);
    QCOMPARE(int(config.cntrl1),     0x08);
    QCOMPARE(config.p1DdcConfig,     3);
}

void TstReceiverManagerPsDdc::hl2PsOnMoxEmitsExpectedPsDdcConfig()
{
    // HL2 PS-on MOX expected wire bytes (mi0bot delta: rx1_rate not ps_rate)
    // Source: mi0bot console.cs:8469-8488 [v2.10.3.13-beta2]
    //   p1DdcConfig=6, cntrl1=4, rate[0]=rate[1]=rx1_rate
    //
    // Inline tag preserved per CLAUDE.md "Inline comment preservation":
    //MI0BOT  [console.cs:8476 `if (hpsdr_model == HPSDRModel.HERMESLITE)
    //          // MI0BOT: HL2 can work at a high sample rate` — HL2-only
    //          rx1_rate override that this test pins]
    P1CodecHl2 codec;
    ReceiverManager mgr;
    mgr.setP1Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::HERMESLITE);

    mgr.setRx1Rate(48000);
    mgr.setRx2Rate(0);
    mgr.setRx2Enabled(false);
    mgr.setRxAdcCtrl1(0x00);
    mgr.setRxAdcCtrl2(0x00);
    mgr.setDiversityEnabled(false);
    mgr.setMox(true);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);
    mgr.setPureSignalEnabled(true);

    QCOMPARE(spy.count(), 1);
    auto config = spy.last().first().value<PsDdcConfig>();
    QCOMPARE(config.p1DdcConfig,     6);
    QCOMPARE(int(config.ddcEnable),  int(DDC0));
    QCOMPARE(int(config.syncEnable), int(DDC1));
    // mi0bot HL2: rate[0] = rate[1] = rx1_rate (not ps_rate!)
    QCOMPARE(int(config.rate[0]),    48000);
    QCOMPARE(int(config.rate[1]),    48000);
    QCOMPARE(int(config.cntrl1),     4);
}

void TstReceiverManagerPsDdc::standardHermesPsOffMoxEmitsExpectedPsDdcConfig()
{
    // Standard HERMES PS-off MOX → p1DdcConfig=4
    // Source: console.cs:8413-8426 [v2.10.3.13]
    P1CodecStandard codec;
    ReceiverManager mgr;
    mgr.setP1Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::HERMES);

    mgr.setRx1Rate(48000);
    mgr.setMox(true);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);
    // PS already false (default) — toggling to false should still emit per
    // setter contract: idempotence is checked separately on actual no-op.
    // Use a different state change to fire the assignment.
    mgr.setRxAdcCtrl1(0x80);

    QCOMPARE(spy.count(), 1);
    auto config = spy.last().first().value<PsDdcConfig>();
    QCOMPARE(config.p1DdcConfig,    4);
    QCOMPARE(int(config.ddcEnable), int(DDC0));
    QCOMPARE(int(config.cntrl1),    0);
}

void TstReceiverManagerPsDdc::setSameStateDoesNotEmit()
{
    P2CodecSaturn codec;
    ReceiverManager mgr;
    mgr.setP2Codec(&codec);
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    // Establish baseline.
    mgr.setPureSignalEnabled(true);
    mgr.setMox(true);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);

    // Same-value setters must not re-fire the wire-byte computation; the
    // idempotence guard avoids redundant codec dispatch / signal emission
    // when the user holds an already-set toggle.
    mgr.setPureSignalEnabled(true);
    mgr.setMox(true);
    mgr.setDiversityEnabled(false);
    mgr.setRx1Rate(48000);

    QCOMPARE(spy.count(), 0);
}

void TstReceiverManagerPsDdc::noCodecInjectedDoesNotEmit()
{
    // No codec injected — graceful no-op rather than crash.  RadioModel sets
    // the codec at hardware-info time; if a state setter fires before that
    // (e.g. construction-time defaults), nothing should happen on the wire.
    ReceiverManager mgr;
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);
    mgr.setPureSignalEnabled(true);
    mgr.setMox(true);

    QCOMPARE(spy.count(), 0);
}

void TstReceiverManagerPsDdc::p2CodecTakesPrecedenceOverP1()
{
    // RadioModel will only set one codec based on the connected protocol,
    // but defensive ordering: if both are non-null, P2 wins.  This pins the
    // contract so a future bug that sets both doesn't silently route through
    // the wrong protocol layer.
    P2CodecSaturn p2codec;
    P1CodecHl2    p1codec;
    ReceiverManager mgr;
    mgr.setP1Codec(&p1codec);
    mgr.setP2Codec(&p2codec);
    mgr.setHpsdrModel(HPSDRModel::ANAN_G2);

    mgr.setRx1Rate(48000);
    mgr.setMox(true);

    QSignalSpy spy(&mgr, &ReceiverManager::ddcConfigChanged);
    mgr.setPureSignalEnabled(true);

    QCOMPARE(spy.count(), 1);
    auto config = spy.last().first().value<PsDdcConfig>();
    // P2 G2-class signature: p1DdcConfig=3, DDC0+DDC2.  HL2 signature would
    // be p1DdcConfig=6, DDC0 only.
    QCOMPARE(config.p1DdcConfig,    3);
    QCOMPARE(int(config.ddcEnable), int(DDC0 + DDC2));
}

QTEST_MAIN(TstReceiverManagerPsDdc)
#include "tst_receiver_manager_ps_ddc.moc"
