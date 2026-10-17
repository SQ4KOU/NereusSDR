// no-port-check: NereusSDR-original PS two-tone keepalive regression.
// Modification history (NereusSDR):
//   2026-10-13 J.J. Boyd (KG4VCF): exercise PS requests through StationClient
//              with in-process transport and watchdog. AI-assisted via OpenAI Codex.
//   2026-10-14 J.J. Boyd (KG4VCF): cover session retirement during a
//              compatibility stop. AI-assisted via OpenAI Codex.
#include <QtTest>

#include "core/AppSettings.h"
#include "core/safety/RemoteTxWatchdog.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/RemoteTransmitClient.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;

namespace {

// The real server supplies the session handshake only. After admission the
// test Core controls command answers; no radio, DSP engine or socket is opened.
struct PsSession {
    QTemporaryDir directory;
    AppSettings settings{directory.filePath(QStringLiteral("station.settings"))};
    RadioModel station;
    StationServer server{&station, settings, Test::seedUpgradedCoreToken(directory.path())};
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy proxy;
    StationClient client{&remote, &proxy};
    Test::LoopbackTransport* coreLink{nullptr};
    Test::LoopbackTransport* clientLink{nullptr};
    QList<SessionMessage> commands;
    RemoteTxWatchdog watchdog;
    qint64 nowMs{0};
    int stops{0};
    int heard{0};

    void join()
    {
        coreLink = new Test::LoopbackTransport(QStringLiteral("core"));
        clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        coreLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(coreLink);
    }

    void offerPsControls()
    {
        // acceptTransport wraps the primary in a SwitchableTransport. Stop
        // its forwarding too, so only the fake Core answers test commands.
        QObject::disconnect(coreLink, nullptr, nullptr, nullptr);
        QObject::connect(coreLink, &SessionTransport::textReceived, &client,
                         [this](const QByteArray& wire) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)
                && message.kind == SessionMessageKind::CommandInvoke
                && message.commandVerb == "ps3.twoTone") {
                commands.append(message);
            }
        });
        StationCapabilities capabilities = client.capabilities();
        capabilities.psAlgorithmVersion = 3;
        capabilities.remoteTxVersion = 1;
        capabilities.txPermitted = true;
        capabilities.propertyResultVersion = 1;
        coreLink->sendText(SessionMessages::encode(
            SessionMessages::capabilities(capabilities.toUpdates())));
        RemoteTxWatchdog::Hooks hooks;
        hooks.clock = [this] { return nowMs; };
        hooks.stop = [this](const QByteArray&, const QString&) { ++stops; };
        watchdog.setHooks(std::move(hooks));
        client.remoteTransmit()->setChannelKeepalive([this](quint64 sequence, quint32 epoch) {
            nowMs += 100;
            if (watchdog.keepalive("window", sequence, epoch,
                                  RemoteTxWatchdog::Path::TxChannel)) {
                ++heard;
            }
            watchdog.onTimer();
            return true;
        });
    }

    PureSignalSessionFacade* facade() { return remote.pureSignalFacade(); }

    quint32 request(bool on)
    {
        return facade()->requestAction(Ps3Action::SetTwoTone, {{"enabled", on}});
    }

    void answer(quint32 id, bool accepted, const QString& phase = QStringLiteral("completed"))
    {
        coreLink->sendText(SessionMessages::encode(SessionMessages::commandResult(
            "ps3.twoTone", id, accepted, accepted ? QString() : QStringLiteral("Test refusal"),
            {}, {{0, "phase", MirrorWireKind::Utf8, phase}})));
    }
};

} // namespace

