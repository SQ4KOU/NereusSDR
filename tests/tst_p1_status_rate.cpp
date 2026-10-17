// no-port-check: NereusSDR-original test; it cites the Anvelina gateware
// and Thetis only as the behavior it asserts against.
// =================================================================
// tests/tst_p1_status_rate.cpp  (NereusSDR)
// =================================================================
//
// P1 radio status at a Protocol 2 radio's rate (2026-10-14).
//
// An HL2 sends about 5,333 EP6 packets a second. parseEp6Frame emitted
// micPttFromRadio, paTelemetryUpdated, supplyVoltsChanged and adcOverflow
// on every one of them, and onReadyRead emitted frameReceived for each, so
// the main thread got thousands of queued calls a second (1.6 s of lag on
// the Pi 4 Core while keyed). A P2 radio sends its high-priority status
// every 1 ms while transmitting or with an overload latched, every 200 ms
// otherwise, and at once on a change of PTT:
//   Anvelina CC_encoder.v:173-175 [@8e86a61]
//     // if Txing or overload then use 1mS update rate, otherwise 200mS
//   CC_encoder.v:185-187, 211, 217-219: overloads latched until sent;
//   CC_encoder.v:195-203: a change of memory[0] (PTT) sent at once.
//
// The cases feed hand-made EP6 packets straight into parseEp6Frame with
// the status clock set by the test (setStatusClockForTest), so simulated
// time runs at the HL2's 187.5 us packet spacing and nothing sleeps:
//   1. no PTT edge is lost or delayed;
//   2. PTT repeats and telemetry at the idle and keyed rates;
//   3. the values emitted at a tick are the latest packet's;
//   4. no ADC overload is lost, and one arrives within 1 ms;
//   5. HL2 packets of I2C responses alone emit no PTT;
//   6. frameReceived once per onReadyRead call (P1FakeRadio loopback);
//   7. a PTT held through a cleared TX inhibit or PA trip keys at the
//      next status tick, and does not without the repeat.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-14 - Original implementation for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted implementation via Anthropic
//                Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QByteArray>
#include <QList>
#include <QPair>

#include <cmath>
#include <functional>

#include "core/P1RadioConnection.h"
#include "core/RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "core/MoxController.h"
#include "core/PttMode.h"
#include "fakes/P1FakeRadio.h"

using namespace NereusSDR;
using NereusSDR::Test::P1FakeRadio;

namespace {

// HL2 EP6 packet spacing: about 5,333 packets a second, 187.5 us apart.
constexpr qint64 kPacketNs = 187'500;
constexpr qint64 kMs = 1'000'000;
// Simulated time starts here, so it is never the seam's -1 (real clock).
constexpr qint64 kT0 = 1'000'000'000;

// A 1032-byte EP6 datagram with both subframes' sync and the given C&C
// (C0..C4) per subframe. Layout per Thetis networkproto1.c:320-357.
QByteArray makeEp6Frame(const quint8 sub0[5], const quint8 sub1[5])
{
    QByteArray pkt(1032, '\0');
    auto* p = reinterpret_cast<quint8*>(pkt.data());
    p[0] = 0xEF; p[1] = 0xFE; p[2] = 0x01; p[3] = 0x06;
    p[8] = 0x7F; p[9] = 0x7F; p[10] = 0x7F;
    for (int i = 0; i < 5; ++i) { p[11 + i] = sub0[i]; }
    p[520] = 0x7F; p[521] = 0x7F; p[522] = 0x7F;
    for (int i = 0; i < 5; ++i) { p[523 + i] = sub1[i]; }
    return pkt;
}

// An ordinary status packet: subframe 0 case 0x00 (PTT in C0 bit 0),
// subframe 1 case 0x00 too.
QByteArray pttFrame(bool ptt)
{
    const quint8 c0 = ptt ? 0x01 : 0x00;
    const quint8 s[5] = {c0, 0, 0, 0, 0};
    return makeEp6Frame(s, s);
}

// Telemetry that changes every packet: subframe 0 case 0x08 (fwd = i),
// subframe 1 case 0x18 (supply = i mod 4096). C0 bit 0 carries PTT.
QByteArray telemetryFrame(int i, bool ptt)
{
    const quint8 pttBit = ptt ? 0x01 : 0x00;
    const quint16 fwd = static_cast<quint16>(i & 0xFFFF);
    const quint16 supply = static_cast<quint16>(i % 4096);
    const quint8 s0[5] = {static_cast<quint8>(0x08 | pttBit), 0, 0,
                          static_cast<quint8>(fwd >> 8), static_cast<quint8>(fwd)};
    const quint8 s1[5] = {static_cast<quint8>(0x18 | pttBit), 0, 0,
                          static_cast<quint8>(supply >> 8), static_cast<quint8>(supply)};
    return makeEp6Frame(s0, s1);
}

// HL2 I2C responses in both subframes (C0 bit 7), bit 0 set or not.
QByteArray i2cOnlyFrame(quint8 c0 = 0x80)
{
    const quint8 s[5] = {c0, 0x12, 0x34, 0x56, 0x78};
    return makeEp6Frame(s, s);
}

// The Hermes supply formula (RadioConnection::convertSupplyVolts, from
// Thetis computeHermesDCVoltage).
float supplyVolts(quint16 raw)
{
    return (raw / 4095.0f) * 3.3f * (5.52f / 0.82f);
}

// Feeds one packet at simulated time t.
void feed(P1RadioConnection& conn, const QByteArray& pkt, qint64 t)
{
    conn.setStatusClockForTest(t);
    conn.parseEp6FrameForTest(pkt);
}

void makeSync(MoxController& ctrl)
{
    ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
}

} // namespace

