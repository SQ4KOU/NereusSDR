// =================================================================
// tests/tst_reported_receiver_pool.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The stream pool and ReceiverManager
// ceiling are NereusSDR constructs. The one upstream fact used here, that
// discovery byte 20 is the radio's receiver count, is cited to Thetis at
// RadioDiscovery.cpp's parse site.
//
// Bench (ANAN-G2): the Core logged "Cannot create receiver: at maximum 4"
// on every connect. The radio's discovery reply reports 4 receivers
// (byte 20), ReceiverManager was capped at that, and the stream pool was
// sized from the board table's five user streams (DDC2-6), so the fifth
// stream had no receiver and a pan landing on it stayed blank.
//
// The radio's reported count is the authority (CLAUDE.md, Radio-
// Authoritative Settings Policy; the gateware's receiver count is a
// compile-time constant that changes between firmware releases). On
// Protocol 2 the pool is min(board table, reported) when the radio reports
// a count, and the table value when it reports 0. Protocol 1 is unchanged.
//
// Modification history (NereusSDR):
//   2026-09-29 - Written by J.J. Boyd (KG4VCF), with AI-assisted
//                implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSignalSpy>

#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h"
#include "core/ReceiverManager.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

// A Protocol 2 discovery reply (layout: RadioDiscovery.cpp parseP2Reply).
QByteArray p2Reply(HPSDRHW board, int reportedReceivers)
{
    QByteArray b(60, '\0');
    b[4] = char(0x02);
    const char mac[6] = {char(0x00), char(0x1c), char(0xc0), char(0x77), char(0x88), char(0x99)};
    for (int i = 0; i < 6; ++i) { b[5 + i] = mac[i]; }
    b[11] = char(static_cast<int>(board));
    b[13] = char(26);
    b[20] = char(reportedReceivers);
    return b;
}

// A Protocol 1 discovery reply (layout: RadioDiscovery.cpp parseP1Reply).
QByteArray p1Reply(quint8 boardByte, int reportedReceivers)
{
    QByteArray b(60, '\0');
    b[0] = char(0xEF);
    b[1] = char(0xFE);
    b[2] = char(0x02);
    const char mac[6] = {char(0x00), char(0x1c), char(0xc0), char(0x11), char(0x22), char(0x33)};
    for (int i = 0; i < 6; ++i) { b[3 + i] = mac[i]; }
    b[9]  = char(72);
    b[10] = char(boardByte);
    b[20] = char(reportedReceivers);
    return b;
}

RadioInfo parsedP2(HPSDRHW board, int reported)
{
    RadioInfo info;
    RadioDiscovery::parseP2Reply(p2Reply(board, reported),
                                 QHostAddress(QStringLiteral("192.168.1.20")), info);
    return info;
}

// connectToRadio's receiver and stream sizing, in its order: the
// ReceiverManager ceiling, receiver 0, the stream pool, then one receiver
// per remaining stream. Returns the pool size.
int connectPool(RadioModel& model, const RadioInfo& info)
{
    model.setLastRadioInfoForTest(info);
    const BoardCapabilities& caps = model.boardCapabilities();
    const int streams = BoardCapsTable::userDdcCountFor(
        caps, info.protocol, info.reportedReceivers);
    ReceiverManager* rm = model.receiverManager();
    rm->setMaxReceivers(RadioModel::receiverPoolCeiling(info, streams));
    rm->createReceiver();
    model.configureStreamPool(streams, caps.maxSlices > 0 ? caps.maxSlices : 1, 192000);
    for (int st = 1; st < streams; ++st) {
        if (rm->receiverConfig(st).receiverIndex < 0) {
            rm->createReceiver();
        }
    }
    return streams;
}

} // namespace

class TestReportedReceiverPool : public QObject
{
    Q_OBJECT

private slots:

    // Discovery keeps the radio's own number apart from the table fallback,
    // so the pool can tell "the radio said 4" from "the radio said nothing".
    void discovery_records_the_reported_count()
    {
        const RadioInfo four = parsedP2(HPSDRHW::Saturn, 4);
        QCOMPARE(four.protocol, ProtocolVersion::Protocol2);
        QCOMPARE(four.reportedReceivers, 4);
        QCOMPARE(four.maxReceivers, 4);

        const RadioInfo none = parsedP2(HPSDRHW::Saturn, 0);
        QCOMPARE(none.reportedReceivers, 0);
        QCOMPARE(none.maxReceivers, RadioInfo::maxReceiversForBoard(HPSDRHW::Saturn));

        RadioInfo p1;
        QVERIFY(RadioDiscovery::parseP1Reply(p1Reply(6, 4),
                                             QHostAddress(QStringLiteral("192.168.1.21")), p1));
        QCOMPARE(p1.reportedReceivers, 4);

        // A radio typed in by hand or restored from the saved list has
        // reported nothing.
        QCOMPARE(RadioInfo{}.reportedReceivers, 0);
    }

