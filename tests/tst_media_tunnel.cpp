// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_media_tunnel.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 29 step 2b (R-IOS-16; the options survey's B.3; link
// section 21, "The media tunnel"): media inside a direct WebSocket session.
// A real Core and a real window joined over an in-process link, with real
// DTLS/SRTP media: when no UDP pair can work (IceConfiguration's test seam
// refuses every candidate but a loopback shim's, since on one computer
// host pairs always work) the media connection runs through the tunnel,
// its datagrams binary messages on the session's link, and audio plays;
// when UDP works, a host pair wins and the tunnel carries nothing. The
// window declares the tunnel only to a Core that carries it.
//
// The Docker traversal harness proves the same with UDP blocked between a
// device and a Core it reaches by its forwarded wss port.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QSignalSpy>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <thread>

#include "RealtimeTestLoad.h"
#include "core/session/IceConfiguration.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/TransmitStateFacade.h"
#include "core/session/media/DaemonMediaController.h"
#include "fakes/RemoteAudioSessionHarness.h"
#include "fakes/LoopbackTransport.h"
#include "core/session/MediaTunnel.h"
#include "core/session/CandidateSourceLease.h"
#include "core/session/SessionTransport.h"
#include "gui/PanadapterStack.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumWidget.h"
#include "gui/RemoteConnectionController.h"
#include "gui/RemoteMediaController.h"

using namespace NereusSDR;

namespace {

// Counts the binary messages (tunnel datagrams) a link end sends.
struct BinaryCount {
    int messages = 0;
    QMetaObject::Connection watch;
    explicit BinaryCount(SessionTransport* link)
    {
        watch = QObject::connect(link, &SessionTransport::binaryReceived, link,
                                 [this](const QByteArray&) { ++messages; });
    }
    ~BinaryCount() { QObject::disconnect(watch); }
};

} // namespace

