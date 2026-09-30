// no-port-check: test-only. Thetis and mi0bot file names appear only in
// source-cite comments that document which upstream line each assertion
// verifies. No upstream logic is ported here; this file is NereusSDR-original.
//
// =================================================================
// Shared-input filters: both filters on one receiver input follow the same
// counted slices, and the operator is told why the low-pass is held.
// =================================================================
//
// Ruling (c), 2026-09-30:
//   * The receive low-pass follows the highest-frequency slice counted on
//     the input, Thetis's "higher of the two" generalised to every slice:
//       From Thetis console.cs:15491-15495 UpdateAlexTXFilter [v2.10.3.15]
//         if (!_rx2_preamp_present && chkRX2.Checked)
//         {
//             if (rx1_dds_freq_mhz > rx2_dds_freq_mhz) setAlexLPF(rx1_dds_freq_mhz, false);
//             else setAlexLPF(rx2_dds_freq_mhz, false);
//         }
//     and on the HL2 the N2ADR board's receive pins follow the high band:
//       From mi0bot-Thetis HPSDR/Penny.cs:185-188 [@c26a8a4]
//         if (Console.getConsole().RX2Enabled && (idxb > idx))     // MI0BOT: Select the filter for the high band
//             bits = RXABitMasks[idxb];
//         else
//             bits = RXABitMasks[idx];
//   * The band-pass bypass rule is unchanged.
//   * Both use the same counted set, with one away rule (Amendment 8a).
// Ruling (d): the chain's state carries the low-pass reason and the slice
// that forces it (AlexAdcState::lowPassReason, lowPassSlice).
//
// Modification history (NereusSDR):
//   2026-09-30  J.J. Boyd / KG4VCF  Created (shared-input filters, rulings
//                                    (c) and (d)). AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-30  J.J. Boyd / KG4VCF  Follow-up: CTUN at a low-pass edge
//                                    (the reason names the filter's
//                                    slice); an HL2 pin edit refreshes
//                                    the held reason. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-30  J.J. Boyd / KG4VCF  Review fix: the HL2 Auto bypass
//                                    clears the reason; P2 away rule;
//                                    6m/ByPass on RX empties the reason.
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <memory>

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/OcMatrix.h"
#include "core/P1RadioConnection.h"
#include "core/P2RadioConnection.h"
#include "core/ReceiverManager.h"
#include "core/accessories/AlexController.h"
#include "core/codec/AlexFilterMap.h"
#include "core/codec/P2CodecSaturn.h"
#include "core/SliceOwnership.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "OperatorWording.h"

using namespace NereusSDR;