class TestP1StatusRate : public QObject {
    Q_OBJECT

private slots:

    // ── 1. No PTT edge is lost or delayed ─────────────────────────────────
    // Press, release 50 ms later, a second press 1 ms after that and its
    // release two packets later. Every edge is emitted in the packet where
    // it first appears, and the emitted levels, with repeats folded, are
    // exactly the per-packet edges. The second row presses 1 ms after an
    // idle tick (the tick at 200 ms).
    void noPttEdgeIsLostOrDelayed_data()
    {
        QTest::addColumn<int>("pressMs");
        QTest::newRow("press at 10 ms") << 10;
        QTest::newRow("press 1 ms after the 200 ms idle tick") << 201;
    }
    void noPttEdgeIsLostOrDelayed()
    {
        QFETCH(int, pressMs);
        P1RadioConnection conn;
        conn.init();

        qint64 now = 0;
        QList<QPair<qint64, bool>> emitted;  // (packet time, level)
        connect(&conn, &RadioConnection::micPttFromRadio, &conn,
                [&](bool level) { emitted.append({now, level}); });

        const qint64 press1 = qint64(pressMs) * kMs;
        const qint64 release1 = press1 + 50 * kMs;
        const qint64 press2 = press1 + 51 * kMs;
        // The second press's packet index, so the release is two packets on.
        const qint64 press2Index = (press2 + kPacketNs - 1) / kPacketNs;
        const qint64 release2Index = press2Index + 2;
        const qint64 endIndex = (press1 + 60 * kMs) / kPacketNs;

        QList<bool> expectedEdges;
        bool prev = false;
        bool first = true;
        for (qint64 i = 0; i <= endIndex; ++i) {
            const qint64 t = i * kPacketNs;
            const bool level = (t >= press1 && t < release1)
                               || (i >= press2Index && i < release2Index);
            now = t;
            const int before = emitted.size();
            feed(conn, pttFrame(level), kT0 + t);
            if (first || level != prev) {
                expectedEdges.append(level);
                // The edge is in this packet's emissions.
                bool found = false;
                for (int k = before; k < emitted.size(); ++k) {
                    if (emitted[k].second == level) { found = true; }
                }
                QVERIFY2(found, qPrintable(QString("edge to %1 at %2 ms not emitted in its packet")
                                               .arg(level).arg(t / 1e6)));
            }
            // Nothing emitted in a packet differs from that packet's level.
            for (int k = before; k < emitted.size(); ++k) {
                QCOMPARE(emitted[k].second, level);
            }
            prev = level;
            first = false;
        }

        QList<bool> folded;
        for (const auto& e : emitted) {
            if (folded.isEmpty() || folded.last() != e.second) { folded.append(e.second); }
        }
        QCOMPARE(folded, expectedEdges);
        QCOMPARE(expectedEdges, (QList<bool>{false, true, false, true, false}));
    }

