// no-port-check: test-only. Thetis file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No Thetis logic is ported here; this file is NereusSDR-original.
//
// =================================================================
// The Alex tab's high-pass switches reach the wire, as Thetis wires them.
// =================================================================
//
// Plan Task 14 fix wave (R-R3-49: every control does what its label says).
// The Setup > Hardware > Alex tab saved these and nothing read them.
//
// HPF Bypass on PureSignal feedback:
//   From Thetis setup.cs:29440-29458 [v2.10.3.15]
//     chkDisableHPFonPS_CheckedChanged -> console.DisableHPFonPS = ...
//   From Thetis console.cs:18764-18773 [v2.10.3.15]
//     DisableHPFonPS { set { disable_hpf_on_ps = value; ... setAlex1HPF(freq); } }
//   From Thetis console.cs:6957 [v2.10.3.15] (setBPF1ForOrionIISaturn)
//     if (_mox && (disable_hpf_on_tx || (disable_hpf_on_ps && PureSignalEnabled)))
// and setBPF1ForOrionIISaturn runs only for Orion MkII, Saturn and
// HermesC10 (setAlex1HPF, console.cs:6827-6836).
//
// HPF Bypass (the master switch, chkAlexHPFBypass "ByPass/55 MHz HPF"):
//   From Thetis setup.cs:15374-15379 [v2.10.3.15]
//     chkAlexHPFBypass_CheckedChanged -> console.AlexHPFBypass = ...
//   From Thetis console.cs:18793-18803 [v2.10.3.15]
//     AlexHPFBypass { set { alex_hpf_bypass = value; ... setAlex1HPF(freq); ... } }
//   From Thetis console.cs:6850-6855 and 6965-6970 [v2.10.3.15]
//     if (alex_hpf_bypass) { NetworkIO.SetAlexHPFBits(0x20); ... return; }
// keyed or not, on every Alex board.
//
// Each switch is driven from the Setup tab on the Core, and from a remote
// window's write arriving at the Core (scheduleRemoteHardwareApply).
// =================================================================

#include <QtTest/QtTest>

#include <QCheckBox>

#include "core/AppSettings.h"
#include "core/P1RadioConnection.h"
#include "core/P2RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "core/codec/AlexFilterMap.h"
#include "gui/setup/hardware/AntennaAlexAlex1Tab.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

const QString kMac = QStringLiteral("AA:BB:CC:DD:EE:15");
constexpr quint64 k40mHz = 7100000ULL;
constexpr quint8  kBypass = 0x20;

class ConnectedP1 final : public P1RadioConnection {
public:
    ConnectedP1() { setState(ConnectionState::Connected); }
};

class ConnectedP2 final : public P2RadioConnection {
public:
    ConnectedP2() { setState(ConnectionState::Connected); }
};

quint32 readBE32(const quint8* buf, int offset)
{
    return (quint32(buf[offset])     << 24)
         | (quint32(buf[offset + 1]) << 16)
         | (quint32(buf[offset + 2]) << 8)
         |  quint32(buf[offset + 3]);
}

quint8 hpfFromWord(quint32 reg)
{
    quint8 bits = 0;
    if (reg & (1u << 1))  { bits |= 0x01; }
    if (reg & (1u << 2))  { bits |= 0x02; }
    if (reg & (1u << 4))  { bits |= 0x04; }
    if (reg & (1u << 5))  { bits |= 0x08; }
    if (reg & (1u << 6))  { bits |= 0x10; }
    if (reg & (1u << 12)) { bits |= 0x20; }
    if (reg & (1u << 3))  { bits |= 0x40; }
    return bits;
}

// Alex0, the word SetAlexHPFBits writes.
quint8 p2Alex0Hpf(P2RadioConnection& conn)
{
    quint8 buf[1444] = {};
    conn.composeCmdHighPriorityForTest(buf);
    return hpfFromWord(readBE32(buf, 1432));
}

// Alex1, which none of these switches touches.
quint8 p2Alex1Hpf(P2RadioConnection& conn)
{
    quint8 buf[1444] = {};
    conn.composeCmdHighPriorityForTest(buf);
    return hpfFromWord(readBE32(buf, 1428));
}

quint8 p1Hpf(const P1RadioConnection& conn)
{
    return quint8(conn.captureBank10ForTest()[3]) & 0x7F;
}

QCheckBox* boxNamed(AntennaAlexAlex1Tab& tab, const QString& text)
{
    for (QCheckBox* box : tab.findChildren<QCheckBox*>()) {
        if (box->text() == text) {
            return box;
        }
    }
    return nullptr;
}

void prepareCore(RadioModel& model, HPSDRHW board, RadioConnection* conn)
{
    model.setBoardForTest(board);
    RadioInfo info;
    info.macAddress = kMac;
    info.boardType = board;
    model.setLastRadioInfoForTest(info);
    model.setConnectionStateForTest(ConnectionState::Connected);
    model.injectConnectionForTest(conn);
}

} // namespace