namespace {

constexpr double k6mHz   = 50125000.0;
constexpr double k10mHz  = 28400000.0;
constexpr double k12mHz  = 24940000.0;
constexpr double k15mHz  = 21200000.0;
constexpr double k17mHz  = 18100000.0;
constexpr double k20mHz  = 14200000.0;
constexpr double k30mHz  = 10120000.0;
constexpr double k40mHz  =  7100000.0;
constexpr double k60mHz  =  5357000.0;
constexpr double k80mHz  =  3700000.0;
constexpr double k160mHz =  1850000.0;

constexpr int kAlex0Offset = 1432;

class ConnectedP1 final : public P1RadioConnection {
public:
    ConnectedP1() { setState(ConnectionState::Connected); }
};

class ConnectedP2 final : public P2RadioConnection {
public:
    ConnectedP2() { setState(ConnectionState::Connected); }
};

struct DetachConnection {
    RadioModel* model{nullptr};
    ~DetachConnection() { if (model) { model->injectConnectionForTest(nullptr); } }
};

// ── Protocol 1 wire reads ──────────────────────────────────────────────────
quint8 p1HpfBits(const P1RadioConnection& conn)
{
    return quint8(conn.captureBank10ForTest()[3]) & 0x7F;   // bit 7 is the T/R relay
}

quint8 p1LpfBits(const P1RadioConnection& conn)
{
    return quint8(conn.captureBank10ForTest()[4]);
}

quint8 p1OcByte(const P1RadioConnection& conn)
{
    quint8 bank0[5] = {};
    conn.composeCcForBankForTest(0, bank0);
    return quint8(bank0[2] >> 1);
}

// ── Protocol 2 wire reads ──────────────────────────────────────────────────
quint32 readBE32(const quint8* buf, int offset)
{
    return (quint32(buf[offset])     << 24)
         | (quint32(buf[offset + 1]) << 16)
         | (quint32(buf[offset + 2]) << 8)
         |  quint32(buf[offset + 3]);
}

// Inverse of the LPF scatter in P2CodecOrionMkII::buildAlex0.
// Bit map from Thetis ChannelMaster/netInterface.c:691-702 [v2.10.3.15].
quint8 lpfMaskFromReg(quint32 reg)
{
    quint8 bits = 0;
    if (reg & (1u << 20)) { bits |= 0x01; }
    if (reg & (1u << 21)) { bits |= 0x02; }
    if (reg & (1u << 22)) { bits |= 0x04; }
    if (reg & (1u << 23)) { bits |= 0x08; }
    if (reg & (1u << 29)) { bits |= 0x10; }
    if (reg & (1u << 30)) { bits |= 0x20; }
    if (reg & (1u << 31)) { bits |= 0x40; }
    return bits;
}

quint8 p2Alex0Lpf(P2RadioConnection& conn)
{
    quint8 buf[1444] = {};
    conn.composeCmdHighPriorityForTest(buf);
    return lpfMaskFromReg(readBE32(buf, kAlex0Offset));
}

quint32 p2Alex0Reg(P2RadioConnection& conn)
{
    quint8 buf[1444] = {};
    conn.composeCmdHighPriorityForTest(buf);
    return readBE32(buf, kAlex0Offset);
}

// A Protocol 1 radio with an injected, Connected connection and the
// production ReceiverManager -> connection wiring.
struct P1Session {
    explicit P1Session(HPSDRHW board)
    {
        AppSettings::instance().clear();
        oc.setPin(Band::Band20m, 0, /*tx=*/false, true);
        oc.setPin(Band::Band40m, 1, /*tx=*/false, true);
        oc.setPin(Band::Band80m, 2, /*tx=*/false, true);

        model.setBoardForTest(board);
        conn.setBoardForTest(board);
        conn.setOcMatrix(&oc);
        model.injectConnectionForTest(&conn);
        detach.model = &model;
        model.wireReceiverManagerHardwarePushesForTest();

        const int streams = (board == HPSDRHW::HermesLite) ? 2 : 4;
        model.configureStreamPool(streams, streams, 192000);
        for (int i = 0; i < streams; ++i) {
            model.receiverManager()->createReceiver();
        }
    }
    ~P1Session() { AppSettings::instance().clear(); }

    int add(double hz)
    {
        const int id = model.addSlice();
        model.sliceById(id)->setFrequency(hz);
        return id;
    }

    OcMatrix         oc;
    RadioModel       model;
    ConnectedP1      conn;
    DetachConnection detach;
};

// An ANAN-G2 (Saturn board, P2CodecSaturn) with an injected, Connected
// connection and the production ReceiverManager -> connection wiring.
struct G2Session {
    G2Session()
    {
        AppSettings::instance().clear();
        model.setBoardForTest(HPSDRHW::Saturn);
        conn.setBoardForTest(HPSDRHW::Saturn);
        model.configureStreamPool(5, 5, 192000);
        model.receiverManager()->setP2Codec(&codec);
        model.injectConnectionForTest(&conn);
        detach.model = &model;
        model.wireReceiverManagerHardwarePushesForTest();
        for (int st = 0; st < 5; ++st) {
            model.receiverManager()->createReceiver();
        }
    }
    ~G2Session() { AppSettings::instance().clear(); }

    int add(double hz)
    {
        const int id = model.addSlice();
        model.sliceById(id)->setFrequency(hz);
        return id;
    }