class TestStationPs3TwoToneKeepalive : public QObject {
    Q_OBJECT
private:
    void admit(PsSession& session)
    {
        session.join();
        QTRY_VERIFY(session.client.isHandshakeComplete());
        session.offerPsControls();
        QTRY_VERIFY(session.client.remoteTransmitAvailable());
        QTRY_COMPARE(session.client.capabilities().psAlgorithmVersion, 3);
        QTRY_VERIFY(session.client.capabilities().txPermitted);
        QVERIFY(session.facade()->applyRemoteProperty("available", true));
        QVERIFY(session.facade()->applyRemoteProperty("canActuate", true));
        QVERIFY(session.facade()->available());
        QVERIFY(session.facade()->canActuate());
    }

private slots:
    void psOnlyStartSustainsTheWatchAndPreservesItsCommandResult()
    {
        PsSession session;
        admit(session);
        session.watchdog.setKeyed("window", true, 7);
        QSignalSpy results(session.facade(), &PureSignalSessionFacade::actionResult);
        const quint32 id = session.request(true);
        QVERIFY2(id != 0, qPrintable(session.facade()->lastActionError()));
        QVERIFY2(session.client.remoteTransmit()->keepaliveRunning(),
                 "PS-only two-tone must start this window's keepalive intent immediately");
        QCOMPARE(session.heard, 1);
        QTRY_VERIFY_WITH_TIMEOUT(session.heard >= 6, 1200);
        QCOMPARE(session.stops, 0);
        // PS actions are submitted once: their asynchronous Core handler
        // refuses a same-ID copy while the original action is pending.
        QTRY_COMPARE(session.commands.size(), 1);
        for (const SessionMessage& command : session.commands) {
            QCOMPARE(command.commandVerb, QByteArray("ps3.twoTone"));
            QCOMPARE(command.commandId, id);
            QCOMPARE(command.arguments.size(), 1);
            QCOMPARE(command.arguments.first().name, QByteArray("enabled"));
            QCOMPARE(command.arguments.first().value, QVariant(true));
        }
        session.answer(id, true);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first()[0].toUInt(), id);
        QCOMPARE(qvariant_cast<Ps3ActionPhase>(results.first()[1]), Ps3ActionPhase::Completed);
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
        session.answer(id, false);
        QCoreApplication::processEvents();
        QCOMPARE(results.size(), 1);
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
    }

    void refusedStartStopsItsIntentButDoesNotEndAnotherIntent()
    {
        PsSession session;
        admit(session);
        QSignalSpy results(session.facade(), &PureSignalSessionFacade::actionResult);
        QSignalSpy transmitRefusals(session.client.remoteTransmit(), &RemoteTransmitClient::refused);
        const quint32 id = session.request(true);
        QVERIFY(id != 0);
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
        session.answer(id, false);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(qvariant_cast<Ps3ActionPhase>(results.first()[1]), Ps3ActionPhase::Failed);
        QCOMPARE(transmitRefusals.size(), 0); // PS owns this action's operator-facing result.
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
        session.client.remoteTransmit()->setVoxArmed(true);
        const quint32 next = session.request(true);
        session.answer(next, false);
        QTRY_COMPARE(results.size(), 2);
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
        session.client.remoteTransmit()->setVoxArmed(false);
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
    }

    void stopKeepsTheReleaseFenceThroughPendingAndCompletesNormally()
    {
        PsSession session;
        admit(session);
        const quint32 start = session.request(true);
        session.answer(start, true);
        QSignalSpy results(session.facade(), &PureSignalSessionFacade::actionResult);
        QTRY_COMPARE(results.size(), 1);
        session.client.remoteTransmit()->setVoxArmed(true);
        const quint32 stop = session.request(false);
        QVERIFY(stop != 0);
        const quint64 sent = session.client.remoteTransmit()->channelKeepalivesSent();
        session.client.remoteTransmit()->keepaliveTick();
        QCOMPARE(session.client.remoteTransmit()->channelKeepalivesSent(), sent);
        session.answer(stop, true, QStringLiteral("pending"));
        QTRY_COMPARE(results.size(), 2);
        QCOMPARE(qvariant_cast<Ps3ActionPhase>(results.last()[1]), Ps3ActionPhase::Pending);
        session.client.remoteTransmit()->keepaliveTick();
        QCOMPARE(session.client.remoteTransmit()->channelKeepalivesSent(), sent);
        session.answer(stop, true);
        QTRY_COMPARE(results.size(), 3);
        QCOMPARE(results.last()[0].toUInt(), stop);
        QCOMPARE(qvariant_cast<Ps3ActionPhase>(results.last()[1]), Ps3ActionPhase::Completed);
        session.client.remoteTransmit()->keepaliveTick();
        QVERIFY(session.client.remoteTransmit()->channelKeepalivesSent() > sent);
        session.client.remoteTransmit()->setVoxArmed(false);
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
    }

    void olderRefusalCannotClearANewerTwoToneRequest()
    {
        PsSession session;
        admit(session);
        const quint32 older = session.request(true);
        const quint32 newer = session.request(true);
        QVERIFY(older != 0 && newer != 0 && older != newer);
        QSignalSpy results(session.facade(), &PureSignalSessionFacade::actionResult);
        session.answer(newer, true);
        session.answer(older, false);
        QTRY_COMPARE(results.size(), 2);
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
        session.client.remoteTransmit()->setCoreTransmitting(false);
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
    }

    void failedStopKeepsOtherIntentBehindTheReleaseFence()
    {
        PsSession session;
        admit(session);
        session.client.remoteTransmit()->setVoxArmed(true);
        const quint32 start = session.request(true);
        QSignalSpy results(session.facade(), &PureSignalSessionFacade::actionResult);
        session.answer(start, true);
        QTRY_COMPARE(results.size(), 1);
        const quint32 stop = session.request(false);
        session.answer(stop, true, QStringLiteral("accepted"));
        QTRY_COMPARE(results.size(), 2);
        session.answer(stop, false);
        QTRY_COMPARE(results.size(), 3);
        QCOMPARE(qvariant_cast<Ps3ActionPhase>(results.last()[1]), Ps3ActionPhase::Failed);
        const quint64 sent = session.client.remoteTransmit()->channelKeepalivesSent();
        session.client.remoteTransmit()->keepaliveTick();
        QCOMPARE(session.client.remoteTransmit()->channelKeepalivesSent(), sent);
        QCOMPARE(session.request(true), 0u);
        session.client.remoteTransmit()->setVoxArmed(false);
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
    }

    void replacedSessionDuringSendDoesNotOverwriteTheNewIntent()
    {
        PsSession session;
        admit(session);
        bool replaced = false;
        QObject::connect(session.clientLink, &Test::LoopbackTransport::outboundText,
                         &session.client, [&](const QByteArray& wire) {
            SessionMessage message;
            if (!replaced && SessionMessages::decode(wire, &message)
                && message.commandVerb == "ps3.twoTone") {
                replaced = true;
                session.client.remoteTransmit()->setAvailable(false);
                session.client.remoteTransmit()->setAvailable(true);
                session.client.remoteTransmit()->setTwoTone(true);
            }
        });
        QCOMPARE(session.request(true), 0u);
        QVERIFY(replaced);
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
    }

    void stopStillReachesACoreWithoutRemoteTransmitCapability()
    {
        PsSession session;
        admit(session);
        StationCapabilities capabilities = session.client.capabilities();
        capabilities.remoteTxVersion = 0;
        session.coreLink->sendText(SessionMessages::encode(
            SessionMessages::capabilities(capabilities.toUpdates())));
        QTRY_VERIFY(!session.client.remoteTransmitAvailable());
        QSignalSpy results(session.facade(), &PureSignalSessionFacade::actionResult);
        const quint32 stop = session.request(false);
        QVERIFY(stop != 0);
        QTRY_COMPARE(session.commands.size(), 1);
        QCOMPARE(session.commands.first().commandId, stop);
        QCOMPARE(session.commands.first().arguments.first().value, QVariant(false));
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
        session.answer(stop, true);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(qvariant_cast<Ps3ActionPhase>(results.first()[1]), Ps3ActionPhase::Completed);
    }

    void unavailableStopIsRetiredWhenTheSessionEndsDuringSend()
    {
        PsSession session;
        admit(session);
        StationCapabilities capabilities = session.client.capabilities();
        capabilities.remoteTxVersion = 0;
        session.coreLink->sendText(SessionMessages::encode(
            SessionMessages::capabilities(capabilities.toUpdates())));
        QTRY_VERIFY(!session.client.remoteTransmitAvailable());
        bool ended = false;
        bool replaced = false;
        QObject::connect(session.client.remoteTransmit(), &RemoteTransmitClient::micSourceChanged,
                         &session.client, [&]() {
            if (ended && !replaced) {
                replaced = true;
                session.join();
            }
        });
        QObject::connect(session.clientLink, &Test::LoopbackTransport::outboundText,
                         &session.client, [&](const QByteArray& wire) {
            SessionMessage message;
            if (!ended && SessionMessages::decode(wire, &message)
                && message.commandVerb == "ps3.twoTone") {
                ended = true;
                session.client.disconnectFromStation(QStringLiteral("test departure"));
            }
        });

        QCOMPARE(session.request(false), 0u);
        QVERIFY(ended);
        QVERIFY(replaced);
        QTRY_VERIFY(session.client.isHandshakeComplete());
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
    }

    void unavailableRetiresRequestsAndOldAnswersCannotClearANewIntent()
    {
        PsSession session;
        admit(session);
        const quint32 old = session.request(true);
        session.client.remoteTransmit()->setAvailable(false);
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
        QCOMPARE(session.request(true), 0u);
        session.client.remoteTransmit()->setAvailable(true);
        const quint32 current = session.request(true);
        QVERIFY(current != 0 && current != old);
        session.answer(old, false);
        QCoreApplication::processEvents();
        QVERIFY(session.client.remoteTransmit()->keepaliveRunning());
        session.client.disconnectFromStation(QStringLiteral("test departure"));
        QVERIFY(!session.client.remoteTransmit()->keepaliveRunning());
        QCOMPARE(session.request(true), 0u);
    }
};

QTEST_GUILESS_MAIN(TestStationPs3TwoToneKeepalive)
#include "tst_station_ps3_two_tone_keepalive.moc"