    // ── 2. Repeats at the idle and keyed rates ────────────────────────────
    void idleSecondRepeatsFiveTimes()
    {
        P1RadioConnection conn;
        conn.init();
        QSignalSpy ptt(&conn, &RadioConnection::micPttFromRadio);
        QSignalSpy telemetry(&conn, &RadioConnection::paTelemetryUpdated);

        const int packets = int(1000 * kMs / kPacketNs);  // 1 s
        for (int i = 0; i < packets; ++i) {
            feed(conn, telemetryFrame(i, false), kT0 + i * kPacketNs);
        }
        qInfo().noquote() << QString("MEASURE idle 1 s: %1 packets, %2 PTT, %3 telemetry")
                                 .arg(packets).arg(ptt.count()).arg(telemetry.count());
        QVERIFY2(ptt.count() >= 5 && ptt.count() <= 6,
                 qPrintable(QString("%1 PTT emissions in 1 s idle").arg(ptt.count())));
        QVERIFY2(telemetry.count() <= 6,
                 qPrintable(QString("%1 telemetry emissions in 1 s idle").arg(telemetry.count())));
    }

    void keyedSecondRepeatsAtMostEveryMillisecond()
    {
        P1RadioConnection conn;
        conn.init();
        conn.setMox(true);

        qint64 now = 0;
        QList<qint64> pttAt;
        QList<qint64> telemetryAt;
        connect(&conn, &RadioConnection::micPttFromRadio, &conn,
                [&](bool) { pttAt.append(now); });
        connect(&conn, &RadioConnection::paTelemetryUpdated, &conn,
                [&](quint16, quint16, quint16, quint16, quint16, quint16) {
                    telemetryAt.append(now);
                });

        const int packets = int(1000 * kMs / kPacketNs);
        for (int i = 0; i < packets; ++i) {
            now = i * kPacketNs;
            feed(conn, telemetryFrame(i, false), kT0 + now);
        }
        qInfo().noquote() << QString("MEASURE keyed 1 s: %1 packets, %2 PTT, %3 telemetry")
                                 .arg(packets).arg(pttAt.size()).arg(telemetryAt.size());
        QVERIFY2(pttAt.size() <= 1001, qPrintable(QString("%1 PTT").arg(pttAt.size())));
        QVERIFY2(telemetryAt.size() <= 1001,
                 qPrintable(QString("%1 telemetry").arg(telemetryAt.size())));
        // The level never changes here, so the first emission is the only
        // change; every later one is a repeat, at least 1 ms apart.
        QVERIFY(pttAt.size() > 800);
        for (int k = 2; k < pttAt.size(); ++k) {
            QVERIFY2(pttAt[k] - pttAt[k - 1] >= kMs,
                     qPrintable(QString("PTT repeats %1 us apart").arg((pttAt[k] - pttAt[k - 1]) / 1000)));
        }
        for (int k = 1; k < telemetryAt.size(); ++k) {
            QVERIFY(telemetryAt[k] - telemetryAt[k - 1] >= kMs);
        }
    }

    // ── 3. Latest value wins ──────────────────────────────────────────────
    void tickEmitsTheLatestPacketsValues()
    {
        P1RadioConnection conn;
        conn.init();

        int current = 0;
        int telemetryChecked = 0;
        int supplyChecked = 0;
        connect(&conn, &RadioConnection::paTelemetryUpdated, &conn,
                [&](quint16 fwd, quint16, quint16, quint16, quint16, quint16 supply) {
                    QCOMPARE(int(fwd), current & 0xFFFF);
                    QCOMPARE(int(supply), current % 4096);
                    ++telemetryChecked;
                });
        connect(&conn, &RadioConnection::supplyVoltsChanged, &conn, [&](float v) {
            QVERIFY(std::fabs(v - supplyVolts(quint16(current % 4096))) < 1e-4f);
            ++supplyChecked;
        });

        const int packets = int(1000 * kMs / kPacketNs);
        for (int i = 0; i < packets; ++i) {
            current = i;
            feed(conn, telemetryFrame(i, false), kT0 + i * kPacketNs);
        }
        QVERIFY(telemetryChecked >= 5);
        QVERIFY(supplyChecked >= 5);
    }