class TstMediaTunnel final : public QObject {
    Q_OBJECT

private slots:
    void directWebSocketTunnelReportsItsCurrentControlSocket()
    {
        QWebSocketServer server(QStringLiteral("local tunnel"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        auto* clientSocket = new QWebSocket;
        WebSocketTransport control(clientSocket, 4096);
        auto tunnel = MediaTunnel::create(&control);
        QVERIFY(tunnel);
        IceConfiguration ice = MediaTunnel::iceFor(tunnel);
        const QString id = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        auto source = ice.makeCandidateSource(IceConfiguration::kMediaLane, id);
        QVERIFY(source);
        source->start([](const QString&) {});
        QVERIFY(!source->networkPathSnapshot());
        clientSocket->open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
        QTRY_VERIFY(clientSocket->state() == QAbstractSocket::ConnectedState);
        QTRY_VERIFY(server.hasPendingConnections());
        QScopedPointer<QWebSocket> accepted(server.nextPendingConnection());
        const auto path = source->networkPathSnapshot();
        QVERIFY(path);
        QCOMPARE(path->kind, NetworkPathSnapshot::Kind::Direct);
        QCOMPARE(path->carrier, NetworkPathSnapshot::Carrier::WebSocket);
        QCOMPARE(path->endpoints, NetworkPathSnapshot::Endpoints::Socket);
        QVERIFY(path->mediaRidesControl);
        QCOMPARE(path->remoteAddress, QStringLiteral("127.0.0.1"));
        QCOMPARE(path->remotePort, server.serverPort());
        QVERIFY(path->localPort != 0);
        control.closeLink(QStringLiteral("test done"));
        QVERIFY(!source->networkPathSnapshot());
        source->stop();
    }

    void tunnelRoutesConcurrentGenerationsAndRejectsOversize()
    {
        Test::LoopbackTransport local(QStringLiteral("local"));
        Test::LoopbackTransport remote(QStringLiteral("remote"));
        local.linkTo(&remote);
        auto tunnel = MediaTunnel::create(&local);
        QVERIFY(tunnel);
        IceConfiguration ice = MediaTunnel::iceFor(tunnel);
        QVERIFY(!ice.makeCandidateSource(IceConfiguration::kMediaLane,
                                         QStringLiteral("00000000-0000-0000-0000-000000000000")));
        QVERIFY(!ice.makeCandidateSource(IceConfiguration::kMediaLane,
                                         QStringLiteral("AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA")));
        const QString a = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
        const QString b = QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
        auto firstLease = CandidateSourceLease::create(ice);
        std::shared_ptr<void> delayedIceLifetime = firstLease;
        auto second = ice.makeCandidateSource(IceConfiguration::kMediaLane, b);
        QVERIFY(second);
        quint16 firstPort = 0;
        quint16 secondPort = 0;
        firstLease->start(IceConfiguration::kMediaLane, a, [&](const QString& candidate) {
            firstPort = candidate.split(' ').at(5).toUShort();
        });
        second->start([&](const QString& candidate) {
            secondPort = candidate.split(' ').at(5).toUShort();
        });
        QVERIFY(firstPort != 0 && secondPort != 0 && firstPort != secondPort);
        QUdpSocket firstAgent;
        QUdpSocket secondAgent;
        QVERIFY(firstAgent.bind(QHostAddress::LocalHost, 0));
        QVERIFY(secondAgent.bind(QHostAddress::LocalHost, 0));
        QSignalSpy outgoing(&remote, &SessionTransport::binaryReceived);
        firstAgent.writeDatagram("one", QHostAddress::LocalHost, firstPort);
        secondAgent.writeDatagram("two", QHostAddress::LocalHost, secondPort);
        QTRY_VERIFY(outgoing.size() >= 2);
        const QByteArray firstUuid = QUuid::fromString(a).toRfc4122();
        const QByteArray secondUuid = QUuid::fromString(b).toRfc4122();
        const QByteArray one = QByteArray(1, '\x02') + firstUuid + "one";
        const QByteArray two = QByteArray(1, '\x02') + secondUuid + "two";
        QVERIFY(outgoing.at(0).at(0).toByteArray() == one
                || outgoing.at(1).at(0).toByteArray() == one);
        QVERIFY(outgoing.at(0).at(0).toByteArray() == two
                || outgoing.at(1).at(0).toByteArray() == two);
        QUdpSocket otherSender;
        QVERIFY(otherSender.bind(QHostAddress::LocalHost, 0));
        otherSender.writeDatagram("wrong-one", QHostAddress::LocalHost, firstPort);
        otherSender.writeDatagram("wrong-two", QHostAddress::LocalHost, secondPort);
        QTRY_COMPARE(tunnel->droppedWrongSender(), quint64(2));
        QCOMPARE(outgoing.size(), 2); // neither generation retargeted
        firstAgent.writeDatagram(QByteArray(1484, 'x'), QHostAddress::LocalHost, firstPort);
        QTRY_VERIFY(outgoing.size() >= 3);
        QCOMPARE(outgoing.last().at(0).toByteArray().size(), 1501);
        firstAgent.writeDatagram(QByteArray(1485, 'x'), QHostAddress::LocalHost, firstPort);
        QTRY_COMPARE(tunnel->droppedOversize(), quint64(1));
        remote.sendBinary(QByteArray(1, '\x02') + firstUuid + QByteArray(1485, 'x'));
        QTRY_COMPARE(tunnel->droppedOversize(), quint64(2));
        remote.sendBinary(QByteArray(1, '\x02') + firstUuid + "to-one");
        remote.sendBinary(QByteArray(1, '\x02') + secondUuid + "to-two");
        QTRY_VERIFY(firstAgent.hasPendingDatagrams());
        QTRY_VERIFY(secondAgent.hasPendingDatagrams());
        QCOMPARE(firstAgent.receiveDatagram().data(), QByteArray("to-one"));
        QCOMPARE(secondAgent.receiveDatagram().data(), QByteArray("to-two"));
        QVERIFY(!otherSender.hasPendingDatagrams());
        // The wrapper can disappear while ICE still holds this lease. Even
        // after the old fixed hold interval, the old socket must remain
        // bound and must never become another generation's route.
        firstLease.reset();
        QTest::qWait(2100);
        remote.sendBinary(QByteArray(1, '\x02') + firstUuid + "still-old");
        remote.sendBinary(QByteArray(1, '\x02') + secondUuid + "live");
        remote.sendBinary(QByteArray(1, '\x02')); // malformed routed frame
        QTRY_VERIFY(firstAgent.hasPendingDatagrams());
        QCOMPARE(firstAgent.receiveDatagram().data(), QByteArray("still-old"));
        QTRY_VERIFY(secondAgent.hasPendingDatagrams());
        QCOMPARE(secondAgent.receiveDatagram().data(), QByteArray("live"));
        QTRY_COMPARE(tunnel->droppedNoRoute(), quint64(1));
        auto third = ice.makeCandidateSource(
            IceConfiguration::kMediaLane,
            QStringLiteral("cccccccc-cccc-4ccc-8ccc-cccccccccccc"));
        auto fourth = ice.makeCandidateSource(
            IceConfiguration::kMediaLane,
            QStringLiteral("dddddddd-dddd-4ddd-8ddd-dddddddddddd"));
        QVERIFY(third && fourth);
        quint16 thirdPort = 0;
        third->start([&](const QString& candidate) {
            thirdPort = candidate.split(' ').at(5).toUShort();
        });
        QVERIFY(thirdPort != 0 && thirdPort != firstPort);
        bool fourthOffered = false;
        fourth->start([&](const QString&) { fourthOffered = true; });
        QVERIFY(!fourthOffered); // current, new and retiring exhaust the bound
        // libdatachannel drops this last holder on its teardown worker.
        // The lease must post socket release back to the Qt thread.
        std::thread teardown([held = std::move(delayedIceLifetime)]() mutable {
            held.reset();
        });
        teardown.join();
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            fourth->start([&](const QString&) { fourthOffered = true; });
            return fourthOffered;
        }(), 3000);
        remote.sendBinary(QByteArray(1, '\x02') + firstUuid + "retired");
        QTRY_COMPARE(tunnel->droppedNoRoute(), quint64(2));
        third->stop();
        fourth->stop();
        second->stop();
    }

