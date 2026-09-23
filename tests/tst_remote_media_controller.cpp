// no-port-check: NereusSDR-original. Authenticated GUI subscription lifecycle.
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include "core/AppSettings.h"
#include "core/AudioDeviceConfig.h"
#include "core/AudioEngine.h"
#include "core/ClarityController.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "core/session/media/RemoteSpectrumContext.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "core/session/media/WidebandDisplayContext.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/Ps3DisplayCodec.h"
#include "core/FFTEngine.h"
#include "core/StepAttenuatorController.h"
#include "gui/RemoteAudioStatus.h"
#include "gui/RemoteMediaController.h"
#include "gui/PanadapterStack.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/RemoteAudioSessionHarness.h"

using namespace NereusSDR;

class DisplayTransport final : public IMediaTransport {
public:
    explicit DisplayTransport(QObject* parent) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { return true; }
    void stop() override { active = false; }
    bool acceptDescription(const QString&, const QString&) override
    {
        if (!descriptionClock.isValid()) { descriptionClock.start(); }
        return true;
    }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray& packet) override {
        if (!active) { return false; }
        displayPackets.append(packet);
        if (other) { other->deliver(packet); }
        return true;
    }
    bool sendRtp(const QByteArray&) override { return active; }
    bool isReady() const override { return active; }
    std::optional<MediaTransportTelemetry> telemetry() const override { return traffic; }
    void activate() { active = true; emit ready(); }
    void deliver(const QByteArray& packet) { emit displayReceived(packet); }
    void failConnection(const QString& reason) { emit connectionFailed(reason); }
    void closeUnexpectedly() { active = false; emit closed(); }
    void reportGenericError(const QString& reason) { emit errorOccurred(reason); }
    bool active = false;
    // Started when Core's media description first reaches this side.
    QElapsedTimer descriptionClock;
    QPointer<DisplayTransport> other;
    QList<QByteArray> displayPackets;
    std::optional<MediaTransportTelemetry> traffic;
};

// A backend that refuses to start, optionally saying why first, as
// LibDataChannelMediaTransport::start does when the peer cannot be built.
class RefusingTransport final : public IMediaTransport {
public:
    RefusingTransport(QObject* parent, QString reason)
        : IMediaTransport(parent), m_reason(std::move(reason)) {}
    bool start(const StartOptions&) override
    {
        if (!m_reason.isEmpty()) { emit errorOccurred(m_reason); }
        return false;
    }
    void stop() override {}
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray&) override { return false; }
    bool sendRtp(const QByteArray&) override { return false; }
    bool isReady() const override { return false; }
private:
    QString m_reason;
};

// Core's side of a session that offers media and then never connects: its
// description reaches the GUI, nothing after it does.
class OfferingTransport final : public IMediaTransport {
public:
    explicit OfferingTransport(QObject* parent) : IMediaTransport(parent) {}
    bool start(const StartOptions& options) override
    {
        if (options.role == Role::Offerer) {
            QTimer::singleShot(0, this, [this] {
                emit localDescription(QStringLiteral("v=0\r\n"), QStringLiteral("offer"));
            });
        }
        return true;
    }
    void stop() override {}
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray&) override { return false; }
    bool sendRtp(const QByteArray&) override { return false; }
    bool isReady() const override { return false; }
};

QStringList g_remoteMediaMessages;
void captureRemoteMediaMessages(QtMsgType, const QMessageLogContext& context,
                                const QString& message)
{
    if (context.category && QByteArray(context.category) == "nereus.remote.media") {
        g_remoteMediaMessages.append(message);
    }
}

namespace {
class ClosingGuiControlTransport final : public Test::LoopbackTransport {
public:
    ClosingGuiControlTransport() : LoopbackTransport(QStringLiteral("client")) {}
    void sendText(const QByteArray& wire) override
    {
        if (!closeOnOp.isEmpty()
            && wire.contains(QByteArray("\"op\":\"") + closeOnOp + '"')) {
            closeOnOp.clear();
            if (beforeClose) { beforeClose(); }
            closeLink(QStringLiteral("test synchronous GUI send closure"));
            return;
        }
        LoopbackTransport::sendText(wire);
    }
    QByteArray closeOnOp;
    std::function<void()> beforeClose;
};

class HoldingAllocationResultTransport final : public Test::LoopbackTransport {
public:
    HoldingAllocationResultTransport() : LoopbackTransport(QStringLiteral("station")) {}
    void sendText(const QByteArray& wire) override
    {
        if (wire.contains("\"op\":\"allocation-result\"") && !passNext) {
            held.append(wire);
            return;
        }
        passNext = false;
        LoopbackTransport::sendText(wire);
    }
    void passNextAllocationResult() { passNext = true; }
    void releaseHeld()
    {
        const QList<QByteArray> messages = std::exchange(held, {});
        for (const QByteArray& wire : messages) { LoopbackTransport::sendText(wire); }
    }
    QList<QByteArray> held;
    bool passNext = false;
};

QJsonObject lastControl(const QSignalSpy& spy, const QString& op)
{
    for (auto it = spy.crbegin(); it != spy.crend(); ++it) {
        const QJsonObject control = it->at(0).toJsonObject();
        if (control.value(QStringLiteral("op")) == op) { return control; }
    }
    return {};
}
// budgetModePaintsWhenCoreGrantsFewerPixels: what happens to the pan that
// sized a shared engine.
constexpr int kStays = 0;
constexpr int kLeaves = 1;
constexpr int kLeavesCoreRefuses = 2;

int countControl(const QSignalSpy& spy, const QString& op)
{
    int count = 0;
    for (const auto& call : spy) {
        if (call.at(0).toJsonObject().value(QStringLiteral("op")) == op) { ++count; }
    }
    return count;
}
QList<QJsonObject> controlsFor(const QSignalSpy& spy, const QString& op)
{
    QList<QJsonObject> controls;
    for (const auto& call : spy) {
        const QJsonObject control = call.at(0).toJsonObject();
        if (control.value(QStringLiteral("op")) == op) { controls.append(control); }
    }
    return controls;
}

// The station's two-slice tone and this computer's speaker, each paced
// every 10 ms, as the real audio session test drives them.
struct PacedRemoteAudio {
    QTimer source;
    QTimer speaker;

    explicit PacedRemoteAudio(Test::RemoteAudioSessionHarness& h)
    {
        source.setInterval(10);
        source.setTimerType(Qt::PreciseTimer);
        QObject::connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        speaker.setInterval(10);
        speaker.setTimerType(Qt::PreciseTimer);
        QObject::connect(&speaker, &QTimer::timeout, &speaker, [&h] {
            h.remoteBus->render(Test::RemoteAudioSessionHarness::kFrames);
        });
        source.start();
        speaker.start();
    }
    void stop()
    {
        source.stop();
        speaker.stop();
    }
};

// Every audio status the controller announced, in order.
struct AudioStatusHistory {
    QList<RemoteAudioStatus> statuses;
    QMetaObject::Connection connection;

    explicit AudioStatusHistory(RemoteMediaController& media)
    {
        connection = QObject::connect(&media, &RemoteMediaController::audioStatusChanged,
                                      &media, [this, &media] {
            statuses.append(media.audioStatus());
        });
    }
    ~AudioStatusHistory() { QObject::disconnect(connection); }
    AudioStatusHistory(const AudioStatusHistory&) = delete;
    AudioStatusHistory& operator=(const AudioStatusHistory&) = delete;

    // Every status from the first one in `state` on is in `allowed`.
    bool onlyFromFirst(RemoteAudioStatus::State state,
                       const QList<RemoteAudioStatus::State>& allowed) const
    {
        const auto first = std::find_if(statuses.cbegin(), statuses.cend(),
            [state](const RemoteAudioStatus& status) { return status.state == state; });
        if (first == statuses.cend()) { return false; }
        return std::all_of(first, statuses.cend(), [&allowed](const RemoteAudioStatus& status) {
            return allowed.contains(status.state);
        });
    }
};

// The status poll the controller owns; it must run only while needed.
QTimer* audioStatusTimer(RemoteMediaController& media)
{
    return media.findChild<QTimer*>(QStringLiteral("remoteAudioStatusTimer"));
}

// The accepted context is off for `reason` and is not the context
// `generation` (0, never a real generation, matches any context).
bool acceptedOff(const RemoteMediaController& media, quint32 generation,
                 RemoteAudioOffReason reason)
{
    const std::optional<RemoteAudioContextMessage> context = media.acceptedAudioContext();
    return context && context->generation != generation && !context->enabled
        && context->offReason == reason;
}

// What the controller logs when this computer's speaker cannot be opened.
const char* const kSpeakerOpenFailedLog =
    "Remote audio playback failed: Remote audio requires a 48 kHz stereo speaker "
    "device with playback timing";

// What it logs when a playing speaker stops reporting timing: the worker's
// pacing check or its next write notices first, with the bounded detail.
QRegularExpression speakerTimingLostLog()
{
    return QRegularExpression(QStringLiteral(
        "^Remote audio playback failed: (Speaker device timing became unavailable"
        "|Could not write remote audio to the speaker device) \\[ageMs="));
}
} // namespace