    // The one stream count, with the report applied.
    void stream_count_is_capped_by_the_report_on_protocol_2()
    {
        const BoardCapabilities& g2 = BoardCapsTable::forBoard(HPSDRHW::Saturn);
        QCOMPARE(g2.userDdcCount, 5);
        QCOMPARE(BoardCapsTable::userDdcCountFor(g2, ProtocolVersion::Protocol2, 4), 4);
        QCOMPARE(BoardCapsTable::userDdcCountFor(g2, ProtocolVersion::Protocol2, 0), 5);
        // A report above the table never widens the pool past the DDCs the
        // slot plan knows how to use.
        QCOMPARE(BoardCapsTable::userDdcCountFor(g2, ProtocolVersion::Protocol2, 8), 5);
        // The two-argument form is the no-report form.
        QCOMPARE(BoardCapsTable::userDdcCountFor(g2, ProtocolVersion::Protocol2),
                 BoardCapsTable::userDdcCountFor(g2, ProtocolVersion::Protocol2, 0));
    }

    // Protocol 1 is unchanged: its frame slot plan sets the count.
    void protocol_1_ignores_the_report()
    {
        for (const BoardCapabilities& caps : BoardCapsTable::all()) {
            const int base = BoardCapsTable::userDdcCountFor(caps, ProtocolVersion::Protocol1);
            for (int reported : {0, 1, 2, 4, 7}) {
                QCOMPARE(BoardCapsTable::userDdcCountFor(caps, ProtocolVersion::Protocol1,
                                                         reported),
                         base);
            }
        }

        RadioInfo p1;
        p1.macAddress = QStringLiteral("00:1c:c0:11:22:33");
        p1.protocol = ProtocolVersion::Protocol1;
        p1.maxReceivers = 2;
        p1.reportedReceivers = 2;
        QCOMPARE(RadioModel::receiverPoolCeiling(p1, 4), 2);

        RadioModel model;
        model.setBoardForTest(HPSDRHW::OrionMKII);
        p1.boardType = HPSDRHW::OrionMKII;
        model.setLastRadioInfoForTest(p1);
        QCOMPARE(model.userStreamCount(), 4);
    }

    // Every reader of the stream count (the Core's advertised count, the
    // pan layout ceiling, the Core's own pool) goes through userStreamCount.
    void user_stream_count_follows_the_report()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);

        model.setLastRadioInfoForTest(parsedP2(HPSDRHW::Saturn, 4));
        QCOMPARE(model.userStreamCount(), 4);

        model.setLastRadioInfoForTest(parsedP2(HPSDRHW::Saturn, 0));
        QCOMPARE(model.userStreamCount(), 5);
    }

    // The bench case: a G2 reporting 4 connects with no warning and four
    // streams, each with a receiver behind it.
    void g2_reporting_four_connects_with_four_streams_and_no_warning()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Cannot create receiver")));

        const int streams = connectPool(model, parsedP2(HPSDRHW::Saturn, 4));
        QCOMPARE(streams, 4);
        QCOMPARE(model.streamPoolSize(), 4);
        QCOMPARE(model.userStreamCount(), 4);
        for (int st = 0; st < 4; ++st) {
            QVERIFY2(model.receiverManager()->receiverConfig(st).receiverIndex >= 0,
                     "every stream in the pool must have a receiver behind it");
        }
    }

    // A fifth pan cannot get a receiver of its own, and the operator is told
    // why in plain words that name the radio's count.
    void g2_reporting_four_refuses_a_fifth_pan_with_a_plain_reason()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        connectPool(model, parsedP2(HPSDRHW::Saturn, 4));

        QSignalSpy rejected(&model, &RadioModel::sliceAddRejected);
        for (int pan = 0; pan < 4; ++pan) {
            model.addSliceOnPan(QStringLiteral("pan-%1").arg(pan));
        }
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(model.activeStreamCount(), 4);
        for (int st = 0; st < 4; ++st) {
            QVERIFY(model.receiverManager()->isReceiverActive(st));
        }

        const int before = model.slices().size();
        model.addSliceOnPan(QStringLiteral("pan-4"));
        QCOMPARE(model.slices().size(), before);
        QCOMPARE(rejected.count(), 1);
        const QString reason = rejected.at(0).at(0).toString();
        QVERIFY2(reason.contains(QStringLiteral("All 4 of the radio's receivers are in use")),
                 qPrintable(reason));
        QVERIFY2(!reason.contains(QStringLiteral("DDC")), qPrintable(reason));
    }

    // Reporting 0 keeps the table value, with no warning, including a radio
    // restored from the saved list (its RadioInfo carries the default
    // receiver count and no report).
    void no_report_keeps_the_table_value()
    {
        {
            RadioModel model;
            model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
            QTest::failOnWarning(QRegularExpression(QStringLiteral("Cannot create receiver")));
            QCOMPARE(connectPool(model, parsedP2(HPSDRHW::Saturn, 0)), 5);
            QCOMPARE(model.streamPoolSize(), 5);
            QCOMPARE(model.receiverManager()->receiverConfig(4).receiverIndex >= 0, true);
        }
        {
            RadioModel model;
            model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
            RadioInfo saved;
            saved.macAddress = QStringLiteral("00:1c:c0:77:88:99");
            saved.boardType = HPSDRHW::Saturn;
            saved.protocol = ProtocolVersion::Protocol2;
            QCOMPARE(saved.reportedReceivers, 0);
            QCOMPARE(connectPool(model, saved), 5);
            QCOMPARE(model.receiverManager()->receiverConfig(4).receiverIndex >= 0, true);
        }
    }
};

QTEST_MAIN(TestReportedReceiverPool)
#include "tst_reported_receiver_pool.moc"