    void cleanup()
    {
        IceConfiguration::setOnlyLoopbackShimCandidatesForTest(false);
        RealtimeTestLoad::printLoadAverageIfFailed();
    }

    // No UDP pair works: audio plays through the tunnel on the session's
    // own link.
    void mediaRunsThroughTheTunnelWhenNothingElseWorks()
    {
        IceConfiguration::setOnlyLoopbackShimCandidatesForTest(true);
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QTimer source;
        source.setInterval(10);
        QObject::connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        QObject::connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        h.connectSession();
        QCOMPARE(h.client.capabilities().mediaTunnelVersion, 1);
        QVERIFY(h.client.mediaTunnelAvailable());
        BinaryCount atCore(h.stationLink);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 20000);
        QVERIFY(atCore.messages > 0);
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void realDtlsMediaReplacesThroughIndependentTunnelSockets()
    {
        IceConfiguration::setOnlyLoopbackShimCandidatesForTest(true);
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QTimer source;
        source.setInterval(10);
        QObject::connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        QObject::connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 20000);
        const QString oldId = remoteMedia.mediaConnectionId();
        QVERIFY(!oldId.isEmpty());
        const int switchFrom = h.remoteBus->heard.size() / 2;
        QVERIFY(remoteMedia.replaceConnection());
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.mediaConnectionId() != oldId, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(h.remoteBus->heard.size() / 2 >= switchFrom + 48000, 10000);
        int silentRun = 0;
        int longestSilentRun = 0;
        const QVector<float>& heard = h.remoteBus->heard;
        for (int frame = switchFrom; (frame + 480) * 2 <= heard.size(); frame += 480) {
            double energy = 0.0;
            for (int i = 0; i < 480; ++i) {
                const double sample = heard.at((frame + i) * 2);
                energy += sample * sample;
            }
            silentRun = energy / 480.0 < 1e-6 ? silentRun + 1 : 0;
            longestSilentRun = std::max(longestSilentRun, silentRun);
        }
        QVERIFY2(longestSilentRun <= 4, qPrintable(QString::number(longestSilentRun * 10)));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A silent tunnel requests session recovery within a few seconds. The
    // in-process link has no address to redial, so the fixture supplies the
    // next link after the production recovery controller closes the first.
    // Both decoded speaker output and a presented display frame must return.
    void aStalledTunnelIsStartedAgainWithinSeconds()
    {
        IceConfiguration::setOnlyLoopbackShimCandidatesForTest(true);
        Test::RemoteAudioSessionHarness h;
        const int stream = h.station.sliceById(h.sliceA)->streamIndex();
        QVERIFY(stream >= 0);
        PanadapterStack stack;
        auto* pan = stack.addPanadapter(QStringLiteral("recovery"));
        pan->setActiveSliceIndex(h.sliceA);
        pan->spectrumWidget()->setDisplayWindowPreservingHistory(
            h.station.streamCentreHz(stream), 48000);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        RemoteMediaController remoteMedia(&h.client, &h.remote, &stack);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        RemoteConnectionController connection(&h.client, &h.remote, RemoteStationOptions{});
        QObject::connect(&remoteMedia, &RemoteMediaController::recoveryRequested,
                         &connection, &RemoteConnectionController::recoverMediaSession,
                         Qt::QueuedConnection);
        QSignalSpy recovery(&remoteMedia, &RemoteMediaController::recoveryRequested);
        QSignalSpy ended(&h.client, &StationClient::sessionEnded);
        QSignalSpy frames(&remoteMedia, &RemoteMediaController::displayFrameReceived);
        QTimer source;
        source.setInterval(10);
        QObject::connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        QTimer display;
        display.setInterval(20);
        QObject::connect(&display, &QTimer::timeout, &display, [&h, stream, &iq] {
            QMetaObject::invokeMethod(&h.station, "rawIqDataForStream", Qt::DirectConnection,
                                      Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
        });
        QTimer speaker;
        speaker.setInterval(10);
        QObject::connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        display.start();
        speaker.start();
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 10000);
        const QString oldId = remoteMedia.mediaConnectionId();
        QVERIFY(!oldId.isEmpty());
        QElapsedTimer silence;
        silence.start();
        h.stationLink->setDropsOutgoing(true);
        QTRY_VERIFY_WITH_TIMEOUT(!recovery.isEmpty(), 10000);
        const qint64 found = silence.elapsed();
        qInfo("A stalled tunnel found after %lld ms", static_cast<long long>(found));
        QVERIFY2(found >= RemoteMediaController::kMediaStallMs && found <= 5000,
                 qPrintable(QString::number(found)));
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
        const int framesBeforeReconnect = frames.size();
        h.connectSession(); // the fixture's replacement for an address-based redial
        const int audioStart = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.mediaConnectionId() != oldId
                                     && !remoteMedia.mediaConnectionId().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(frames.size() > framesBeforeReconnect, 10000);
        const qint64 displayResumeMs = silence.elapsed();
        const auto heardAgain = [&h, audioStart] {
            const auto& heard = h.remoteBus->heard;
            for (int frame = audioStart; (frame + 480) * 2 <= heard.size(); frame += 480) {
                double energy = 0.0;
                for (int i = 0; i < 480; ++i) {
                    const double sample = heard.at((frame + i) * 2);
                    energy += sample * sample;
                }
                if (energy / 480.0 >= 1e-6) { return true; }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(heardAgain(), 10000);
        const qint64 audioResumeMs = silence.elapsed();
        qInfo("Tunnel recovery decoded audio after %lld ms and displayed a frame after %lld ms",
              static_cast<long long>(audioResumeMs), static_cast<long long>(displayResumeMs));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void tuneSilenceDoesNotDiscardTheTunnel_data()
    {
        QTest::addColumn<bool>("resumeAudio");
        QTest::newRow("rx-audio-resumes") << true;
        QTest::newRow("rx-audio-still-missing") << false;
    }

    void tuneSilenceDoesNotDiscardTheTunnel()
    {
        QFETCH(bool, resumeAudio);
        IceConfiguration::setOnlyLoopbackShimCandidatesForTest(true);
        Test::RemoteAudioSessionHarness h;
        const int stream = h.station.sliceById(h.sliceA)->streamIndex();
        QVERIFY(stream >= 0);
        PanadapterStack stack;
        auto* pan = stack.addPanadapter(QStringLiteral("tune"));
        pan->setActiveSliceIndex(h.sliceA);
        pan->spectrumWidget()->setDisplayWindowPreservingHistory(
            h.station.streamCentreHz(stream), 48000);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        RemoteMediaController remoteMedia(&h.client, &h.remote, &stack);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy recovery(&remoteMedia, &RemoteMediaController::recoveryRequested);
        QSignalSpy frames(&remoteMedia, &RemoteMediaController::displayFrameReceived);
        QTimer source;
        source.setInterval(10);
        QObject::connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        QTimer display;
        display.setInterval(20);
        QObject::connect(&display, &QTimer::timeout, &display, [&h, stream, &iq] {
            QMetaObject::invokeMethod(&h.station, "rawIqDataForStream", Qt::DirectConnection,
                                      Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
        });
        QTimer speaker;
        speaker.setInterval(10);
        QObject::connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        display.start();
        speaker.start();
        h.connectSession();
        QVERIFY(h.client.capabilities().txStateVersion >= 1);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 10000);
        const QString mediaId = remoteMedia.mediaConnectionId();
        QVERIFY(!mediaId.isEmpty());
        const auto heardTone = [&h](int firstFrame) {
            const auto& heard = h.remoteBus->heard;
            for (int frame = firstFrame; (frame + 480) * 2 <= heard.size(); frame += 480) {
                double energy = 0.0;
                for (int i = 0; i < 480; ++i) {
                    const double sample = heard.at((frame + i) * 2);
                    energy += sample * sample;
                }
                if (energy / 480.0 >= 1e-6) { return true; }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(heardTone(0), 5000);

        h.station.moxController()->setMoxCheck({}); // fake Core, no radio
        h.station.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        // The fake Core has no physical transmitter. Drive its existing
        // MOX/TUNE state machine and mirrored transmit model directly.
        h.station.transmitModel().setTune(true);
        h.station.moxController()->setTune(true);
        QTRY_VERIFY_WITH_TIMEOUT(h.client.transmitState()->keyed()
                                     && h.client.transmitState()->tuning(), 5000);
        source.stop(); // half-duplex RX silence, while display remains active
        // A tunnel selected after TX began must still shorten the control
        // heartbeat. Reset the selection to model that path-settling edge.
        h.client.setMediaTunnelInUse(false);
        QCOMPARE(h.client.effectiveHeartbeatIntervalMs(),
                 StationClient::kDefaultHeartbeatIntervalMs);
        QTRY_COMPARE_WITH_TIMEOUT(h.client.effectiveHeartbeatIntervalMs(),
                                  StationClient::kRelayedHeartbeatIntervalMs, 1500);
        QTest::qWait(RemoteMediaController::kMediaStallMs + 600);
        QCOMPARE(recovery.size(), 0);
        QCOMPARE(remoteMedia.mediaConnectionId(), mediaId);

        h.station.moxController()->setTune(false);
        h.station.transmitModel().setTune(false);
        QTRY_VERIFY_WITH_TIMEOUT(!h.client.transmitState()->keyed()
                                     && !h.client.transmitState()->tuning()
                                     && !h.client.transmitState()->txEnding(), 5000);
        QElapsedTimer rxSilence;
        rxSilence.start();
        const int framesBeforeRx = frames.size();
        if (resumeAudio) {
            const int audioStart = h.remoteBus->heard.size() / 2;
            source.start();
            QTRY_VERIFY_WITH_TIMEOUT(heardTone(audioStart), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(frames.size() > framesBeforeRx, 5000);
            QCOMPARE(recovery.size(), 0);
            QCOMPARE(remoteMedia.mediaConnectionId(), mediaId);
        } else {
            // Unrelated stateChanged notifications while idle must not
            // postpone an already armed RX stall clock.
            QSignalSpy unrelatedUpdates(h.client.transmitState(),
                                        &TransmitState::stateChanged);
            QTimer unrelated;
            unrelated.setInterval(100);
            int counter = 0;
            QObject::connect(&unrelated, &QTimer::timeout, &unrelated, [&] {
                const int sliceId = (++counter & 1) ? h.sliceB : h.sliceA;
                h.client.transmitState()->applyStationValue("txSliceId", sliceId);
            });
            unrelated.start();
            QTRY_VERIFY_WITH_TIMEOUT(!recovery.isEmpty(), 6000);
            QVERIFY(unrelatedUpdates.size() >= 10);
            QVERIFY2(rxSilence.elapsed() >= RemoteMediaController::kMediaStallMs
                         && rxSilence.elapsed() <= 5000,
                     qPrintable(QString::number(rxSilence.elapsed())));
        }
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // UDP works: a host pair wins, and the tunnel is only a candidate.
    void aHostPairStillWins()
    {
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QTimer source;
        source.setInterval(10);
        QObject::connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        QObject::connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        h.connectSession();
        BinaryCount atCore(h.stationLink);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 20000);
        // Two seconds of audio (about 50 packets): none of it came through
        // the tunnel, which carries at most the agent's checks of its pair.
        const int before = atCore.messages;
        QTest::qWait(2000);
        QVERIFY2(atCore.messages - before < 10,
                 qPrintable(QString::number(atCore.messages - before)));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

QTEST_MAIN(TstMediaTunnel)
#include "tst_media_tunnel.moc"