class TestRemoteMediaController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const QString profile = QStringLiteral("remote-media-controller-%1")
                                    .arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        QCOMPARE(AppSettings::instance().filePath(), AppSettings::resolveSettingsPath(profile));
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void pureSignalChunksShareMediaWithoutChangingMessageLimit()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("station.settings"));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> coreMedia;
        DaemonMediaController core(&server, &station, nullptr,
            [&coreMedia](QObject* owner) -> IMediaTransport* {
                coreMedia = new DisplayTransport(owner);
                return coreMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> guiMedia;
        RemoteMediaController gui(&client, &remote, nullptr, nullptr,
            [&guiMedia](QObject* owner) -> IMediaTransport* {
                guiMedia = new DisplayTransport(owner);
                return guiMedia;
            });
        auto* stationLink = new Test::LoopbackTransport("station");
        auto* clientLink = new Test::LoopbackTransport("client");
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(coreMedia && guiMedia);
        coreMedia->other = guiMedia;
        guiMedia->other = coreMedia;
        coreMedia->activate();
        guiMedia->activate();
        QCOMPARE(client.capabilities().psDisplayVersion, 1);
        PureSignalSessionFacade* coreFacade = station.pureSignalFacade();
        PureSignalSessionFacade* guiFacade = remote.pureSignalFacade();
        QTRY_COMPARE(guiFacade->displayGeneration(), coreFacade->displayGeneration());
        Ps3Snapshot frame;
        frame.channelId = 3;
        frame.sessionGeneration = coreFacade->displayGeneration();
        frame.sequence = 1;
        frame.sampleCount = Ps3Snapshot::kMaxSampleCount;
        frame.correctionCount = Ps3Snapshot::kMaxCorrectionCount;
        frame.x.assign(frame.sampleCount, 0.5);
        frame.ym.assign(frame.sampleCount, 0.45);
        frame.yc.assign(frame.sampleCount, 1.0);
        frame.ys.assign(frame.sampleCount, 0.0);
        frame.xmCorrection.assign(frame.correctionCount, 0.6);
        frame.ymCorrection.assign(frame.correctionCount, 0.65);
        frame.xaCorrection.assign(frame.correctionCount, 0.7);
        frame.yaCorrection.assign(frame.correctionCount, 2.0);
        // Only the DSP sample source is synthetic. Authenticated control,
        // subscription, chunk scheduling, peer bounds and GUI assembly are real.
        emit coreFacade->displaySnapshotReady(frame);
        QCoreApplication::processEvents();
        QVERIFY(coreMedia->displayPackets.isEmpty());
        guiFacade->setAmpViewSubscribed(true);
        QTRY_VERIFY(coreFacade->remoteAmpViewSubscribed());
        emit coreFacade->displaySnapshotReady(frame);
        QTRY_VERIFY(guiFacade->displaySnapshot().has_value());
        const Ps3Snapshot received = *guiFacade->displaySnapshot();
        QCOMPARE(received.sequence, 1u);
        QCOMPARE(received.sampleCount, 4096);
        QCOMPARE(received.xaCorrection.at(0), 0.7);
        QCOMPARE(received.xmCorrection.at(0), 0.6);
        const QList<QByteArray> expected = Ps3DisplayCodec::encode(frame);
        QCOMPARE(coreMedia->displayPackets, expected);
        for (const QByteArray& packet : coreMedia->displayPackets) {
            QVERIFY(packet.size() <= 64 * 1024);
        }
        guiFacade->setAmpViewSubscribed(false);
        QTRY_VERIFY(!coreFacade->remoteAmpViewSubscribed());
        ++frame.sequence;
        emit coreFacade->displaySnapshotReady(frame);
        QCoreApplication::processEvents();
        QCOMPARE(coreMedia->displayPackets.size(), expected.size());
        client.disconnectFromStation(QStringLiteral("test completed"));
        QVERIFY(!guiFacade->displaySnapshot());
    }

    void preReadyTypedFailureRequestsRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        const quint32 epoch = client.sessionEpoch();

        // PeerFailed is transient connectivity even before ICE reaches ready.
        media->failConnection(QStringLiteral("media peer connection failed"));
        QTRY_COMPARE(recoveries.size(), 1);
        QCOMPARE(recoveries.constFirst().at(0).toUInt(), epoch);
    }

    void diagnosticConsumerMayDeleteControllerDuringRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        QPointer<RemoteMediaController> controller = new RemoteMediaController(
            &client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        int recoveries = 0;
        connect(controller, &RemoteMediaController::recoveryRequested,
                this, [&recoveries](quint32, const QString&) { ++recoveries; });
        connect(controller, &RemoteMediaController::errorOccurred,
                this, [controller](const QString&) { delete controller.data(); });
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        media->activate();

        media->closeUnexpectedly();
        QVERIFY(controller.isNull());
        QCOMPARE(recoveries, 0);
    }

    void establishedMediaCloseRequestsOneEpochScopedRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);

        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        media->activate();
        const quint32 epoch = client.sessionEpoch();

        media->closeUnexpectedly();
        QTRY_COMPARE(recoveries.size(), 1);
        QCOMPARE(recoveries.constFirst().at(0).toUInt(), epoch);
        QVERIFY(recoveries.constFirst().at(1).toString().contains(
            QStringLiteral("media"), Qt::CaseInsensitive));

        // A terminal failure may be followed by the backend's closed event.
        // The controller must request one full-session recovery, not two.
        if (media) {
            media->failConnection(QStringLiteral("media peer connection failed"));
            media->closeUnexpectedly();
        }
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 1);
    }

    void displayDropsAreCountedApartFromBytesAndReported()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        qint64 nowMs = 1'000;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            },
            [&nowMs] { return nowMs; });
        QCOMPARE(controller.displayMessagesDropped(), quint64(0));

        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        media->activate();

        g_remoteMediaMessages.clear();
        const QtMessageHandler previous = qInstallMessageHandler(captureRemoteMediaMessages);
        const auto restore = qScopeGuard([previous] { qInstallMessageHandler(previous); });
        const auto reports = [] {
            return g_remoteMediaMessages.filter(QStringLiteral("Remote display: skipped"));
        };

        MediaTransportTelemetry traffic;
        traffic.receivedDisplayPayloadBytes = 90'000;
        media->traffic = traffic;
        media->deliver(QByteArrayLiteral("not-a-display-frame"));
        QCOMPARE(controller.displayMessagesDropped(), quint64(0));
        QVERIFY(reports().isEmpty());

        // Drops are their own count; received bytes still count every arrival.
        traffic.displayMessagesDropped = 3;
        media->traffic = traffic;
        media->deliver(QByteArrayLiteral("not-a-display-frame"));
        QCOMPARE(controller.displayMessagesDropped(), quint64(3));
        QCOMPARE(controller.trafficTelemetry()->traffic.receivedDisplayPayloadBytes,
                 quint64(90'000));
        QCOMPARE(reports().size(), 1);
        QCOMPARE(reports().constLast(),
                 QStringLiteral("Remote display: skipped 3 late updates on this computer "
                                "to keep the picture current (3 this session)"));

        // At most one report every ten seconds.
        traffic.displayMessagesDropped = 5;
        media->traffic = traffic;
        nowMs += 1'000;
        media->deliver(QByteArrayLiteral("not-a-display-frame"));
        QCOMPARE(reports().size(), 1);
        nowMs += 10'000;
        media->deliver(QByteArrayLiteral("not-a-display-frame"));
        QCOMPARE(reports().size(), 2);
        QCOMPARE(reports().constLast(),
                 QStringLiteral("Remote display: skipped 2 late updates on this computer "
                                "to keep the picture current (5 this session)"));

        client.disconnectFromStation(QStringLiteral("test complete"));
        QTRY_COMPARE(controller.displayMessagesDropped(), quint64(0));
    }

    void deliberateSessionEndAndGenericMediaErrorDoNotRequestRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        auto connectSession = [&] {
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
            QTRY_VERIFY(client.isHandshakeComplete());
            QTRY_VERIFY(media);
            media->activate();
        };

        connectSession();
        media->reportGenericError(QStringLiteral("invalid media packet"));
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 0);

        client.disconnectFromStation(QStringLiteral("reset after generic error"));
        connectSession();
        QVERIFY(media && media->active);
        // This disconnect retires a live current media peer. Its local stop
        // must not be reclassified as an unexpected transport close.
        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 0);
    }

    // R-R3-28. Media that never reaches ready is bounded in two stages and
    // then enters the same epoch-scoped recovery a typed failure does.
    // Core's media description must arrive within one control heartbeat
    // interval; once it has, the pinned library's own slowest serial
    // failure report bounds the connection (review minor 1).
    void mediaThatNeverConnectsRequestsRecoveryAtTheDeadline()
    {
        // The derivations, checked against their named sources.
        QCOMPARE(RemoteMediaController::kMediaDescriptionDeadlineMs,
                 StationClient::kDefaultHeartbeatIntervalMs);
        // ICE 39.5 s + DTLS 31 s + SCTP 35 s; see the constant's derivation.
        QCOMPARE(RemoteMediaController::kMediaConnectDeadlineMs, 39'500 + 31'000 + 35'000);
        QCOMPARE(RemoteMediaController::kMediaEstablishmentDeadlineMs,
                 RemoteMediaController::kMediaDescriptionDeadlineMs
                     + RemoteMediaController::kMediaConnectDeadlineMs);

        constexpr int kDescriptionMs = 150;
        constexpr int kConnectMs = 1500;
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        // Core answers media only when a test case wants its description.
        bool coreOffers = false;
        std::unique_ptr<DaemonMediaController> core;
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            }, {}, 10'000, kDescriptionMs, kConnectMs);
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        QSignalSpy errors(&controller, &RemoteMediaController::errorOccurred);
        // Started before the session, so it can only overstate the time
        // since media started. The establish timer is a precise timer, so
        // it does not fire early; the 20 ms covers millisecond rounding and
        // the gap between these clocks starting and the timer being armed.
        QElapsedTimer sinceStart;
        const auto connectSession = [&] {
            media = nullptr;
            if (coreOffers && !core) {
                core = std::make_unique<DaemonMediaController>(&server, &station, nullptr,
                    [](QObject* owner) -> IMediaTransport* {
                        return new OfferingTransport(owner);
                    });
            }
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            sinceStart.start();
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
            QTRY_VERIFY(client.isHandshakeComplete());
            QTRY_VERIFY(media);
        };

        // Stage one: no description from Core within the first stage.
        connectSession();
        const quint32 silentEpoch = client.sessionEpoch();
        QTRY_COMPARE_WITH_TIMEOUT(recoveries.size(), 1, 5000);
        QVERIFY(sinceStart.elapsed() >= kDescriptionMs - 20);
        QVERIFY2(sinceStart.elapsed() < kConnectMs, qPrintable(QString::number(sinceStart.elapsed())));
        QCOMPARE(recoveries.constLast().at(0).toUInt(), silentEpoch);
        QCOMPARE(recoveries.constLast().at(1).toString(),
                 QStringLiteral("Core sent no station media description within 0.15 seconds"));
        QCOMPARE(errors.size(), 1);
        // One recovery per establishment, not one per elapsed deadline.
        QTest::qWait(kDescriptionMs * 2);
        QCOMPARE(recoveries.size(), 1);

        // Stage two: the description arrives, then nothing. The first stage
        // stands down and the connection gets the whole second stage,
        // counted from the description.
        client.disconnectFromStation(QStringLiteral("next case"));
        coreOffers = true;
        connectSession();
        const quint32 offeredEpoch = client.sessionEpoch();
        QTRY_VERIFY(media && media->descriptionClock.isValid());
        QTRY_COMPARE_WITH_TIMEOUT(recoveries.size(), 2, 5000);
        QVERIFY2(media->descriptionClock.elapsed() >= kConnectMs - 20,
                 qPrintable(QString::number(media->descriptionClock.elapsed())));
        QCOMPARE(recoveries.constLast().at(0).toUInt(), offeredEpoch);
        QCOMPARE(recoveries.constLast().at(1).toString(),
                 QStringLiteral("Station media did not connect within 1.5 seconds"));
        QCOMPARE(errors.size(), 2);
        QTest::qWait(kDescriptionMs * 2);
        QCOMPARE(recoveries.size(), 2);
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // R-R3-28. The deadline never pre-empts what the library reports, never
    // fires once media is ready, is cancelled by a deliberate end, and a
    // retired session's deadline cannot fire into a newer session.
    void establishmentDeadlineYieldsToReadyTypedFailureAndDisconnect()
    {
        constexpr int kDeadlineMs = 150;
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        // No Core media controller answers, so the first stage is the one
        // that runs.
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            }, {}, 10'000, kDeadlineMs, kDeadlineMs * 10);
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        QElapsedTimer sinceStart;
        auto connectSession = [&] {
            media = nullptr;
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            sinceStart.start();
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
            QTRY_VERIFY(client.isHandshakeComplete());
            QTRY_VERIFY(media);
        };

        // The library's own typed reason arrives first and is the one reported.
        connectSession();
        media->failConnection(QStringLiteral("media peer connection failed"));
        QTRY_COMPARE(recoveries.size(), 1);
        QCOMPARE(recoveries.constFirst().at(1).toString(),
                 QStringLiteral("media peer connection failed"));
        QTest::qWait(kDeadlineMs * 3);
        QCOMPARE(recoveries.size(), 1);

        // Media that becomes ready is established; the deadline stands down.
        client.disconnectFromStation(QStringLiteral("next case"));
        connectSession();
        media->activate();
        QTest::qWait(kDeadlineMs * 3);
        QCOMPARE(recoveries.size(), 1);

        // Manual Disconnect during the wait cancels it.
        client.disconnectFromStation(QStringLiteral("next case"));
        connectSession();
        QTest::qWait(kDeadlineMs / 2);
        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        QTest::qWait(kDeadlineMs * 3);
        QCOMPARE(recoveries.size(), 1);

        // A newer session's deadline is its own. The retired session's wait
        // is still half run when the newer one starts; were it left armed
        // it would fire about half a deadline into the newer session.
        connectSession();
        QTest::qWait(kDeadlineMs / 2);
        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        connectSession();
        const quint32 newer = client.sessionEpoch();
        QTRY_COMPARE_WITH_TIMEOUT(recoveries.size(), 2, 5000);
        // sinceStart restarted with the newer session, before its media.
        QVERIFY2(sinceStart.elapsed() >= kDeadlineMs - 20,
                 qPrintable(QString::number(sinceStart.elapsed())));
        QCOMPARE(recoveries.constLast().at(0).toUInt(), newer);
        QTest::qWait(kDeadlineMs * 3);
        QCOMPARE(recoveries.size(), 2);
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // R-R3-28, amended 2026-09-23. A start refusal is retried only when the
    // transport could not be built (the factory threw, or the transport
    // threw building its peer); every other refusal is permanent and keeps
    // stop-and-error with the reason shown and control left up.
    void backendStartRefusalRetriesOnlyWhenTheTransportCouldNotBeBuilt()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        enum class Build { ThrowsBuildingPeer, FactoryThrows, RefusesWithoutError, NoTransport };
        Build build = Build::ThrowsBuildingPeer;
        int transportsBuilt = 0;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&build, &transportsBuilt](QObject* owner) -> IMediaTransport* {
                ++transportsBuilt;
                switch (build) {
                case Build::ThrowsBuildingPeer:
                    // LibDataChannelMediaTransport::start's catch: the
                    // peer could not be built, reported, then refused.
                    return new RefusingTransport(owner,
                        QStringLiteral("could not create the peer connection"));
                case Build::FactoryThrows:
                    throw std::runtime_error("no transport");
                case Build::RefusesWithoutError:
                    // A precondition refusal, like an SSRC of zero.
                    return new RefusingTransport(owner, QString());
                case Build::NoTransport:
                    return nullptr;
                }
                return nullptr;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        QSignalSpy errors(&controller, &RemoteMediaController::errorOccurred);
        auto connectSession = [&] {
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
            QTRY_VERIFY(client.isHandshakeComplete());
        };

        // Transient: the transport threw while building its peer.
        connectSession();
        QTRY_COMPARE(recoveries.size(), 1);
        QCOMPARE(transportsBuilt, 1);
        QCOMPARE(recoveries.constLast().at(0).toUInt(), client.sessionEpoch());
        QCOMPARE(recoveries.constLast().at(1).toString(),
                 QStringLiteral("Station media could not start on this computer: "
                                "could not create the peer connection"));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.constLast().at(0).toString(), recoveries.constLast().at(1).toString());

        // Transient: the factory itself threw.
        client.disconnectFromStation(QStringLiteral("next case"));
        build = Build::FactoryThrows;
        connectSession();
        QTRY_COMPARE(recoveries.size(), 2);
        QCOMPARE(recoveries.constLast().at(1).toString(),
                 QStringLiteral("Station media could not start on this computer: "
                                "media transport factory failed"));
        QCOMPARE(errors.size(), 2);

        // Permanent: a refusal without an error, as for an SSRC of zero.
        // Stop and error, once, with the reason; control stays up.
        client.disconnectFromStation(QStringLiteral("next case"));
        build = Build::RefusesWithoutError;
        connectSession();
        QTRY_COMPARE(errors.size(), 3);
        QCOMPARE(errors.constLast().at(0).toString(),
                 QStringLiteral("Station media could not start on this computer"));
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 2);
        QVERIFY(client.isHandshakeComplete());
        QCOMPARE(controller.activeEndpointCount(), 0);

        // Permanent: the factory returned no transport.
        client.disconnectFromStation(QStringLiteral("next case"));
        build = Build::NoTransport;
        connectSession();
        QTRY_COMPARE(errors.size(), 4);
        QCOMPARE(errors.constLast().at(0).toString(),
                 QStringLiteral("Station media could not start on this computer: "
                                "media transport factory returned an invalid object"));
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 2);
        QVERIFY(client.isHandshakeComplete());
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void remoteCtunProjectionPreservesPreferenceWithoutEcho()
    {
        SpectrumWidget widget;
        widget.setCtunEnabled(true);
        QSignalSpy gestures(&widget, &SpectrumWidget::ctunEnabledChanged);
        QSignalSpy centres(&widget, &SpectrumWidget::centerChanged);
        widget.applyRemoteCtunState(false, false);
        QVERIFY(!widget.ctunAvailable());
        QVERIFY(!widget.ctunEnabled());
        QVERIFY(widget.ctunPreference());
        widget.setCtunEnabled(true); // An unsupported Core cannot pretend to pin.
        QVERIFY(!widget.ctunEnabled());
        widget.applyRemoteCtunState(true, true);
        QVERIFY(widget.ctunEnabled());
        QCOMPARE(gestures.size(), 0);
        QCOMPARE(centres.size(), 0);
    }

    void acceptedWidebandContextControlsRemoteZoomWithoutLocalDemand()
    {
        SpectrumWidget widget;
        widget.resize(500, 300);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.setConnectionState(ConnectionState::Connected);
        widget.setExtendedViewAllowed(true);
        widget.setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        widget.setWfUpdatePeriodMs(20);
        QSignalSpy localDemand(&widget, &SpectrumWidget::widebandExtensionStateChanged);

        SpectrumEndpointContext context;
        context.codec = {41, 1, -180, 0, 128, 128, 96};
        context.exactCentreHz = 14225000;
        context.exactSpanHz = 192000;
        context.wideCentreHz = context.exactCentreHz;
        context.wideSpanHz = 96000;
        context.wideband.available = true;
        context.wideband.active = false;
        context.wideband.physicalAdcIndex = 1;
        context.wideband.filterChainIndex = 0;
        context.wideband.adcRateHz = 4000000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);

        QVERIFY(widget.remoteWidebandAvailable());
        QVERIFY(!widget.remoteWidebandActive());
        QCOMPARE(widget.maxZoomOutBandwidthHz(), 2000000.0);
        QVERIFY(!widget.extendedMode());
        QCOMPARE(localDemand.size(), 0);

        DisplayCodecFrame frame;
        frame.context = context.codec;
        frame.traceDbm = QVector<float>(128, -80);
        frame.waterfallDbm = QVector<float>(128, -120);
        frame.wideDbm = QVector<float>(96, -105);
        frame.waterfallAdvance = true;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QTRY_COMPARE(widget.dssRowsPushedForTest(), 1);
        QCOMPARE(widget.dssNewestRowWideBandwidthForTest(), 0.096);

        // Subscription renewal retires live planes but keeps the accepted
        // availability and painted RF history while the gesture is in flight.
        widget.setDisplayWindowPreservingHistory(context.exactCentreHz, 1000000);
        widget.invalidateRemoteSpectrumFrame();
        QCOMPARE(widget.maxZoomOutBandwidthHz(), 2000000.0);
        QCOMPARE(widget.dssRowsPushedForTest(), 1);

        ++context.codec.contextGeneration;
        context.exactSpanHz = 1000000;
        context.wideband.active = true;
        context.wideband.sourceGeneration = 7;
        widget.setRemoteSpectrumContext(context, 14225000, 192000);
        QVERIFY(widget.remoteWidebandActive());
        QVERIFY(widget.extendedMode());
        QCOMPARE(widget.dssRowsPushedForTest(), 1);
        QCOMPARE(localDemand.size(), 0);

        frame.context = context.codec;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QTRY_COMPARE(widget.dssRowsPushedForTest(), 2);
        // The exact row may be composite; the optional wide row remains the
        // separately described DDC-only 3D history plane.
        QCOMPARE(widget.dssNewestRowWideBandwidthForTest(), 0.096);

        widget.setDisplayWindowPreservingHistory(context.exactCentreHz, 192000);
        widget.invalidateRemoteSpectrumFrame();
        ++context.codec.contextGeneration;
        context.exactSpanHz = 192000;
        context.wideband.active = false;
        context.wideband.sourceGeneration = 0;
        widget.setRemoteSpectrumContext(context, 14225000, 192000);
        QVERIFY(!widget.extendedMode());
        QCOMPARE(widget.dssRowsPushedForTest(), 2);
        QCOMPARE(localDemand.size(), 0);

        widget.clearRemoteSpectrum();
        QVERIFY(!widget.remoteWidebandAvailable());
        QVERIFY(!widget.remoteWidebandActive());
        QCOMPARE(widget.maxZoomOutBandwidthHz(), 192000.0);
        QVERIFY(widget.extendedViewAllowed());
        QCOMPARE(widget.dssRowsPushedForTest(), 0);
        QCOMPARE(localDemand.size(), 0);
    }

    void ctunWheelKeepsSourceAndDragMovesCoreCentre()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* sourceSlice = station.sliceById(sliceId);
        QVERIFY(sourceSlice);
        const int stream = sourceSlice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* first = stack.addPanadapter(QStringLiteral("first"));
        auto* cohost = stack.addPanadapter(QStringLiteral("cohost"));
        stack.setActivePan(QStringLiteral("first"));
        for (auto* applet : {first, cohost}) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
            applet->spectrumWidget()->setVfoFrequency(centre);
        }
        auto* widget = first->spectrumWidget();
        widget->setCtunEnabled(true);
        cohost->spectrumWidget()->setCtunEnabled(false);
        // This is the existing MainWindow click/wheel -> mirrored VFO path.
        connect(widget, &SpectrumWidget::frequencyClicked, &remote,
            [&remote, sliceId](double hz) { remote.sliceById(sliceId)->setFrequency(hz); });
        stack.resize(600, 700);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        const auto connectSession = [&] {
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
        };
        connectSession();
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QVector<float> iq(2048, 0.001f);
        const auto feed = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !widget->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(feed(), 5000);
        QTRY_VERIFY(sourceSlice->streamCtunPinned());
        QTRY_VERIFY(widget->ctunEnabled());
        QTRY_VERIFY(cohost->spectrumWidget()->ctunEnabled());
        const int contexts = countControl(inbound, QStringLiteral("context"));
        QSignalSpy centreChanges(&station, &RadioModel::streamCentreChanged);
        widget->frequencyClicked(centre + 100);
        QTRY_COMPARE(sourceSlice->frequency(), centre + 100);
        QCOMPARE(station.streamCentreHz(stream), centre);
        QCOMPARE(sourceSlice->shiftOffsetHz(), 100.0);
        QCOMPARE(centreChanges.size(), 0);
        QTest::qWait(150);
        QCOMPARE(countControl(inbound, QStringLiteral("context")), contexts);
        QVERIFY(feed());

        // Pan dragging applies the view then emits centerChanged. The plain
        // setter deliberately does not emit a user gesture.
        widget->setCenterFrequency(centre + 1000.25);
        widget->centerChanged(centre + 1000.25); // Fractional pixel -> whole-Hz DDC.
        QTRY_COMPARE(station.streamCentreHz(stream), centre + 1000);
        QCOMPARE(sourceSlice->frequency(), centre + 100);
        QCOMPARE(sourceSlice->shiftOffsetHz(), -900.0);
        QTRY_VERIFY_WITH_TIMEOUT(feed() && widget->ddcCenterFrequency() == centre + 1000, 5000);
        QVERIFY(countControl(inbound, QStringLiteral("context")) > contexts);
        QVERIFY(widget->ctunEnabled());

        // A cohost near the far edge makes this otherwise valid pan move
        // unsafe. Core refuses it; every affected view returns to Core truth.
        const int cohostId = station.addSlice();
        auto* cohostSlice = station.sliceById(cohostId);
        QVERIFY(cohostSlice);
        cohostSlice->setFrequency(centre + 80000);
        QCOMPARE(cohostSlice->streamIndex(), stream);
        QTRY_VERIFY(remote.sliceById(cohostId));
        // A selection followed immediately by a gesture precedes the next
        // subscription poll. Resolve the pan's current slice for both verbs.
        QSignalSpy pinResults(&client, &StationClient::streamCtunPinFinished);
        QSignalSpy centreResults(&client, &StationClient::streamCentreFinished);
        first->setActiveSliceIndex(cohostId);
        widget->ctunEnabledChanged(true);
        widget->centerChanged(centre + 1000);
        QTRY_VERIFY(!pinResults.isEmpty());
        QTRY_VERIFY(!centreResults.isEmpty());
        QCOMPARE(pinResults.first().at(0).toInt(), cohostId);
        QCOMPARE(centreResults.first().at(0).toInt(), cohostId);
        first->setActiveSliceIndex(sliceId);
        const double rejectedCentre = centre - 50000;
        widget->setCenterFrequency(rejectedCentre);
        widget->centerChanged(rejectedCentre);
        QTRY_VERIFY_WITH_TIMEOUT(feed()
            && std::abs(widget->centerFrequency() - (centre + 1000)) < 50, 5000);
        QCOMPARE(widget->ddcCenterFrequency(), centre + 1000);
        QCOMPARE(station.streamCentreHz(stream), centre + 1000);
        QCOMPARE(sourceSlice->frequency(), centre + 100);
        QCOMPARE(cohostSlice->frequency(), centre + 80000);
        station.removeSlice(cohostId);
        QTRY_VERIFY(!remote.sliceById(cohostId));

        widget->setCtunEnabled(false);
        QTRY_VERIFY(!sourceSlice->streamCtunPinned());
        QTRY_VERIFY(!cohost->spectrumWidget()->ctunEnabled());
        widget->frequencyClicked(centre + 2000);
        QTRY_COMPARE(station.streamCentreHz(stream), centre + 2000);
        QCOMPARE(sourceSlice->shiftOffsetHz(), 0.0);
        widget->setCtunEnabled(true);
        QTRY_VERIFY(sourceSlice->streamCtunPinned());
        client.disconnectFromStation(QStringLiteral("C-Tune reconnect test"));
        QTRY_VERIFY(!sourceSlice->streamCtunPinned());
        QVERIFY(!widget->ctunAvailable());
        QVERIFY(widget->ctunPreference());
        connectSession();
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_VERIFY_WITH_TIMEOUT(feed(), 5000);
        QTRY_VERIFY(sourceSlice->streamCtunPinned());
        QTRY_VERIFY(widget->ctunEnabled());

        // Retire and reuse stream 0 without an intervening empty GUI poll.
        // A stream index is reusable; its Core lifetime identity is not.
        const quint64 previousEpoch = sourceSlice->streamEpoch();
        const int spareId = station.addSlice();
        station.sliceById(spareId)->setFrequency(7100000);
        station.removeSlice(sliceId);
        const int replacementId = station.addSlice();
        auto* replacement = station.sliceById(replacementId);
        replacement->setFrequency(centre + 2000);
        QCOMPARE(replacement->streamIndex(), stream);
        QVERIFY(replacement->streamEpoch() != previousEpoch);
        QVERIFY(!replacement->streamCtunPinned());
        first->setActiveSliceIndex(replacementId);
        cohost->setActiveSliceIndex(replacementId);
        // Keep producing I/Q across retirement: an old painted frame may
        // still be visible before the new source/context is established.
        QTRY_VERIFY_WITH_TIMEOUT(feed() && replacement->streamCtunPinned(), 5000);
        QTRY_VERIFY(widget->ctunEnabled());
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void sharedWindowChangeAndRadioReconnectResumeBothPanes()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const bool hadWindow = appSettings.contains(QStringLiteral("DisplayFftWindow"));
        const bool hadFft = appSettings.contains(QStringLiteral("DisplayFftSize"));
        const QVariant savedWindow = appSettings.value(QStringLiteral("DisplayFftWindow"));
        const QVariant savedFft = appSettings.value(QStringLiteral("DisplayFftSize"));
        appSettings.setValue(QStringLiteral("DisplayFftWindow"), QString::number(int(WindowFunction::Hann)));
        appSettings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("4096"));
        const auto restore = qScopeGuard([&] {
            if (hadWindow) { appSettings.setValue(QStringLiteral("DisplayFftWindow"), savedWindow); }
            else { appSettings.remove(QStringLiteral("DisplayFftWindow")); }
            if (hadFft) { appSettings.setValue(QStringLiteral("DisplayFftSize"), savedFft); }
            else { appSettings.remove(QStringLiteral("DisplayFftSize")); }
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        StepAttenuatorController stationAttenuator(&station);
        stationAttenuator.setStepAttEnabled(true);
        stationAttenuator.setAttenuation(0);
        station.setStepAttController(&stationAttenuator);
        const auto detachStationAttenuator = qScopeGuard([&] {
            station.setStepAttController(nullptr);
        });
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* slice = station.sliceById(sliceId);
        QVERIFY(slice);
        const int stream = slice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        ClarityController clarity;
        remote.setClarityController(&clarity);
        const auto detachClarity = qScopeGuard([&] { remote.setClarityController(nullptr); });
        clarity.setEnabled(true);
        QSignalSpy liveFloors(&clarity, &ClarityController::noiseFloorChanged);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* first = stack.addPanadapter(QStringLiteral("first"));
        auto* second = stack.addPanadapter(QStringLiteral("second"));
        stack.setActivePan(QStringLiteral("first"));
        for (auto* applet : {first, second}) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
        }
        stack.resize(600, 700);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        // Feed real tagged ingress in bounded radio-sized chunks. Repeated
        // calls also wait for the asynchronous source configuration to finish.
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            // Keep the station-calibration matrix well below the codec's
            // 0 dBm ceiling; otherwise a positive offset can hide a shift.
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        const auto bothHaveFrames = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !first->spectrumWidget()->renderedPixels().isEmpty()
                && !second->spectrumWidget()->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!liveFloors.isEmpty(), 5000);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        // DaemonMediaController applies the station offset before reducing
        // and encoding. Exercise all authoritative receiver paths using the
        // same tagged I/Q, rather than relying on the remote client's local
        // DisplayCalOffset preference.
        const auto traceLevels = [&] {
            QVector<float> bins = first->spectrumWidget()->renderedPixels();
            std::sort(bins.begin(), bins.end());
            return qMakePair(bins.last(), bins.at(bins.size() / 2));
        };
        const auto feedAndAwaitOffset = [&](double expectedOffsetDb,
                                            const QPair<float, float>& referenceDbm) {
            return QTest::qWaitFor([&] {
                QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                    Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
                const float offset = static_cast<float>(expectedOffsetDb);
                const auto levels = traceLevels();
                return std::abs(levels.first - (referenceDbm.first + offset)) < 1.0f
                    && std::abs(levels.second - (referenceDbm.second + offset)) < 1.0f;
            }, 5000);
        };
        const auto baseTraceDbm = traceLevels();
        const double baseOffsetDb = station.rxMeterOffsetDb();
        stationAttenuator.setAttenuation(10);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto stepAttTraceDbm = traceLevels();
        stationAttenuator.setStepAttEnabled(false);
        stationAttenuator.setPreampMode(PreampMode::Off);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto preampOffTraceDbm = traceLevels();
        stationAttenuator.setPreampMode(PreampMode::On);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto preampOnTraceDbm = traceLevels();
        QVERIFY(std::abs(stepAttTraceDbm.first - baseTraceDbm.first) > 1.0f);
        QVERIFY(std::abs(preampOffTraceDbm.first - stepAttTraceDbm.first) > 1.0f);
        QVERIFY(std::abs(preampOnTraceDbm.first - preampOffTraceDbm.first) > 1.0f);
        outbound.clear();
        inbound.clear();
        appSettings.setValue(QStringLiteral("DisplayFftWindow"), QString::number(int(WindowFunction::BlackmanHarris4)));
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QCOMPARE(countControl(outbound, QStringLiteral("unsubscribe")), 2);
        // Both old users of the source must retire before either replacement.
        QStringList operations;
        for (const auto& call : outbound) {
            const QString op = call.at(0).toJsonObject().value(QStringLiteral("op")).toString();
            if (op == QStringLiteral("subscribe") || op == QStringLiteral("unsubscribe")) {
                operations.append(op);
            }
        }
        QCOMPARE(operations, QStringList({QStringLiteral("unsubscribe"), QStringLiteral("unsubscribe"),
                                         QStringLiteral("subscribe"), QStringLiteral("subscribe")}));
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        QTRY_COMPARE(countControl(inbound, QStringLiteral("context")), 2);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        station.setConnectionStateForTest(ConnectionState::LinkLost);
        QTRY_COMPARE(gui.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeEndpointCount(), 0);
        QVERIFY(first->spectrumWidget()->renderedPixels().isEmpty());
        QVERIFY(second->spectrumWidget()->renderedPixels().isEmpty());
        station.setConnectionStateForTest(ConnectionState::Disconnected);
        QCOMPARE(daemon.activeEndpointCount(), 0);
        station.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void synchronousControlClosureRetiresGuiBindings_data()
    {
        QTest::addColumn<QString>("trigger");
        QTest::newRow("pane removal") << QStringLiteral("remove");
        QTest::newRow("stack destruction") << QStringLiteral("destroy");
        QTest::newRow("radio disconnect") << QStringLiteral("disconnect");
        QTest::newRow("subscription renewal") << QStringLiteral("renew");
    }

    void synchronousControlClosureRetiresGuiBindings()
    {
        QFETCH(QString, trigger);
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto stack = std::make_unique<PanadapterStack>();
        stack->applyLayout(QStringLiteral("2v"),
                           {QStringLiteral("pan-0"), QStringLiteral("pan-1")});
        for (PanadapterApplet* applet : stack->allApplets()) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(
                station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
        }
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, stack.get(), nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new ClosingGuiControlTransport;
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(controller.activeEndpointCount(), 2);
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        int endpointCountAtClose = -1;
        clientLink->beforeClose = [&] { endpointCountAtClose = controller.activeEndpointCount(); };
        clientLink->closeOnOp = trigger == QLatin1String("renew") ? "subscribe" : "unsubscribe";
        if (trigger == QLatin1String("remove")) {
            stack->removePanadapter(QStringLiteral("pan-0"));
        } else if (trigger == QLatin1String("destroy")) {
            stack.reset();
        } else if (trigger == QLatin1String("disconnect")) {
            station.setConnectionStateForTest(ConnectionState::Disconnected);
        } else {
            SpectrumWidget* widget = stack->spectrum(QStringLiteral("pan-0"));
            widget->setDisplayWindowPreservingHistory(widget->centerFrequency(), 24000);
        }
        QTRY_VERIFY(endpointCountAtClose >= 0);
        // A removal releases its binding before the send can re-enter stop().
        QCOMPARE(endpointCountAtClose, trigger == QLatin1String("renew") ? 2 : 1);
        QTRY_VERIFY(!client.mediaAvailable());
        QCOMPARE(controller.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeSourceCount(), 0);
    }

    void logicalPanLifecycleKeepsMediaAcrossReparentAndRetiresItOnRemoval()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        SliceModel* stationSlice = station.sliceById(sliceId);
        QVERIFY(stationSlice);
        const int stream = stationSlice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto stack = std::make_unique<PanadapterStack>();
        PanadapterApplet* first = stack->addPanadapter(QStringLiteral("pan-0"));
        first->setActiveSliceIndex(sliceId);
        SpectrumWidget* firstWidget = first->spectrumWidget();
        firstWidget->setDisplayWindowPreservingHistory(centre, 48000);
        firstWidget->setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        firstWidget->setWfUpdatePeriodMs(20);
        stack->resize(600, 400);
        stack->show();
        QVERIFY(QTest::qWaitForWindowExposed(stack.get()));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, stack.get(), nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy controls(&server, &StationServer::mediaControlReceived);
        QSignalSpy frames(&controller, &RemoteMediaController::displayFrameReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sinkMedia->other = sourceMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(daemon.activeEndpointCount(), 1);
        QTRY_COMPARE(daemon.activeSourceCount(), 1);
        QTRY_COMPARE(controller.activeEndpointCount(), 1);
        QVERIFY(!client.remoteDisplayBudgetLimits());
        QVERIFY(first->remoteDisplayStatus().isEmpty());
        const QJsonObject firstSubscription = lastControl(controls, QStringLiteral("subscribe"));
        const quint32 firstEndpoint = quint32(firstSubscription.value(QStringLiteral("endpointId")).toDouble());
        QVERIFY(firstEndpoint != 0);

        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        const auto feedFirst = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !firstWidget->renderedPixels().isEmpty()
                && firstWidget->dssRowsPushedForTest() > 0;
        };
        QTRY_VERIFY_WITH_TIMEOUT(feedFirst(), 5000);
        QVERIFY(first->remoteDisplayStatus().isEmpty());
        const int rowsBeforeReparent = firstWidget->dssRowsPushedForTest();
        const QByteArray stalePacket = sourceMedia->displayPackets.constLast();
        QVERIFY(!stalePacket.isEmpty());

        QTimer* subscriptionTimer = nullptr;
        for (QTimer* timer : controller.findChildren<QTimer*>()) {
            if (timer->interval() == 100) {
                QVERIFY(!subscriptionTimer);
                subscriptionTimer = timer;
            }
        }
        QVERIFY(subscriptionTimer);
        const auto fireSubscriptionTimer = [&] {
            QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        };
        const int subscriptionsBeforeReparent = countControl(controls, QStringLiteral("subscribe"));
        const int unsubscriptionsBeforeReparent = countControl(controls, QStringLiteral("unsubscribe"));

        stack->floatPanadapter(QStringLiteral("pan-0"));
        QVERIFY(!firstWidget->isVisible());
        fireSubscriptionTimer();
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), subscriptionsBeforeReparent);
        QCOMPARE(countControl(controls, QStringLiteral("unsubscribe")), unsubscriptionsBeforeReparent);
        QCOMPARE(controller.activeEndpointCount(), 1);
        QCOMPARE(daemon.activeEndpointCount(), 1);
        QCOMPARE(firstWidget->dssRowsPushedForTest(), rowsBeforeReparent);
        QTRY_VERIFY(firstWidget->isVisible());

        stack->dockPanadapter(QStringLiteral("pan-0"));
        QVERIFY(!firstWidget->isVisible());
        fireSubscriptionTimer();
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), subscriptionsBeforeReparent);
        QCOMPARE(countControl(controls, QStringLiteral("unsubscribe")), unsubscriptionsBeforeReparent);
        QCOMPARE(controller.activeEndpointCount(), 1);
        QCOMPARE(daemon.activeEndpointCount(), 1);
        QCOMPARE(firstWidget->dssRowsPushedForTest(), rowsBeforeReparent);
        QTRY_VERIFY(firstWidget->isVisible());

        // Rebuilding a layout around the same logical pan may renew runtime
        // geometry, but it must retain the endpoint identity and its history.
        stack->applyLayout(QStringLiteral("1"), {QStringLiteral("pan-0")});
        QTRY_VERIFY(firstWidget->isVisible());
        QTRY_COMPARE(controller.activeEndpointCount(), 1);
        QCOMPARE(countControl(controls, QStringLiteral("unsubscribe")), unsubscriptionsBeforeReparent);
        for (const auto& call : controls) {
            const QJsonObject control = call.at(0).toJsonObject();
            if (control.value(QStringLiteral("op")) == QLatin1String("subscribe")) {
                QCOMPARE(quint32(control.value(QStringLiteral("endpointId")).toDouble()), firstEndpoint);
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(feedFirst()
            && firstWidget->dssRowsPushedForTest() > rowsBeforeReparent, 5000);

        // A layout shrink is a real retirement, unlike the reparenting above.
        PanadapterApplet* second = stack->addPanadapter(QStringLiteral("pan-1"));
        second->setActiveSliceIndex(sliceId);
        second->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
        stack->applyLayout(QStringLiteral("2v"),
                           {QStringLiteral("pan-0"), QStringLiteral("pan-1")});
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        QTRY_COMPARE(daemon.activeSourceCount(), 1);
        QPointer<PanadapterApplet> retiredByLayout(second);
        QPointer<SpectrumWidget> retiredWidgetByLayout(second->spectrumWidget());
        stack->applyLayout(QStringLiteral("1"), {QStringLiteral("pan-0")});
        QTRY_COMPARE(daemon.activeEndpointCount(), 1);
        QTRY_COMPARE(daemon.activeSourceCount(), 1);
        QTRY_COMPARE(controller.activeEndpointCount(), 1);
        QTRY_COMPARE(countControl(controls, QStringLiteral("unsubscribe")),
                     unsubscriptionsBeforeReparent + 1);
        QTRY_VERIFY(retiredByLayout.isNull());
        QTRY_VERIFY(retiredWidgetByLayout.isNull());

        // Last logical pan removal releases its endpoint and source. Do not
        // touch the pointers after this deferred QObject destruction path.
        QPointer<PanadapterApplet> retiredFirst(first);
        QPointer<SpectrumWidget> retiredFirstWidget(firstWidget);
        stack->removePanadapter(QStringLiteral("pan-0"));
        QTRY_COMPARE(daemon.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeSourceCount(), 0);
        QTRY_COMPARE(controller.activeEndpointCount(), 0);
        QTRY_COMPARE(countControl(controls, QStringLiteral("unsubscribe")),
                     unsubscriptionsBeforeReparent + 2);
        QTRY_VERIFY(retiredFirst.isNull());
        QTRY_VERIFY(retiredFirstWidget.isNull());

        // Reusing the pan id creates a fresh endpoint. A retained packet for
        // the retired id must not be decoded into that replacement widget.
        PanadapterApplet* replacement = stack->addPanadapter(QStringLiteral("pan-0"));
        replacement->setActiveSliceIndex(sliceId);
        SpectrumWidget* replacementWidget = replacement->spectrumWidget();
        replacementWidget->setDisplayWindowPreservingHistory(centre, 48000);
        replacementWidget->setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        replacementWidget->setWfUpdatePeriodMs(20);
        replacement->show();
        QTRY_COMPARE(daemon.activeEndpointCount(), 1);
        QTRY_COMPARE(daemon.activeSourceCount(), 1);
        const QJsonObject replacementSubscription = lastControl(controls, QStringLiteral("subscribe"));
        const quint32 replacementEndpoint = quint32(
            replacementSubscription.value(QStringLiteral("endpointId")).toDouble());
        QVERIFY(replacementEndpoint != 0);
        QVERIFY(replacementEndpoint != firstEndpoint);
        const int framesBeforeStaleDelivery = frames.size();
        sinkMedia->deliver(stalePacket);
        QCOMPARE(frames.size(), framesBeforeStaleDelivery);
        QVERIFY(replacementWidget->renderedPixels().isEmpty());
        const auto feedReplacement = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !replacementWidget->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(feedReplacement(), 5000);

        // Controller outlives the stack. Stack destruction must retire its
        // current logical pan rather than retaining a dangling binding.
        QPointer<PanadapterStack> destroyedStack(stack.get());
        QPointer<SpectrumWidget> destroyedWidget(replacementWidget);
        stack.reset();
        QTRY_VERIFY(destroyedStack.isNull());
        QTRY_VERIFY(destroyedWidget.isNull());
        QTRY_COMPARE(controller.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeSourceCount(), 0);
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void staleZeroAllocationResultCannotRetireNewerReservation()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 1}));
        // Refused row: Core's state moves ahead of the GUI's view. When the
        // survivor asks again, Core already holds PureSignal display, which
        // the GUI has not heard of yet. Connected before Core's own handler,
        // so it runs first.
        const auto armedEndpoint = std::make_shared<quint32>(0);
        connect(&server, &StationServer::mediaControlReceived, &station,
            [&station, armedEndpoint](const QJsonObject& control) {
                if (*armedEndpoint != 0
                    && control.value(QStringLiteral("op")) == QLatin1String("subscribe")
                    && quint32(control.value(QStringLiteral("endpointId")).toDouble())
                        == *armedEndpoint) {
                    *armedEndpoint = 0;
                    station.pureSignalFacade()->setRemoteAmpViewSubscribed(true);
                }
            });
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        PanadapterApplet* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(sliceId);
        SpectrumWidget* widget = applet->spectrumWidget();
        widget->setDisplayWindowPreservingHistory(
            station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QTRY_COMPARE(countControl(inbound, QStringLiteral("allocation-result")), 1);
        const QJsonObject first = lastControl(outbound, QStringLiteral("subscribe"));
        const quint32 firstRevision = quint32(first.value(QStringLiteral("revision")).toDouble());
        const quint32 endpointId = quint32(first.value(QStringLiteral("endpointId")).toDouble());

        widget->setCenterFrequency(widget->centerFrequency() + 500);
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QTRY_COMPARE(countControl(inbound, QStringLiteral("allocation-result")), 2);
        const QJsonObject second = lastControl(outbound, QStringLiteral("subscribe"));
        QVERIFY(quint32(second.value(QStringLiteral("revision")).toDouble()) > firstRevision);
        QVERIFY(applet->remoteDisplayStatus().startsWith(QStringLiteral("Display target")));

        const QJsonObject stale{
            {QStringLiteral("op"), QStringLiteral("allocation-result")},
            {QStringLiteral("connectionId"), first.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
            {QStringLiteral("revision"), static_cast<qint64>(firstRevision)},
            {QStringLiteral("accepted"), false},
            {QStringLiteral("reason"), QStringLiteral("delayed refusal")},
            {QStringLiteral("budgetGeneration"), 1},
            {QStringLiteral("acceptedRevision"), 0},
            {QStringLiteral("applicationBytesPerSecond"), 0},
            {QStringLiteral("spectrumSampleUnitsPerSecond"), 0},
            {QStringLiteral("messagesPerSecond"), 0}};
        QVERIFY(server.sendMediaControl(stale, server.mediaSessionEpoch()));
        QTRY_COMPARE(countControl(inbound, QStringLiteral("allocation-result")), 3);

        QTimer* subscriptionTimer = nullptr;
        for (QTimer* timer : controller.findChildren<QTimer*>()) {
            if (timer->interval() == 100) { subscriptionTimer = timer; break; }
        }
        QVERIFY(subscriptionTimer);
        QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QCOMPARE(controller.activeEndpointCount(), 1);
        QVERIFY(applet->remoteDisplayStatus().startsWith(QStringLiteral("Display target")));
    }

    void missingAllocationAcknowledgementStallsThenLateResultReconciles()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 1}));
        // Refused row: Core's state moves ahead of the GUI's view. When the
        // survivor asks again, Core already holds PureSignal display, which
        // the GUI has not heard of yet. Connected before Core's own handler,
        // so it runs first.
        const auto armedEndpoint = std::make_shared<quint32>(0);
        connect(&server, &StationServer::mediaControlReceived, &station,
            [&station, armedEndpoint](const QJsonObject& control) {
                if (*armedEndpoint != 0
                    && control.value(QStringLiteral("op")) == QLatin1String("subscribe")
                    && quint32(control.value(QStringLiteral("endpointId")).toDouble())
                        == *armedEndpoint) {
                    *armedEndpoint = 0;
                    station.pureSignalFacade()->setRemoteAmpViewSubscribed(true);
                }
            });
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        PanadapterApplet* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(sliceId);
        applet->spectrumWidget()->setDisplayWindowPreservingHistory(
            station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
        qint64 nowMs = 0;
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            }, [&nowMs] { return nowMs; }, 10'000);
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        auto* stationLink = new HoldingAllocationResultTransport;
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QTRY_COMPARE(stationLink->held.size(), 1);
        QVERIFY(applet->remoteDisplayStatus().contains(QStringLiteral("pending")));

        QTimer* subscriptionTimer = nullptr;
        for (QTimer* timer : controller.findChildren<QTimer*>()) {
            if (timer->interval() == 100) { subscriptionTimer = timer; break; }
        }
        QVERIFY(subscriptionTimer);
        nowMs = 10'000;
        QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QVERIFY(applet->remoteDisplayStatus().contains(QStringLiteral("stalled")));
        QVERIFY(client.mediaAvailable());

        stationLink->releaseHeld();
        QTRY_VERIFY(applet->remoteDisplayStatus().startsWith(QStringLiteral("Display target")));
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QCOMPARE(controller.activeEndpointCount(), 1);
    }

    void budgetFocusSwapReducesBeforeGrowthAndRestoresOnRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const bool hadFps = appSettings.contains(QStringLiteral("DisplaySpectrumFps"));
        const QVariant savedFps = appSettings.value(QStringLiteral("DisplaySpectrumFps"));
        appSettings.setValue(QStringLiteral("DisplaySpectrumFps"), QStringLiteral("30"));
        const auto restoreFps = qScopeGuard([&] {
            if (hadFps) { appSettings.setValue(QStringLiteral("DisplaySpectrumFps"), savedFps); }
            else { appSettings.remove(QStringLiteral("DisplaySpectrumFps")); }
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        const QStringList panIds{QStringLiteral("pan-0"), QStringLiteral("pan-1"),
                                 QStringLiteral("pan-2"), QStringLiteral("pan-3")};
        stack.applyLayout(QStringLiteral("2x2"), panIds);
        for (PanadapterApplet* applet : stack.allApplets()) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(
                station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
            applet->spectrumWidget()->setWfUpdatePeriodMs(20);
        }
        stack.resize(1600, 900);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        stack.setActivePan(QStringLiteral("pan-0"));
        QList<DisplayBudgetCharge> constrainedCharges;
        for (PanadapterApplet* applet : stack.allApplets()) {
            const int requestedPixels = qBound(
                1, applet->spectrumWidget()->width()
                    - applet->spectrumWidget()->reservedRightEdgeWidth(),
                SpectrumEndpoint::kMaxPixels);
            const int fps = applet->panId() == QStringLiteral("pan-0") ? 30 : 10;
            const auto cost = spectrumDisplayCost(requestedPixels, fps, false);
            QVERIFY(cost.has_value());
            constrainedCharges.append(cost->charge);
        }
        const auto constrained = sumDisplayCharges(constrainedCharges);
        QVERIFY(constrained.has_value());

        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({constrained->applicationBytesPerSecond,
                                               constrained->spectrumSampleUnitsPerSecond, 1}));
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        auto* stationLink = new HoldingAllocationResultTransport;
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 4);
        QTRY_COMPARE(stationLink->held.size(), 4);
        stationLink->releaseHeld();
        QTRY_VERIFY([&] {
            for (PanadapterApplet* applet : stack.allApplets()) {
                if (!applet->remoteDisplayStatus().startsWith(QStringLiteral("Display target"))) {
                    return false;
                }
            }
            return true;
        }());

        const QList<QJsonObject> initial = controlsFor(outbound, QStringLiteral("subscribe"));
        const auto activeInitial = std::find_if(initial.cbegin(), initial.cend(),
            [](const QJsonObject& control) {
                return control.value(QStringLiteral("fps")).toInt() == 30;
            });
        QVERIFY(activeInitial != initial.cend());
        const quint32 oldActiveEndpoint = quint32(
            activeInitial->value(QStringLiteral("endpointId")).toInteger());

        stack.setActivePan(QStringLiteral("pan-1"));
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 5);
        QTRY_COMPARE(stationLink->held.size(), 1);
        const QJsonObject reduction = lastControl(outbound, QStringLiteral("subscribe"));
        QCOMPARE(quint32(reduction.value(QStringLiteral("endpointId")).toInteger()),
                 oldActiveEndpoint);
        QCOMPARE(reduction.value(QStringLiteral("fps")).toInt(), 10);
        QTimer* subscriptionTimer = nullptr;
        for (QTimer* timer : controller.findChildren<QTimer*>()) {
            if (timer->interval() == 100) { subscriptionTimer = timer; break; }
        }
        QVERIFY(subscriptionTimer);
        QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), 5);

        stationLink->releaseHeld();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 6);
        QTRY_COMPARE(stationLink->held.size(), 1);
        const QJsonObject growth = lastControl(outbound, QStringLiteral("subscribe"));
        QVERIFY(quint32(growth.value(QStringLiteral("endpointId")).toInteger())
                != oldActiveEndpoint);
        QCOMPARE(growth.value(QStringLiteral("fps")).toInt(), 30);
        stationLink->releaseHeld();

        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 2}));
        QTRY_VERIFY(countControl(outbound, QStringLiteral("subscribe")) > 6);
        QTRY_VERIFY(!stationLink->held.isEmpty());
        stationLink->releaseHeld();
        QTRY_VERIFY([&] {
            for (PanadapterApplet* applet : stack.allApplets()) {
                const QString status = applet->remoteDisplayStatus();
                if (!status.startsWith(QStringLiteral("Display target"))
                    || status.contains(QStringLiteral("requested"))) {
                    return false;
                }
            }
            return true;
        }());
    }

    // R-R3-08/37: a Core-busy cut slows the background pans first and leaves
    // the active pan alone; each reduced pan carries the reason in its state
    // and says so in its status line. Restore clears both.
    void coreBusyCutReducesBackgroundPansFirstAndRestoreClearsIt()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const bool hadFps = appSettings.contains(QStringLiteral("DisplaySpectrumFps"));
        const QVariant savedFps = appSettings.value(QStringLiteral("DisplaySpectrumFps"));
        appSettings.setValue(QStringLiteral("DisplaySpectrumFps"), QStringLiteral("30"));
        const auto restoreFps = qScopeGuard([&] {
            if (hadFps) { appSettings.setValue(QStringLiteral("DisplaySpectrumFps"), savedFps); }
            else { appSettings.remove(QStringLiteral("DisplaySpectrumFps")); }
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        const QStringList panIds{QStringLiteral("pan-0"), QStringLiteral("pan-1"),
                                 QStringLiteral("pan-2"), QStringLiteral("pan-3")};
        stack.applyLayout(QStringLiteral("2x2"), panIds);
        for (PanadapterApplet* applet : stack.allApplets()) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(
                station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
            applet->spectrumWidget()->setWfUpdatePeriodMs(20);
        }
        stack.resize(1600, 900);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        stack.setActivePan(QStringLiteral("pan-0"));
        // Exactly the active pan at 30 fps and three background pans at the
        // 10 fps floor.
        QList<DisplayBudgetCharge> cutCharges;
        for (PanadapterApplet* applet : stack.allApplets()) {
            const int requestedPixels = qBound(
                1, applet->spectrumWidget()->width()
                    - applet->spectrumWidget()->reservedRightEdgeWidth(),
                SpectrumEndpoint::kMaxPixels);
            const int fps = applet->panId() == QStringLiteral("pan-0") ? 30 : 10;
            const auto cost = spectrumDisplayCost(requestedPixels, fps, false);
            QVERIFY(cost.has_value());
            cutCharges.append(cost->charge);
        }
        const auto cut = sumDisplayCharges(cutCharges);
        QVERIFY(cut.has_value());

        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 1}));
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 4);

        const auto statusOf = [&stack](const QString& panId) {
            for (PanadapterApplet* applet : stack.allApplets()) {
                if (applet->panId() == panId) { return applet->remoteDisplayStatus(); }
            }
            return QString();
        };
        const auto allAtRequestedQuality = [&] {
            for (const QString& panId : panIds) {
                const QString status = statusOf(panId);
                if (!status.startsWith(QStringLiteral("Display target"))
                    || status.contains(QStringLiteral("requested"))
                    || controller.panDisplayBudgetReason(panId) != DisplayBudgetReason::None) {
                    return false;
                }
            }
            return true;
        };
        QTRY_VERIFY(allAtRequestedQuality());

        QVERIFY(server.setDisplayBudgetLimits(
            {cut->applicationBytesPerSecond, cut->spectrumSampleUnitsPerSecond, 2},
            DisplayBudgetReason::CoreBusy));
        QTRY_COMPARE(client.remoteDisplayBudgetReason(), DisplayBudgetReason::CoreBusy);
        QTRY_VERIFY([&] {
            for (const QString& panId : panIds) {
                const QString status = statusOf(panId);
                const bool active = panId == QStringLiteral("pan-0");
                if (active) {
                    if (!status.contains(QStringLiteral("@ 30 fps"))
                        || status.contains(QStringLiteral("requested"))
                        || controller.panDisplayBudgetReason(panId)
                            != DisplayBudgetReason::None) {
                        return false;
                    }
                } else if (!status.contains(QStringLiteral("@ 10 fps"))
                           || !status.contains(QStringLiteral("; Core busy)"))
                           || controller.panDisplayBudgetReason(panId)
                               != DisplayBudgetReason::CoreBusy) {
                    return false;
                }
            }
            return true;
        }());
        // Three reductions, all background pans to 10 fps; the active pan
        // was never asked to change.
        const QList<QJsonObject> subscribes = controlsFor(outbound, QStringLiteral("subscribe"));
        QCOMPARE(subscribes.size(), 7);
        for (int i = 4; i < subscribes.size(); ++i) {
            QCOMPARE(subscribes.at(i).value(QStringLiteral("fps")).toInt(), 10);
        }

        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 3},
                                              DisplayBudgetReason::None));
        QTRY_COMPARE(client.remoteDisplayBudgetReason(), DisplayBudgetReason::None);
        QTRY_VERIFY(allAtRequestedQuality());
    }

    void budgetRetirementWaitsForPendingSubscribeThenReleasesReservation()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 1}));
        // Refused row: Core's state moves ahead of the GUI's view. When the
        // survivor asks again, Core already holds PureSignal display, which
        // the GUI has not heard of yet. Connected before Core's own handler,
        // so it runs first.
        const auto armedEndpoint = std::make_shared<quint32>(0);
        connect(&server, &StationServer::mediaControlReceived, &station,
            [&station, armedEndpoint](const QJsonObject& control) {
                if (*armedEndpoint != 0
                    && control.value(QStringLiteral("op")) == QLatin1String("subscribe")
                    && quint32(control.value(QStringLiteral("endpointId")).toDouble())
                        == *armedEndpoint) {
                    *armedEndpoint = 0;
                    station.pureSignalFacade()->setRemoteAmpViewSubscribed(true);
                }
            });
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        PanadapterApplet* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(sliceId);
        applet->spectrumWidget()->setDisplayWindowPreservingHistory(
            station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        auto* stationLink = new HoldingAllocationResultTransport;
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QTRY_COMPARE(stationLink->held.size(), 1);
        QCOMPARE(controller.activeEndpointCount(), 1);

        QPointer<PanadapterApplet> retiredApplet(applet);
        stack.removePanadapter(QStringLiteral("pan-0"));
        QTRY_VERIFY(retiredApplet.isNull());
        QCOMPARE(countControl(outbound, QStringLiteral("unsubscribe")), 0);
        QCOMPARE(controller.activeEndpointCount(), 1);

        stationLink->releaseHeld();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("unsubscribe")), 1);
        QTRY_COMPARE(stationLink->held.size(), 1);
        QCOMPARE(controller.activeEndpointCount(), 1);

        stationLink->releaseHeld();
        QTRY_COMPARE(controller.activeEndpointCount(), 0);
        QCOMPARE(countControl(outbound, QStringLiteral("unsubscribe")), 1);
    }

    void matchingSourceRetirementClearsAcceptedReservationDuringPendingUpdate()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 1}));
        // Refused row: Core's state moves ahead of the GUI's view. When the
        // survivor asks again, Core already holds PureSignal display, which
        // the GUI has not heard of yet. Connected before Core's own handler,
        // so it runs first.
        const auto armedEndpoint = std::make_shared<quint32>(0);
        connect(&server, &StationServer::mediaControlReceived, &station,
            [&station, armedEndpoint](const QJsonObject& control) {
                if (*armedEndpoint != 0
                    && control.value(QStringLiteral("op")) == QLatin1String("subscribe")
                    && quint32(control.value(QStringLiteral("endpointId")).toDouble())
                        == *armedEndpoint) {
                    *armedEndpoint = 0;
                    station.pureSignalFacade()->setRemoteAmpViewSubscribed(true);
                }
            });
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        PanadapterApplet* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(sliceId);
        SpectrumWidget* widget = applet->spectrumWidget();
        widget->setDisplayWindowPreservingHistory(
            station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        auto* stationLink = new HoldingAllocationResultTransport;
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QTRY_COMPARE(stationLink->held.size(), 1);
        stationLink->releaseHeld();
        QTRY_VERIFY(applet->remoteDisplayStatus().startsWith(QStringLiteral("Display target")));

        widget->setCenterFrequency(widget->centerFrequency() + 500);
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QTRY_COMPARE(stationLink->held.size(), 1);
        const QJsonObject pending = lastControl(outbound, QStringLiteral("subscribe"));
        const int subscriptions = countControl(outbound, QStringLiteral("subscribe"));
        const QJsonObject retired{
            {QStringLiteral("op"), QStringLiteral("allocation-result")},
            {QStringLiteral("connectionId"), pending.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), pending.value(QStringLiteral("endpointId"))},
            {QStringLiteral("revision"), pending.value(QStringLiteral("revision"))},
            {QStringLiteral("accepted"), false},
            {QStringLiteral("reason"), QStringLiteral("source retired")},
            {QStringLiteral("budgetGeneration"), 1},
            {QStringLiteral("acceptedRevision"), 0},
            {QStringLiteral("applicationBytesPerSecond"), 0},
            {QStringLiteral("spectrumSampleUnitsPerSecond"), 0},
            {QStringLiteral("messagesPerSecond"), 0}};
        stationLink->passNextAllocationResult();
        QVERIFY(server.sendMediaControl(retired, server.mediaSessionEpoch()));
        QTRY_VERIFY(applet->remoteDisplayStatus().contains(QStringLiteral("source retired")));
        QCOMPARE(controller.activeEndpointCount(), 1);

        QTimer* subscriptionTimer = nullptr;
        for (QTimer* timer : controller.findChildren<QTimer*>()) {
            if (timer->interval() == 100) { subscriptionTimer = timer; break; }
        }
        QVERIFY(subscriptionTimer);
        QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), subscriptions);

        stationLink->releaseHeld();
        QCoreApplication::processEvents();
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), subscriptions);
        QVERIFY(applet->remoteDisplayStatus().contains(QStringLiteral("source retired")));
    }

    void ps3EnableWaitsForReductionAndRefusalRestoresQualityWithoutRetry()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const bool hadFps = appSettings.contains(QStringLiteral("DisplaySpectrumFps"));
        const QVariant savedFps = appSettings.value(QStringLiteral("DisplaySpectrumFps"));
        appSettings.setValue(QStringLiteral("DisplaySpectrumFps"), QStringLiteral("30"));
        const auto restoreFps = qScopeGuard([&] {
            if (hadFps) { appSettings.setValue(QStringLiteral("DisplaySpectrumFps"), savedFps); }
            else { appSettings.remove(QStringLiteral("DisplaySpectrumFps")); }
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        QVERIFY(station.sliceById(sliceId));
        const auto floor = spectrumDisplayCost(256, 10, false);
        QVERIFY(floor.has_value());
        const auto ps3AndFloor = sumDisplayCharges({ps3DisplayCharge(), floor->charge});
        QVERIFY(ps3AndFloor.has_value());
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({ps3AndFloor->applicationBytesPerSecond,
                                               10'000'000, 1}));
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        server.setPs3DisplayAdmissionHandler([](bool enabled, QString* refusal) {
            if (!enabled) { return true; }
            if (refusal) { *refusal = QStringLiteral("test PS3 refusal"); }
            return false;
        });

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        PanadapterApplet* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(sliceId);
        SpectrumWidget* widget = applet->spectrumWidget();
        widget->setDisplayWindowPreservingHistory(
            station.streamCentreHz(station.sliceById(sliceId)->streamIndex()), 48000);
        widget->setWfUpdatePeriodMs(20);
        stack.resize(1200, 600);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        QSignalSpy started(&client, &StationClient::ps3DisplaySubscriptionStarted);
        QSignalSpy finished(&client, &StationClient::ps3DisplaySubscriptionFinished);
        auto* stationLink = new HoldingAllocationResultTransport;
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 1);
        QTRY_COMPARE(stationLink->held.size(), 1);
        const QJsonObject original = lastControl(outbound, QStringLiteral("subscribe"));
        QVERIFY(original.value(QStringLiteral("pixels")).toInt() > 256
                || original.value(QStringLiteral("fps")).toInt() > 10);
        stationLink->releaseHeld();
        QTRY_VERIFY(applet->remoteDisplayStatus().startsWith(QStringLiteral("Display target")));

        remote.pureSignalFacade()->setAmpViewSubscribed(true);
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QTRY_COMPARE(stationLink->held.size(), 1);
        QCOMPARE(started.size(), 0);
        const QJsonObject reduction = lastControl(outbound, QStringLiteral("subscribe"));
        QVERIFY(reduction.value(QStringLiteral("pixels")).toInt()
                <= original.value(QStringLiteral("pixels")).toInt());
        QVERIFY(reduction.value(QStringLiteral("fps")).toInt()
                <= original.value(QStringLiteral("fps")).toInt());

        QTimer* subscriptionTimer = nullptr;
        for (QTimer* timer : controller.findChildren<QTimer*>()) {
            if (timer->interval() == 100) { subscriptionTimer = timer; break; }
        }
        QVERIFY(subscriptionTimer);
        QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QCOMPARE(started.size(), 0);

        stationLink->releaseHeld();
        QTRY_COMPARE(started.size(), 1);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QCOMPARE(finished.first().at(2).toBool(), false);
        QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("test PS3 refusal")));
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 3);
        QTRY_COMPARE(stationLink->held.size(), 1);
        const QJsonObject restored = lastControl(outbound, QStringLiteral("subscribe"));
        QCOMPARE(restored.value(QStringLiteral("pixels")), original.value(QStringLiteral("pixels")));
        QCOMPARE(restored.value(QStringLiteral("fps")), original.value(QStringLiteral("fps")));
        stationLink->releaseHeld();
        QTRY_VERIFY(applet->remoteDisplayStatus().contains(QStringLiteral("test PS3 refusal")));
        // The refusal is already visible while the restored allocation's
        // queued acknowledgment is still in flight. Wait for accepted quality.
        QTRY_VERIFY(!applet->remoteDisplayStatus().contains(QStringLiteral("requested")));
        QVERIFY(!client.remotePs3DisplaySubscribed());

        for (int attempt = 0; attempt < 3; ++attempt) {
            QVERIFY(QMetaObject::invokeMethod(subscriptionTimer, "timeout", Qt::DirectConnection));
        }
        QCOMPARE(countControl(outbound, QStringLiteral("subscribe")), 3);
        QCOMPARE(started.size(), 1);
        QCOMPARE(finished.size(), 1);
    }

    void contextMediaAndRetirementStayInAuthenticatedSession()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.setConnectionStateForTest(ConnectionState::Connected);
        station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(!station.slices().isEmpty());
        SliceModel* stationSlice = station.slices().first();
        stationSlice->setStreamIndex(0);
        stationSlice->setFrequency(14225000);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        ClarityController clarity;
        remote.setClarityController(&clarity);
        const auto detachClarity = qScopeGuard([&] { remote.setClarityController(nullptr); });
        clarity.setEnabled(true);
        clarity.setPollIntervalMs(0);
        clarity.setSmoothingTauSec(0);
        clarity.setDeadbandDb(0);
        QSignalSpy floors(&clarity, &ClarityController::noiseFloorChanged);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(stationSlice->sliceIndex());
        auto* widget = applet->spectrumWidget();
        connect(&clarity, &ClarityController::waterfallThresholdsChanged,
                widget, [widget](float low, float high) {
            widget->setClarityActive(true);
            widget->setClarityWaterfallThresholds(low, high);
        });
        const float savedLow = widget->wfLowThreshold();
        const float savedHigh = widget->wfHighThreshold();
        widget->setDisplayWindowPreservingHistory(14225000, 24000);
        widget->setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        widget->setWfUpdatePeriodMs(20);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy controls(&server, &StationServer::mediaControlReceived);
        QSignalSpy receivedControls(&client, &StationClient::mediaControlReceived);
        QSignalSpy frames(&controller, &RemoteMediaController::displayFrameReceived);
        QVERIFY(!media); // No pre-authentication peer or subscription.
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.mediaAvailable());
        QTRY_VERIFY(media);
        QTRY_COMPARE(countControl(controls, QStringLiteral("start")), 1);
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), 0);
        media->activate();
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 1);
        const QJsonObject subscription = lastControl(controls, QStringLiteral("subscribe"));
        QVERIFY(client.remoteWidebandAvailable());
        QVERIFY(!widget->extendedMode());
        QVERIFY(subscription.value(QStringLiteral("extendedView")).isBool());
        QVERIFY(subscription.value(QStringLiteral("extendedView")).toBool());
        const quint32 id = quint32(subscription.value(QStringLiteral("endpointId")).toDouble());
        QJsonObject noiseFloor{
            {QStringLiteral("op"), QStringLiteral("noise-floor")},
            {QStringLiteral("connectionId"), subscription.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), double(id)},
            {QStringLiteral("revision"), subscription.value(QStringLiteral("revision"))},
            {QStringLiteral("contextGeneration"), 1},
            {QStringLiteral("floorDbm"), -132.375}};
        const auto deliverFloor = [&](const QJsonObject& message) {
            const int before = countControl(receivedControls, QStringLiteral("noise-floor"));
            QVERIFY(server.sendMediaControl(message, server.mediaSessionEpoch()));
            QTRY_COMPARE(countControl(receivedControls, QStringLiteral("noise-floor")), before + 1);
        };
        deliverFloor(noiseFloor); // No accepted display context yet.
        QCOMPARE(floors.size(), 0);
        DisplayCodecFrame frame;
        frame.context = {id, 1, -180, 0, 128, 128, 0};
        frame.traceDbm = QVector<float>(128, -75);
        frame.waterfallDbm = QVector<float>(128, -125);
        frame.waterfallAdvance = true;
        DisplayCodecEncoder encoder;
        const QByteArray packet = encoder.encode(frame);
        QVERIFY(!packet.isEmpty());
        media->deliver(packet); // Media may race its reliable context.
        QCOMPARE(frames.count(), 0);
        QJsonObject context{
            {QStringLiteral("op"), QStringLiteral("context")},
            {QStringLiteral("connectionId"), subscription.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), double(id)},
            {QStringLiteral("revision"), subscription.value(QStringLiteral("revision"))},
            {QStringLiteral("contextGeneration"), 1}, {QStringLiteral("sourceStream"), 0},
            {QStringLiteral("sourceCentreHz"), 14225000}, {QStringLiteral("sampleRateHz"), 192000},
            {QStringLiteral("centreHz"), 14225023.4375}, {QStringLiteral("spanHz"), 24046.875},
            {QStringLiteral("wideCentreHz"), 0}, {QStringLiteral("wideSpanHz"), 0},
            {QStringLiteral("traceSamples"), 128}, {QStringLiteral("waterfallSamples"), 128},
            {QStringLiteral("wideSamples"), 0}, {QStringLiteral("minDbm"), -180},
            {QStringLiteral("maxDbm"), 0}, {QStringLiteral("fps"), 30},
            {QStringLiteral("framesPerLine"), 1},
            {QStringLiteral("wideband"), WidebandDisplayContext{}.toJson()},
            // Minor 9: what Core granted this endpoint.
            {QStringLiteral("grantedFftSize"), 4096}, {QStringLiteral("grantedTier"), QStringLiteral("wide")},
            {QStringLiteral("requestedPixels"), 128}, {QStringLiteral("grantedPixels"), 128},
            {QStringLiteral("limit"), QStringLiteral("none")}};
        QVERIFY(controller.spectrumGrantNegotiated());
        QJsonObject invalid = context;
        // The minor-8 shape is refused once minor 9 is agreed.
        for (const char* key : {"grantedFftSize", "grantedTier", "requestedPixels",
                                "grantedPixels", "limit"}) {
            invalid.remove(QLatin1String(key));
        }
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        media->deliver(packet);
        QCOMPARE(frames.count(), 0);
        invalid = context;
        invalid.insert(QStringLiteral("traceSamples"), 128.5);
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        media->deliver(packet);
        QCOMPARE(frames.count(), 0);
        invalid = context;
        invalid.remove(QStringLiteral("wideband"));
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        media->deliver(packet);
        QCOMPARE(frames.count(), 0);
        invalid = context;
        QJsonObject malformedWideband = WidebandDisplayContext{}.toJson();
        malformedWideband.insert(QStringLiteral("active"), true);
        invalid.insert(QStringLiteral("wideband"), malformedWideband);
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        media->deliver(packet);
        QCOMPARE(frames.count(), 0);

        const auto availableWideband = [](bool active) {
            WidebandDisplayContext wideband;
            wideband.available = true;
            wideband.active = active;
            wideband.physicalAdcIndex = 0;
            wideband.filterChainIndex = 0;
            wideband.sourceGeneration = active ? 7 : 0;
            wideband.adcRateHz = 4000000;
            return wideband.toJson();
        };

        // An active context needs both the saved request permission and RF
        // geometry that actually leaves the DDC source window.
        widget->setDisplayWindowPreservingHistory(14500000, 24000);
        widget->setExtendedViewAllowed(false);
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 2);
        const QJsonObject permissionOff = lastControl(controls, QStringLiteral("subscribe"));
        QVERIFY(!permissionOff.value(QStringLiteral("extendedView")).toBool());
        invalid = context;
        invalid.insert(QStringLiteral("revision"),
                       permissionOff.value(QStringLiteral("revision")));
        invalid.insert(QStringLiteral("centreHz"), 14500000);
        invalid.insert(QStringLiteral("spanHz"), 24000);
        invalid.insert(QStringLiteral("wideband"), availableWideband(true));
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        QCOMPARE(countControl(controls, QStringLiteral("keyframe")), 0);

        widget->setDisplayWindowPreservingHistory(14225000, 24000);
        widget->setExtendedViewAllowed(true);
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 3);
        const QJsonObject permissionOn = lastControl(controls, QStringLiteral("subscribe"));
        QVERIFY(permissionOn.value(QStringLiteral("extendedView")).toBool());
        context.insert(QStringLiteral("revision"),
                       permissionOn.value(QStringLiteral("revision")));
        noiseFloor.insert(QStringLiteral("revision"),
                          permissionOn.value(QStringLiteral("revision")));

        // A narrow inactive view is still extended geometry when it sits
        // wholly outside sourceCentre +/- sampleRate/2.
        invalid = context;
        invalid.insert(QStringLiteral("centreHz"), 14500000);
        invalid.insert(QStringLiteral("spanHz"), 24000);
        invalid.insert(QStringLiteral("wideband"), availableWideband(false));
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        QCOMPARE(countControl(controls, QStringLiteral("keyframe")), 0);

        // Permission alone cannot make an in-DDC accepted crop active.
        invalid = context;
        invalid.insert(QStringLiteral("centreHz"), 14225000);
        invalid.insert(QStringLiteral("spanHz"), 24000);
        invalid.insert(QStringLiteral("wideband"), availableWideband(true));
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        QCOMPARE(countControl(controls, QStringLiteral("keyframe")), 0);

        // SpectrumEndpoint's real inactive crop rounds outward to bin edges:
        // 14,225,023.4375 +/- 12,023.4375 Hz. It remains inside the DDC
        // bounds and must be admitted without any guessed bin-width margin.
        context.insert(QStringLiteral("wideband"), availableWideband(false));
        QVERIFY(server.sendMediaControl(context, server.mediaSessionEpoch()));
        QTRY_COMPARE(countControl(controls, QStringLiteral("keyframe")), 1);
        // Temporary render hides also preserve the active pan's Clarity
        // observations. Logical retirement below still rejects later samples.
        widget->hide();
        deliverFloor(noiseFloor);
        QCOMPARE(floors.size(), 1);
        widget->show();
        QCOMPARE(clarity.smoothedFloor(), -132.375f);
        QCOMPARE(widget->wfActiveLowThreshold(), -137.375f);
        QCOMPARE(widget->wfActiveHighThreshold(), -77.375f);
        QCOMPARE(widget->wfLowThreshold(), savedLow);
        QCOMPARE(widget->wfHighThreshold(), savedHigh);
        const QList<QPair<QString, QJsonValue>> invalidFields{
            {QStringLiteral("connectionId"), QStringLiteral("00000000-0000-4000-8000-000000000001")},
            {QStringLiteral("endpointId"), double(id + 1000)},
            {QStringLiteral("revision"), permissionOn.value(QStringLiteral("revision")).toDouble() + 1},
            {QStringLiteral("contextGeneration"), 2},
            {QStringLiteral("contextGeneration"), 1.5},
            {QStringLiteral("floorDbm"), QStringLiteral("-120")},
            {QStringLiteral("floorDbm"), -401},
            {QStringLiteral("floorDbm"), 101},
            {QStringLiteral("floorDbm"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("extra"), 1}};
        for (const auto& [key, value] : invalidFields) {
            QJsonObject badFloor = noiseFloor;
            badFloor.insert(key, value);
            deliverFloor(badFloor);
            QCOMPARE(floors.size(), 1);
        }
        client.mediaControlReceived(noiseFloor, client.sessionEpoch() - 1);
        QCOMPARE(floors.size(), 1);
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        QCOMPARE(widget->renderedPixels().size(), 128);
        QVERIFY(std::abs(widget->renderedPixels().first() + 75) < 0.4);
        QVERIFY(std::abs(widget->wfRenderedPixels().first() + 125) < 0.4);
        QTRY_COMPARE(widget->dssRowsPushedForTest(), 1);
        QTest::qWait(250);
        // Bin-aligned accepted geometry must not create a resubscribe loop.
        const int acceptedSubscriptionCount = 3;
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")),
                 acceptedSubscriptionCount);

        // A regular tune must keep painted history even during the request/ACK
        // gap. Old media is retired immediately; only the new generation paints.
        widget->setCenterFrequency(widget->centerFrequency() + 500);
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")),
                     acceptedSubscriptionCount + 1);
        QCOMPARE(widget->dssRowsPushedForTest(), 1);
        QVERIFY(widget->renderedPixels().isEmpty());
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        const auto tuned = lastControl(controls, QStringLiteral("subscribe"));
        context.insert(QStringLiteral("revision"), tuned.value(QStringLiteral("revision")));
        context.insert(QStringLiteral("contextGeneration"), 2);
        context.insert(QStringLiteral("centreHz"), widget->centerFrequency());
        context.insert(QStringLiteral("spanHz"), widget->bandwidth());
        const int contextsBefore = countControl(receivedControls, QStringLiteral("context"));
        QVERIFY(server.sendMediaControl(context, server.mediaSessionEpoch()));
        QTRY_COMPARE(countControl(receivedControls, QStringLiteral("context")), contextsBefore + 1);
        QCOMPARE(widget->dssRowsPushedForTest(), 1);
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        frame.context.contextGeneration = 2;
        media->deliver(encoder.encode(frame));
        QCOMPARE(frames.count(), 2);
        QTRY_COMPARE(widget->dssRowsPushedForTest(), 2);
        noiseFloor.insert(QStringLiteral("revision"), tuned.value(QStringLiteral("revision")));
        noiseFloor.insert(QStringLiteral("contextGeneration"), 2);
        noiseFloor.insert(QStringLiteral("floorDbm"), -131);
        deliverFloor(noiseFloor);
        QCOMPARE(floors.size(), 2);

        widget->setDisplayWindowPreservingHistory(14425000, 24000);
        deliverFloor(noiseFloor); // A gesture retires old RF meaning before its ACK.
        QCOMPARE(floors.size(), 2);
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")),
                     acceptedSubscriptionCount + 2);
        const auto extended = lastControl(controls, QStringLiteral("subscribe"));
        context.insert(QStringLiteral("revision"), extended.value(QStringLiteral("revision")));
        context.insert(QStringLiteral("contextGeneration"), 3);
        context.insert(QStringLiteral("centreHz"), widget->centerFrequency());
        context.insert(QStringLiteral("spanHz"), widget->bandwidth());
        context.insert(QStringLiteral("wideband"), availableWideband(true));
        QVERIFY(server.sendMediaControl(context, server.mediaSessionEpoch()));
        QTRY_VERIFY(widget->remoteWidebandActive());
        QVERIFY(widget->extendedMode());
        QCOMPARE(widget->dssRowsPushedForTest(), 2);
        frame.context.contextGeneration = 3;
        media->deliver(encoder.encode(frame));
        QCOMPARE(frames.count(), 3);
        QTRY_COMPARE(widget->dssRowsPushedForTest(), 3);
        // Logical pan retirement, rather than a transient QWidget hide,
        // terminates this endpoint. Keep guarded pointers because the stack
        // deletes the applet after emitting panRetired().
        QPointer<PanadapterApplet> retiredApplet(applet);
        QPointer<SpectrumWidget> retiredWidget(widget);
        stack.removePanadapter(QStringLiteral("pan-0"));
        QTRY_COMPARE(countControl(controls, QStringLiteral("unsubscribe")), 1);
        QCOMPARE(controller.activeEndpointCount(), 0);
        deliverFloor(noiseFloor);
        QCOMPARE(floors.size(), 2);
        media->deliver(packet);
        QCOMPARE(frames.count(), 3);
        QTRY_VERIFY(retiredApplet.isNull());
        QTRY_VERIFY(retiredWidget.isNull());
        client.disconnectFromStation(QStringLiteral("test complete"));
        QVERIFY(!media || !media->active);
    }

    // R-R3-01/08: while Core reports a limited grant the pan shows one plain
    // line; nothing for an unlimited grant, and the line goes with the
    // endpoint.
    void limitedGrantShowsOnePlainPanStatusLine()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.setConnectionStateForTest(ConnectionState::Connected);
        station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(!station.slices().isEmpty());
        SliceModel* stationSlice = station.slices().first();
        stationSlice->setStreamIndex(0);
        stationSlice->setFrequency(14225000);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(stationSlice->sliceIndex());
        auto* widget = applet->spectrumWidget();
        widget->setDisplayWindowPreservingHistory(14225000, 24000);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy controls(&server, &StationServer::mediaControlReceived);
        QSignalSpy receivedControls(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.mediaAvailable());
        QTRY_VERIFY(media);
        media->activate();
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 1);
        QVERIFY(controller.spectrumGrantNegotiated());
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        const QJsonObject subscription = lastControl(controls, QStringLiteral("subscribe"));
        const quint32 id = quint32(subscription.value(QStringLiteral("endpointId")).toDouble());
        QVERIFY(applet->remoteDisplayStatus().isEmpty());

        SpectrumContextMessage message;
        message.connectionId = subscription.value(QStringLiteral("connectionId")).toString();
        message.endpointId = id;
        message.revision = quint32(subscription.value(QStringLiteral("revision")).toDouble());
        message.sourceStream = 0;
        message.sourceCentreHz = 14225000;
        message.sampleRateHz = 192000;
        // SpectrumEndpoint's bin-aligned crop of this window (see
        // contextMediaAndRetirementStayInAuthenticatedSession).
        message.centreHz = 14225023.4375;
        message.spanHz = 24046.875;
        message.traceSamples = 128;
        message.waterfallSamples = 128;
        message.minDbm = -180;
        message.maxDbm = 0;
        message.fps = 30;
        message.framesPerLine = 1;
        message.wideband = WidebandDisplayContext{};
        const auto sendGrant = [&](SpectrumLimitReason limit, bool grantShape = true) {
            ++message.contextGeneration;
            SpectrumContextGrant grant;
            grant.grantedFftSize = 4096;
            grant.requestedPixels = 600;
            grant.grantedPixels = 128;
            grant.limit = limit;
            message.grant = grant;
            const int before = countControl(receivedControls, QStringLiteral("context"));
            QVERIFY(server.sendMediaControl(encodeRemoteSpectrumContext(message, grantShape),
                                            server.mediaSessionEpoch()));
            QTRY_COMPARE(countControl(receivedControls, QStringLiteral("context")), before + 1);
        };
        const QString sourceBins =
            QStringLiteral("Showing 128 points: the receiver has no finer detail here");
        const QString shared = QStringLiteral(
            "Zoom detail limited: this receiver's spectrum is shared with another pan");
        const QString largest = QStringLiteral("Zoom detail is at the station's maximum");

        sendGrant(SpectrumLimitReason::SourceBins);
        QTRY_COMPARE(applet->remoteDisplayStatus(), sourceBins);
        sendGrant(SpectrumLimitReason::SharedEngine);
        QTRY_COMPARE(applet->remoteDisplayStatus(), shared);
        sendGrant(SpectrumLimitReason::LargestSize);
        QTRY_COMPARE(applet->remoteDisplayStatus(), largest);
        // No longer limited: nothing is shown, and the periodic refresh
        // does not bring an old line back.
        sendGrant(SpectrumLimitReason::None);
        QTRY_VERIFY(applet->remoteDisplayStatus().isEmpty());
        QTest::qWait(250);
        QVERIFY(applet->remoteDisplayStatus().isEmpty());
        sendGrant(SpectrumLimitReason::SharedEngine);
        QTRY_COMPARE(applet->remoteDisplayStatus(), shared);
        // A context without the grant is refused, so it cannot clear the line.
        sendGrant(SpectrumLimitReason::None, false);
        QTest::qWait(250);
        QCOMPARE(applet->remoteDisplayStatus(), shared);

        // The endpoint goes away with the session; so does its line.
        client.disconnectFromStation(QStringLiteral("test complete"));
        QTRY_VERIFY2(applet->remoteDisplayStatus().isEmpty(),
                     qPrintable(applet->remoteDisplayStatus()));
        QVERIFY(!media || !media->active);
    }

    // R-R3-01/08/37: outside budget mode Core's five-key per-pan refusal
    // puts the budget-mode line on the pan, with no toast, until the pan's
    // request changes. A retirement for a slice the operator removed or
    // rebound is not shown as a refusal, even while this window still has
    // the slice.
    void perPanRefusalShowsPlainStatusLineOutsideBudgetMode()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.setConnectionStateForTest(ConnectionState::Connected);
        station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(!station.slices().isEmpty());
        SliceModel* stationSlice = station.slices().first();
        stationSlice->setStreamIndex(0);
        stationSlice->setFrequency(14225000);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(stationSlice->sliceIndex());
        auto* widget = applet->spectrumWidget();
        widget->setDisplayWindowPreservingHistory(14225000, 24000);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy errors(&controller, &RemoteMediaController::errorOccurred);
        QSignalSpy controls(&server, &StationServer::mediaControlReceived);
        QSignalSpy receivedControls(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.mediaAvailable());
        QTRY_VERIFY(media);
        media->activate();
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 1);
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        QVERIFY(applet->remoteDisplayStatus().isEmpty());

        const auto refuse = [&](const QString& reason) {
            const QJsonObject subscription = lastControl(controls, QStringLiteral("subscribe"));
            const int before = countControl(receivedControls, QStringLiteral("rejected"));
            // Exactly what DaemonMediaController::sendRejected() sends.
            QVERIFY(server.sendMediaControl({
                {QStringLiteral("op"), QStringLiteral("rejected")},
                {QStringLiteral("connectionId"), subscription.value(QStringLiteral("connectionId"))},
                {QStringLiteral("endpointId"), subscription.value(QStringLiteral("endpointId"))},
                {QStringLiteral("revision"), subscription.value(QStringLiteral("revision"))},
                {QStringLiteral("reason"), reason}}, server.mediaSessionEpoch()));
            QTRY_COMPARE(countControl(receivedControls, QStringLiteral("rejected")), before + 1);
        };
        const QString refused =
            QStringLiteral("Display allocation refused: requested crop is outside source coverage");

        refuse(QStringLiteral("requested crop is outside source coverage"));
        QTRY_COMPARE(applet->remoteDisplayStatus(), refused);
        // The periodic refresh keeps the reason while the request stands.
        QTest::qWait(250);
        QCOMPARE(applet->remoteDisplayStatus(), refused);
        QCOMPARE(errors.count(), 0);

        // A new request clears it when it goes out.
        widget->setDisplayWindowPreservingHistory(14226000, 24000);
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 2);
        QTRY_VERIFY2(applet->remoteDisplayStatus().isEmpty(),
                     qPrintable(applet->remoteDisplayStatus()));
        QTest::qWait(250);
        QVERIFY(applet->remoteDisplayStatus().isEmpty());

        // Core retires the endpoint because the slice went or was rebound,
        // and this window has not yet seen that change: no refusal line.
        for (const QString& operatorChange :
             {QString::fromLatin1(kRetireReasonSliceRemoved),
              QString::fromLatin1(kRetireReasonStreamBindingChanged)}) {
            refuse(operatorChange);
            QTest::qWait(250);
            QVERIFY2(applet->remoteDisplayStatus().isEmpty(),
                     qPrintable(applet->remoteDisplayStatus()));
            QVERIFY(remote.sliceById(stationSlice->sliceIndex()) != nullptr);
            const int subscribes = countControl(controls, QStringLiteral("subscribe"));
            widget->setDisplayWindowPreservingHistory(
                widget->centerFrequency() + 1000, 24000);
            QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), subscribes + 1);
        }
        QCOMPARE(errors.count(), 0);

        client.disconnectFromStation(QStringLiteral("test complete"));
        QVERIFY(!media || !media->active);
    }

    // R-R3-01/09: a Core and a GUI that agree minor 8 keep today's context
    // and keep painting; minor 9 carries the grant. Each GUI accepts only
    // the shape it negotiated, so neither direction of a mixed pair breaks.
    void spectrumContextShapeFollowsTheAgreedMinor_data()
    {
        QTest::addColumn<int>("minor");
        QTest::newRow("minor 8") << int(kRemoteSpectrumGrantSessionProtocolMinor - 1);
        QTest::newRow("minor 9") << int(kRemoteSpectrumGrantSessionProtocolMinor);
    }

    void spectrumContextShapeFollowsTheAgreedMinor()
    {
        QFETCH(int, minor);
        const bool grantAgreed = minor >= kRemoteSpectrumGrantSessionProtocolMinor;
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* slice = station.sliceById(sliceId);
        QVERIFY(slice);
        const int stream = slice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* applet = stack.addPanadapter(QStringLiteral("only"));
        applet->setActiveSliceIndex(sliceId);
        applet->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        QSignalSpy frames(&gui, &RemoteMediaController::displayFrameReceived);
        // Both ends announce the row's minor, so each believes the other is
        // a build of that minor.
        auto* stationLink = new Test::RewritingTransport(QStringLiteral("station"),
                                                         quint16(minor));
        auto* clientLink = new Test::RewritingTransport(QStringLiteral("client"),
                                                        quint16(minor));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(sourceMedia && sinkMedia);
        QCOMPARE(client.agreedMinor(), quint16(minor));
        QCOMPARE(server.spectrumGrantAvailable(), grantAgreed);
        QCOMPARE(client.spectrumGrantAvailable(), grantAgreed);
        QCOMPARE(gui.spectrumGrantNegotiated(), grantAgreed);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(daemon.activeEndpointCount(), 1);
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        const auto painted = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return frames.count() > 0
                && !applet->spectrumWidget()->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(painted(), 5000);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);
        const QList<QJsonObject> contexts = controlsFor(inbound, QStringLiteral("context"));
        QVERIFY(!contexts.isEmpty());
        for (const QJsonObject& context : contexts) {
            const bool wideband = context.contains(QStringLiteral("wideband"));
            QCOMPARE(context.size(), (grantAgreed ? 24 : 19) + (wideband ? 1 : 0));
            QCOMPARE(context.contains(QStringLiteral("limit")), grantAgreed);
            QVERIFY(decodeRemoteSpectrumContext(context, grantAgreed).has_value());
            QVERIFY(!decodeRemoteSpectrumContext(context, !grantAgreed).has_value());
        }
        if (!grantAgreed) {
            // No grant was reported, so no grant line can appear.
            QVERIFY(!applet->remoteDisplayStatus().contains(QStringLiteral("Zoom detail")));
            QVERIFY(!applet->remoteDisplayStatus().contains(QStringLiteral("points:")));
        }
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // C1 (R-R3-01, R-R3-08, R-R3-09, R-R3-37): in budget mode a pan Core
    // grants fewer pixels than it asked for still paints. A minor 9 GUI
    // accepts the smaller granted charge; a minor 8 GUI, which knows nothing
    // of grants, is charged exactly what it requested, as before.
    void budgetModePaintsWhenCoreGrantsFewerPixels_data()
    {
        QTest::addColumn<int>("minor");
        QTest::addColumn<bool>("shared");
        QTest::addColumn<int>("leave");
        const int grant = int(kRemoteSpectrumGrantSessionProtocolMinor);
        QTest::newRow("minor 9 crop past the source edge") << grant << false << kStays;
        QTest::newRow("minor 9 shared engine") << grant << true << kStays;
        QTest::newRow("minor 8 crop past the source edge") << grant - 1 << false << kStays;
        QTest::newRow("minor 8 shared engine") << grant - 1 << true << kStays;
        // R-R3-01, R-R3-08, R-R3-37: the pan that sized the engine leaves.
        QTest::newRow("minor 9 shared engine, first pan leaves")
            << grant << true << kLeaves;
        QTest::newRow("minor 9 shared engine, first pan leaves, Core refuses more")
            << grant << true << kLeavesCoreRefuses;
    }

    void budgetModePaintsWhenCoreGrantsFewerPixels()
    {
        QFETCH(int, minor);
        QFETCH(bool, shared);
        QFETCH(int, leave);
        const bool grantAgreed = minor >= kRemoteSpectrumGrantSessionProtocolMinor;
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const bool hadFft = appSettings.contains(QStringLiteral("DisplayFftSize"));
        const QVariant savedFft = appSettings.value(QStringLiteral("DisplayFftSize"));
        appSettings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("4096"));
        const auto restoreFft = qScopeGuard([&] {
            if (hadFft) { appSettings.setValue(QStringLiteral("DisplayFftSize"), savedFft); }
            else { appSettings.remove(QStringLiteral("DisplayFftSize")); }
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* slice = station.sliceById(sliceId);
        QVERIFY(slice);
        const int stream = slice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits({10'000'000, 10'000'000, 1}));
        // Refused row: Core's state moves ahead of the GUI's view. When the
        // survivor asks again, Core already holds PureSignal display, which
        // the GUI has not heard of yet. Connected before Core's own handler,
        // so it runs first.
        const auto armedEndpoint = std::make_shared<quint32>(0);
        connect(&server, &StationServer::mediaControlReceived, &station,
            [&station, armedEndpoint](const QJsonObject& control) {
                if (*armedEndpoint != 0
                    && control.value(QStringLiteral("op")) == QLatin1String("subscribe")
                    && quint32(control.value(QStringLiteral("endpointId")).toDouble())
                        == *armedEndpoint) {
                    *armedEndpoint = 0;
                    station.pureSignalFacade()->setRemoteAmpViewSubscribed(true);
                }
            });
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* first = stack.addPanadapter(QStringLiteral("first"));
        first->setActiveSliceIndex(sliceId);
        first->spectrumWidget()->setExtendedViewAllowed(false);
        // Shared: a "fine" pan that sizes the engine. Edge: a crop that
        // overlaps the source by 4 kHz, so fewer bins than pixels exist.
        if (shared) {
            first->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 12000);
        } else {
            first->spectrumWidget()->setDisplayWindowPreservingHistory(centre + 116000, 48000);
        }
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::RewritingTransport(QStringLiteral("station"),
                                                         quint16(minor));
        auto* clientLink = new Test::RewritingTransport(QStringLiteral("client"),
                                                        quint16(minor));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.remoteDisplayBudgetLimits().has_value());
        QTRY_VERIFY(sourceMedia && sinkMedia);
        QCOMPARE(gui.spectrumGrantNegotiated(), grantAgreed);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        // Enough samples per poll for the longest FFT these rows use.
        const auto feed = [&] {
            for (int block = 0; block < 32; ++block) {
                QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                    Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            }
        };
        const auto paints = [&](PanadapterApplet* applet) {
            feed();
            return !applet->spectrumWidget()->renderedPixels().isEmpty();
        };
        QTRY_VERIFY2_WITH_TIMEOUT(paints(first), qPrintable(first->remoteDisplayStatus()), 5000);

        PanadapterApplet* limited = first;
        if (shared) {
            // A deeper zoom on the same receiver asks for a longer "fine"
            // FFT than the first pan's engine; it is granted that engine.
            limited = stack.addPanadapter(QStringLiteral("second"));
            limited->setActiveSliceIndex(sliceId);
            limited->spectrumWidget()->setExtendedViewAllowed(false);
            limited->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 6000);
            QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        }
        QTRY_VERIFY2_WITH_TIMEOUT(paints(limited), qPrintable(limited->remoteDisplayStatus()),
                                  5000);
        QVERIFY2(!limited->remoteDisplayStatus().contains(QStringLiteral("stalled")),
                 qPrintable(limited->remoteDisplayStatus()));

        // The limited pan really was granted fewer pixels than it asked for.
        bool reduced = false;
        quint32 limitedEndpoint = 0;
        QJsonObject limitedRequest;
        int limitedGrantedPixels = 0;
        for (const QJsonObject& context : controlsFor(inbound, QStringLiteral("context"))) {
            const auto decoded = decodeRemoteSpectrumContext(context, grantAgreed);
            QVERIFY(decoded.has_value());
            for (const QJsonObject& request : controlsFor(outbound, QStringLiteral("subscribe"))) {
                if (quint32(request.value(QStringLiteral("endpointId")).toDouble())
                        == decoded->endpointId
                    && decoded->traceSamples
                        < request.value(QStringLiteral("pixels")).toInt()) {
                    reduced = true;
                    limitedEndpoint = decoded->endpointId;
                    limitedRequest = request;
                    limitedGrantedPixels = decoded->traceSamples;
                    if (grantAgreed) {
                        QCOMPARE(decoded->grant->limit, shared
                            ? SpectrumLimitReason::SharedEngine
                            : SpectrumLimitReason::SourceBins);
                    }
                }
            }
        }
        QVERIFY(reduced);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        if (leave != kStays) {
            const auto requestsFromLimited = [&] {
                int count = 0;
                for (const QJsonObject& request
                     : controlsFor(outbound, QStringLiteral("subscribe"))) {
                    if (quint32(request.value(QStringLiteral("endpointId")).toDouble())
                        == limitedEndpoint) { ++count; }
                }
                return count;
            };
            const int requestedPixels = limitedRequest.value(QStringLiteral("pixels")).toInt();
            QVERIFY(limitedGrantedPixels < requestedPixels);
            if (leave == kLeavesCoreRefuses) {
                // Core takes PureSignal display just as the survivor asks
                // again (armedEndpoint above): the GUI's own budget check
                // passes, and Core's admission refuses the survivor any more
                // pixels. The limits leave room for the survivor's granted
                // pixels plus PureSignal, and for both pans as they were.
                const int fps = limitedRequest.value(QStringLiteral("fps")).toInt();
                const bool wide = limitedRequest.value(QStringLiteral("wideSpanFactor"))
                    .toDouble() > 1.0;
                const auto granted = spectrumDisplayCost(limitedGrantedPixels, fps, wide);
                const auto full = spectrumDisplayCost(requestedPixels, fps, wide);
                QVERIFY(granted && full);
                quint64 others = 0;
                for (const QJsonObject& request
                     : controlsFor(outbound, QStringLiteral("subscribe"))) {
                    if (quint32(request.value(QStringLiteral("endpointId")).toDouble())
                        != limitedEndpoint) {
                        const auto cost = spectrumDisplayCost(
                            request.value(QStringLiteral("pixels")).toInt(),
                            request.value(QStringLiteral("fps")).toInt(),
                            request.value(QStringLiteral("wideSpanFactor")).toDouble() > 1.0);
                        QVERIFY(cost);
                        others = std::max(others, cost->charge.applicationBytesPerSecond);
                    }
                }
                const quint64 ps3 = ps3DisplayCharge().applicationBytesPerSecond;
                const quint64 limit = granted->charge.applicationBytesPerSecond + ps3;
                QVERIFY(full->charge.applicationBytesPerSecond
                        > granted->charge.applicationBytesPerSecond);
                QVERIFY(others + full->charge.applicationBytesPerSecond <= limit);
                const int before = requestsFromLimited();
                QVERIFY(server.setDisplayBudgetLimits({limit, 10'000'000, 2}));
                QTRY_COMPARE(client.remoteDisplayBudgetLimits()->generation, quint32(2));
                QTest::qWait(100);
                QCOMPARE(requestsFromLimited(), before);
                *armedEndpoint = limitedEndpoint;
            }
            const int requestsBeforeLeave = requestsFromLimited();
            stack.removePanadapter(QStringLiteral("first"));
            QTRY_COMPARE(daemon.activeEndpointCount(), 1);
            const auto survivorContext = [&]() -> std::optional<SpectrumContextMessage> {
                std::optional<SpectrumContextMessage> latest;
                for (const QJsonObject& context
                     : controlsFor(inbound, QStringLiteral("context"))) {
                    const auto decoded = decodeRemoteSpectrumContext(context, grantAgreed);
                    if (decoded && decoded->endpointId == limitedEndpoint) { latest = decoded; }
                }
                return latest;
            };
            if (leave == kLeaves) {
                // The same request goes out again and Core grants all of it.
                // Core renews the survivor's context on the engine's next frame.
                QTRY_VERIFY_WITH_TIMEOUT(
                    (feed(), requestsFromLimited() == requestsBeforeLeave + 1), 5000);
                const QJsonObject again = controlsFor(outbound, QStringLiteral("subscribe")).last();
                QCOMPARE(quint32(again.value(QStringLiteral("endpointId")).toDouble()),
                         limitedEndpoint);
                QCOMPARE(again.value(QStringLiteral("pixels")).toInt(), requestedPixels);
                QTRY_VERIFY_WITH_TIMEOUT((feed(), survivorContext()
                    && survivorContext()->traceSamples == requestedPixels), 5000);
                QCOMPARE(survivorContext()->grant->grantedPixels, requestedPixels);
                QCOMPARE(survivorContext()->grant->limit, SpectrumLimitReason::None);
                QTRY_VERIFY2_WITH_TIMEOUT(paints(limited),
                    qPrintable(limited->remoteDisplayStatus()), 5000);
                QVERIFY2(!limited->remoteDisplayStatus().contains(QStringLiteral("points:")),
                         qPrintable(limited->remoteDisplayStatus()));
            } else {
                // Core refuses once; the refusal is not asked again.
                const auto refusals = [&] {
                    int count = 0;
                    for (const QJsonObject& result
                         : controlsFor(inbound, QStringLiteral("allocation-result"))) {
                        if (quint32(result.value(QStringLiteral("endpointId")).toDouble())
                                == limitedEndpoint
                            && !result.value(QStringLiteral("accepted")).toBool()) {
                            ++count;
                        }
                    }
                    return count;
                };
                QTRY_COMPARE_WITH_TIMEOUT((feed(), refusals()), 1, 5000);
                QCOMPARE(requestsFromLimited(), requestsBeforeLeave + 1);
                QElapsedTimer settle;
                settle.start();
                while (settle.elapsed() < 1000) {
                    feed();
                    QTest::qWait(20);
                }
                QCOMPARE(requestsFromLimited(), requestsBeforeLeave + 1);
                QCOMPARE(refusals(), 1);
                for (const QJsonObject& result
                     : controlsFor(inbound, QStringLiteral("allocation-result"))) {
                    if (!result.value(QStringLiteral("accepted")).toBool()) {
                        QCOMPARE(result.value(QStringLiteral("reason")).toString(),
                                 QStringLiteral("session display budget exceeded"));
                    }
                }
                // The pan keeps painting what Core already granted.
                QVERIFY(paints(limited));
                QVERIFY(survivorContext());
                QCOMPARE(survivorContext()->traceSamples, limitedGrantedPixels);
            }
        }
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // R-R3-23 (a): a real speaker loss becomes a lasting playback problem
    // with Retry. Mute, unmute, a device change, a disabled context and
    // time do not clear it while the speaker is still gone.
    void speakerLossPersistsAcrossMuteDeviceChangeAndDisabledContext()
    {
        using State = RemoteAudioStatus::State;
        using Fault = RemoteAudioReceiver::Fault;
        Test::RemoteAudioSessionHarness h;
        // Both controllers go before either AudioEngine: the receiver owns
        // worker callbacks and each peer owns libdatachannel.
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy errors(&remoteMedia, &RemoteMediaController::errorOccurred);
        AudioStatusHistory history(remoteMedia);
        QTimer* const statusTimer = audioStatusTimer(remoteMedia);
        QVERIFY(statusTimer);
        QCOMPARE(remoteMedia.audioStatus().state, State::NotConnected);
        QCOMPARE(remoteMedia.audioStatus().selectedOutput, QStringLiteral("System default"));
        QVERIFY(!statusTimer->isActive());
        remoteMedia.retryAudio(); // No media session: nothing to retry.
        QCOMPARE(remoteMedia.audioStatus().state, State::NotConnected);

        PacedRemoteAudio audio(h);
        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        QVERIFY(remoteMedia.audioStatus().detailNegotiated);
        QVERIFY(!remoteMedia.audioStatus().retryAvailable);
        QVERIFY(statusTimer->isActive());
        QCOMPARE(errors.size(), 0);

        // The speaker goes away mid-play and stops reporting its timing. The
        // real receiver notices through AudioEngine; nothing fakes a fault.
        QTest::ignoreMessage(QtWarningMsg, speakerTimingLostLog());
        h.remoteBus->setOutputPacingAvailableForTesting(false);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        RemoteAudioStatus status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::PlaybackProblem);
        QVERIFY(status.problem.has_value());
        QVERIFY(*status.problem == Fault::SpeakerTimingUnavailable
                || *status.problem == Fault::SpeakerWriteFailed);
        QVERIFY(status.retryAvailable);
        const QString toast = errors.at(0).at(0).toString();
        QCOMPARE(toast, remoteAudioProblemText(*status.problem));
        QVERIFY(!toast.contains(QLatin1Char('[')));

        // Core confirms the disable the fault sent. A disabled context
        // clears nothing, and neither does time.
        QTRY_VERIFY_WITH_TIMEOUT(acceptedOff(remoteMedia, 0, RemoteAudioOffReason::ClientDisabled),
                                 5000);
        QCOMPARE(remoteMedia.audioStatus().state, State::PlaybackProblem);
        QTest::qWait(3 * 250);
        QCOMPARE(remoteMedia.audioStatus().state, State::PlaybackProblem);
        QVERIFY(statusTimer->isActive()); // The problem awaits recovery.

        // Muted here: the mute is what shows, and the problem stays behind it.
        const quint32 beforeMute = remoteMedia.acceptedAudioContext()->generation;
        h.remote.audioEngine()->setMasterMuted(true);
        status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::MutedHere);
        QVERIFY(status.problem.has_value());
        QVERIFY(!status.retryAvailable);
        QTRY_VERIFY_WITH_TIMEOUT(acceptedOff(remoteMedia, beforeMute,
                                             RemoteAudioOffReason::ClientDisabled), 5000);
        QCOMPARE(remoteMedia.audioStatus().state, State::MutedHere);

        // Unmuting asks Core again, but the speaker is still gone.
        QTest::ignoreMessage(QtWarningMsg, kSpeakerOpenFailedLog);
        h.remote.audioEngine()->setMasterMuted(false);
        QCOMPARE(remoteMedia.audioStatus().state, State::PlaybackProblem);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 5000);
        QCOMPARE(errors.at(1).at(0).toString(),
                 QStringLiteral("The selected speaker device could not be opened."));
        status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::PlaybackProblem);
        QVERIFY(status.problem == Fault::SpeakerOpenFailed);
        QVERIFY(status.retryAvailable);

        // A device change asks again too, and names the new selection. It is
        // only the selection: the speaker is still gone.
        AppSettings::instance().setValue(QStringLiteral("audio/Speakers/DeviceName"),
                                         QStringLiteral("Desk headphones"));
        const auto restoreSelection = qScopeGuard([] {
            AppSettings::instance().setValue(QStringLiteral("audio/Speakers/DeviceName"),
                                             QString());
        });
        QTest::ignoreMessage(QtWarningMsg, kSpeakerOpenFailedLog);
        emit h.remote.audioEngine()->speakersConfigChanged(
            AudioDeviceConfig::loadFromSettings(QStringLiteral("audio/Speakers")));
        QCOMPARE(remoteMedia.audioStatus().selectedOutput, QStringLiteral("Desk headphones"));
        QCOMPARE(remoteMedia.audioStatus().state, State::PlaybackProblem);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 3, 5000);
        QCOMPARE(remoteMedia.audioStatus().state, State::PlaybackProblem);
        QVERIFY(history.onlyFromFirst(State::PlaybackProblem,
                                      {State::PlaybackProblem, State::MutedHere}));

        // The problem belongs to its session, and ends with it.
        audio.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
        status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::NotConnected);
        QVERIFY(!status.problem.has_value());
        QVERIFY(!status.retryAvailable);
        QVERIFY(!statusTimer->isActive());
    }

    // R-R3-23 (b): Retry sends a newer request, and the problem clears only
    // once the new context's receiver has made the speaker consume audio.
    void retryClearsTheProblemOnlyOnceTheNewContextPlays()
    {
        using State = RemoteAudioStatus::State;
        using Fault = RemoteAudioReceiver::Fault;
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy errors(&remoteMedia, &RemoteMediaController::errorOccurred);
        QSignalSpy coreControls(&h.server, &StationServer::mediaControlReceived);
        PacedRemoteAudio audio(h);
        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);

        // Mute, and while muted the speaker goes away, so the next start
        // cannot open it. Retry does nothing while muted.
        const quint32 playingContext = remoteMedia.acceptedAudioContext()->generation;
        h.remote.audioEngine()->setMasterMuted(true);
        QTRY_VERIFY_WITH_TIMEOUT(acceptedOff(remoteMedia, playingContext,
                                             RemoteAudioOffReason::ClientDisabled), 5000);
        const int requestsWhileMuted = countControl(coreControls, QStringLiteral("audio"));
        remoteMedia.retryAudio();
        QTest::qWait(100);
        QCOMPARE(countControl(coreControls, QStringLiteral("audio")), requestsWhileMuted);
        h.remoteBus->setOutputPacingAvailableForTesting(false);
        QTest::ignoreMessage(QtWarningMsg, kSpeakerOpenFailedLog);
        h.remote.audioEngine()->setMasterMuted(false);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        RemoteAudioStatus status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::PlaybackProblem);
        QVERIFY(status.problem == Fault::SpeakerOpenFailed);
        QVERIFY(status.retryAvailable);
        // Core's answer to the disable the failure sent. The failed start
        // was for an enabled context, so any disabled one is that answer.
        QTRY_VERIFY_WITH_TIMEOUT(acceptedOff(remoteMedia, 0,
                                             RemoteAudioOffReason::ClientDisabled), 5000);

        // The speaker is back. Retry asks Core again with a newer revision.
        h.remoteBus->setOutputPacingAvailableForTesting(true);
        const QList<QJsonObject> requestsBefore = controlsFor(coreControls, QStringLiteral("audio"));
        QVERIFY(!requestsBefore.isEmpty());
        std::optional<RemoteAudioStatus> statusAtStart;
        std::optional<RemoteAudioReceiverTelemetry> playbackAtStart;
        const QMetaObject::Connection probe = connect(
            &remoteMedia, &RemoteMediaController::audioContextAccepted, this, [&] {
                const std::optional<RemoteAudioContextMessage> context =
                    remoteMedia.acceptedAudioContext();
                if (!statusAtStart && context && context->enabled) {
                    statusAtStart = remoteMedia.audioStatus();
                    playbackAtStart = remoteMedia.audioTelemetry();
                }
            });
        const auto dropProbe = qScopeGuard([probe] { QObject::disconnect(probe); });
        remoteMedia.retryAudio();
        QTRY_VERIFY_WITH_TIMEOUT(controlsFor(coreControls, QStringLiteral("audio")).size()
                                     > requestsBefore.size(), 5000);
        const QJsonObject retry = controlsFor(coreControls, QStringLiteral("audio")).constLast();
        QVERIFY(retry.value(QStringLiteral("revision")).toDouble()
                > requestsBefore.constLast().value(QStringLiteral("revision")).toDouble());
        QVERIFY(retry.value(QStringLiteral("enabled")).toBool());

        // Core's new context starts a new receiver. Until that receiver has
        // made the speaker consume audio, the problem is still shown.
        QTRY_VERIFY_WITH_TIMEOUT(statusAtStart.has_value(), 5000);
        QVERIFY(playbackAtStart->running);
        QCOMPARE(playbackAtStart->deviceConsumedFrames, quint64(0));
        QCOMPARE(statusAtStart->state, State::PlaybackProblem);
        QVERIFY(statusAtStart->problem == Fault::SpeakerOpenFailed);

        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 10000);
        status = remoteMedia.audioStatus();
        QVERIFY(!status.problem.has_value());
        QVERIFY(!status.retryAvailable);
        QVERIFY(remoteMedia.audioTelemetry().deviceConsumedFrames > 0);
        QCOMPARE(errors.size(), 1);

        audio.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // R-R3-23 (c): a fault still queued from an ended session changes
    // nothing in the next one, and a recorded problem ends with its session.
    void lateFaultFromAnEndedSessionChangesNothing()
    {
        using State = RemoteAudioStatus::State;
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy errors(&remoteMedia, &RemoteMediaController::errorOccurred);
        PacedRemoteAudio audio(h);
        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        const RemoteAudioStatus playing = remoteMedia.audioStatus();

        // The speaker's timing goes while this session plays. The event loop
        // is held, so the receiver's report is still queued when the session
        // ends: the worker has stopped, and the controller has not heard.
        h.remoteBus->setOutputPacingAvailableForTesting(false);
        QElapsedTimer held;
        held.start();
        while (remoteMedia.audioTelemetry().running && held.elapsed() < 5000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QVERIFY(!remoteMedia.audioTelemetry().running);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Its report is posted.
        QVERIFY(remoteMedia.audioStatus() == playing);
        h.remoteBus->setOutputPacingAvailableForTesting(true);
        h.client.disconnectFromStation(QStringLiteral("end the first session"));
        QCOMPARE(remoteMedia.audioStatus().state, State::NotConnected);

        // The next session plays; the old report arrives and changes nothing.
        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        QTest::qWait(250);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(remoteMedia.audioStatus().state, State::Playing);
        QVERIFY(!remoteMedia.audioStatus().problem.has_value());

        // A problem recorded in this session does not outlive it either.
        QTest::ignoreMessage(QtWarningMsg, speakerTimingLostLog());
        h.remoteBus->setOutputPacingAvailableForTesting(false);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        QCOMPARE(remoteMedia.audioStatus().state, State::PlaybackProblem);
        h.remoteBus->setOutputPacingAvailableForTesting(true);
        h.client.disconnectFromStation(QStringLiteral("end the second session"));
        QCOMPARE(remoteMedia.audioStatus().state, State::NotConnected);
        QVERIFY(!remoteMedia.audioStatus().problem.has_value());
        QVERIFY(!remoteMedia.audioStatus().retryAvailable);
        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        QVERIFY(!remoteMedia.audioStatus().problem.has_value());
        QCOMPARE(errors.size(), 1);

        audio.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // R-R3-23 (e): a minor-7 Core still plays; it just reports no codec.
    void minorSevenCorePlaysWithoutCodecDetail()
    {
        using State = RemoteAudioStatus::State;
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy errors(&remoteMedia, &RemoteMediaController::errorOccurred);
        PacedRemoteAudio audio(h);
        h.connectSession(quint16{7});
        if (QTest::currentTestFailed()) { return; }
        QVERIFY(!remoteMedia.audioDetailNegotiated());
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        const RemoteAudioStatus status = remoteMedia.audioStatus();
        QVERIFY(!status.detailNegotiated);
        QVERIFY(!status.encoder.has_value());
        QCOMPARE(remoteAudioCodecText(status), QStringLiteral("Not reported by this Core"));
        QVERIFY(!status.problem.has_value());
        QVERIFY(!status.retryAvailable);
        QCOMPARE(errors.size(), 0);

        audio.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // R-R3-23 (f): Core's reasons reach the status. encoder-unavailable is
    // "Core could not start audio" with Retry; radio-offline is "Radio
    // offline", and stays so when this GUI's own withdrawal makes Core's
    // latest reason client-disabled.
    void coreReasonsShowAsCoreCouldNotStartAndRadioOffline()
    {
        using State = RemoteAudioStatus::State;
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy errors(&remoteMedia, &RemoteMediaController::errorOccurred);
        QSignalSpy guiControls(&h.client, &StationClient::mediaControlReceived);
        PacedRemoteAudio audio(h);
        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);

        // A Core whose encoder cannot start answers a request with audio off
        // for encoder-unavailable. That answer goes on the wire ahead of this
        // Core's real one, which then arrives with the same generation.
        h.stationLink->forgeNextAudioContext = [](const QJsonObject& real) {
            QJsonObject unavailable = real;
            unavailable.insert(QStringLiteral("enabled"), false);
            unavailable.remove(QStringLiteral("encoder"));
            unavailable.insert(QStringLiteral("reason"), QStringLiteral("encoder-unavailable"));
            return unavailable;
        };
        const quint32 playingContext = remoteMedia.acceptedAudioContext()->generation;
        remoteMedia.retryAudio();
        QTRY_VERIFY_WITH_TIMEOUT(acceptedOff(remoteMedia, playingContext,
                                             RemoteAudioOffReason::EncoderUnavailable), 5000);
        QCOMPARE(h.stationLink->forgedContexts, 1);
        const quint32 refusedGeneration = remoteMedia.acceptedAudioContext()->generation;
        const auto contextsWith = [&guiControls](quint32 generation) {
            int count = 0;
            for (const QJsonObject& context :
                 controlsFor(guiControls, QStringLiteral("audio-context"))) {
                if (context.value(QStringLiteral("generation")).toInteger() == generation) {
                    ++count;
                }
            }
            return count;
        };
        QTRY_COMPARE_WITH_TIMEOUT(contextsWith(refusedGeneration), 2, 5000);
        RemoteAudioStatus status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::CoreCouldNotStart);
        QVERIFY(status.retryAvailable);
        QVERIFY(status.detailNegotiated);
        QVERIFY(!status.encoder.has_value());
        QCOMPARE(remoteAudioCodecText(status), QStringLiteral("Audio is off"));
        QVERIFY(!status.problem.has_value());

        // Retry asks again, and this Core's encoder answers.
        remoteMedia.retryAudio();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 10000);
        QVERIFY(remoteMedia.audioStatus().encoder.has_value());

        // The station radio drops with audio playing. Core says why at once;
        // this GUI's mirror follows and withdraws its own request, so Core's
        // latest reason becomes client-disabled. The radio is still the
        // reason shown, from the first moment to the last.
        AudioStatusHistory history(remoteMedia);
        const quint32 beforeDrop = remoteMedia.acceptedAudioContext()->generation;
        h.station.setConnectionStateForTest(ConnectionState::Disconnected);
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::RadioOffline, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!h.remote.isConnected(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(acceptedOff(remoteMedia, beforeDrop,
                                             RemoteAudioOffReason::ClientDisabled), 5000);
        status = remoteMedia.audioStatus();
        QCOMPARE(status.state, State::RadioOffline);
        QVERIFY(!status.retryAvailable);
        QCOMPARE(remoteAudioCodecText(status), QStringLiteral("Audio is off"));
        QVERIFY(history.onlyFromFirst(State::RadioOffline, {State::RadioOffline}));

        // The radio returns, and so does audio.
        h.station.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        QCOMPARE(errors.size(), 0);

        audio.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};
QTEST_MAIN(TestRemoteMediaController)
#include "tst_remote_media_controller.moc"
