// no-port-check: NereusSDR-original. Exercises the bounded Core telemetry
// collector, including its real queued RadioConnection observation boundary.

#include <QtTest>

#include "core/AppSettings.h"
#include "core/RadioConnection.h"
#include "core/daemon/DaemonTelemetryController.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "models/RadioModel.h"

#include <QPointer>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>

#include <memory>

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

class NullRadioConnection final : public RadioConnection {
    Q_OBJECT
public:
    using RadioConnection::RadioConnection;
    void init() override {}
    void connectToRadio(const RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void setMox(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void sendTxIq(const float*, int) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
    void setWatchdogEnabled(bool) override {}
};

struct SessionHarness {
    QTemporaryDir directory;
    AppSettings settings;
    RadioModel station;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy proxy;
    StationClient client{&remote, &proxy};

    SessionHarness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&station, settings, directory.path())
    {
        Q_ASSERT(directory.isValid());
    }

    void connectClient(QObject* owner)
    {
        auto* stationLink = new LoopbackTransport(QStringLiteral("telemetry-station"),
                                                  owner);
        auto* clientLink = new LoopbackTransport(QStringLiteral("telemetry-client"),
                                                 owner);
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
    }
};

StationTelemetrySnapshot lastSnapshot(const QSignalSpy& samples)
{
    return qvariant_cast<StationTelemetrySnapshot>(samples.constLast().at(0));
}

struct ThreadConnection {
    explicit ThreadConnection(RadioModel& owner) : model(owner) {}

    ~ThreadConnection()
    {
        if (model.connection() == connection.data()) {
            model.injectConnectionForTest(nullptr);
        }
        if (connection) {
            if (!thread.isRunning()) {
                thread.start();
            }
            const std::shared_ptr<QSemaphore> dispatched
                = std::make_shared<QSemaphore>();
            NullRadioConnection* const retiring = connection.data();
            QMetaObject::invokeMethod(retiring, [retiring, dispatched] {
                retiring->deleteLater();
                dispatched->release();
            });
            dispatched->tryAcquire(1, 1000);
            thread.quit();
            thread.wait();
        } else if (thread.isRunning()) {
            thread.quit();
            thread.wait();
        }
    }

    void moveToOwnerThread(bool start)
    {
        connection->moveToThread(&thread);
        if (start) {
            thread.start();
        }
    }

    RadioModel& model;
    QThread thread;
    QPointer<NullRadioConnection> connection{new NullRadioConnection};
};

} // namespace

class TstDaemonTelemetry final : public QObject {
    Q_OBJECT
private slots:
    void audioRatesUseElapsedTimeAndRetireInvalidBaselines()
    {
        SessionHarness h;
        qint64 nowMs = 100;
        DaemonAudioDiagnostics audio;
        audio.activeContext = true;
        audio.contextGeneration = 7;
        audio.sender.source.capturedValidRateFrames = 100;
        audio.sender.source.sourceDropEvents = 2;
        audio.sender.encodedPackets = 4;
        audio.sender.encodeFailures = 1;
        audio.sendAccepted = 3;
        audio.sendRejected = 1;

        DaemonTelemetryController controller(
            &h.server, &h.station, nullptr, nullptr,
            [&] { return nowMs; }, [&] { return audio; });
        controller.disableAutomaticSamplingForTest();
        h.server.setTelemetryEnabled(true);
        QSignalSpy samples(&h.client, &StationClient::telemetryReceived);
        h.connectClient(this);
        QTRY_VERIFY(h.client.telemetryAvailable());

        nowMs = 200;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 1);
        StationTelemetrySnapshot snapshot = lastSnapshot(samples);
        QCOMPARE(snapshot.sequence, quint32{1});
        QCOMPARE(snapshot.sampledElapsedMs, qint64{100});
        QVERIFY(snapshot.audio.active);
        QCOMPARE(snapshot.audio.contextGeneration, quint32{7});
        QVERIFY(!snapshot.audio.sourceFramesPerSecond);
        QVERIFY(!snapshot.audio.sendAcceptedPerSecond);

