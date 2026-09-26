// =================================================================
// tests/tst_remote_transmit_client.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test.
//
// iPhone app plan, desktop remote transmit (R-IOS-13, R-R3-42): the remote
// window's side of the transmit verbs, against a recording sender. The
// copies rule, the epoch, a release before the answer, a key the Core ends
// on its own, a program's key under the operator's, TUNE off before its
// answer, and a lost link.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25 - Created for the desktop remote window's transmit
//                (R-IOS-13, R-R3-42). J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QSignalSpy>

#include "core/session/RemoteTransmitClient.h"

using namespace NereusSDR;

namespace {

struct Sent {
    quint32 id;
    QByteArray verb;
    QList<MirrorUpdate> arguments;
};

struct Recorder {
    QList<Sent> sent;
    quint32 next = 100;
    bool linked = true;

    RemoteTransmitClient::Sender sender()
    {
        return [this](const QByteArray& verb, const QList<MirrorUpdate>& arguments) -> quint32 {
            if (!linked) { return 0; }
            const quint32 id = next++;
            sent.append({id, verb, arguments});
            return id;
        };
    }

    QVariant argument(int index, const char* name) const
    {
        for (const MirrorUpdate& a : sent.at(index).arguments) {
            if (a.name == name) { return a.value; }
        }
        return {};
    }
};

QList<MirrorUpdate> epochValue(quint32 epoch)
{
    return {MirrorUpdate{0, QByteArrayLiteral("epoch"), MirrorWireKind::Int64,
                         QVariant(static_cast<qlonglong>(epoch))}};
}

QList<MirrorUpdate> refusal(const char* code, const char* fix)
{
    return {MirrorUpdate{0, QByteArrayLiteral("refusalCode"), MirrorWireKind::Utf8,
                         QVariant(QString::fromLatin1(code))},
            MirrorUpdate{0, QByteArrayLiteral("refusalFix"), MirrorWireKind::Utf8,
                         QVariant(QString::fromLatin1(fix))}};
}

// The Core answers every copy.
void answerCopies(RemoteTransmitClient& client, const Sent& command, bool accepted,
                  const QString& reason, const QList<MirrorUpdate>& values)
{
    for (int c = 0; c < RemoteTransmitClient::kCopies; ++c) {
        client.commandFinished(command.id, command.verb, accepted, reason, values);
    }
}

} // namespace

class TestRemoteTransmitClient : public QObject {
    Q_OBJECT

private slots:
    void aPressKeysWithTheScreenTriggerAndTheReleaseNamesItsEpoch()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        QSignalSpy keyDown(&client, &RemoteTransmitClient::micKeyDownChanged);
        QSignalSpy holds(&client, &RemoteTransmitClient::holdsTransmitChanged);

        client.setScreenKey(true);
        QCOMPARE(core.sent.size(), 1);
        QCOMPARE(core.sent.at(0).verb, QByteArrayLiteral("tx.key"));
        QCOMPARE(core.argument(0, "trigger").toString(), QStringLiteral("screen"));
        QVERIFY(client.micKeyDown());
        QVERIFY(!client.holdsTransmit());
        QCOMPARE(keyDown.count(), 1);

        answerCopies(client, core.sent.at(0), true, {}, epochValue(7));
        QVERIFY(client.holdsTransmit());
        QCOMPARE(client.screenEpoch(), 7u);
        QCOMPARE(holds.count(), 1);

        client.setScreenKey(true);   // still down: nothing more
        QCOMPARE(core.sent.size(), 1);

