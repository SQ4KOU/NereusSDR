// =================================================================
// tests/tst_session_end_reasons.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.
//
// R3 completion carry, review finding I1 (R-R3-21, R-R3-38, R-IOS-01): the
// takeover and version reasons are worded and read in one place. What
// SessionEndReasons formats, it parses back to the same kind, the other
// app's address and both versions, in both directions of the version
// case; the words are plain and say "Core".
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Created for review finding I1.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "OperatorWording.h"
#include "core/session/SessionEndReasons.h"

using namespace NereusSDR;

using MajorList = QList<quint16>;
using Kind = SessionEndReasons::Parsed::Kind;

class TstSessionEndReasons : public QObject {
    Q_OBJECT

private slots:
    void takeoverRoundTrip_data()
    {
        QTest::addColumn<QString>("otherApp");
        QTest::addColumn<QString>("address");

        // WebSocketTransport::peerDescription(): "address:port".
        QTest::newRow("IPv4") << QStringLiteral("192.0.2.7:50123")
                              << QStringLiteral("192.0.2.7");
        QTest::newRow("IPv4 written as IPv6") << QStringLiteral("::ffff:127.0.0.1:40000")
                                              << QStringLiteral("127.0.0.1");
        QTest::newRow("IPv6") << QStringLiteral("2001:db8::5:443")
                              << QStringLiteral("2001:db8::5");
        // The Core has no address for the other app.
        QTest::newRow("no address") << QStringLiteral("<unknown>") << QString();
        QTest::newRow("detached") << QStringLiteral("<detached>") << QString();
    }

    void takeoverRoundTrip()
    {
        QFETCH(QString, otherApp);
        QFETCH(QString, address);
        const QString reason = SessionEndReasons::takenOver(otherApp);
        QCOMPARE(reason, QStringLiteral("Another app at %1 connected to the Core and took "
                                        "over. Connect again to take it back.")
                             .arg(otherApp));
        const SessionEndReasons::Parsed parsed = SessionEndReasons::parse(reason);
        QCOMPARE(parsed.kind, Kind::TakenOver);
        QCOMPARE(parsed.otherAppAddress, address);
        QCOMPARE(parsed.coreMajor, -1);
        QCOMPARE(parsed.appMajor, -1);
    }

    void versionRoundTrip_data()
    {
        QTest::addColumn<MajorList>("core");
        QTest::addColumn<MajorList>("app");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("coreMajor");
        QTest::addColumn<int>("appMajor");

        // The iPhone plan's example.
        QTest::newRow("Core older")
            << MajorList{1} << MajorList{2, 3}
            << QStringLiteral("This Core runs link version 1 and this app runs version 3. "
                              "Update the Core.")
            << 1 << 3;
        QTest::newRow("app older")
            << MajorList{2, 3} << MajorList{1}
            << QStringLiteral("This Core runs link version 3 and this app runs version 1. "
                              "Update this app.")
            << 3 << 1;
        QTest::newRow("two digits")
            << MajorList{11, 12} << MajorList{9, 10}
            << QStringLiteral("This Core runs link version 12 and this app runs version 10. "
                              "Update this app.")
            << 12 << 10;
    }

    void versionRoundTrip()
    {
        QFETCH(MajorList, core);
        QFETCH(MajorList, app);
        QFETCH(QString, expected);
        QFETCH(int, coreMajor);
        QFETCH(int, appMajor);
        const QString reason = SessionEndReasons::versionRefused(core, app);
        QCOMPARE(reason, expected);
        QVERIFY2(OperatorWording::isPlain(reason),
                 qPrintable(OperatorWording::internalTermIn(reason)));
        const SessionEndReasons::Parsed parsed = SessionEndReasons::parse(reason);
        QCOMPARE(parsed.kind, Kind::VersionRefused);
        QCOMPARE(parsed.coreMajor, coreMajor);
        QCOMPARE(parsed.appMajor, appMajor);
        QVERIFY(parsed.otherAppAddress.isEmpty());
    }

    void takeoverWordsArePlain()
    {
        const QString reason = SessionEndReasons::takenOver(QStringLiteral("192.0.2.7:50123"));
        QVERIFY2(OperatorWording::isPlain(reason),
                 qPrintable(OperatorWording::internalTermIn(reason)));
    }

    // Anything else is neither, including the words these two replaced
    // and a reason that only contains one of them.
    void otherReasonsAreNeither_data()
    {
        QTest::addColumn<QString>("reason");
        QTest::newRow("empty") << QString();
        QTest::newRow("another refusal") << QStringLiteral("undecodable message");
        QTest::newRow("old takeover words")
            << QStringLiteral("Displaced by a newer authenticated connection from "
                              "192.0.2.7:50123");
        QTest::newRow("old version words")
            << QStringLiteral("Protocol major version mismatch: station speaks 2.0, client "
                              "speaks 1.11. A differing major means an incompatible wire "
                              "contract.");
        QTest::newRow("station wording")
            << QStringLiteral("This station runs link version 1 and this app runs version 3. "
                              "Update the station.");
        QTest::newRow("quoted inside another")
            << QStringLiteral("Refused: Another app at 192.0.2.7:1 connected to the Core and "
                              "took over. Connect again to take it back.");
    }

    void otherReasonsAreNeither()
    {
        QFETCH(QString, reason);
        const SessionEndReasons::Parsed parsed = SessionEndReasons::parse(reason);
        QCOMPARE(parsed.kind, Kind::Other);
        QVERIFY(parsed.otherAppAddress.isEmpty());
        QCOMPARE(parsed.coreMajor, -1);
        QCOMPARE(parsed.appMajor, -1);
    }
};

QTEST_MAIN(TstSessionEndReasons)
#include "tst_session_end_reasons.moc"