        audio.sender.source.capturedValidRateFrames += 120;
        audio.sender.source.sourceDropEvents += 1;
        audio.sender.encodedPackets += 3;
        audio.sender.encodeFailures += 2;
        audio.sendAccepted += 2;
        audio.sendRejected += 1;
        nowMs = 500; // 300 ms, deliberately not the nominal timer period.
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 2);
        snapshot = lastSnapshot(samples);
        QCOMPARE(*snapshot.audio.sourceFramesPerSecond, 400.0);
        QCOMPARE(*snapshot.audio.sourceDropsPerSecond, 1000.0 / 300.0);
        QCOMPARE(*snapshot.audio.encodedPacketsPerSecond, 10.0);
        QCOMPARE(*snapshot.audio.encodeFailuresPerSecond, 2000.0 / 300.0);
        QCOMPARE(*snapshot.audio.sendAcceptedPerSecond, 2000.0 / 300.0);
        QCOMPARE(*snapshot.audio.sendRejectedPerSecond, 1000.0 / 300.0);

        nowMs = 1000;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 3);
        snapshot = lastSnapshot(samples);
        QVERIFY(snapshot.audio.sourceFramesPerSecond);
        QCOMPARE(*snapshot.audio.sourceFramesPerSecond, 0.0);
        QCOMPARE(*snapshot.audio.sourceDropsPerSecond, 0.0);

        // One regressing counter invalidates this complete rate set; no
        // mixture of old and reset lifetimes is published.
        audio.sender.encodedPackets = 1;
        nowMs = 1250;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 4);
        snapshot = lastSnapshot(samples);
        QVERIFY(!snapshot.audio.sourceFramesPerSecond);
        QVERIFY(!snapshot.audio.encodedPacketsPerSecond);
        QVERIFY(!snapshot.audio.sendAcceptedPerSecond);

        audio.activeContext = false;
        nowMs = 1500;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 5);
        snapshot = lastSnapshot(samples);
        QVERIFY(!snapshot.audio.active);
        QVERIFY(!snapshot.audio.sourceFramesPerSecond);

        // Retirement and a changed generation both establish baselines;
        // neither invents zero-rate activity at the boundary.
        audio.activeContext = true;
        audio.contextGeneration = 8;
        audio.sender.source.capturedValidRateFrames = 10;
        audio.sender.source.sourceDropEvents = 0;
        audio.sender.encodedPackets = 1;
        audio.sender.encodeFailures = 0;
        audio.sendAccepted = 1;
        audio.sendRejected = 0;
        nowMs = 1750;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 6);
        snapshot = lastSnapshot(samples);
        QVERIFY(snapshot.audio.active);
        QVERIFY(!snapshot.audio.sourceFramesPerSecond);
    }

    void queuedRadioReadsRejectAReplyFromTheReplacedConnection()
    {
        SessionHarness h;
        qint64 nowMs = 0;
        DaemonTelemetryController controller(
            &h.server, &h.station, nullptr, nullptr, [&] { return nowMs; });
        controller.disableAutomaticSamplingForTest();
        h.server.setTelemetryEnabled(true);

        ThreadConnection oldOwner(h.station);
        NullRadioConnection* const oldConnection = oldOwner.connection.data();
        oldConnection->recordBytesReceived(125000); // 1 Mbps over 1 s.
        oldConnection->recordBytesSent(125000);
        oldOwner.moveToOwnerThread(false); // Leave stopped to hold the reply.
        QSemaphore oldReplyEmitted;
        const QMetaObject::Connection oldReplyGate = connect(
            oldConnection, &RadioConnection::telemetryObservationReady,
            oldConnection, [&] { oldReplyEmitted.release(); },
            Qt::DirectConnection);
        h.station.injectConnectionForTest(oldConnection);

        QSignalSpy samples(&h.client, &StationClient::telemetryReceived);
        h.connectClient(this);
        QTRY_VERIFY(h.client.telemetryAvailable());

        ThreadConnection newOwner(h.station);
        NullRadioConnection* const newConnection = newOwner.connection.data();
        newOwner.moveToOwnerThread(true);
        QVERIFY(QMetaObject::invokeMethod(
            newConnection, [newConnection] {
                newConnection->recordBytesReceived(250000); // 2 Mbps over 1 s.
                newConnection->recordBytesSent(375000);     // 3 Mbps over 1 s.
                newConnection->notePingSent();
            }, Qt::BlockingQueuedConnection));
        QTest::qWait(5);
        QVERIFY(QMetaObject::invokeMethod(
            newConnection, [newConnection] {
                newConnection->notePingReceived();
            }, Qt::BlockingQueuedConnection));
        QSignalSpy newReplies(newConnection,
                              &RadioConnection::telemetryObservationReady);

        // Let the old owner-thread read complete, but deliberately do not run
        // this thread's event loop yet. Its reply is now queued to the
        // controller and will arrive only after replacement.
        oldOwner.thread.start();
        QVERIFY(oldReplyEmitted.tryAcquire(1, 1000));
        QObject::disconnect(oldReplyGate);

        // Same Connected state means no model signal. sampleNow() must still
        // detect the pointer replacement, retire the old request and queue a
        // request to the new object's owning thread. The already-queued old
        // reply is rejected when this thread next processes events.
        h.station.injectConnectionForTest(newConnection);
        nowMs = 100;
        controller.sampleNow();
        QTRY_VERIFY(newReplies.count() >= 1);
        nowMs = 1000;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 2);
        const StationTelemetrySnapshot snapshot = lastSnapshot(samples);
        QVERIFY(snapshot.radio.connected);
        QVERIFY(snapshot.radio.rxMbps);
        QVERIFY(snapshot.radio.txMbps);
        QCOMPARE(*snapshot.radio.rxMbps, 2.0);
        QCOMPARE(*snapshot.radio.txMbps, 3.0);
        QVERIFY(snapshot.radio.rttMs);
        QVERIFY(snapshot.radio.rttAgeMs);
        QVERIFY(*snapshot.radio.rttAgeMs >= *snapshot.radio.rttMs);

        // Stop owner-thread replies and advance beyond three periods. The
        // last radio value becomes unavailable rather than being repeated as
        // a fresh sample indefinitely.
        newOwner.thread.quit();
        QVERIFY(newOwner.thread.wait(1000));
        nowMs = 5001;
        controller.sampleNow();
        QTRY_COMPARE(samples.count(), 3);
        const StationTelemetrySnapshot stale = lastSnapshot(samples);
        QVERIFY(stale.radio.connected);
        QVERIFY(!stale.radio.rxMbps);
        QVERIFY(!stale.radio.txMbps);
        QVERIFY(!stale.radio.rttMs);
        QVERIFY(!stale.radio.rttAgeMs);
        newOwner.thread.start();
    }

    void authenticatedLifecycleResetsSequenceAndStopsOldEpochPublication()
    {
        SessionHarness h;
        qint64 nowMs = 50;
        auto controller = std::make_unique<DaemonTelemetryController>(
            &h.server, &h.station, nullptr, nullptr, [&] { return nowMs; });
        controller->disableAutomaticSamplingForTest();
        h.server.setTelemetryEnabled(true);
        QSignalSpy samples(&h.client, &StationClient::telemetryReceived);

        h.connectClient(this);
        QTRY_VERIFY(h.client.telemetryAvailable());
        QVERIFY(controller->isCollecting());
        const quint64 oldServerEpoch = h.server.sessionEpoch();
        const quint32 oldClientEpoch = h.client.sessionEpoch();
        nowMs = 150;
        controller->sampleNow();
        QTRY_COMPARE(samples.count(), 1);
        QCOMPARE(lastSnapshot(samples).sequence, quint32{1});

        h.client.disconnectFromStation(QStringLiteral("end telemetry epoch"));
        QTRY_VERIFY(!h.server.telemetryAvailable());
        QTRY_VERIFY(!controller->isCollecting());
        nowMs = 1150;
        controller->sampleNow();
        QCOMPARE(samples.count(), 1);
        StationTelemetrySnapshot stale;
        stale.sequence = 2;
        stale.sampledElapsedMs = 1000;
        QVERIFY(!h.server.sendTelemetry(stale, oldServerEpoch));

        h.connectClient(this);
        QTRY_VERIFY(h.client.telemetryAvailable());
        QVERIFY(controller->isCollecting());
        QVERIFY(h.server.sessionEpoch() != oldServerEpoch);
        QVERIFY(h.client.sessionEpoch() != oldClientEpoch);
        nowMs = 1250;
        controller->sampleNow();
        QTRY_COMPARE(samples.count(), 2);
        const StationTelemetrySnapshot fresh = lastSnapshot(samples);
        QCOMPARE(fresh.sequence, quint32{1});
        QCOMPARE(fresh.sampledElapsedMs, qint64{100});

        // DaemonApp uses this exact ownership order: the collector is gone
        // before server/media/model teardown, leaving no live timer callback.
        QPointer<DaemonTelemetryController> guard(controller.get());
        controller.reset();
        QVERIFY(guard.isNull());
    }
};

QTEST_MAIN(TstDaemonTelemetry)
#include "tst_daemon_telemetry.moc"