        client.setScreenKey(false);
        QCOMPARE(core.sent.size(), 2);
        QCOMPARE(core.sent.at(1).verb, QByteArrayLiteral("tx.unkey"));
        QCOMPARE(core.argument(1, "epoch").toLongLong(), 7LL);
        QVERIFY(!client.micKeyDown());
        QVERIFY(!client.holdsTransmit());
    }

    void aReleaseBeforeTheAnswerStopsWhateverKeyed()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        client.setScreenKey(true);
        client.setScreenKey(false);
        QCOMPARE(core.sent.size(), 2);
        QCOMPARE(core.argument(1, "epoch").toLongLong(),
                 qlonglong(RemoteTransmitClient::kReleaseAnyEpoch));
        // The key's answer after the release changes nothing.
        answerCopies(client, core.sent.at(0), true, {}, epochValue(3));
        QVERIFY(!client.holdsTransmit());
        QVERIFY(!client.micKeyDown());
        QCOMPARE(core.sent.size(), 2);
    }

    void aRefusalIsShownOnceWithItsCodeAndFix()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        QSignalSpy refused(&client, &RemoteTransmitClient::refused);
        client.setScreenKey(true);
        answerCopies(client, core.sent.at(0), false, QStringLiteral("Radio has the transmitter."),
                     refusal("otherDeviceHolds", "takeTransmit"));
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.at(0).at(0).toString(), QStringLiteral("Radio has the transmitter."));
        QCOMPARE(refused.at(0).at(1).toString(), QStringLiteral("otherDeviceHolds"));
        QCOMPARE(refused.at(0).at(2).toString(), QStringLiteral("takeTransmit"));
        QVERIFY(!client.micKeyDown());
        // The next press is a new command.
        client.setScreenKey(true);
        QCOMPARE(core.sent.size(), 2);
        QVERIFY(core.sent.at(1).id != core.sent.at(0).id);
    }

    void aKeyTheCoreEndsMakesTheNextPressANewCommand()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        client.setScreenKey(true);
        client.setCoreTransmitting(true);   // the state can come first
        answerCopies(client, core.sent.at(0), true, {}, epochValue(4));
        QVERIFY(client.holdsTransmit());
        client.setCoreTransmitting(false);   // a safety stop at the Core
        QVERIFY(!client.holdsTransmit());
        QVERIFY(!client.micKeyDown());
        client.setScreenKey(true);
        QCOMPARE(core.sent.size(), 2);
        QCOMPARE(core.sent.at(1).verb, QByteArrayLiteral("tx.key"));
        QVERIFY(core.sent.at(1).id != core.sent.at(0).id);
    }

    void aProgramsReleaseUnderTheOperatorsKeySendsNothing()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        client.setScreenKey(true);
        answerCopies(client, core.sent.at(0), true, {}, epochValue(5));
        RemoteTransmitClient::Answer answer;
        client.keyForProgram([&answer](const RemoteTransmitClient::Answer& a) { answer = a; });
        QCOMPARE(core.argument(1, "trigger").toString(), QStringLiteral("tci"));
        answerCopies(client, core.sent.at(1), true, {}, epochValue(5));
        QVERIFY(answer.accepted);
        QCOMPARE(answer.epoch, 5u);
        client.unkeyForProgram(5);
        QCOMPARE(core.sent.size(), 2);   // the operator's MOX holds it
        QVERIFY(client.holdsTransmit());
        // The operator's MOX off ends it.
        client.setScreenKey(false);
        QCOMPARE(core.sent.last().verb, QByteArrayLiteral("tx.unkey"));
        QCOMPARE(core.argument(2, "epoch").toLongLong(), 5LL);
    }

    void aProgramKeyAloneIsReleasedByItsUnkeyOrByMoxOff()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        bool accepted = false;
        client.keyForProgram([&accepted](const RemoteTransmitClient::Answer& a) {
            accepted = a.accepted;
        });
        QVERIFY(client.micKeyDown());
        answerCopies(client, core.sent.at(0), true, {}, epochValue(9));
        QVERIFY(accepted);
        client.setScreenKey(false);   // MOX off while the program keys
        QCOMPARE(core.sent.size(), 2);
        QCOMPARE(core.argument(1, "epoch").toLongLong(), 9LL);
        QVERIFY(!client.holdsTransmit());
    }

    void tuneOffGoesEvenBeforeItsAnswer()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        client.setTune(true);
        QVERIFY(client.tuneAsked());
        QVERIFY(core.argument(0, "on").toBool());
        client.setTune(false);
        QVERIFY(!client.tuneAsked());
        QVERIFY(!core.argument(1, "on").toBool());
        // A refused TUNE on is no longer asked.
        client.setTune(true);
        answerCopies(client, core.sent.at(2), false, QStringLiteral("x"), {});
        QVERIFY(!client.tuneAsked());
        // Neither TUNE nor two-tone takes the microphone.
        QVERIFY(!client.micKeyDown());
    }

    void moxOffWhileTheCoreTransmitsForAnotherKeyAsksTheCore()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        client.setCoreTransmitting(true);   // this device's VOX key, say
        client.setScreenKey(false);
        QCOMPARE(core.sent.size(), 1);
        QCOMPARE(core.sent.at(0).verb, QByteArrayLiteral("tx.unkey"));
        QCOMPARE(core.argument(0, "epoch").toLongLong(),
                 qlonglong(RemoteTransmitClient::kReleaseAnyEpoch));
        client.setCoreTransmitting(false);
        client.setScreenKey(false);   // nothing transmits: nothing to ask
        QCOMPARE(core.sent.size(), 1);
    }

    void aLostLinkForgetsEveryKeyAndAnswersAWaitingProgram()
    {
        Recorder core;
        RemoteTransmitClient client(core.sender());
        client.setAvailable(true);
        client.setScreenKey(true);
        QString programReason;
        bool programAnswered = false;
        client.keyForProgram([&](const RemoteTransmitClient::Answer& a) {
            programAnswered = true;
            programReason = a.reason;
        });
        client.setAvailable(false);
        QVERIFY(programAnswered);
        QCOMPARE(programReason, QString::fromLatin1(RemoteTransmitClient::kNoLinkReason));
        QVERIFY(!client.micKeyDown());
        // A late answer for the old key does nothing.
        answerCopies(client, core.sent.at(0), true, {}, epochValue(2));
        QVERIFY(!client.holdsTransmit());
        // With no link a press is refused here.
        core.linked = false;
        client.setAvailable(true);
        QSignalSpy refused(&client, &RemoteTransmitClient::refused);
        client.setScreenKey(true);
        QCOMPARE(refused.count(), 1);
        QVERIFY(!client.micKeyDown());
    }
};

QTEST_MAIN(TestRemoteTransmitClient)
#include "tst_remote_transmit_client.moc"