    P2CodecSaturn    codec;
    RadioModel       model;
    ConnectedP2      conn;
    DetachConnection detach;
};

QString letterOn(const RadioModel& model, int id, const char* band)
{
    return QStringLiteral("%1 on %2").arg(model.sliceById(id)->sliceLetter(),
                                          QLatin1String(band));
}

} // namespace

class TestSharedInputFilters : public QObject {
    Q_OBJECT

private slots:

    // ── 80 m and 20 m on one input: the low-pass is the higher slice's ───
    //
    // Slice A (RX1) on 80 m, slice B on 20 m. Thetis's RX1 rule would leave
    // A's 80 m low-pass in front of B on the boards with an RX2 front end;
    // the counted rule passes B. The band-pass bypasses as it did, and the
    // chain names B as the slice holding the low-pass.
    void p1_twoBands_lowPassFollowsTheHigherSlice_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("Hermes / ANAN-10 / ANAN-100 (one ADC)") << int(HPSDRHW::Hermes);
        QTest::newRow("ANAN-100D (two ADCs, one bank)")        << int(HPSDRHW::Angelia);
        QTest::newRow("ANAN-G2E (HermesC10)")                  << int(HPSDRHW::HermesC10);
    }
    void p1_twoBands_lowPassFollowsTheHigherSlice()
    {
        QFETCH(int, board);
        const HPSDRHW hw = HPSDRHW(board);
        P1Session s(hw);

        const int a = s.add(k80mHz);
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k80mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);

        const int b = s.add(k20mHz);
        QCOMPARE(s.model.sliceChainIndex(a), 0);
        QCOMPARE(s.model.sliceChainIndex(b), 0);

        // The low-pass: the higher slice's.
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k20mHz / 1e6));
        // The band-pass: bypassed, as today.
        QCOMPARE(s.model.filterChainState(0).effective, AlexController::BpfEffective::Bypass);
        QCOMPARE(p1HpfBits(s.conn), quint8(0x20));
        // The reason names B, and A below it.
        const AlexController::AlexAdcState& st = s.model.filterChainState(0);
        QCOMPARE(st.lowPassSlice, b);
        QVERIFY2(st.lowPassReason.contains(letterOn(s.model, b, "20m")),
                 qPrintable(st.lowPassReason));
        QVERIFY2(st.lowPassReason.contains(letterOn(s.model, a, "80m")),
                 qPrintable(st.lowPassReason));
        QVERIFY2(OperatorWording::isPlain(st.lowPassReason), qPrintable(st.lowPassReason));
        QVERIFY(s.model.rxFilter0LowPassReason() == st.lowPassReason);
        QCOMPARE(s.model.rxFilter0LowPassSlice(), b);

        // B closed: A alone again, and nothing is held.
        s.model.removeSlice(b);
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k80mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());
    }

    void g2_twoBands_lowPassFollowsTheHigherSlice()
    {
        G2Session s;
        const int a = s.add(k80mHz);
        const int b = s.add(k20mHz);
        QCOMPARE(s.model.sliceChainIndex(a), 0);
        QCOMPARE(s.model.sliceChainIndex(b), 0);

        // Thetis gives the G2 RX1's low-pass (80 m); the counted rule passes
        // the higher slice on the shared input.
        QCOMPARE(p2Alex0Lpf(s.conn), codec::alex::computeLpf(k20mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).effective, AlexController::BpfEffective::Bypass);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);
        QVERIFY2(s.model.filterChainState(0).lowPassReason.contains(letterOn(s.model, b, "20m")),
                 qPrintable(s.model.filterChainState(0).lowPassReason));

        // B retuned below A: A's low-pass, and nothing below A is held.
        s.model.sliceById(b)->setFrequency(k160mHz);
        QCOMPARE(p2Alex0Lpf(s.conn), codec::alex::computeLpf(k80mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, a);
    }

    // ── One slice: the selection is what it was, on every band ──────────
    void oneSlice_everyBandUnchanged_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("Hermes") << int(HPSDRHW::Hermes);
        QTest::newRow("ANAN-100D") << int(HPSDRHW::Angelia);
        QTest::newRow("HL2") << int(HPSDRHW::HermesLite);
    }
    void oneSlice_everyBandUnchanged()
    {
        QFETCH(int, board);
        const HPSDRHW hw = HPSDRHW(board);
        P1Session s(hw);
        const int a = s.add(k160mHz);
        for (double hz : {k160mHz, k80mHz, k60mHz, k40mHz, k30mHz, k20mHz, k17mHz,
                          k15mHz, k12mHz, k10mHz, k6mHz}) {
            s.model.sliceById(a)->setFrequency(hz);
            QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(hz / 1e6));
            QCOMPARE(p1OcByte(s.conn), s.oc.maskFor(bandFromFrequency(hz), /*tx=*/false));
            QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
            QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());
        }
    }

    void g2_oneSlice_everyBandUnchanged()
    {
        G2Session s;
        const int a = s.add(k160mHz);
        for (double hz : {k160mHz, k80mHz, k60mHz, k40mHz, k30mHz, k20mHz, k17mHz,
                          k15mHz, k12mHz, k10mHz, k6mHz}) {
            s.model.sliceById(a)->setFrequency(hz);
            QCOMPARE(p2Alex0Lpf(s.conn), codec::alex::computeLpf(hz / 1e6));
            QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        }
    }

    // ── A slice the away rule leaves out changes neither filter ─────────
    //
    // Slice A on 80 m (device B's), slice B on 20 m (device A's). With
    // device A away, B no longer counts: the band-pass filters for A alone,
    // as it already did (Amendment 8a), and now so does the low-pass.
    void anAwaySlice_changesNeitherFilter()
    {
        P1Session s(HPSDRHW::Hermes);
        const int a = s.add(k80mHz);
        const int b = s.add(k20mHz);
        SliceOwnership* ownership = s.model.sliceOwnership();
        QVERIFY(ownership != nullptr);
        const QByteArray devA = QByteArrayLiteral("device-a");
        ownership->setOwner(a, QByteArrayLiteral("device-b"));
        ownership->setOwner(b, devA);
        s.model.requestDdcAssignment();
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k20mHz / 1e6));
        QCOMPARE(p1HpfBits(s.conn), quint8(0x20));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);

        ownership->setAwayDevices({devA});
        QVERIFY(ownership->isAwaySlice(b));
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k80mHz / 1e6));
        QCOMPARE(p1HpfBits(s.conn),
                 codec::alex::computeRxPreselector(k80mHz / 1e6, HPSDRHW::Hermes));
        QCOMPARE(s.model.filterChainState(0).effective, AlexController::BpfEffective::Filtered);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());

        // Retuning the away slice moves neither filter.
        s.model.sliceById(b)->setFrequency(k10mHz);
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k80mHz / 1e6));
        QCOMPARE(p1HpfBits(s.conn),
                 codec::alex::computeRxPreselector(k80mHz / 1e6, HPSDRHW::Hermes));

        // Back: B counts again at once.
        ownership->setAwayDevices({});
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k10mHz / 1e6));
        QCOMPARE(p1HpfBits(s.conn), quint8(0x20));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);
    }

    // The same on Protocol 2 (ANAN-G2): the away slice leaves the Alex0
    // register, band-pass and low-pass together, where slice A puts it.
    void g2_anAwaySlice_changesNeitherFilter()
    {
        G2Session s;
        const int a = s.add(k80mHz);
        const int b = s.add(k20mHz);
        SliceOwnership* ownership = s.model.sliceOwnership();
        QVERIFY(ownership != nullptr);
        const QByteArray devA = QByteArrayLiteral("device-a");
        ownership->setOwner(a, QByteArrayLiteral("device-b"));
        ownership->setOwner(b, devA);
        s.model.requestDdcAssignment();
        QCOMPARE(p2Alex0Lpf(s.conn), codec::alex::computeLpf(k20mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).effective, AlexController::BpfEffective::Bypass);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);

        ownership->setAwayDevices({devA});
        QVERIFY(ownership->isAwaySlice(b));
        QCOMPARE(p2Alex0Lpf(s.conn), codec::alex::computeLpf(k80mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).effective, AlexController::BpfEffective::Filtered);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());
        const quint32 aloneReg = p2Alex0Reg(s.conn);

        // Retuning the away slice moves neither filter.
        s.model.sliceById(b)->setFrequency(k10mHz);
        QCOMPARE(p2Alex0Reg(s.conn), aloneReg);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);

        // Back: B counts again at once.
        ownership->setAwayDevices({});
        QCOMPARE(p2Alex0Lpf(s.conn), codec::alex::computeLpf(k10mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).effective, AlexController::BpfEffective::Bypass);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);
    }

    // ── 6m/ByPass on RX: one low-pass for every slice, nothing held ──────
    //
    // With the switch on, the receive low-pass is the 6 m filter whatever
    // the slices are tuned to (codec::alex::setAlexLpf), so no slice is
    // held behind another's and the reason is empty.
    void lpfBypassOnRx_leavesTheReasonEmpty()
    {
        const QString mac = QStringLiteral("AA:BB:CC:DD:EE:5F");
        P1Session s(HPSDRHW::Hermes);
        RadioInfo info;
        info.macAddress = mac;
        info.boardType = HPSDRHW::Hermes;
        s.model.setLastRadioInfoForTest(info);
        s.model.setConnectionStateForTest(ConnectionState::Connected);

        const int a = s.add(k80mHz);
        const int b = s.add(k20mHz);
        QCOMPARE(s.model.sliceChainIndex(a), 0);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);
        QVERIFY(!s.model.filterChainState(0).lowPassReason.isEmpty());

        AppSettings::instance().setHardwareValue(mac, QStringLiteral("alex/master/lpfBypass"),
                                                 QStringLiteral("True"));
        s.model.applyAlexHpfSwitchSettings();
        QTRY_COMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k6mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());
        QVERIFY(s.model.rxFilter0LowPassReason().isEmpty());

        // Off again: B holds A once more.
        AppSettings::instance().setHardwareValue(mac, QStringLiteral("alex/master/lpfBypass"),
                                                 QStringLiteral("False"));
        s.model.applyAlexHpfSwitchSettings();
        QTRY_COMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k20mHz / 1e6));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);
    }

    // ── The HL2 follows mi0bot's high-band rule ─────────────────────────
    //
    // The N2ADR board's receive pins are the HL2's filter. With the policy
    // forcing a filter (so the multi-band bypass does not clear the pins)
    // the pins are the HIGH band's, whichever slice is RX1.
    void hl2_receivePinsFollowTheHighBand()
    {
        P1Session s(HPSDRHW::HermesLite);
        s.oc.setPin(Band::Band10m, 3, /*tx=*/false, true);
        s.model.alexControllerMutable().setBpfMode(0, AlexController::BpfMode::ForceBand);

        const int a = s.add(k80mHz);
        const int b = s.add(k20mHz);
        QCOMPARE(s.conn.rx1SlotForTest(), 0);
        QCOMPARE(p1OcByte(s.conn), s.oc.maskFor(Band::Band20m, /*tx=*/false));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);

        // RX1 above B: RX1 keeps it (mi0bot's idxb > idx is strict).
        s.model.sliceById(a)->setFrequency(k10mHz);
        QCOMPARE(p1OcByte(s.conn), s.oc.maskFor(Band::Band10m, /*tx=*/false));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, a);

        // Same band on both: RX1's, and nothing is held.
        s.model.sliceById(b)->setFrequency(k10mHz + 10000.0);
        QCOMPARE(p1OcByte(s.conn), s.oc.maskFor(Band::Band10m, /*tx=*/false));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
    }

    // In Auto the multi-band bypass still clears the pins, as it did. With
    // the pins cleared no low-pass is on the wire, so nothing is held and
    // the reason is empty (the reason describes only what is on the wire).
    void hl2_autoBypassStillClearsThePins()
    {
        P1Session s(HPSDRHW::HermesLite);
        s.add(k80mHz);
        s.add(k20mHz);
        QCOMPARE(p1OcByte(s.conn), quint8(0x00));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());
    }

    // The band order mi0bot compares (enums.cs [@c26a8a4]).
    void extCtrlBandIndex_isMi0botsOrder()
    {
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::Band160m), 0);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::Band6m), 10);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::Band2m), 11);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::WWV), 12);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::Band120m), 28);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::Band11m), 40);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::GEN), -1);
        QCOMPARE(OcMatrix::extCtrlBandIndex(Band::XVTR), -1);
    }

    // ── CTUN at a low-pass edge: the reason names the filter's slice ─────
    //
    // Thetis picks the Alex low-pass from the DDS frequency, which under
    // click-tune is the DDC centre (console.cs:31894-31910 [v2.10.3.15]).
    // A sits at 16.55 MHz on a centre of 16.46 MHz (the 30/20 m row, which
    // ends at 16.5 MHz); B sits at 16.52 MHz on a centre of 16.53 MHz (the
    // 17/15 m row). By VFO A is higher; by centre B is. The filter follows
    // B's centre, and the reason must name B, with A and C (40 m) held.
    void ctun_reasonNamesTheSliceTheFilterFollows_data()
    {
        QTest::addColumn<bool>("protocol2");
        QTest::newRow("Protocol 1 (Hermes)") << false;
        QTest::newRow("Protocol 2 (G2)") << true;
    }
    void ctun_reasonNamesTheSliceTheFilterFollows()
    {
        QFETCH(bool, protocol2);
        constexpr double kVfoA = 16550000.0;
        constexpr double kCentreA = 16460000.0;
        constexpr double kFirstB = 16620000.0;   // outside A's window, so B has its own DDC
        constexpr double kVfoB = 16520000.0;
        constexpr double kCentreB = 16530000.0;

        std::unique_ptr<P1Session> p1;
        std::unique_ptr<G2Session> p2;
        RadioModel* model = nullptr;
        if (protocol2) {
            p2 = std::make_unique<G2Session>();
            model = &p2->model;
        } else {
            p1 = std::make_unique<P1Session>(HPSDRHW::Hermes);
            model = &p1->model;
        }
        const auto add = [&](double hz) { return protocol2 ? p2->add(hz) : p1->add(hz); };
        const auto lowPass = [&]() { return protocol2 ? p2Alex0Lpf(p2->conn) : p1LpfBits(p1->conn); };

        const int c = add(k40mHz);
        const int a = add(kVfoA);
        QVERIFY(model->requestStreamCtunPinned(a, true));
        QVERIFY(model->requestStreamCentre(a, kCentreA));
        const int b = add(kFirstB);
        QVERIFY(model->requestStreamCtunPinned(b, true));
        QVERIFY(model->requestStreamCentre(b, kCentreB));
        model->sliceById(b)->setFrequency(kVfoB);

        // The two orders disagree.
        const int streamA = model->sliceById(a)->streamIndex();
        const int streamB = model->sliceById(b)->streamIndex();
        QVERIFY(streamA >= 0 && streamB >= 0 && streamA != streamB);
        QCOMPARE(model->streamCentreHzForTest(streamA), kCentreA);
        QCOMPARE(model->streamCentreHzForTest(streamB), kCentreB);
        QVERIFY(model->sliceById(a)->frequency() > model->sliceById(b)->frequency());
        QCOMPARE(model->sliceChainIndex(a), 0);
        QCOMPARE(model->sliceChainIndex(b), 0);
        QCOMPARE(model->sliceChainIndex(c), 0);

        // The filter follows B's centre, and the reason names B.
        QCOMPARE(lowPass(), codec::alex::computeLpf(kCentreB / 1e6));
        QVERIFY(codec::alex::computeLpf(kCentreB / 1e6) != codec::alex::computeLpf(kCentreA / 1e6));
        const AlexController::AlexAdcState& st = model->filterChainState(0);
        QCOMPARE(st.lowPassSlice, b);
        const QString letterB = model->sliceById(b)->sliceLetter();
        const QString letterA = model->sliceById(a)->sliceLetter();
        QVERIFY2(st.lowPassReason.startsWith(
                     QStringLiteral("The receive low-pass filter is set for slice %1 ").arg(letterB)),
                 qPrintable(st.lowPassReason));
        QVERIFY2(st.lowPassReason.contains(QStringLiteral("%1 on ").arg(letterA)),
                 qPrintable(st.lowPassReason));
        QVERIFY2(st.lowPassReason.contains(letterOn(*model, c, "40m")), qPrintable(st.lowPassReason));
        QVERIFY2(OperatorWording::isPlain(st.lowPassReason), qPrintable(st.lowPassReason));
    }

    // ── HL2: a pin edit refreshes the held reason at once ────────────────
    //
    // The N2ADR pins are the HL2's receive low-pass. With 20 m and 17 m on
    // the same pin the two slices share one filter and nothing is held;
    // giving 17 m its own pin in Setup holds the 20 m slice behind the
    // 17 m filter straight away, with no slice or band change. The policy
    // forces a filter so the pins stay on the wire (in Auto, two pin sets
    // bypass the band-pass and clear the pins, and nothing is held).
    void hl2_pinEditRefreshesTheHeldReason()
    {
        P1Session s(HPSDRHW::HermesLite);
        OcMatrix& oc = s.model.ocMatrixMutable();
        s.conn.setOcMatrix(&oc);
        oc.setPin(Band::Band20m, 0, /*tx=*/false, true);
        oc.setPin(Band::Band17m, 0, /*tx=*/false, true);
        s.model.alexControllerMutable().setBpfMode(0, AlexController::BpfMode::ForceBand);

        const int a = s.add(k20mHz);
        const int b = s.add(k17mHz);
        QCOMPARE(s.model.sliceChainIndex(a), 0);
        QCOMPARE(s.model.sliceChainIndex(b), 0);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());

        QSignalSpy changed(&s.model, &RadioModel::filterStateChanged);
        oc.setPin(Band::Band17m, 1, /*tx=*/false, true);
        QCOMPARE(p1OcByte(s.conn), oc.maskFor(Band::Band17m, /*tx=*/false));
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, b);
        QVERIFY2(s.model.filterChainState(0).lowPassReason.contains(letterOn(s.model, a, "20m")),
                 qPrintable(s.model.filterChainState(0).lowPassReason));
        QVERIFY(changed.count() > 0);

        // 17 m back on pin 0 alone: one filter again, nothing held.
        oc.setPin(Band::Band17m, 1, /*tx=*/false, false);
        QCOMPARE(s.model.filterChainState(0).lowPassSlice, -1);
        QVERIFY(s.model.filterChainState(0).lowPassReason.isEmpty());
    }

    // ── Keyed: the transmit low-pass, untouched ──────────────────────────
    void keyed_theCountedSetMovesNothing()
    {
        P1Session s(HPSDRHW::Hermes);
        s.add(k80mHz);
        s.conn.setTxFrequency(quint64(k40mHz));
        s.conn.setMox(true);
        const quint8 keyed = p1LpfBits(s.conn);
        QCOMPARE(keyed, codec::alex::computeLpf(k40mHz / 1e6));
        s.add(k20mHz);
        QCOMPARE(p1LpfBits(s.conn), keyed);
        s.conn.setMox(false);
        QCOMPARE(p1LpfBits(s.conn), codec::alex::computeLpf(k20mHz / 1e6));
    }
};

QTEST_MAIN(TestSharedInputFilters)
#include "tst_shared_input_filters.moc"