    // ── 4. No overload is lost ────────────────────────────────────────────
    void overloadArrivesWithinAMillisecondAndNoneIsLost()
    {
        P1RadioConnection conn;
        conn.init();

        qint64 now = 0;
        QList<QPair<qint64, int>> ovl;
        connect(&conn, &RadioConnection::adcOverflow, &conn,
                [&](int adc) { ovl.append({now, adc}); });

        const quint8 quiet[5] = {0x00, 0, 0, 0, 0};
        const quint8 adc0[5] = {0x00, 0x01, 0, 0, 0};
        const quint8 adc1[5] = {0x20, 0x00, 0x01, 0x00, 0};
        const quint8 adc2[5] = {0x20, 0x00, 0x00, 0x01, 0};
        const QByteArray quietPkt = makeEp6Frame(quiet, quiet);

        // Idle from t = 0 (first status ticks at once), then one ADC0
        // overload at about 50 ms, between idle ticks.
        int i = 0;
        auto t = [&] { return qint64(i) * kPacketNs; };
        for (; t() < 50 * kMs; ++i) {
            now = t();
            feed(conn, quietPkt, kT0 + now);
        }
        const qint64 adc0At = t();
        now = adc0At;
        feed(conn, makeEp6Frame(adc0, quiet), kT0 + now);
        ++i;
        QCOMPARE(ovl.size(), 1);
        QCOMPARE(ovl[0].second, 0);
        QVERIFY(ovl[0].first - adc0At <= kMs);

        // ADC1 and ADC2 in the next two packets, inside the 1 ms interval
        // that began with that tick, and ADC1 twice: both arrive at the
        // next tick, once each.
        const qint64 adc1At = t();
        now = t();
        feed(conn, makeEp6Frame(adc1, quiet), kT0 + now);
        ++i;
        now = t();
        feed(conn, makeEp6Frame(quiet, adc2), kT0 + now);
        ++i;
        now = t();
        feed(conn, makeEp6Frame(adc1, quiet), kT0 + now);
        ++i;
        QCOMPARE(ovl.size(), 1);  // nothing before the tick
        for (; t() < 60 * kMs; ++i) {
            now = t();
            feed(conn, quietPkt, kT0 + now);
        }
        QCOMPARE(ovl.size(), 3);
        QCOMPARE(ovl[1].second, 1);
        QCOMPARE(ovl[2].second, 2);
        QCOMPARE(ovl[1].first, ovl[2].first);
        QVERIFY(ovl[1].first - adc1At <= kMs);
        QVERIFY(ovl[1].first - adc0At >= kMs);
    }

    // ── 5. HL2 I2C-only packets emit no PTT and are not a change ──────────
    void i2cOnlyPacketsEmitNoPtt()
    {
        P1RadioConnection conn;
        conn.init();
        QSignalSpy ptt(&conn, &RadioConnection::micPttFromRadio);

        qint64 t = 0;
        feed(conn, i2cOnlyFrame(), kT0 + t);
        QCOMPARE(ptt.count(), 0);

        t += kPacketNs;
        feed(conn, pttFrame(true), kT0 + t);
        QCOMPARE(ptt.count(), 1);
        QCOMPARE(ptt.at(0).at(0).toBool(), true);

        // I2C responses alone, bit 0 clear and set, across several status
        // ticks (PTT down is keyed: 1 ms): no PTT, and no release.
        for (int k = 0; k < 20; ++k) {
            t += kPacketNs;
            feed(conn, i2cOnlyFrame(k % 2 ? 0x81 : 0x80), kT0 + t);
        }
        QCOMPARE(ptt.count(), 1);

        // The repeat that fell due waits for the next ordinary packet.
        t += kPacketNs;
        feed(conn, pttFrame(true), kT0 + t);
        QCOMPARE(ptt.count(), 2);
        QCOMPARE(ptt.at(1).at(0).toBool(), true);
    }