class TestAlexHpfSwitches : public QObject {
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clearHardwareValues(kMac); }
    void cleanup() { AppSettings::instance().clearHardwareValues(kMac); }

    // ── HPF Bypass on PureSignal feedback: the boards Thetis names ───────
    void psBypass_followsTheSetupTab_onBandPassBoards_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("Saturn (G2)")      << int(HPSDRHW::Saturn);
        QTest::newRow("HermesC10 (G2E)")  << int(HPSDRHW::HermesC10);
        QTest::newRow("Orion MkII")       << int(HPSDRHW::OrionMKII);
    }
    void psBypass_followsTheSetupTab_onBandPassBoards()
    {
        QFETCH(int, board);
        const HPSDRHW hw = HPSDRHW(board);
        ConnectedP2 conn;
        conn.setBoardForTest(hw);
        conn.setReceiverFrequency(2, k40mHz);
        const quint8 filtered = p2Alex0Hpf(conn);
        QVERIFY(filtered != kBypass);
        const quint8 alex1 = p2Alex1Hpf(conn);

        RadioModel model;
        prepareCore(model, hw, &conn);
        AntennaAlexAlex1Tab tab(&model);
        tab.restoreSettings(kMac);
        tab.setImdWarningResultForTest(AntennaAlexAlex1Tab::TestImdResult::ConfirmOk);
        QCheckBox* box = tab.hpfBypassOnPsCheckboxForTest();
        QVERIFY(box);
        QVERIFY(box->isChecked());  // Thetis's default

        conn.setPuresignalRun(true);
        conn.setMox(true);
        // On (the default): keyed with PureSignal, Alex0 is the bypass alone,
        // as SetAlexHPFBits(0x20) leaves it. Alex1 is not written.
        QCOMPARE(p2Alex0Hpf(conn), kBypass);
        QCOMPARE(p2Alex1Hpf(conn), alex1);

        // Off: the band's filter stays in while keyed with PureSignal.
        box->setChecked(false);
        QCOMPARE(p2Alex0Hpf(conn), filtered);

        // On again, applied at once.
        box->setChecked(true);
        QCOMPARE(p2Alex0Hpf(conn), kBypass);

        // Unkeyed, or keyed without PureSignal, the switch does nothing.
        conn.setMox(false);
        QCOMPARE(p2Alex0Hpf(conn), filtered);
        conn.setPuresignalRun(false);
        conn.setMox(true);
        QCOMPARE(p2Alex0Hpf(conn), filtered);
        conn.setMox(false);

        model.injectConnectionForTest(nullptr);
    }

    // ── Other boards: setAlexHPF has no PureSignal arm ───────────────────
    void psBypass_isNotAppliedOnHighPassBoards()
    {
        ConnectedP2 conn;
        conn.setBoardForTest(HPSDRHW::Orion);  // ANAN-200D on Protocol 2
        conn.setReceiverFrequency(2, k40mHz);
        const quint8 filtered = p2Alex0Hpf(conn);
        QVERIFY(filtered != kBypass);
        QVERIFY(conn.hpfBypassOnPs());

        conn.setPuresignalRun(true);
        conn.setMox(true);
        QCOMPARE(p2Alex0Hpf(conn), filtered);
        conn.setMox(false);
    }

    // ── Protocol 1 with a band-pass board: Thetis's rule is the same ─────
    void psBypass_p1BandPassBoard()
    {
        ConnectedP1 conn;
        conn.setBoardForTest(HPSDRHW::OrionMKII);
        conn.setReceiverFrequency(0, k40mHz);
        const quint8 filtered = p1Hpf(conn);
        QVERIFY(filtered != kBypass);

        conn.setPuresignalRun(true);
        conn.setMox(true);
        QCOMPARE(p1Hpf(conn), kBypass);
        conn.setHpfBypassOnPs(false);
        QCOMPARE(p1Hpf(conn), filtered);
        conn.setMox(false);
        conn.setPuresignalRun(false);
    }

    // ── A remote window's change reaches the Core ────────────────────────
    void psBypass_remoteWindowWrite_reachesTheCore()
    {
        ConnectedP2 conn;
        conn.setBoardForTest(HPSDRHW::Saturn);
        conn.setReceiverFrequency(2, k40mHz);
        const quint8 filtered = p2Alex0Hpf(conn);

        RadioModel core;
        prepareCore(core, HPSDRHW::Saturn, &conn);
        QStringList reloads;
        core.setHardwareApplyObserverForTest([&reloads](const QString& name) { reloads << name; });

        conn.setPuresignalRun(true);
        conn.setMox(true);
        QCOMPARE(p2Alex0Hpf(conn), kBypass);

        const QString key =
            QStringLiteral("hardware/%1/alex/master/hpfBypassOnPs").arg(kMac);
        AppSettings::instance().setValue(key, QStringLiteral("False"));
        core.scheduleRemoteHardwareApply(key);
        QTRY_COMPARE(reloads, QStringList{QStringLiteral("alex")});
        QCOMPARE(p2Alex0Hpf(conn), filtered);

        reloads.clear();
        AppSettings::instance().setValue(key, QStringLiteral("True"));
        core.scheduleRemoteHardwareApply(key);
        QTRY_COMPARE(reloads, QStringList{QStringLiteral("alex")});
        QCOMPARE(p2Alex0Hpf(conn), kBypass);
        conn.setMox(false);

        core.injectConnectionForTest(nullptr);
    }
    // ── HPF Bypass (master): keyed or not, both protocols ────────────────
    void masterBypass_followsTheSetupTab_data()
    {
        QTest::addColumn<int>("protocol");
        QTest::addColumn<int>("board");
        QTest::newRow("P1 Angelia (high-pass ladder)") << 1 << int(HPSDRHW::Angelia);
        QTest::newRow("P1 Orion MkII (band-pass)")     << 1 << int(HPSDRHW::OrionMKII);
        QTest::newRow("P2 Saturn (G2)")                << 2 << int(HPSDRHW::Saturn);
        QTest::newRow("P2 Orion (ANAN-200D)")          << 2 << int(HPSDRHW::Orion);
    }
    void masterBypass_followsTheSetupTab()
    {
        QFETCH(int, protocol);
        QFETCH(int, board);
        const HPSDRHW hw = HPSDRHW(board);
        ConnectedP1 p1;
        ConnectedP2 p2;
        RadioConnection* conn = nullptr;
        if (protocol == 1) {
            p1.setBoardForTest(hw);
            p1.setReceiverFrequency(0, k40mHz);
            conn = &p1;
        } else {
            p2.setBoardForTest(hw);
            p2.setReceiverFrequency(2, k40mHz);
            conn = &p2;
        }
        auto hpf = [&]() { return protocol == 1 ? p1Hpf(p1) : p2Alex0Hpf(p2); };
        const quint8 filtered = hpf();
        QVERIFY(filtered != kBypass);
        const quint8 alex1 = protocol == 2 ? p2Alex1Hpf(p2) : 0;

        RadioModel model;
        prepareCore(model, hw, conn);
        AntennaAlexAlex1Tab tab(&model);
        tab.restoreSettings(kMac);
        QCheckBox* box = boxNamed(tab, QStringLiteral("HPF Bypass (master)"));
        QVERIFY(box);
        QVERIFY(!box->isChecked());  // Thetis's default

        box->setChecked(true);
        QCOMPARE(hpf(), kBypass);
        if (protocol == 2) {
            QCOMPARE(p2Alex1Hpf(p2), alex1);
        }
        conn->setMox(true);
        QCOMPARE(hpf(), kBypass);
        conn->setMox(false);

        box->setChecked(false);
        QCOMPARE(hpf(), filtered);

        model.injectConnectionForTest(nullptr);
    }

    // ── HPF Bypass (master) from a remote window ─────────────────────────
    void masterBypass_remoteWindowWrite_reachesTheCore()
    {
        ConnectedP1 conn;
        conn.setBoardForTest(HPSDRHW::Angelia);
        conn.setReceiverFrequency(0, k40mHz);
        const quint8 filtered = p1Hpf(conn);

        RadioModel core;
        prepareCore(core, HPSDRHW::Angelia, &conn);
        QStringList reloads;
        core.setHardwareApplyObserverForTest([&reloads](const QString& name) { reloads << name; });

        const QString key =
            QStringLiteral("hardware/%1/alex/master/hpfBypass").arg(kMac);
        AppSettings::instance().setValue(key, QStringLiteral("True"));
        core.scheduleRemoteHardwareApply(key);
        QTRY_COMPARE(reloads, QStringList{QStringLiteral("alex")});
        QCOMPARE(p1Hpf(conn), kBypass);

        reloads.clear();
        AppSettings::instance().setValue(key, QStringLiteral("False"));
        core.scheduleRemoteHardwareApply(key);
        QTRY_COMPARE(reloads, QStringList{QStringLiteral("alex")});
        QCOMPARE(p1Hpf(conn), filtered);

        core.injectConnectionForTest(nullptr);
    }

    // ── The HL2 has no Alex board: the master switch leaves it alone ─────
    void masterBypass_hl2IsUntouched()
    {
        ConnectedP1 conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);
        conn.setReceiverFrequency(0, k40mHz);
        const quint8 before = p1Hpf(conn);
        codec::alex::Alex1HpfSwitches sw;
        sw.hpfBypass = true;
        QCOMPARE(codec::alex::applyAlex1HpfSwitches(before, HPSDRHW::HermesLite,
                                                    false, false, sw),
                 kBypass);  // the rule itself would bypass...
        conn.setAlexHpfBypass(true);
        QCOMPARE(p1Hpf(conn), before);  // ...but the HL2 has no Alex board
    }
};

QTEST_MAIN(TestAlexHpfSwitches)
#include "tst_alex_hpf_switches.moc"
