// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_tgxl_answer_tracker.cpp  (NereusSDR)
// =================================================================
//
// TgxlAnswerTracker, one table: each row is a sequence of what the Core
// sent the Tuner Genius and what the tuner sent back, with the verdict on
// each of the tuner's `transmit tune on` lines (an answer, or its own
// front-panel press). Timings follow captures/flex-tgxl-direct-CONTROL.pcapng
// (autotune at T+172.199, tuning=1 at +2 ms, tune on at +503 ms).
//
// Script tokens, each `<event>@<ms>`:
//   A<n>   the Core sent `autotune` with sequence n
//   X<n>   the tuner refused sequence n (no time)
//   B1/B0  the Core broadcast tune=1 / tune=0
//   T1/T0  the tuner's tuning went up / down
//   ON=a   the tuner's tune on, expected to be an answer
//   ON=p   the tuner's tune on, expected to be its own press
//   OFF    the tuner's tune off
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-01: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), TGXL tune lane round 2, with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include "core/TgxlAnswerTracker.h"

#include <QStringList>
#include <QTest>

using namespace NereusSDR;

class TgxlAnswerTrackerTest : public QObject {
    Q_OBJECT

private slots:
    void sequences_data()
    {
        QTest::addColumn<QString>("script");
        QTest::addColumn<int>("leftWaiting");

        QTest::newRow("pcap: autotune answered, then a press")
            << "A1@0 T1@2 ON@503=a ON@1000=p" << 0;
        QTest::newRow("nothing sent: a press") << "ON@0=p" << 0;
        QTest::newRow("I-A: a plain tune's echo arrives after it ended")
            << "B1@0 B0@50 ON@200=a OFF@210 ON@1000=p" << 0;
        QTest::newRow("a device's tune: answer and echo both counted")
            << "A1@0 B1@30 ON@503=a ON@520=a ON@1000=p" << 0;
        QTest::newRow("a device's tune, one line only: closed until the window")
            << "A1@0 B1@30 ON@503=a ON@1000=a ON@3100=p" << 0;
        QTest::newRow("m-A1: the second autotune refused, the first still counted")
            << "A1@0 A2@10 X2@0 ON@503=a ON@600=p" << 0;
        QTest::newRow("the only autotune refused: nothing to answer")
            << "A1@0 X1@0 ON@100=p" << 0;
        QTest::newRow("m-A2: a fall from an earlier sweep does not clear it")
            << "T1@0 A1@10 T0@20 ON@500=a" << 0;
        QTest::newRow("its own sweep ends without a tune on: cleared")
            << "A1@0 T1@2 T0@1500 ON@1600=p" << 0;
        QTest::newRow("m-A2: an echoed tune off does not clear it")
            << "A1@0 B0@5 OFF@10 ON@503=a" << 0;
        QTest::newRow("a tune off before its sweep starts does not clear it")
            << "A1@0 OFF@3 ON@503=a" << 0;
        QTest::newRow("a tune off after its sweep started clears it")
            << "A1@0 T1@2 OFF@100 ON@200=p" << 0;
        QTest::newRow("the window's last millisecond still counts")
            << "A1@0 ON@3000=a" << 0;
        QTest::newRow("past the window: a press") << "A1@0 ON@3001=p" << 0;
        QTest::newRow("an echoed tune off is not an answer to a tune on")
            << "B0@0 OFF@10 ON@20=p" << 0;
        QTest::newRow("still waiting") << "A1@0 B1@30 ON@503=a" << 1;
    }

    void sequences()
    {
        QFETCH(QString, script);
        QFETCH(int, leftWaiting);
        TgxlAnswerTracker tracker;
        qint64 now = 0;
        for (const QString& token : script.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const int at = token.indexOf(QLatin1Char('@'));
            QVERIFY2(at > 0, qPrintable(token));
            const QString event = token.left(at);
            QString time = token.mid(at + 1);
            QString verdict;
            const int eq = time.indexOf(QLatin1Char('='));
            if (eq >= 0) {
                verdict = time.mid(eq + 1);
                time = time.left(eq);
            }
            if (!event.startsWith(QLatin1Char('X'))) {
                now = time.toLongLong();
            }
            if (event == QLatin1String("ON")) {
                const bool answers = tracker.tuneOnAnswers(now);
                QVERIFY2(answers == (verdict == QLatin1String("a")), qPrintable(token));
            } else if (event == QLatin1String("OFF")) {
                tracker.tuneOff(now);
            } else if (event == QLatin1String("B1") || event == QLatin1String("B0")) {
                tracker.tuneBroadcast(event == QLatin1String("B1"), now);
            } else if (event == QLatin1String("T1") || event == QLatin1String("T0")) {
                tracker.tuningChanged(event == QLatin1String("T1"), now);
            } else if (event.startsWith(QLatin1Char('A'))) {
                tracker.autotuneSent(event.mid(1).toUInt(), now);
            } else if (event.startsWith(QLatin1Char('X'))) {
                tracker.autotuneRejected(event.mid(1).toUInt());
            } else {
                QFAIL(qPrintable(QStringLiteral("unknown token ") + token));
            }
        }
        QCOMPARE(tracker.awaitingTuneOn(now), leftWaiting);
    }
};

QTEST_GUILESS_MAIN(TgxlAnswerTrackerTest)
#include "tst_tgxl_answer_tracker.moc"