    // ── 6. frameReceived once per read ────────────────────────────────────
    void frameReceivedOncePerRead()
    {
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        P1RadioConnection conn;
        conn.init();
        QSignalSpy frames(&conn, &RadioConnection::frameReceived);
        int parsed = 0;
        connect(&conn, &RadioConnection::iqDataReceived, &conn,
                [&parsed](int rx, const QVector<float>&) { if (rx == 0) { ++parsed; } });

        RadioInfo info;
        info.address         = fake.localAddress();
        info.port            = fake.localPort();
        info.boardType       = HPSDRHW::HermesLite;
        info.protocol        = ProtocolVersion::Protocol1;
        info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
        info.firmwareVersion = 72;
        info.name            = QStringLiteral("FakeHL2");
        conn.connectToRadio(info);
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);
        fake.sendEp6Frames(1);
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(parsed, 1, 3000);
        frames.clear();
        parsed = 0;

        const int cap = P1RadioConnection::maxEp6DatagramsPerReadForTest();
        fake.sendEp6Frames(cap * 2);
        conn.onReadyReadForTest();
        QCOMPARE(parsed, cap);
        QCOMPARE(frames.count(), 1);

        conn.disconnect();
        fake.stop();
    }

    // ── 7. A PTT held through a cleared block keys at the next tick ───────
    // MoxController::onMicPttFromRadio runs one PollPTT pass per call;
    // clearing TX inhibit or the PA trip runs none, so the held PTT keys on
    // the next call. The repeat at the status tick is that call.
    void heldPttKeysAfterBlockClears_data()
    {
        QTest::addColumn<int>("block");    // 0 TX inhibit, 1 PA trip
        QTest::addColumn<bool>("repeat");
        QTest::newRow("TX inhibit, repeat on") << 0 << true;
        QTest::newRow("TX inhibit, repeat off") << 0 << false;
        QTest::newRow("PA trip, repeat on") << 1 << true;
        QTest::newRow("PA trip, repeat off") << 1 << false;
    }
    void heldPttKeysAfterBlockClears()
    {
        QFETCH(int, block);
        QFETCH(bool, repeat);

        P1RadioConnection conn;
        conn.init();
        conn.setStatusPttRepeatForTest(repeat);
        MoxController ctrl;
        makeSync(ctrl);
        connect(&conn, &RadioConnection::micPttFromRadio,
                &ctrl, &MoxController::onMicPttFromRadio, Qt::DirectConnection);

        const std::function<void(bool)> setBlock = [&](bool on) {
            if (block == 0) { ctrl.setTxInhibited(on); } else { ctrl.setPaTripped(on); }
        };
        setBlock(true);

        qint64 t = 0;
        // Idle, then PTT pressed and held through 100 ms of the block.
        for (; t < 20 * kMs; t += kPacketNs) { feed(conn, pttFrame(false), kT0 + t); }
        for (; t < 120 * kMs; t += kPacketNs) { feed(conn, pttFrame(true), kT0 + t); }
        QVERIFY(!ctrl.isMox());

        setBlock(false);
        const qint64 clearedAt = t;
        qint64 keyedAt = -1;
        for (; t < clearedAt + 400 * kMs; t += kPacketNs) {
            feed(conn, pttFrame(true), kT0 + t);
            if (keyedAt < 0 && ctrl.isMox()) { keyedAt = t; }
        }

        if (repeat) {
            QVERIFY2(keyedAt >= 0, "the held PTT never keyed after the block cleared");
            QCOMPARE(ctrl.pttMode(), PttMode::Mic);
            // A held PTT counts as keyed for the interval: 1 ms.
            QVERIFY2(keyedAt - clearedAt <= kMs,
                     qPrintable(QString("keyed %1 ms after the clear").arg((keyedAt - clearedAt) / 1e6)));
        } else {
            // On-change only: nothing calls PollPTT again while it is held.
            QVERIFY2(keyedAt < 0, "keyed without the repeat");
        }
    }
};

QTEST_GUILESS_MAIN(TestP1StatusRate)
#include "tst_p1_status_rate.moc"
