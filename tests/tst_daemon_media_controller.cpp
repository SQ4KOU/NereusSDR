// =================================================================
// tests/tst_daemon_media_controller.cpp  (NereusSDR)
// =================================================================
// R3 daemon media controller integration coverage over a real authenticated
// StationServer/StationClient control session. Synthetic I/Q is test-only;
// production reaches the source exclusively through RadioModel's tagged tap.
// =================================================================

#include <QtTest>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/FFTEngine.h"
#include "core/P2RadioConnection.h"
#include "core/DdcAssignment.h"
#include "core/WidebandFrameAccumulator.h"
#include "core/HpsdrModel.h"
#include "core/NoiseFloorEstimator.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/Ps3DisplayCodec.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/LibDataChannelMediaTransport.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/RemoteSpectrumContext.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numbers>
#include <numeric>
#include <thread>
#include <utility>

using namespace NereusSDR;

namespace {

constexpr char kConnectionId[] = "11111111-2222-4333-8444-555555555555";

// Messages the controller wrote to its own log category while installed.
QStringList g_daemonMediaMessages;
void captureDaemonMediaMessages(QtMsgType, const QMessageLogContext& context,
                                const QString& message)
{
    if (context.category && QByteArray(context.category) == "nereus.daemon.media") {
        g_daemonMediaMessages.append(message);
    }
}

bool isKeyframe(const QByteArray& frame)
{
    return frame.size() > 5 && (static_cast<quint8>(frame.at(5)) & 0x01) != 0;
}

class FakeTransport final : public IMediaTransport {
public:
    explicit FakeTransport(QObject* parent = nullptr) : IMediaTransport(parent) {}

    bool start(const StartOptions& options) override
    {
        startOptions = options;
        started = true;
        return true;
    }
    void stop() override { started = readyState = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray& bytes) override
    {
        if (sctpWindowBytes > 0) {
            const DisplaySendResult result = submitDisplay(bytes);
            return result == DisplaySendResult::Sent || result == DisplaySendResult::Queued;
        }
        if (!readyState) { return false; }
        displays.append(bytes);
        const auto callback = onDisplaySend;
        if (callback) { return callback(); }
        return true;
    }
    // With sctpWindowBytes set, models libdatachannel over usrsctp on a
    // link whose acknowledgements have not come back: SCTP takes a message
    // only while it fits beside the unacknowledged bytes; otherwise the
    // library holds one message, and a further one is Busy until the held
    // message goes out. `displays` records what went to SCTP, in order.
    DisplaySendResult submitDisplay(const QByteArray& bytes) override
    {
        if (sctpWindowBytes <= 0 && nextSubmitResult) {
            const DisplaySendResult result = *std::exchange(nextSubmitResult, std::nullopt);
            if (readyState && (result == DisplaySendResult::Sent
                               || result == DisplaySendResult::Queued)) {
                displays.append(bytes);
            }
            return result;
        }
        if (sctpWindowBytes <= 0) { return IMediaTransport::submitDisplay(bytes); }
        if (!readyState) { return DisplaySendResult::Refused; }
        if (!heldDisplay.isEmpty()) {
            ++busyDisplays;
            return DisplaySendResult::Busy;
        }
        if (unacknowledgedBytes + bytes.size() <= sctpWindowBytes) {
            unacknowledgedBytes += bytes.size();
            displays.append(bytes);
            return DisplaySendResult::Sent;
        }
        heldDisplay = bytes;
        ++queuedDisplays;
        return DisplaySendResult::Queued;
    }
    bool displayBusy() const override { return sctpWindowBytes > 0 && !heldDisplay.isEmpty(); }
    // The peer acknowledges everything outstanding; a held message goes out.
    void acknowledgeDisplayWindow()
    {
        unacknowledgedBytes = 0;
        if (heldDisplay.isEmpty()) { return; }
        unacknowledgedBytes = heldDisplay.size();
        displays.append(std::exchange(heldDisplay, {}));
        emit displayWritable();
    }
    bool sendRtp(const QByteArray& packet) override
    {
        ++rtpAttempts;
        if (readyState) { rtpPackets.append(packet); }
        return readyState;
    }
    bool isReady() const override { return readyState; }
    void becomeReady() { readyState = true; emit ready(); }

    bool started{false};
    bool readyState{false};
    StartOptions startOptions{Role::Answerer, 0};
    QList<QByteArray> displays;
    QList<QByteArray> rtpPackets;
    int rtpAttempts{0};
    std::function<bool()> onDisplaySend;
    qsizetype sctpWindowBytes{0};
    qsizetype unacknowledgedBytes{0};
    QByteArray heldDisplay;
    int busyDisplays{0};
    int queuedDisplays{0};
    // The outcome of the next submitDisplay() outside window mode.
    std::optional<DisplaySendResult> nextSubmitResult;
};

class ClosingControlTransport final : public Test::LoopbackTransport {
public:
    ClosingControlTransport() : LoopbackTransport(QStringLiteral("station")) {}
    void sendText(const QByteArray& wire) override
    {
        if (!closeOnOp.isEmpty()
            && wire.contains(QByteArray("\"op\":\"") + closeOnOp + '"')) {
            closeOnOp.clear();
            closeLink(QStringLiteral("test synchronous send closure"));
            return;
        }
        LoopbackTransport::sendText(wire);
    }
    QByteArray closeOnOp;
};

QVector<float> syntheticIq(int complexSamples, double cyclesPerSample)
{
    QVector<float> samples;
    samples.reserve(complexSamples * 2);
    for (int sample = 0; sample < complexSamples; ++sample) {
        const double phase = 2.0 * std::numbers::pi * cyclesPerSample * sample;
        samples.append(static_cast<float>(std::cos(phase)));
        samples.append(static_cast<float>(std::sin(phase)));
    }
    return samples;
}

QJsonObject plane()
{
    return {{QStringLiteral("detector"), static_cast<int>(SpectrumDetectorMode::Peak)},
            {QStringLiteral("averageMode"), -1},
            {QStringLiteral("averageAlpha"), 0.0}};
}

QJsonObject subscription(quint32 endpointId, quint32 revision, int sliceId,
                         double centreHz, int fftSize = 1024)
{
    return {{QStringLiteral("op"), QStringLiteral("subscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
            {QStringLiteral("revision"), static_cast<qint64>(revision)},
            {QStringLiteral("sliceId"), sliceId},
            {QStringLiteral("tier"), QStringLiteral("wide")},
            {QStringLiteral("fftSize"), fftSize},
            {QStringLiteral("windowType"), static_cast<int>(WindowFunction::Hann)},
            {QStringLiteral("centreHz"), centreHz},
            {QStringLiteral("spanHz"), 48000.0},
            {QStringLiteral("pixels"), 128},
            {QStringLiteral("fps"), 60},
            {QStringLiteral("framesPerLine"), 1},
            {QStringLiteral("trace"), plane()},
            {QStringLiteral("waterfall"), plane()},
            {QStringLiteral("minDbm"), -180.0},
            {QStringLiteral("maxDbm"), 0.0},
            {QStringLiteral("wideSpanFactor"), 0.0}};
}

QJsonObject tieredSubscription(quint32 endpointId, quint32 revision, int sliceId,
                               double centreHz, const QString& tier, int fftSize)
{
    QJsonObject request = subscription(endpointId, revision, sliceId, centreHz, fftSize);
    request.insert(QStringLiteral("tier"), tier);
    return request;
}

QJsonObject unsubscription(quint32 endpointId)
{
    return {{QStringLiteral("op"), QStringLiteral("unsubscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)}};
}

int messageCount(const QSignalSpy& messages, const QString& op, quint32 endpointId)
{
    int count = 0;
    for (const auto& call : messages) {
        const QJsonObject message = call.at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == op
            && static_cast<quint32>(message.value(QStringLiteral("endpointId")).toInteger())
                == endpointId) {
            ++count;
        }
    }
    return count;
}

int messageIndex(const QSignalSpy& messages, const QString& op, quint32 endpointId)
{
    for (int index = 0; index < messages.count(); ++index) {
        const QJsonObject message = messages.at(index).at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == op
            && static_cast<quint32>(message.value(QStringLiteral("endpointId")).toInteger())
                == endpointId) {
            return index;
        }
    }
    return -1;
}

QJsonObject messageFor(const QSignalSpy& messages, const QString& op,
                       quint32 endpointId)
{
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        const QJsonObject message = it->at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == op
            && static_cast<quint32>(message.value(QStringLiteral("endpointId")).toInteger())
                == endpointId) {
            return message;
        }
    }
    return {};
}

QJsonObject allocationFor(const QSignalSpy& messages, quint32 endpointId,
                          quint32 revision)
{
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        const QJsonObject message = it->at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == QLatin1String("allocation-result")
            && static_cast<quint32>(message.value(QStringLiteral("endpointId")).toInteger())
                == endpointId
            && static_cast<quint32>(message.value(QStringLiteral("revision")).toInteger())
                == revision) {
            return message;
        }
    }
    return {};
}

int displayMessageCount(const QList<QByteArray>& messages, const QByteArray& magic)
{
    return static_cast<int>(std::count_if(messages.cbegin(), messages.cend(),
        [&magic](const QByteArray& bytes) { return bytes.startsWith(magic); }));
}

Ps3Snapshot maximumPs3Snapshot(quint64 generation, quint64 sequence)
{
    Ps3Snapshot snapshot;
    snapshot.channelId = 3;
    snapshot.sessionGeneration = generation;
    snapshot.sequence = sequence;
    snapshot.capturedAtUnixMilliseconds = static_cast<qint64>(sequence);
    snapshot.sampleCount = Ps3Snapshot::kMaxSampleCount;
    snapshot.correctionCount = Ps3Snapshot::kMaxCorrectionCount;
    const auto fill = [](std::vector<double>& values, int count, double base) {
        values.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            values.push_back(base + static_cast<double>(index) / 8192.0);
        }
    };
    fill(snapshot.x, snapshot.sampleCount, 0.0);
    fill(snapshot.ym, snapshot.sampleCount, 1.0);
    fill(snapshot.yc, snapshot.sampleCount, 2.0);
    fill(snapshot.ys, snapshot.sampleCount, 3.0);
    fill(snapshot.xmCorrection, snapshot.correctionCount, 4.0);
    fill(snapshot.ymCorrection, snapshot.correctionCount, 5.0);
    fill(snapshot.xaCorrection, snapshot.correctionCount, 6.0);
    fill(snapshot.yaCorrection, snapshot.correctionCount, 7.0);
    return snapshot;
}

QJsonObject audioControl(quint32 revision, bool enabled)
{
    return {{QStringLiteral("op"), QStringLiteral("audio")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("revision"), static_cast<qint64>(revision)},
            {QStringLiteral("enabled"), enabled}};
}

// Every audio context a raw control peer has received, in arrival order.
QList<QJsonObject> receivedAudioContexts(const Test::LoopbackTransport& peer)
{
    QList<QJsonObject> contexts;
    for (const QByteArray& wire : peer.received()) {
        SessionMessage message;
        if (SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::MediaControl
            && message.mediaPayload.value(QStringLiteral("op"))
                == QLatin1String("audio-context")) {
            contexts.append(message.mediaPayload);
        }
    }
    return contexts;
}

QStringList sortedKeys(const QJsonObject& object)
{
    QStringList keys = object.keys();
    keys.sort();
    return keys;
}

// The minor-7 audio context, key for key and JSON type for type: what every
// GUI built before the audio status detail parses.
bool hasLegacyAudioContextShape(const QJsonObject& context)
{
    const QStringList legacyKeys{
        QStringLiteral("connectionId"), QStringLiteral("enabled"),
        QStringLiteral("firstSequence"), QStringLiteral("firstTimestamp"),
        QStringLiteral("generation"), QStringLiteral("op"),
        QStringLiteral("revision"), QStringLiteral("ssrc")};
    return sortedKeys(context) == legacyKeys
        && context.value(QStringLiteral("op")) == QLatin1String("audio-context")
        && context.value(QStringLiteral("connectionId")).isString()
        && context.value(QStringLiteral("enabled")).isBool()
        && context.value(QStringLiteral("revision")).isDouble()
        && context.value(QStringLiteral("generation")).isDouble()
        && context.value(QStringLiteral("ssrc")).isDouble()
        && context.value(QStringLiteral("firstSequence")).isDouble()
        && context.value(QStringLiteral("firstTimestamp")).isDouble();
}

struct Harness {
    QTemporaryDir directory;
    AppSettings settings;
    P2RadioConnection p2;
    RadioModel radio;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy settingsProxy;
    StationClient client{&remote, &settingsProxy};
    QPointer<FakeTransport> mediaTransport;
    // When set, Core's media transport comes from here instead of a fake.
    std::function<IMediaTransport*(QObject*)> realTransport;
    QPointer<ClosingControlTransport> stationTransport;
    qint64 nowNs{0};
    QPointer<QTimer> manualSender;
    DaemonMediaController controller;
    int sliceId{-1};
    int spareSliceId{-1};
    int streamIndex{-1};

    explicit Harness(std::optional<DisplayBudgetLimits> limits = std::nullopt)
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, directory.path())
        , controller(&server, &radio, nullptr,
                     [this](QObject* parent) -> IMediaTransport* {
                         if (realTransport) { return realTransport(parent); }
                         mediaTransport = new FakeTransport(parent);
                         return mediaTransport;
                     },
                     [this] { return nowNs; })
    {
        Q_ASSERT(directory.isValid());
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        sliceId = radio.addSlice();
        Q_ASSERT(sliceId >= 0);
        // RadioModel deliberately keeps one local slice alive. Keep this
        // second slice so removal below exercises its real removal signal.
        // Do not put addSlice() inside Q_ASSERT: release test builds compile
        // assertions out and would silently skip this required setup.
        spareSliceId = radio.addSlice();
        Q_ASSERT(spareSliceId >= 0);
        SliceModel* const slice = radio.sliceById(sliceId);
        Q_ASSERT(slice != nullptr);
        streamIndex = slice->streamIndex();
        Q_ASSERT(streamIndex >= 0 && radio.streamActive(streamIndex));
        server.setMediaEnabled(true);
        if (limits) {
            QVERIFY(server.setDisplayBudgetLimits(*limits));
        }
    }

    void enableWidebandSource()
    {
        p2.setBoardForTest(HPSDRHW::Saturn);
        radio.injectConnectionForTest(&p2);
        RadioInfo info;
        info.protocol = ProtocolVersion::Protocol2;
        radio.setLastRadioInfoForTest(info);
        radio.wireWidebandConnectionForTest();
    }

    void establishSession()
    {
        auto* stationLink = new ClosingControlTransport;
        stationTransport = stationLink;
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(server.mediaAvailable());
    }

    void startReadyPeer()
    {
        QVERIFY(client.sendMediaControl({
            {QStringLiteral("op"), QStringLiteral("start")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}},
            client.sessionEpoch()));
        QTRY_VERIFY(mediaTransport);
        QVERIFY(mediaTransport->started);
        QVERIFY(mediaTransport->startOptions.localAudioSsrc != 0);
        mediaTransport->becomeReady();
    }

    void feedRadio(double cyclesPerSample = 0.125)
    {
        // This invokes the actual production RadioModel tap. The daemon
        // source's DirectConnection bridge owns the worker-thread hop.
        const bool invoked = QMetaObject::invokeMethod(
            &radio, "rawIqDataForStream", Qt::DirectConnection,
            Q_ARG(int, streamIndex),
            Q_ARG(QVector<float>, syntheticIq(1026, cyclesPerSample)));
        QVERIFY(invoked);
    }

    void feedZeroRadio(int complexSamples = 1026)
    {
        // Exercise the same tagged RadioModel tap with a known full-source
        // floor: FFTEngine maps every zero-power bin to -200 dBFS.
        const QVector<float> zeros(complexSamples * 2, 0.0f);
        const bool invoked = QMetaObject::invokeMethod(
            &radio, "rawIqDataForStream", Qt::DirectConnection,
            Q_ARG(int, streamIndex), Q_ARG(QVector<float>, zeros));
        QVERIFY(invoked);
    }

    void useManualDisplayTicks()
    {
        for (QTimer* timer : controller.findChildren<QTimer*>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (timer->interval() == static_cast<int>(kDisplaySenderIntervalMs)) {
                manualSender = timer;
                // Production keeps starting this same timer. A long fixture
                // interval leaves the real timeout slot under explicit control.
                timer->setInterval(60'000);
                return;
            }
        }
        QFAIL("display sender timer missing");
    }

    void sendDisplayTick()
    {
        QTimer* sender = manualSender;
        const auto timers = controller.findChildren<QTimer*>(
            QString(), Qt::FindDirectChildrenOnly);
        for (QTimer* timer : timers) {
            if (timer->interval() == static_cast<int>(kDisplaySenderIntervalMs)) {
                sender = timer;
                break;
            }
        }
        QVERIFY(sender);
        sender->stop();
        QVERIFY(QMetaObject::invokeMethod(sender, "timeout", Qt::DirectConnection));
    }

    void finish()
    {
        client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

} // namespace

class TstDaemonMediaController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
#ifndef HAVE_FFTW3
        QSKIP("FFTEngine has no FFTW3 backend in this build");
#endif
        qRegisterMetaType<QVector<float>>();
        qRegisterMetaType<MediaSourceKey>();
    }

    void authenticatedControlProducesContextThenDecodedDisplayAndUnsubscribes();
    void extendedPermissionFirstCaptureAndSharedEndpointLifetimes();
    void widebandSourceReplacementAndSessionRetirement();
    void olderPeerKeepsLegacyContextAndCannotAcquireWideband();
    void minorSevenPeerReceivesLegacyAudioContexts();
    void minorEightAudioContextsCarryEncoderOrReason();
    void configuredAudioBitrateReachesOfferAndContext();
    void minorEightPeerReceivesTodaysSpectrumContext();
    void minorNineSpectrumContextsReportTheGrant();
    void localCaptureDuringConnectingGetsIdentityBeforeFirstAdcRow();
    void synchronousDisplayClosureRetiresDemandAndAllowsNewPeer_data();
    void synchronousDisplayClosureRetiresDemandAndAllowsNewPeer();
    void synchronousControlClosureRetiresDemand_data();
    void synchronousControlClosureRetiresDemand();
    void staleRevisionAndWrongEpochPreserveTheActiveSource();
    void streamRemovalRetiresEndpointAndSource();
    void nonoverlappingCropIsRejectedBeforeSourceActivation();
    void fullSourceNoiseFloorFollowsContextCalibrationCadenceAndRetirement();
    void nonzeroNoiseFloorMatchesLocalFftBeforeCropAndQuantization();
    void configuredBudgetReturnsExactAllocationResultsAndRejectsOvercommit();
    void revisionedUnsubscribeCannotRetireOrResurrectNewerState();
    void boundedNonliveCacheNeverResurrectsEvictedEndpointIds();
    void rapidReplacementCannotMintSpectrumBurstCredit();
    void ps3PinsCurrentMultipartFrameAndPromotesOnlyLatest();
    void runtimeCapDecreaseAllowsOnlyComponentwiseReductions();
    void failedDisplayAttemptDebitsCreditAndRecoversWithKeyframe();
    void exhaustedDisplayCreditDoesNotBlockAudioRtp();
    void mediaPeerReplacementDoesNotMintDisplayCredit();
    void mixedSpectrumAndPs3StayWithinInjectedIntervalBoundsAndBothProgress();
    void failedMiddlePs3ChunkDropsRemainderPromotesLatestAndDoesNotRefund();
    void ps3SnapshotCompletesWhileAcknowledgementsLag();
    void spectrumAndPs3ShareALaggingWindowAndBothProgress();
    void radioProductionRestartWithinEpochDoesNotMintDisplayCredit();
    void replayedAllocationResultCanSynchronouslyRetireControllerState();
    void sourceRetirementPublishesZeroChargeAtLatestOperationRevision();
    void failedSourceUpdateReleasesAllocationAndCanRecover();
    void sharedEngineKeepsOtherPanWhileNeighbourChurns();
    void sharedEngineKeepsOtherPanAcrossFrameRates();
    void grantReportsLargestSizeSharedEngineAndSourceBins();
    void budgetChargesGrantedPixels();
    void regrantAfterNeighbourLeavesStaysWithinAdmittedCharge();
    void outOfRangeRequestsAreRejectedAndLeaveEndpointUntouched();
    void displayDiagnosticsMeasureSentFramesRefusalsAndErrors();
    void realDisplayErrorIsCountedAndLoggedOnce();
    void displayDiagnosticsLineReportsBytesAndFragments();
    void staleEpochAndForeignConnectionLeaveEndpointsUntouched();
};

void TstDaemonMediaController::configuredBudgetReturnsExactAllocationResultsAndRejectsOvercommit()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 7});
    harness.establishSession();
    QVERIFY(harness.server.displayBudgetAvailable());
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();

    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    const QJsonObject first = subscription(70, 1, harness.sliceId, centre);
    QVERIFY(harness.client.sendMediaControl(first, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 70, 1).isEmpty());
    const QJsonObject accepted = allocationFor(controls, 70, 1);
    QCOMPARE(accepted.size(), 11);
    QCOMPARE(accepted.value(QStringLiteral("accepted")).toBool(), true);
    QCOMPARE(accepted.value(QStringLiteral("budgetGeneration")).toInteger(), qint64{7});
    QCOMPARE(accepted.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{1});
    QCOMPARE(accepted.value(QStringLiteral("applicationBytesPerSecond")).toInteger(),
             static_cast<qint64>(cost.charge.applicationBytesPerSecond));
    QCOMPARE(accepted.value(QStringLiteral("spectrumSampleUnitsPerSecond")).toInteger(),
             static_cast<qint64>(cost.charge.spectrumSampleUnitsPerSecond));
    QCOMPARE(accepted.value(QStringLiteral("messagesPerSecond")).toInteger(), qint64{60});

    QVERIFY(harness.client.sendMediaControl(
        subscription(71, 1, harness.sliceId, centre), harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 71, 1).isEmpty());
    const QJsonObject refused = allocationFor(controls, 71, 1);
    QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(), false);
    QCOMPARE(refused.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{0});
    QCOMPARE(refused.value(QStringLiteral("applicationBytesPerSecond")).toInteger(), qint64{0});
    QCOMPARE(harness.controller.activeEndpointCount(), 1);

    QJsonObject malformed = subscription(72, 1, harness.sliceId, centre);
    malformed.insert(QStringLiteral("unexpected"), true);
    QVERIFY(harness.client.sendMediaControl(malformed, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 72, 1).isEmpty());
    QVERIFY(!allocationFor(controls, 72, 1).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeEndpointCount(), 1);
    harness.finish();
}

void TstDaemonMediaController::revisionedUnsubscribeCannotRetireOrResurrectNewerState()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 9});
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QVERIFY(harness.client.sendMediaControl(
        subscription(80, 2, harness.sliceId, centre), harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 80, 2).value(QStringLiteral("accepted")).toBool());

    const auto unsubscribe = [&](quint32 revision) {
        return harness.client.sendMediaControl({
            {QStringLiteral("op"), QStringLiteral("unsubscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), 80},
            {QStringLiteral("revision"), static_cast<qint64>(revision)}},
            harness.client.sessionEpoch());
    };
    QVERIFY(unsubscribe(1));
    QTRY_VERIFY(!allocationFor(controls, 80, 1).isEmpty());
    QCOMPARE(allocationFor(controls, 80, 1).value(QStringLiteral("accepted")).toBool(), false);
    QCOMPARE(harness.controller.activeEndpointCount(), 1);

    QVERIFY(unsubscribe(3));
    QTRY_VERIFY(allocationFor(controls, 80, 3).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeEndpointCount(), 0);

    QJsonObject malformedReuse = subscription(80, 4, harness.sliceId, centre);
    malformedReuse.insert(QStringLiteral("unexpected"), true);
    QVERIFY(harness.client.sendMediaControl(malformedReuse, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 80, 4).isEmpty());
    QCOMPARE(allocationFor(controls, 80, 4).value(QStringLiteral("accepted")).toBool(), false);
    QVERIFY(harness.client.sendMediaControl(
        subscription(80, 5, harness.sliceId, centre), harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 80, 5).isEmpty());
    QCOMPARE(allocationFor(controls, 80, 5).value(QStringLiteral("accepted")).toBool(), false);
    QCOMPARE(harness.controller.activeEndpointCount(), 0);
    harness.finish();
}

void TstDaemonMediaController::boundedNonliveCacheNeverResurrectsEvictedEndpointIds()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 11});
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);

    for (quint32 endpointId = 1; endpointId <= 65; ++endpointId) {
        QJsonObject invalid = subscription(endpointId, 1, std::numeric_limits<int>::max(), centre);
        QVERIFY(harness.client.sendMediaControl(invalid, harness.client.sessionEpoch()));
        QTRY_VERIFY(!allocationFor(controls, endpointId, 1).isEmpty());
        QVERIFY(!allocationFor(controls, endpointId, 1)
                     .value(QStringLiteral("accepted")).toBool());
    }
    QCOMPARE(harness.controller.activeEndpointCount(), 0);

    // A failed initial request still in the bounded cache may retry with a
    // newer revision, while an evicted identifier at or below high-water may
    // never be resurrected.
    QJsonObject retry = subscription(65, 2, harness.sliceId, centre);
    QVERIFY(harness.client.sendMediaControl(retry, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 65, 2).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeEndpointCount(), 1);

    QVERIFY(harness.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("unsubscribe")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
        {QStringLiteral("endpointId"), 65},
        {QStringLiteral("revision"), 3}}, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 65, 3).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeEndpointCount(), 0);

    retry = subscription(1, 2, harness.sliceId, centre);
    retry.insert(QStringLiteral("unexpected"), true);
    QVERIFY(harness.client.sendMediaControl(retry, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 1, 2).isEmpty());
    QVERIFY(!allocationFor(controls, 1, 2).value(QStringLiteral("accepted")).toBool());
    retry = subscription(1, 3, harness.sliceId, centre);
    QVERIFY(harness.client.sendMediaControl(retry, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 1, 3).isEmpty());
    QVERIFY(!allocationFor(controls, 1, 3).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeEndpointCount(), 0);
    harness.finish();
}

void TstDaemonMediaController::rapidReplacementCannotMintSpectrumBurstCredit()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(4096, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 13});
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QJsonObject request = subscription(90, 1, harness.sliceId, centre);
    request.insert(QStringLiteral("pixels"), 4096);
    // Include enough source bins that the first actual frame exhausts the
    // remaining credit needed for another worst-case preflight. The budget
    // charges granted pixels, so the FFT must supply all 4096 of them.
    request.insert(QStringLiteral("spanHz"), 192000.0);
    request.insert(QStringLiteral("fftSize"), 4096);
    request.insert(QStringLiteral("fps"), 60);

    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 90, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 90)
            .value(QStringLiteral("revision")).toInteger() == 1;
    })());
    harness.sendDisplayTick();
    QTRY_COMPARE(harness.mediaTransport->displays.size(), 1);

    for (quint32 revision = 2; revision <= 8; ++revision) {
        request.insert(QStringLiteral("revision"), static_cast<qint64>(revision));
        QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
        QTRY_VERIFY(allocationFor(controls, 90, revision)
                        .value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(([&] {
            harness.feedRadio(0.125 + static_cast<double>(revision) / 1024.0);
            return messageFor(controls, QStringLiteral("context"), 90)
                .value(QStringLiteral("revision")).toInteger() == revision;
        })());
        harness.sendDisplayTick();
    }
    quint64 attemptedAtZero = 0;
    for (const QByteArray& bytes : std::as_const(harness.mediaTransport->displays)) {
        attemptedAtZero += static_cast<quint64>(bytes.size());
    }
    QVERIFY(attemptedAtZero <= kMaximumSpectrumDisplayFrameBytes);
    QCOMPARE(harness.mediaTransport->displays.size(), 1);

    harness.nowNs = 1'000'000'000;
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.1875);
        harness.sendDisplayTick();
        return harness.mediaTransport->displays.size() == 2;
    })());
    harness.finish();
}

void TstDaemonMediaController::ps3PinsCurrentMultipartFrameAndPromotesOnlyLatest()
{
    const DisplayBudgetCharge charge = ps3DisplayCharge();
    Harness harness(DisplayBudgetLimits{charge.applicationBytesPerSecond, 1, 15});
    harness.useManualDisplayTicks();
    harness.establishSession();
    harness.startReadyPeer();
    PureSignalSessionFacade* facade = harness.radio.pureSignalFacade();
    facade->setRemoteAmpViewSubscribed(true);
    const quint64 generation = facade->displayGeneration();
    const Ps3Snapshot first = maximumPs3Snapshot(generation, 1);
    const Ps3Snapshot latest = maximumPs3Snapshot(generation, 2);
    QCOMPARE(Ps3DisplayCodec::encode(first).size(), 3);

    facade->displaySnapshotReady(first);
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1);
    facade->displaySnapshotReady(latest);
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1); // no same-time credit

    for (int attempt = 1; attempt <= 5; ++attempt) {
        harness.nowNs += 50'000'000;
        harness.sendDisplayTick();
    }
    QCOMPARE(harness.mediaTransport->displays.size(), 6);

    Ps3DisplayAssembler assembler(generation);
    QList<quint64> completed;
    for (const QByteArray& bytes : std::as_const(harness.mediaTransport->displays)) {
        QString error;
        const auto frame = assembler.accept(bytes, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        if (frame) { completed.append(frame->sequence); }
    }
    QCOMPARE(completed, QList<quint64>({1, 2}));
    harness.finish();
}

void TstDaemonMediaController::runtimeCapDecreaseAllowsOnlyComponentwiseReductions()
{
    const SpectrumDisplayCost initialCost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{initialCost.charge.applicationBytesPerSecond,
                                        initialCost.charge.spectrumSampleUnitsPerSecond, 17});
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QJsonObject request = subscription(95, 1, harness.sliceId, centre);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 95, 1).value(QStringLiteral("accepted")).toBool());

    const SpectrumDisplayCost loweredCap = *spectrumDisplayCost(64, 30, false);
    QVERIFY(harness.server.setDisplayBudgetLimits(
        {loweredCap.charge.applicationBytesPerSecond,
         loweredCap.charge.spectrumSampleUnitsPerSecond, 18}));
    QTRY_COMPARE(harness.client.remoteDisplayBudgetLimits()->generation, quint32{18});

    request.insert(QStringLiteral("revision"), 2);
    request.insert(QStringLiteral("fps"), 59);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 95, 2).value(QStringLiteral("accepted")).toBool());

    request.insert(QStringLiteral("revision"), 3);
    request.insert(QStringLiteral("fps"), 60);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 95, 3).isEmpty());
    const QJsonObject refused = allocationFor(controls, 95, 3);
    QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(refused.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{2});
    const SpectrumDisplayCost retained = *spectrumDisplayCost(128, 59, false);
    QCOMPARE(refused.value(QStringLiteral("applicationBytesPerSecond")).toInteger(),
             static_cast<qint64>(retained.charge.applicationBytesPerSecond));
    request.insert(QStringLiteral("revision"), 4);
    request.insert(QStringLiteral("fps"), 59);
    request.insert(QStringLiteral("sliceId"), std::numeric_limits<int>::max());
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 95, 4).isEmpty());
    const QJsonObject sourceRefused = allocationFor(controls, 95, 4);
    QVERIFY(!sourceRefused.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(sourceRefused.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{2});
    QCOMPARE(sourceRefused.value(QStringLiteral("applicationBytesPerSecond")).toInteger(),
             static_cast<qint64>(retained.charge.applicationBytesPerSecond));

    QJsonObject older = request;
    older.insert(QStringLiteral("revision"), 3);
    older.insert(QStringLiteral("sliceId"), harness.sliceId);
    older.insert(QStringLiteral("fps"), 58);
    QVERIFY(harness.client.sendMediaControl(older, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 95, 3).isEmpty());
    QCOMPARE(allocationFor(controls, 95, 3)
                 .value(QStringLiteral("acceptedRevision")).toInteger(), qint64{2});

    QVERIFY(harness.server.setDisplayBudgetLimits(
        {initialCost.charge.applicationBytesPerSecond,
         initialCost.charge.spectrumSampleUnitsPerSecond, 19}));
    const int priorRevisionFourResults = messageCount(
        controls, QStringLiteral("allocation-result"), 95);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(messageCount(controls, QStringLiteral("allocation-result"), 95)
                > priorRevisionFourResults);
    const QJsonObject replayed = allocationFor(controls, 95, 4);
    QVERIFY(!replayed.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(replayed.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{2});
    QCOMPARE(harness.controller.activeEndpointCount(), 1);
    harness.finish();
}

void TstDaemonMediaController::failedDisplayAttemptDebitsCreditAndRecoversWithKeyframe()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(4096, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 19});
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QJsonObject request = subscription(96, 1, harness.sliceId, centre);
    request.insert(QStringLiteral("pixels"), 4096);
    // Include enough source bins that the first actual frame exhausts the
    // remaining credit needed for another worst-case preflight. The budget
    // charges granted pixels, so the FFT must supply all 4096 of them.
    request.insert(QStringLiteral("spanHz"), 192000.0);
    request.insert(QStringLiteral("fftSize"), 4096);
    request.insert(QStringLiteral("fps"), 60);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 96, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 96)
            .value(QStringLiteral("revision")).toInteger() == 1;
    })());

    bool reject = true;
    harness.mediaTransport->onDisplaySend = [&reject] { return !reject; };
    harness.sendDisplayTick();
    QTRY_COMPARE(harness.mediaTransport->displays.size(), 1);
    reject = false;

    request.insert(QStringLiteral("revision"), 2);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 96, 2).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.1875);
        return messageFor(controls, QStringLiteral("context"), 96)
            .value(QStringLiteral("revision")).toInteger() == 2;
    })());
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1); // failed attempt was not refunded

    harness.nowNs = 1'000'000'000;
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.21875);
        harness.sendDisplayTick();
        return harness.mediaTransport->displays.size() == 2;
    })());
    DisplayCodecDecoder decoder;
    const DisplayCodecDecodeResult recovered = decoder.decode(
        harness.mediaTransport->displays.constLast());
    QCOMPARE(recovered.disposition, DisplayCodecDisposition::Accepted);
    harness.finish();
}

void TstDaemonMediaController::displayDiagnosticsMeasureSentFramesRefusalsAndErrors()
{
    g_daemonMediaMessages.clear();
    const QtMessageHandler previous = qInstallMessageHandler(captureDaemonMediaMessages);
    const auto restore = qScopeGuard([previous] { qInstallMessageHandler(previous); });

    Harness harness;
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    QCOMPARE(harness.controller.displayDiagnostics(), DaemonDisplayDiagnostics{});
    QJsonObject request = subscription(
        61, 1, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex));
    request.insert(QStringLiteral("pixels"), 1024);
    request.insert(QStringLiteral("spanHz"), 192000.0);
    request.insert(QStringLiteral("fftSize"), 4096);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 61).isEmpty();
    })());

    int cycle = 0;
    const auto sendOneFrame = [&harness, &cycle] {
        const qsizetype before = harness.mediaTransport->displays.size();
        QTRY_VERIFY(([&] {
            harness.feedRadio(0.125 + 0.0078125 * (++cycle % 16));
            harness.sendDisplayTick();
            return harness.mediaTransport->displays.size() == before + 1;
        })());
    };
    QList<QByteArray> accepted;
    for (int i = 0; i < 4; ++i) {
        sendOneFrame();
        accepted.append(harness.mediaTransport->displays.constLast());
    }

    // 1024/1024 points, no 3D row: 42 + 2 * (3 + 5 * 8 + 1024) = 2176 bytes.
    QVERIFY(isKeyframe(accepted.constFirst()));
    QCOMPARE(accepted.constFirst().size(),
             int(kDisplayCodecHeaderBytes + 2 * displayCodecWorstCasePlaneBytes(1024)));
    QCOMPARE(accepted.constFirst().size(), 2176);
    quint32 largestDelta = 0;
    for (qsizetype i = 1; i < accepted.size(); ++i) {
        QVERIFY(!isKeyframe(accepted.at(i)));
        QVERIFY(accepted.at(i).size() <= accepted.constFirst().size());
        largestDelta = std::max(largestDelta, quint32(accepted.at(i).size()));
    }
    DaemonDisplayDiagnostics diagnostics = harness.controller.displayDiagnostics();
    QCOMPARE(diagnostics.displayMaxKeyframeBytes, quint32(2176));
    QCOMPARE(diagnostics.displayMaxDeltaBytes, largestDelta);
    QCOMPARE(diagnostics.displayMaxFragments, quint32(3)); // ceil(2176 / 876)
    QCOMPARE(diagnostics.displaySendRefusals, quint64(0));
    QCOMPARE(diagnostics.displayTransportErrors, quint64(0));

    // A full transport refuses the frame: counted, never resent, never
    // measured, and the next frame that goes is a keyframe.
    harness.mediaTransport->onDisplaySend = [] { return false; };
    sendOneFrame();
    harness.mediaTransport->onDisplaySend = {};
    QCOMPARE(harness.controller.displayDiagnostics().displaySendRefusals, quint64(1));
    sendOneFrame();
    QVERIFY(isKeyframe(harness.mediaTransport->displays.constLast()));
    sendOneFrame();
    QVERIFY(!isKeyframe(harness.mediaTransport->displays.constLast()));

    // A frame the library takes but holds until SCTP has room is still
    // delivered: measured, counted as queued late, not refused, and the
    // delta chain goes on.
    harness.mediaTransport->nextSubmitResult = IMediaTransport::DisplaySendResult::Queued;
    sendOneFrame();
    QVERIFY(!isKeyframe(harness.mediaTransport->displays.constLast()));
    QCOMPARE(harness.controller.displayDiagnostics().displayQueuedLate, quint64(1));
    QCOMPARE(harness.controller.displayDiagnostics().displaySendRefusals, quint64(1));
    sendOneFrame();
    QVERIFY(!isKeyframe(harness.mediaTransport->displays.constLast()));

    // A media error that is not about the display channel (signalling, the
    // transport factory, audio) is logged once but is not a display error.
    emit harness.mediaTransport->errorOccurred(QStringLiteral("remote candidate rejected"));
    emit harness.mediaTransport->errorOccurred(QStringLiteral("remote candidate rejected"));
    QCOMPARE(harness.controller.displayDiagnostics().displayTransportErrors, quint64(0));
    QCOMPARE(g_daemonMediaMessages.filter(QStringLiteral("remote candidate rejected")).size(), 1);
    sendOneFrame();
    QVERIFY(!isKeyframe(harness.mediaTransport->displays.constLast()));

    // Display-channel errors: each counts, each distinct text is logged
    // once, and each forces the next frame to be a keyframe like a failed
    // send.
    emit harness.mediaTransport->displayErrorOccurred(QStringLiteral("sctp send failed"));
    emit harness.mediaTransport->displayErrorOccurred(QStringLiteral("sctp send failed"));
    emit harness.mediaTransport->displayErrorOccurred(QStringLiteral("dtls record rejected"));
    diagnostics = harness.controller.displayDiagnostics();
    QCOMPARE(diagnostics.displayTransportErrors, quint64(3));
    QCOMPARE(diagnostics.displaySendRefusals, quint64(1));
    QCOMPARE(g_daemonMediaMessages.filter(QStringLiteral("media transport error:")).size(), 2);
    QCOMPARE(g_daemonMediaMessages.filter(QStringLiteral("sctp send failed")).size(), 1);
    sendOneFrame();
    QVERIFY(isKeyframe(harness.mediaTransport->displays.constLast()));
    QCOMPARE(harness.controller.displayDiagnostics().displayMaxKeyframeBytes, quint32(2176));

    // Retiring the peer writes the final line; a new peer starts from zero.
    harness.mediaTransport->onDisplaySend = [transport = harness.mediaTransport] {
        emit transport->closed();
        return true;
    };
    sendOneFrame();
    QTRY_VERIFY(harness.mediaTransport.isNull());
    const QStringList finals =
        g_daemonMediaMessages.filter(QStringLiteral("daemon display diagnostics final"));
    QCOMPARE(finals.size(), 1);
    QVERIFY2(finals.constFirst().contains(
                 QStringLiteral("largestKeyframe=2176 bytes/3 fragments")),
             qPrintable(finals.constFirst()));
    QVERIFY(finals.constFirst().contains(QStringLiteral("transportErrors=3")));
    harness.startReadyPeer();
    QCOMPARE(harness.controller.displayDiagnostics(), DaemonDisplayDiagnostics{});
    harness.finish();
}

// R-R3-03/R-R3-05: a display error from the real libdatachannel transport,
// through MediaPeer, reaches Core once: counted and logged as a display
// error, never also logged as a media peer error.
void TstDaemonMediaController::realDisplayErrorIsCountedAndLoggedOnce()
{
    g_daemonMediaMessages.clear();
    const QtMessageHandler previous = qInstallMessageHandler(captureDaemonMediaMessages);
    const auto restore = qScopeGuard([previous] { qInstallMessageHandler(previous); });

    Harness harness;
    QPointer<LibDataChannelMediaTransport> core;
    harness.realTransport = [&core](QObject* parent) -> IMediaTransport* {
        core = new LibDataChannelMediaTransport(parent);
        return core;
    };
    harness.useManualDisplayTicks();
    harness.establishSession();

    // The far end answers Core's offer over the session, as a GUI does.
    LibDataChannelMediaTransport far;
    QSignalSpy farDisplays(&far, &IMediaTransport::displayReceived);
    QVERIFY(far.start({IMediaTransport::Role::Answerer, 0x4e523302U}));
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    connect(&harness.client, &StationClient::mediaControlReceived, &far,
            [&far](const QJsonObject& control) {
        const QString op = control.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("description")) {
            QVERIFY(far.acceptDescription(control.value(QStringLiteral("sdp")).toString(),
                                          control.value(QStringLiteral("type")).toString()));
        } else if (op == QLatin1String("candidate")) {
            QVERIFY(far.acceptCandidate(control.value(QStringLiteral("candidate")).toString(),
                                        control.value(QStringLiteral("mid")).toString()));
        }
    });
    connect(&far, &IMediaTransport::localDescription, &harness.client,
            [&harness](const QString& sdp, const QString& type) {
        QVERIFY(harness.client.sendMediaControl({
            {QStringLiteral("op"), QStringLiteral("description")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("sdp"), sdp},
            {QStringLiteral("type"), type}}, harness.client.sessionEpoch()));
    });
    connect(&far, &IMediaTransport::localCandidate, &harness.client,
            [&harness](const QString& candidate, const QString& mid) {
        QVERIFY(harness.client.sendMediaControl({
            {QStringLiteral("op"), QStringLiteral("candidate")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("candidate"), candidate},
            {QStringLiteral("mid"), mid}}, harness.client.sessionEpoch()));
    });
    QVERIFY(harness.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("start")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}},
        harness.client.sessionEpoch()));
    QTRY_VERIFY_WITH_TIMEOUT(core && core->isReady() && far.isReady(), 10'000);

    QVERIFY(harness.client.sendMediaControl(
        subscription(62, 1, harness.sliceId,
                     harness.radio.streamCentreHz(harness.streamIndex)),
        harness.client.sessionEpoch()));
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 62).isEmpty();
    })());
    int cycle = 0;
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        harness.feedRadio(0.125 + 0.0078125 * (++cycle % 16));
        harness.sendDisplayTick();
        return !farDisplays.isEmpty();
    })(), 10'000);
    QCOMPARE(harness.controller.displayDiagnostics().displayTransportErrors, quint64(0));

    // A frame waits to be sent. The far end goes away; before Core's event
    // loop hears of it, the next display send finds the channel closed and
    // libdatachannel throws.
    const quint64 submitted = core->telemetry()->submittedDisplayPayloadBytes;
    harness.feedRadio(0.3125);
    QTest::qWait(200);
    far.stop();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    harness.sendDisplayTick();
    QVERIFY2(core, "Core retired its transport before the send");
    QVERIFY2(core->telemetry()->submittedDisplayPayloadBytes > submitted,
             "no display frame was waiting to be sent");

    QCOMPARE(harness.controller.displayDiagnostics().displayTransportErrors, quint64(1));
    const QStringList displayErrors =
        g_daemonMediaMessages.filter(QStringLiteral("media transport error:"));
    QCOMPARE(displayErrors.size(), 1);
    QVERIFY2(displayErrors.constFirst().contains(QStringLiteral("DataChannel")),
             qPrintable(displayErrors.constFirst()));
    QCOMPARE(g_daemonMediaMessages.filter(QStringLiteral("media peer error:")).size(), 0);
    harness.finish();
}

void TstDaemonMediaController::displayDiagnosticsLineReportsBytesAndFragments()
{
    DaemonDisplayDiagnostics small;
    small.displayMaxKeyframeBytes = 2977; // 1024/1024 points with a 768-point 3D row
    small.displayMaxDeltaBytes = 1800;
    small.displayMaxFragments = 4;
    QCOMPARE(daemonDisplayDiagnosticsLine(small),
             QStringLiteral("largestKeyframe=2977 bytes/4 fragments"
                            " largestDelta=1800 bytes/3 fragments"
                            " maxFragments=4 sendRefusals=0 transportErrors=0"
                            " queuedLate=0"));

    DaemonDisplayDiagnostics largest;
    largest.displayMaxKeyframeBytes = 9361; // 4096/4096 points with a 768-point 3D row
    largest.displayMaxDeltaBytes = 9361;
    largest.displayMaxFragments = 11;
    largest.displaySendRefusals = 7;
    largest.displayTransportErrors = 2;
    largest.displayQueuedLate = 3;
    QCOMPARE(daemonDisplayDiagnosticsLine(largest),
             QStringLiteral("largestKeyframe=9361 bytes/11 fragments"
                            " largestDelta=9361 bytes/11 fragments"
                            " maxFragments=11 sendRefusals=7 transportErrors=2"
                            " queuedLate=3"));
    QCOMPARE(IMediaTransport::sctpFragmentCount(876), quint64(1));
    QCOMPARE(IMediaTransport::sctpFragmentCount(877), quint64(2));
    QCOMPARE(IMediaTransport::sctpFragmentCount(65536), quint64(75));
}

void TstDaemonMediaController::exhaustedDisplayCreditDoesNotBlockAudioRtp()
{
    OpusAudioEncoder encoder;
    if (!encoder.isReady()) {
        QSKIP("Opus encoder is unavailable in this build");
    }
    const DisplayBudgetCharge charge = ps3DisplayCharge();
    Harness harness(DisplayBudgetLimits{charge.applicationBytesPerSecond, 1, 21});
    AudioEngine* const engine = harness.radio.audioEngine();
    QVERIFY(engine);
    engine->masterMixForTest().setRampFrames(1);
    engine->masterMixForTest().setSlewUpFrames(0);
    engine->setSliceStreaming(harness.sliceId, true);
    engine->setSliceStreaming(harness.spareSliceId, true);
    harness.useManualDisplayTicks();
    harness.establishSession();
    harness.startReadyPeer();

    PureSignalSessionFacade* facade = harness.radio.pureSignalFacade();
    facade->setRemoteAmpViewSubscribed(true);
    facade->displaySnapshotReady(maximumPs3Snapshot(facade->displayGeneration(), 1));
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1);
    QCOMPARE(harness.mediaTransport->displays.constFirst().size(),
             static_cast<qsizetype>(Ps3DisplayCodec::kMaxChunkBytes));
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1); // global display bucket exhausted

    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    QVERIFY(harness.client.sendMediaControl(
        audioControl(1, true), harness.client.sessionEpoch()));
    QTRY_VERIFY(messageFor(controls, QStringLiteral("audio-context"), 0)
                    .value(QStringLiteral("enabled")).toBool());
    QVector<float> left(64 * 2, 0.20f);
    QVector<float> right(64 * 2, 0.30f);
    for (int delivered = 0; delivered < DaemonAudioSource::kBlockFrames; delivered += 64) {
        engine->rxBlockReady(harness.sliceId, left.constData(), 64);
        engine->rxBlockReady(harness.spareSliceId, right.constData(), 64);
    }
    QTRY_VERIFY(harness.mediaTransport->rtpAttempts > 0);
    QTRY_VERIFY(!harness.mediaTransport->rtpPackets.isEmpty());
    harness.finish();
}

void TstDaemonMediaController::mediaPeerReplacementDoesNotMintDisplayCredit()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(4096, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 22});
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    QJsonObject request = subscription(
        97, 1, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex));
    request.insert(QStringLiteral("pixels"), 4096);
    // Include enough source bins that the first actual frame exhausts the
    // remaining credit needed for another worst-case preflight. The budget
    // charges granted pixels, so the FFT must supply all 4096 of them.
    request.insert(QStringLiteral("spanHz"), 192000.0);
    request.insert(QStringLiteral("fftSize"), 4096);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 97, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 97)
            .value(QStringLiteral("revision")).toInteger() == 1;
    })());
    harness.mediaTransport->onDisplaySend = [transport = harness.mediaTransport] {
        emit transport->closed();
        return true;
    };
    harness.sendDisplayTick();
    QTRY_VERIFY(harness.mediaTransport.isNull());

    controls.clear();
    harness.startReadyPeer();
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 97, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.1875);
        return messageFor(controls, QStringLiteral("context"), 97)
            .value(QStringLiteral("revision")).toInteger() == 1;
    })());
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 0);

    harness.nowNs = 1'000'000'000;
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.21875);
        harness.sendDisplayTick();
        return harness.mediaTransport->displays.size() == 1;
    })());
    harness.finish();
}

void TstDaemonMediaController::mixedSpectrumAndPs3StayWithinInjectedIntervalBoundsAndBothProgress()
{
    const SpectrumDisplayCost spectrum = *spectrumDisplayCost(128, 60, false);
    const DisplayBudgetCharge combined = *sumDisplayCharges(
        {spectrum.charge, ps3DisplayCharge()});
    const DisplayBudgetLimits limits{combined.applicationBytesPerSecond,
                                     combined.spectrumSampleUnitsPerSecond, 24};
    Harness harness(limits);
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QVERIFY(harness.client.sendMediaControl(
        subscription(98, 1, harness.sliceId, centre), harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 98, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 98)
            .value(QStringLiteral("revision")).toInteger() == 1;
    })());

    PureSignalSessionFacade* facade = harness.radio.pureSignalFacade();
    facade->setRemoteAmpViewSubscribed(true);
    facade->displaySnapshotReady(maximumPs3Snapshot(facade->displayGeneration(), 1));

    for (int interval = 0; interval < 8; ++interval) {
        harness.nowNs += 50'000'000;
        harness.feedRadio(0.125 + static_cast<double>(interval) / 1024.0);
        const int before = harness.mediaTransport->displays.size();
        QTRY_VERIFY(([&] {
            harness.feedRadio(0.125 + static_cast<double>(interval) / 1024.0);
            harness.sendDisplayTick();
            return harness.mediaTransport->displays.size() > before;
        })());
    }

    const int spectrumMessages = displayMessageCount(
        harness.mediaTransport->displays, QByteArrayLiteral("NSDC"));
    const int ps3Messages = displayMessageCount(
        harness.mediaTransport->displays, QByteArrayLiteral("PS3D"));
    QVERIFY(spectrumMessages > 0);
    QVERIFY(ps3Messages >= 3);

    quint64 attemptedBytes = 0;
    quint64 attemptedSamples = 0;
    DisplayCodecDecoder decoder;
    for (const QByteArray& bytes : std::as_const(harness.mediaTransport->displays)) {
        attemptedBytes += static_cast<quint64>(bytes.size());
        if (!bytes.startsWith(QByteArrayLiteral("NSDC"))) { continue; }
        const DisplayCodecDecodeResult decoded = decoder.decode(bytes);
        QCOMPARE(decoded.disposition, DisplayCodecDisposition::Accepted);
        attemptedSamples += static_cast<quint64>(decoded.frame.traceDbm.size())
            + static_cast<quint64>(decoded.frame.waterfallDbm.size())
            + static_cast<quint64>(decoded.frame.wideDbm.size());
    }
    const quint64 elapsedNs = static_cast<quint64>(harness.nowNs);
    const quint64 allowedBytes = kMaximumDisplayMessageBytes
        + limits.applicationBytesPerSecond * elapsedNs / 1'000'000'000ULL;
    const quint64 allowedSamples = kMaximumSpectrumDisplayFrameSampleUnits
        + limits.spectrumSampleUnitsPerSecond * elapsedNs / 1'000'000'000ULL;
    QVERIFY(attemptedBytes <= allowedBytes);
    QVERIFY(attemptedSamples <= allowedSamples);
    harness.finish();
}

void TstDaemonMediaController::failedMiddlePs3ChunkDropsRemainderPromotesLatestAndDoesNotRefund()
{
    const DisplayBudgetCharge charge = ps3DisplayCharge();
    Harness harness(DisplayBudgetLimits{charge.applicationBytesPerSecond, 1, 25});
    harness.useManualDisplayTicks();
    harness.establishSession();
    harness.startReadyPeer();
    PureSignalSessionFacade* facade = harness.radio.pureSignalFacade();
    facade->setRemoteAmpViewSubscribed(true);
    const quint64 generation = facade->displayGeneration();
    const Ps3Snapshot first = maximumPs3Snapshot(generation, 1);
    const Ps3Snapshot latest = maximumPs3Snapshot(generation, 2);
    QCOMPARE(Ps3DisplayCodec::encode(first).size(), 3);
    QCOMPARE(Ps3DisplayCodec::encode(latest).size(), 3);

    facade->displaySnapshotReady(first);
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1);
    facade->displaySnapshotReady(latest);

    bool rejectNext = true;
    harness.mediaTransport->onDisplaySend = [&rejectNext] {
        const bool accepted = !rejectNext;
        rejectNext = false;
        return accepted;
    };
    harness.nowNs += 50'000'000;
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 2);
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 2); // failed bytes were not refunded
    harness.mediaTransport->onDisplaySend = {};

    for (int chunk = 0; chunk < 3; ++chunk) {
        harness.nowNs += 50'000'000;
        harness.sendDisplayTick();
    }
    QCOMPARE(harness.mediaTransport->displays.size(), 5);

    Ps3DisplayAssembler assembler(generation);
    QList<quint64> completed;
    for (int attempt = 0; attempt < harness.mediaTransport->displays.size(); ++attempt) {
        if (attempt == 1) { continue; } // transport rejected this middle chunk
        const QByteArray& bytes = harness.mediaTransport->displays.at(attempt);
        QString error;
        const auto frame = assembler.accept(bytes, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        if (frame) { completed.append(frame->sequence); }
    }
    QCOMPARE(completed, QList<quint64>({2}));
    harness.finish();
}

// R-R3-03/R-R3-05/R-R3-09: over a link whose acknowledgements take longer
// than one send tick, SCTP takes a 49 KB PureSignal chunk only beside
// little unacknowledged data. The library then holds one chunk and the next
// is Busy. The snapshot must still complete: a held chunk counts as sent,
// a Busy chunk waits for the display channel to clear, nothing is resent,
// and a newer snapshot still replaces the one waiting to start.
void TstDaemonMediaController::ps3SnapshotCompletesWhileAcknowledgementsLag()
{
    Harness harness;
    harness.useManualDisplayTicks();
    harness.establishSession();
    harness.startReadyPeer();
    harness.mediaTransport->sctpWindowBytes = IMediaTransport::kSctpSendBufferBytes;
    PureSignalSessionFacade* facade = harness.radio.pureSignalFacade();
    facade->setRemoteAmpViewSubscribed(true);
    const quint64 generation = facade->displayGeneration();
    QCOMPARE(Ps3DisplayCodec::encode(maximumPs3Snapshot(generation, 1)).size(), 3);

    facade->displaySnapshotReady(maximumPs3Snapshot(generation, 1));
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1);
    harness.sendDisplayTick(); // the second chunk does not fit: the library holds it
    QVERIFY(!harness.mediaTransport->heldDisplay.isEmpty());
    for (int tick = 0; tick < 5; ++tick) { harness.sendDisplayTick(); }
    QCOMPARE(harness.mediaTransport->displays.size(), 1);

    // Two newer snapshots arrive while the first is still going out; only
    // the newest waits to follow it.
    facade->displaySnapshotReady(maximumPs3Snapshot(generation, 2));
    facade->displaySnapshotReady(maximumPs3Snapshot(generation, 3));

    for (int round = 0; round < 20 && harness.mediaTransport->displays.size() < 6; ++round) {
        harness.mediaTransport->acknowledgeDisplayWindow();
        QCoreApplication::processEvents();
        harness.sendDisplayTick();
    }
    const QList<QByteArray>& sent = harness.mediaTransport->displays;
    QCOMPARE(sent.size(), 6);
    QCOMPARE(QSet<QByteArray>(sent.cbegin(), sent.cend()).size(), 6); // nothing resent
    Ps3DisplayAssembler assembler(generation);
    QList<quint64> completed;
    for (const QByteArray& bytes : sent) {
        QString error;
        const auto frame = assembler.accept(bytes, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        if (frame) { completed.append(frame->sequence); }
    }
    QCOMPARE(completed, QList<quint64>({1, 3}));
    // Core never offered a chunk while the library held one, and every held
    // chunk is counted as queued late rather than refused.
    QCOMPARE(harness.mediaTransport->busyDisplays, 0);
    QVERIFY(harness.mediaTransport->queuedDisplays > 0);
    const DaemonDisplayDiagnostics diagnostics = harness.controller.displayDiagnostics();
    QCOMPARE(diagnostics.displayQueuedLate, quint64(harness.mediaTransport->queuedDisplays));
    QCOMPARE(diagnostics.displaySendRefusals, quint64(0));
    harness.finish();
}

// R-R3-03/R-R3-05/R-R3-37: two spectrum endpoints and a PureSignal
// snapshot share one display channel over a link whose acknowledgements lag
// a send tick. Every acknowledgement cycle frees room for one small spectrum
// frame beside the chunk that goes out, and the next chunk is then held by
// the library. So while a snapshot is in flight spectrum gets about one
// frame per chunk cycle, whatever the tick rate, and both kinds progress:
// snapshots complete and each endpoint keeps receiving frames.
void TstDaemonMediaController::spectrumAndPs3ShareALaggingWindowAndBothProgress()
{
    Harness harness;
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    harness.mediaTransport->sctpWindowBytes = IMediaTransport::kSctpSendBufferBytes;
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    for (const quint32 endpointId : {63U, 64U}) {
        QVERIFY(harness.client.sendMediaControl(
            subscription(endpointId, 1, harness.sliceId, centre),
            harness.client.sessionEpoch()));
    }
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 63).isEmpty()
            && !messageFor(controls, QStringLiteral("context"), 64).isEmpty();
    })());

    PureSignalSessionFacade* facade = harness.radio.pureSignalFacade();
    facade->setRemoteAmpViewSubscribed(true);
    const quint64 generation = facade->displayGeneration();
    quint64 sequence = 0;
    facade->displaySnapshotReady(maximumPs3Snapshot(generation, ++sequence));

    // One cycle: new spectrum for both endpoints, several send ticks (more
    // than the link can take), then the window is acknowledged.
    constexpr int kCycles = 24;
    constexpr int kTicksPerCycle = 4;
    QList<int> spectrumPerCycle;
    QList<int> ps3PerCycle;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        harness.nowNs += 50'000'000;
        harness.feedRadio(0.125 + 0.0078125 * (cycle % 16));
        QTest::qWait(20); // the FFT worker hands its frame back
        // Keep a newer snapshot waiting, so one is always in flight.
        facade->displaySnapshotReady(maximumPs3Snapshot(generation, ++sequence));
        const QList<QByteArray> before = harness.mediaTransport->displays;
        for (int tick = 0; tick < kTicksPerCycle; ++tick) { harness.sendDisplayTick(); }
        harness.mediaTransport->acknowledgeDisplayWindow();
        QCoreApplication::processEvents();
        const QList<QByteArray> sent = harness.mediaTransport->displays.mid(before.size());
        spectrumPerCycle.append(displayMessageCount(sent, QByteArrayLiteral("NSDC")));
        ps3PerCycle.append(displayMessageCount(sent, QByteArrayLiteral("PS3D")));
    }

    const QList<QByteArray>& sent = harness.mediaTransport->displays;
    QCOMPARE(QSet<QByteArray>(sent.cbegin(), sent.cend()).size(), sent.size()); // no resend
    // Spectrum: at most one frame per chunk cycle, and it keeps coming.
    const int spectrumTotal = displayMessageCount(sent, QByteArrayLiteral("NSDC"));
    QVERIFY2(*std::max_element(spectrumPerCycle.cbegin(), spectrumPerCycle.cend()) <= 1,
             qPrintable(QStringLiteral("spectrum per cycle %1").arg(
                 [&] { QStringList parts; for (int n : spectrumPerCycle) {
                           parts.append(QString::number(n)); } return parts.join(u' '); }())));
    QVERIFY2(spectrumTotal >= kCycles / 2,
             qPrintable(QStringLiteral("%1 spectrum frames in %2 cycles")
                            .arg(spectrumTotal).arg(kCycles)));
    QSet<quint32> endpoints;
    for (const QByteArray& bytes : sent) {
        if (bytes.startsWith(QByteArrayLiteral("NSDC"))) {
            endpoints.insert(qFromBigEndian<quint32>(bytes.constData() + 8));
        }
    }
    QCOMPARE(endpoints, QSet<quint32>({63U, 64U}));
    // PureSignal: a chunk in most cycles, and whole snapshots complete.
    QVERIFY(std::accumulate(ps3PerCycle.cbegin(), ps3PerCycle.cend(), 0) >= kCycles / 2);
    Ps3DisplayAssembler assembler(generation);
    int completed = 0;
    for (const QByteArray& bytes : sent) {
        if (!bytes.startsWith(QByteArrayLiteral("PS3D"))) { continue; }
        QString error;
        if (assembler.accept(bytes, &error)) { ++completed; }
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    QVERIFY2(completed >= kCycles / 6,
             qPrintable(QStringLiteral("%1 snapshots completed").arg(completed)));
    QCOMPARE(harness.mediaTransport->busyDisplays, 0);
    QCOMPARE(harness.controller.displayDiagnostics().displaySendRefusals, quint64(0));

    // The limit is the snapshot's: with PureSignal display off, the same
    // cycles carry a frame for each endpoint.
    facade->setRemoteAmpViewSubscribed(false);
    harness.mediaTransport->acknowledgeDisplayWindow();
    QCoreApplication::processEvents();
    int spectrumAlone = 0;
    constexpr int kAloneCycles = 6;
    for (int cycle = 0; cycle < kAloneCycles; ++cycle) {
        harness.nowNs += 50'000'000;
        harness.feedRadio(0.25 + 0.0078125 * cycle);
        QTest::qWait(20);
        const qsizetype before = harness.mediaTransport->displays.size();
        for (int tick = 0; tick < kTicksPerCycle; ++tick) { harness.sendDisplayTick(); }
        harness.mediaTransport->acknowledgeDisplayWindow();
        QCoreApplication::processEvents();
        spectrumAlone += displayMessageCount(
            harness.mediaTransport->displays.mid(before), QByteArrayLiteral("NSDC"));
    }
    QVERIFY2(spectrumAlone >= 2 * kAloneCycles - 1,
             qPrintable(QStringLiteral("%1 spectrum frames alone in %2 cycles")
                            .arg(spectrumAlone).arg(kAloneCycles)));
    harness.finish();
}

void TstDaemonMediaController::radioProductionRestartWithinEpochDoesNotMintDisplayCredit()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(4096, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 26});
    harness.useManualDisplayTicks();
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    QJsonObject request = subscription(
        99, 1, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex));
    request.insert(QStringLiteral("pixels"), 4096);
    // Include enough source bins that the first actual frame exhausts the
    // remaining credit needed for another worst-case preflight. The budget
    // charges granted pixels, so the FFT must supply all 4096 of them.
    request.insert(QStringLiteral("spanHz"), 192000.0);
    request.insert(QStringLiteral("fftSize"), 4096);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 99, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 99)
            .value(QStringLiteral("revision")).toInteger() == 1;
    })());
    harness.sendDisplayTick();
    QTRY_COMPARE(harness.mediaTransport->displays.size(), 1);

    harness.radio.setConnectionStateForTest(ConnectionState::Disconnected);
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    harness.radio.setConnectionStateForTest(ConnectionState::Connected);
    request.insert(QStringLiteral("revision"), 2);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 99, 2).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.1875);
        return messageFor(controls, QStringLiteral("context"), 99)
            .value(QStringLiteral("revision")).toInteger() == 2;
    })());
    harness.sendDisplayTick();
    QCOMPARE(harness.mediaTransport->displays.size(), 1);

    harness.nowNs = 1'000'000'000;
    QTRY_VERIFY(([&] {
        harness.feedRadio(0.21875);
        harness.sendDisplayTick();
        return harness.mediaTransport->displays.size() == 2;
    })());
    harness.finish();
}

void TstDaemonMediaController::replayedAllocationResultCanSynchronouslyRetireControllerState()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 27});
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const QJsonObject request = subscription(
        100, 1, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex));
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 100, 1).value(QStringLiteral("accepted")).toBool());
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 1);

    harness.stationTransport->closeOnOp = QByteArrayLiteral("allocation-result");
    const bool replaySendReturned = harness.client.sendMediaControl(
        request, harness.client.sessionEpoch());
    Q_UNUSED(replaySendReturned);
    QTRY_VERIFY(!harness.server.mediaAvailable());
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    QTRY_VERIFY(harness.mediaTransport.isNull());
}

void TstDaemonMediaController::sourceRetirementPublishesZeroChargeAtLatestOperationRevision()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 28});
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QJsonObject request = subscription(101, 1, harness.sliceId, centre - 90000.0);
    request.insert(QStringLiteral("spanHz"), 1000.0);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 101, 1).value(QStringLiteral("accepted")).toBool());
    request.insert(QStringLiteral("revision"), 2);
    request.insert(QStringLiteral("pixels"), 4096);
    // The budget charges granted pixels: a crop reaching 30 kHz into the
    // source grants 160 of them, over this 128-pixel budget.
    request.insert(QStringLiteral("spanHz"), 48000.0);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 101, 2).isEmpty());
    QVERIFY(!allocationFor(controls, 101, 2).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(allocationFor(controls, 101, 2).value(QStringLiteral("acceptedRevision")).toInteger(), qint64{1});
    controls.clear();

    // Both receive slices remain inside the DDC. Only the old narrow display
    // crop loses coverage, so its retirement must be explicit to a budget GUI.
    QVERIFY(harness.radio.requestStreamCentre(harness.sliceId, centre + 50000.0));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    QTRY_VERIFY(!allocationFor(controls, 101, 2).isEmpty());
    const QJsonObject retired = allocationFor(controls, 101, 2);
    QVERIFY(!retired.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(retired.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{0});
    QCOMPARE(retired.value(QStringLiteral("applicationBytesPerSecond")).toInteger(), qint64{0});
    QCOMPARE(retired.value(QStringLiteral("spectrumSampleUnitsPerSecond")).toInteger(), qint64{0});
    QVERIFY(messageFor(controls, QStringLiteral("rejected"), 101).isEmpty());

    request.insert(QStringLiteral("revision"), 3);
    request.insert(QStringLiteral("pixels"), 128);
    request.insert(QStringLiteral("spanHz"), 1000.0);
    request.insert(QStringLiteral("centreHz"), centre + 50000.0);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 101, 3).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeEndpointCount(), 1);

    QVERIFY(harness.radio.requestStreamCentre(harness.sliceId, centre - 50000.0));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    QTRY_VERIFY(!allocationFor(controls, 101, 3).value(QStringLiteral("accepted")).toBool());
    controls.clear();
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 101, 3).isEmpty());
    const QJsonObject duplicateAfterRetirement = allocationFor(controls, 101, 3);
    QVERIFY(!duplicateAfterRetirement.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(duplicateAfterRetirement.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{0});
    QCOMPARE(duplicateAfterRetirement.value(QStringLiteral("applicationBytesPerSecond")).toInteger(), qint64{0});
    harness.finish();
}

void TstDaemonMediaController::failedSourceUpdateReleasesAllocationAndCanRecover()
{
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    Harness harness(DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 29});
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const double centre = harness.radio.streamCentreHz(harness.streamIndex);
    QJsonObject request = subscription(102, 1, harness.sliceId, centre);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 102, 1).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeSourceCount(), 1);

    // Remove the actual producer through its existing QObject/public seam.
    // The next overlapping retune reaches update() on a now-missing source
    // and exercises its real refusal without replacing the controller logic.
    auto* source = harness.controller.findChild<DaemonSpectrumSource*>();
    QVERIFY(source);
    source->deactivate({harness.streamIndex, FftTier::Wide});
    QCOMPARE(harness.controller.activeSourceCount(), 0);
    controls.clear();
    QVERIFY(harness.radio.requestStreamCentre(harness.sliceId, centre + 1000.0));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    QTRY_VERIFY(!allocationFor(controls, 102, 1).isEmpty());
    const QJsonObject refused = allocationFor(controls, 102, 1);
    QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
    QCOMPARE(refused.value(QStringLiteral("acceptedRevision")).toInteger(), qint64{0});
    QCOMPARE(refused.value(QStringLiteral("applicationBytesPerSecond")).toInteger(), qint64{0});

    request.insert(QStringLiteral("revision"), 2);
    QVERIFY(harness.client.sendMediaControl(request, harness.client.sessionEpoch()));
    QTRY_VERIFY(allocationFor(controls, 102, 2).value(QStringLiteral("accepted")).toBool());
    QCOMPARE(harness.controller.activeSourceCount(), 1);
    QTRY_VERIFY(([&] {
        harness.feedRadio();
        return !harness.mediaTransport->displays.isEmpty();
    })());
    harness.finish();
}

void TstDaemonMediaController::authenticatedControlProducesContextThenDecodedDisplayAndUnsubscribes()
{
    Harness harness;
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();

    QVERIFY(harness.client.sendMediaControl(
        subscription(7, 1, harness.sliceId,
                     harness.radio.streamCentreHz(harness.streamIndex)),
        harness.client.sessionEpoch()));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 1);
    QTRY_COMPARE(harness.controller.activeSourceCount(), 1);

    harness.feedRadio();
    QTRY_VERIFY(!messageFor(controls, QStringLiteral("context"), 7).isEmpty());
    const QJsonObject context = messageFor(controls, QStringLiteral("context"), 7);
    // Minor 9: today's 19 fields plus the five grant fields.
    QVERIFY(harness.server.spectrumGrantAvailable());
    QVERIFY(harness.client.spectrumGrantAvailable());
    QCOMPARE(context.size(), 24);
    const auto reported = decodeRemoteSpectrumContext(context, true);
    QVERIFY(reported.has_value() && reported->grant.has_value());
    QCOMPARE(reported->grant->grantedFftSize, 1024);
    QCOMPARE(reported->grant->grantedTier, FftTier::Wide);
    QCOMPARE(reported->grant->requestedPixels, 128);
    QCOMPARE(reported->grant->grantedPixels, 128);
    QCOMPARE(reported->grant->limit, SpectrumLimitReason::None);
    QVERIFY(!decodeRemoteSpectrumContext(context, false).has_value());
    QCOMPARE(context.value(QStringLiteral("sourceStream")).toInt(), harness.streamIndex);
    QCOMPARE(context.value(QStringLiteral("sourceCentreHz")).toDouble(),
             harness.radio.streamCentreHz(harness.streamIndex));
    QCOMPARE(context.value(QStringLiteral("sampleRateHz")).toDouble(), 192000.0);
    QCOMPARE(context.value(QStringLiteral("traceSamples")).toInt(), 128);
    QCOMPARE(context.value(QStringLiteral("waterfallSamples")).toInt(), 128);

    // The GUI requests a keyframe after it accepts context. Exercise that
    // ordering before decoding a fresh direct-media packet in isolation.
    QVERIFY(harness.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("keyframe")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
        {QStringLiteral("endpointId"), 7},
        {QStringLiteral("contextGeneration"),
         context.value(QStringLiteral("contextGeneration")).toInteger()}},
        harness.client.sessionEpoch()));
    QTest::qWait(20); // source cadence is 60 fps
    harness.mediaTransport->displays.clear();
    harness.feedRadio(0.1875);
    QTRY_VERIFY(!harness.mediaTransport->displays.isEmpty());

    DisplayCodecDecoder decoder;
    const DisplayCodecDecodeResult decoded = decoder.decode(
        harness.mediaTransport->displays.constLast());
    QCOMPARE(decoded.disposition, DisplayCodecDisposition::Accepted);
    QCOMPARE(decoded.frame.context.endpointId, quint32{7});
    QCOMPARE(decoded.frame.context.contextGeneration,
             static_cast<quint32>(context.value(QStringLiteral("contextGeneration")).toInteger()));
    QCOMPARE(decoded.frame.traceDbm.size(), 128);

    QVERIFY(harness.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("unsubscribe")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
        {QStringLiteral("endpointId"), 7}}, harness.client.sessionEpoch()));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    QTRY_COMPARE(harness.controller.activeSourceCount(), 0);
    harness.finish();
}

void TstDaemonMediaController::fullSourceNoiseFloorFollowsContextCalibrationCadenceAndRetirement()
{
    auto& appSettings = AppSettings::instance();
    const QVariant savedMeterOffset = appSettings.value(QStringLiteral("RX1_MeterCalOffsetDb"));
    appSettings.setValue(QStringLiteral("RX1_MeterCalOffsetDb"), QStringLiteral("-3.0"));
    const auto restoreMeterOffset = qScopeGuard([&] {
        appSettings.setValue(QStringLiteral("RX1_MeterCalOffsetDb"), savedMeterOffset);
    });

    Harness harness;
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const QJsonObject initial = subscription(
        14, 1, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex));
    QVERIFY(harness.client.sendMediaControl(initial, harness.client.sessionEpoch()));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 1);

    const auto hasInitialContext = [&] {
        harness.feedZeroRadio();
        return !messageFor(controls, QStringLiteral("context"), 14).isEmpty();
    };
    QTRY_VERIFY(hasInitialContext());
    QTRY_VERIFY(!messageFor(controls, QStringLiteral("noise-floor"), 14).isEmpty());
    const QJsonObject firstFloor = messageFor(controls, QStringLiteral("noise-floor"), 14);
    QCOMPARE(firstFloor.size(), 6);
    QCOMPARE(firstFloor.value(QStringLiteral("connectionId")).toString(),
             QLatin1String(kConnectionId));
    QCOMPARE(firstFloor.value(QStringLiteral("revision")).toInteger(), qint64{1});
    const float expectedFloor = -200.0f + static_cast<float>(harness.radio.rxMeterOffsetDb());
    QVERIFY(std::abs(firstFloor.value(QStringLiteral("floorDbm")).toDouble() - expectedFloor) < 0.01);
    QVERIFY(messageIndex(controls, QStringLiteral("context"), 14)
            < messageIndex(controls, QStringLiteral("noise-floor"), 14));

    const int initialCount = messageCount(controls, QStringLiteral("noise-floor"), 14);
    harness.feedZeroRadio();
    QTest::qWait(50);
    QCOMPARE(messageCount(controls, QStringLiteral("noise-floor"), 14), initialCount);
    QTest::qWait(500);
    harness.feedZeroRadio();
    QTRY_COMPARE(messageCount(controls, QStringLiteral("noise-floor"), 14), initialCount + 1);

    // A source reconfiguration creates a new context and resets the per-
    // endpoint cadence, so its first valid frame carries a floor immediately.
    controls.clear();
    QJsonObject replacement = initial;
    replacement.insert(QStringLiteral("revision"), 2);
    replacement.insert(QStringLiteral("fftSize"), 2048);
    QVERIFY(harness.client.sendMediaControl(replacement, harness.client.sessionEpoch()));
    const auto hasReplacementContext = [&] {
        harness.feedZeroRadio(2050);
        return messageFor(controls, QStringLiteral("context"), 14)
            .value(QStringLiteral("revision")).toInteger() == 2;
    };
    QTRY_VERIFY(hasReplacementContext());
    QTRY_VERIFY(!messageFor(controls, QStringLiteral("noise-floor"), 14).isEmpty());
    const QJsonObject replacementContext = messageFor(controls, QStringLiteral("context"), 14);
    const QJsonObject replacementFloor = messageFor(controls, QStringLiteral("noise-floor"), 14);
    QCOMPARE(replacementFloor.value(QStringLiteral("revision")).toInteger(), qint64{2});
    QCOMPARE(replacementFloor.value(QStringLiteral("contextGeneration")).toInteger(),
             replacementContext.value(QStringLiteral("contextGeneration")).toInteger());
    QVERIFY(messageIndex(controls, QStringLiteral("context"), 14)
            < messageIndex(controls, QStringLiteral("noise-floor"), 14));

    controls.clear();
    QVERIFY(harness.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("unsubscribe")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
        {QStringLiteral("endpointId"), 14}}, harness.client.sessionEpoch()));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    harness.feedZeroRadio(2048);
    QTest::qWait(30);
    QVERIFY(messageFor(controls, QStringLiteral("noise-floor"), 14).isEmpty());
    harness.finish();
}

void TstDaemonMediaController::nonzeroNoiseFloorMatchesLocalFftBeforeCropAndQuantization()
{
    auto& settings = AppSettings::instance();
    const QVariant saved = settings.value(QStringLiteral("RX1_MeterCalOffsetDb"));
    const auto restore = qScopeGuard([&] {
        settings.setValue(QStringLiteral("RX1_MeterCalOffsetDb"), saved);
    });
    Harness harness;
    harness.establishSession();
    harness.startReadyPeer();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    for (int revision = 1; revision <= 2; ++revision) {
        const int fftSize = revision == 1 ? 1024 : 2048;
        settings.setValue(QStringLiteral("RX1_MeterCalOffsetDb"),
                          revision == 1 ? QStringLiteral("-3") : QStringLiteral("7"));
        QVector<float> iq((fftSize + 2) * 2);
        quint32 state = 0x18792345u;
        const auto noise = [&state] {
            state = state * 1664525u + 1013904223u;
            return (double(state) / double(0xffffffffu) - 0.5) * 0.002;
        };
        for (int sample = 0; sample < fftSize + 2; ++sample) {
            // Deterministic broadband input plus a strong carrier outside
            // the subscribed crop. The reference uses the complete FFT.
            const double phase = 2.0 * std::numbers::pi * 0.3 * sample;
            iq[2 * sample] = float(noise() + 0.05 * std::cos(phase));
            iq[2 * sample + 1] = float(noise() + 0.05 * std::sin(phase));
        }
        NereusSDR::FFTEngine local(0);
        local.setOutputFps(60);
        local.setFftSizeBaseline(fftSize);
        local.setFftSize(fftSize);
        local.setWindowFunction(WindowFunction::Hann);
        local.setSampleRate(192000);
        QVector<float> localBins;
        connect(&local, &NereusSDR::FFTEngine::fftReady, &local,
                [&localBins](int, const QVector<float>& bins) { localBins = bins; });
        local.feedIQ(iq);
        QCOMPARE(localBins.size(), fftSize);
        NoiseFloorEstimator estimator;
        const double expected = estimator.estimate(localBins) + harness.radio.rxMeterOffsetDb();
        QVERIFY(expected > -180); // This covers ordinary nonzero data, not the floor sentinel.

        controls.clear();
        QVERIFY(harness.client.sendMediaControl(subscription(
            15, revision, harness.sliceId,
            harness.radio.streamCentreHz(harness.streamIndex), fftSize),
            harness.client.sessionEpoch()));
        const auto hasFloor = [&] {
            QMetaObject::invokeMethod(&harness.radio, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, harness.streamIndex), Q_ARG(QVector<float>, iq));
            return messageFor(controls, QStringLiteral("noise-floor"), 15)
                .value(QStringLiteral("revision")).toInteger() == revision;
        };
        QTRY_VERIFY(hasFloor());
        const QJsonObject received = messageFor(controls, QStringLiteral("noise-floor"), 15);
        QVERIFY2(std::abs(received.value(QStringLiteral("floorDbm")).toDouble() - expected) < 0.01,
                 "Remote Clarity must match full local FFT percentile plus station calibration");
    }
    harness.finish();
}

void TstDaemonMediaController::staleRevisionAndWrongEpochPreserveTheActiveSource()
{
    Harness harness;
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    const QJsonObject accepted = subscription(
        9, 4, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex));
    QVERIFY(harness.client.sendMediaControl(accepted, harness.client.sessionEpoch()));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 1);
    QTRY_COMPARE(harness.controller.activeSourceCount(), 1);
    harness.feedRadio();
    QTRY_VERIFY(!messageFor(controls, QStringLiteral("context"), 9).isEmpty());
    const QJsonObject oldContext = messageFor(controls, QStringLiteral("context"), 9);

    QJsonObject stale = accepted;
    stale.insert(QStringLiteral("fftSize"), 2048);
    QVERIFY(harness.client.sendMediaControl(stale, harness.client.sessionEpoch()));
    QTRY_VERIFY(!messageFor(controls, QStringLiteral("rejected"), 9).isEmpty());
    QCOMPARE(harness.controller.activeEndpointCount(), 1);
    QCOMPARE(harness.controller.activeSourceCount(), 1);
    QTest::qWait(20);
    harness.feedRadio(0.1875);
    QTest::qWait(20);
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 9)
                 .value(QStringLiteral("contextGeneration")).toInteger(),
             oldContext.value(QStringLiteral("contextGeneration")).toInteger());

    QJsonObject newer = accepted;
    newer.insert(QStringLiteral("revision"), 5);
    QVERIFY(!harness.client.sendMediaControl(newer, harness.client.sessionEpoch() + 1));
    QCOMPARE(harness.controller.activeEndpointCount(), 1);
    QCOMPARE(harness.controller.activeSourceCount(), 1);
    harness.finish();
}

void TstDaemonMediaController::nonoverlappingCropIsRejectedBeforeSourceActivation()
{
    Harness harness;
    harness.establishSession();
    QSignalSpy controls(&harness.client, &StationClient::mediaControlReceived);
    harness.startReadyPeer();
    QJsonObject outside = subscription(
        12, 1, harness.sliceId,
        harness.radio.streamCentreHz(harness.streamIndex) + 500000.0);
    QVERIFY(harness.client.sendMediaControl(outside, harness.client.sessionEpoch()));
    QTRY_VERIFY(!messageFor(controls, QStringLiteral("rejected"), 12).isEmpty());
    QCOMPARE(harness.controller.activeEndpointCount(), 0);
    QCOMPARE(harness.controller.activeSourceCount(), 0);
    QVERIFY(messageFor(controls, QStringLiteral("context"), 12).isEmpty());
    harness.finish();
}

void TstDaemonMediaController::streamRemovalRetiresEndpointAndSource()
{
    Harness harness;
    harness.establishSession();
    harness.startReadyPeer();
    QVERIFY(harness.spareSliceId >= 0);
    QCOMPARE(harness.radio.slices().size(), 2);
    QVERIFY(harness.client.sendMediaControl(subscription(
        11, 1, harness.sliceId, harness.radio.streamCentreHz(harness.streamIndex)),
        harness.client.sessionEpoch()));
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 1);
    QTRY_COMPARE(harness.controller.activeSourceCount(), 1);

    harness.radio.removeSlice(harness.sliceId);
    QTRY_COMPARE(harness.controller.activeEndpointCount(), 0);
    QTRY_COMPARE(harness.controller.activeSourceCount(), 0);
    harness.finish();
}

void TstDaemonMediaController::extendedPermissionFirstCaptureAndSharedEndpointLifetimes()
{
    Harness h;
    h.enableWidebandSource();
    h.establishSession();
    QVERIFY(h.client.remoteWidebandAvailable());
    h.startReadyPeer();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    auto request = subscription(21, 1, h.sliceId, centre);
    request.insert(QStringLiteral("extendedView"), true);
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QCOMPARE(h.p2.wbEnableMask(), quint8(0));
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 21).isEmpty();
    })());
    auto context = messageFor(controls, QStringLiteral("context"), 21);
    QCOMPARE(context.size(), 25); // wideband and the minor-9 grant
    QVERIFY(decodeRemoteSpectrumContext(context, true).has_value());
    auto wideband = WidebandDisplayContext::fromJson(context.value(QStringLiteral("wideband")).toObject());
    QVERIFY(wideband && wideband->available && !wideband->active);
    QCOMPARE(wideband->sourceGeneration, quint32(0));
    QCOMPARE(h.p2.wbEnableMask(), quint8(0));

    request.insert(QStringLiteral("revision"), 2);
    request.insert(QStringLiteral("spanHz"), 1'000'000.0);
    const auto packetsBefore = h.mediaTransport->displays.size();
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_COMPARE(h.p2.wbEnableMask(), quint8(1));
    // No synthetic ADC burst has been sent: capture enable alone must give
    // the first view an identity, a context and explicit empty wings.
    QVERIFY(!h.radio.latestWidebandSpectrum(0));
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 21)
            .value(QStringLiteral("revision")).toInt() == 2;
    })());
    context = messageFor(controls, QStringLiteral("context"), 21);
    QCOMPARE(context.value(QStringLiteral("spanHz")).toDouble(), 1'000'000.0);
    wideband = WidebandDisplayContext::fromJson(context.value(QStringLiteral("wideband")).toObject());
    QVERIFY(wideband && wideband->active && wideband->sourceGeneration != 0);
    QCOMPARE(wideband->physicalAdcIndex, 0);
    QTRY_VERIFY(h.mediaTransport->displays.size() > packetsBefore);
    DisplayCodecDecoder decoder;
    const auto first = decoder.decode(h.mediaTransport->displays.constLast());
    QCOMPARE(first.disposition, DisplayCodecDisposition::Accepted);
    QCOMPARE(first.frame.context.contextGeneration,
             quint32(context.value(QStringLiteral("contextGeneration")).toInteger()));
    QVERIFY(std::abs(first.frame.traceDbm.first() - (-180.0f)) < 0.02f);
    QVERIFY(std::abs(first.frame.traceDbm.last() - (-180.0f)) < 0.02f);

    // Replacement rollback/invalid requests cannot disturb the active owner.
    auto malformed = request;
    malformed.insert(QStringLiteral("revision"), 3);
    malformed.insert(QStringLiteral("physicalAdcIndex"), 1);
    QVERIFY(h.client.sendMediaControl(malformed, h.client.sessionEpoch()));
    QCoreApplication::processEvents();
    QCOMPARE(h.controller.activeEndpointCount(), 1);
    QCOMPARE(h.p2.wbEnableMask(), quint8(1));

    request.insert(QStringLiteral("endpointId"), 22);
    request.insert(QStringLiteral("revision"), 1);
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    QCOMPARE(h.radio.widebandSourceDescriptor(0)->sourceGeneration, wideband->sourceGeneration);
    const auto unsubscribe = [&](int id) {
        return h.client.sendMediaControl({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), id}}, h.client.sessionEpoch());
    };
    QVERIFY(unsubscribe(21));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QCOMPARE(h.p2.wbEnableMask(), quint8(1));
    QVERIFY(unsubscribe(22));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 0);
    QCOMPARE(h.p2.wbEnableMask(), quint8(0));
    QVERIFY(!h.radio.widebandSourceDescriptor(0));
    h.finish();
}

void TstDaemonMediaController::widebandSourceReplacementAndSessionRetirement()
{
    Harness h;
    h.enableWidebandSource();
    h.establishSession();
    h.startReadyPeer();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    auto request = subscription(31, 1, h.sliceId, h.radio.streamCentreHz(h.streamIndex));
    request.insert(QStringLiteral("extendedView"), true);
    request.insert(QStringLiteral("spanHz"), 1'000'000.0);
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_VERIFY(([&] { h.feedRadio(); return !messageFor(controls, QStringLiteral("context"), 31).isEmpty(); })());
    const auto before = messageFor(controls, QStringLiteral("context"), 31);
    const auto sourceBefore = h.radio.widebandSourceDescriptor(0);
    QVERIFY(sourceBefore);

    const auto accumulators = h.p2.findChildren<WidebandFrameAccumulator*>();
    QCOMPARE(accumulators.size(), 8);
    const QByteArray samples(1024, char(0x20));
    for (int sequence = 0; sequence < 32; ++sequence) {
        accumulators[0]->pushPacket(sequence, samples);
    }
    QTRY_VERIFY(h.radio.latestWidebandSpectrum(0));
    h.p2.setWidebandEnabled(0, false);
    h.p2.setWidebandEnabled(0, true);
    QVERIFY(!h.radio.latestWidebandSpectrum(0));
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 31)
            .value(QStringLiteral("contextGeneration")).toInteger()
            > before.value(QStringLiteral("contextGeneration")).toInteger();
    })());
    const auto after = messageFor(controls, QStringLiteral("context"), 31);
    const auto wideband = WidebandDisplayContext::fromJson(after.value(QStringLiteral("wideband")).toObject());
    QVERIFY(wideband && wideband->active);
    QVERIFY(wideband->sourceGeneration > sourceBefore->sourceGeneration);
    QVERIFY(!h.radio.latestWidebandSpectrum(0));

    // Change only the physical ADC routing. The DDC geometry stays fixed;
    // source identity and mask must still follow the new physical input.
    DdcAssignment assignment{};
    assignment.streamDdc[h.streamIndex] = 0;
    assignment.rate[0] = 192000;
    assignment.ddcEnable = 1;
    assignment.adcCtrl1 = 1;
    h.radio.publishDdcAssignmentForTest(assignment);
    QTRY_COMPARE(h.p2.wbEnableMask(), quint8(2));
    QTRY_VERIFY(([&] {
        h.feedRadio();
        const auto metadata = messageFor(controls, QStringLiteral("context"), 31)
            .value(QStringLiteral("wideband")).toObject();
        return metadata.value(QStringLiteral("physicalAdcIndex")).toInt(-1) == 1;
    })());
    const auto remapped = messageFor(controls, QStringLiteral("context"), 31);
    QVERIFY(remapped.value(QStringLiteral("contextGeneration")).toInteger()
            > after.value(QStringLiteral("contextGeneration")).toInteger());
    QVERIFY(!h.radio.latestWidebandSpectrum(1));
    h.finish();
    QTRY_COMPARE(h.controller.activeEndpointCount(), 0);
    QTRY_COMPARE(h.p2.wbEnableMask(), quint8(0));
}

void TstDaemonMediaController::localCaptureDuringConnectingGetsIdentityBeforeFirstAdcRow()
{
    Harness h;
    h.enableWidebandSource();
    h.radio.setConnectionStateForTest(ConnectionState::Connecting);
    h.radio.sliceById(h.sliceId)->setWidebandExtensionRequested(true);
    // The fixture has retired an earlier Connected state, so normal demand
    // is gated. Simulate P2 capture applied during the next connection setup,
    // before the model publishes Connected and re-applies retained demand.
    h.p2.setWidebandEnabled(0, true);
    QCOMPARE(h.p2.wbEnableMask(), quint8(1));
    QVERIFY(!h.radio.widebandSourceDescriptor(0));
    h.radio.setConnectionStateForTest(ConnectionState::Connected);
    // Production Connected re-publishes its DDC assignment and reconciles
    // demand. This narrow fixture runs the same reconciliation explicitly.
    h.radio.reconcileWidebandDemand();
    QVERIFY(h.radio.widebandSourceDescriptor(0));
    QVERIFY(!h.radio.latestWidebandSpectrum(0));
    h.establishSession();
    h.startReadyPeer();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    auto request = subscription(51, 1, h.sliceId, h.radio.streamCentreHz(h.streamIndex));
    request.insert(QStringLiteral("extendedView"), true);
    request.insert(QStringLiteral("spanHz"), 1'000'000.0);
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 51)
            .value(QStringLiteral("wideband")).toObject().value(QStringLiteral("active")).toBool();
    })());
    QVERIFY(!h.radio.latestWidebandSpectrum(0));
    h.finish();
}

void TstDaemonMediaController::synchronousDisplayClosureRetiresDemandAndAllowsNewPeer_data()
{
    QTest::addColumn<bool>("sendAccepted");
    QTest::newRow("refused") << false;
    QTest::newRow("accepted-before-close") << true;
}

void TstDaemonMediaController::synchronousDisplayClosureRetiresDemandAndAllowsNewPeer()
{
    QFETCH(bool, sendAccepted);
    Harness h;
    h.enableWidebandSource();
    h.establishSession();
    h.startReadyPeer();
    QPointer<QObject> retiredPeer = h.mediaTransport->parent();
    bool returnedFromClose = false;
    bool peerAliveDuringSend = false;
    h.mediaTransport->onDisplaySend = [&, transport = h.mediaTransport] {
        emit transport->closed();
        peerAliveDuringSend = !retiredPeer.isNull();
        returnedFromClose = true;
        return sendAccepted;
    };
    auto request = subscription(61, 1, h.sliceId, h.radio.streamCentreHz(h.streamIndex));
    request.insert(QStringLiteral("extendedView"), true);
    request.insert(QStringLiteral("spanHz"), 1'000'000.0);
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_COMPARE(h.p2.wbEnableMask(), quint8(1));
    QTRY_VERIFY(([&] { h.feedRadio(); return returnedFromClose; })());
    QVERIFY(peerAliveDuringSend);
    QCOMPARE(h.controller.activeEndpointCount(), 0);
    QCOMPARE(h.controller.activeSourceCount(), 0);
    QCOMPARE(h.p2.wbEnableMask(), quint8(0));
    QTRY_VERIFY(retiredPeer.isNull());
    QTRY_VERIFY(h.mediaTransport.isNull());

    // The control session survives a media close. A fresh peer can reuse the
    // endpoint number without an old send modifying its new context.
    h.startReadyPeer();
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_VERIFY(([&] { h.feedRadio(); return !h.mediaTransport->displays.isEmpty(); })());
    QCOMPARE(h.p2.wbEnableMask(), quint8(1));
    h.finish();
}

void TstDaemonMediaController::synchronousControlClosureRetiresDemand_data()
{
    QTest::addColumn<QByteArray>("op");
    QTest::newRow("context") << QByteArray("context");
    QTest::newRow("noise-floor") << QByteArray("noise-floor");
}

void TstDaemonMediaController::synchronousControlClosureRetiresDemand()
{
    QFETCH(QByteArray, op);
    Harness h;
    h.enableWidebandSource();
    h.establishSession();
    h.startReadyPeer();
    auto request = subscription(62, 1, h.sliceId, h.radio.streamCentreHz(h.streamIndex));
    request.insert(QStringLiteral("extendedView"), true);
    request.insert(QStringLiteral("spanHz"), 1'000'000.0);
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_COMPARE(h.p2.wbEnableMask(), quint8(1));
    h.stationTransport->closeOnOp = op;
    QTRY_VERIFY(([&] { h.feedRadio(); return !h.server.mediaAvailable(); })());
    QCOMPARE(h.controller.activeEndpointCount(), 0);
    QCOMPARE(h.controller.activeSourceCount(), 0);
    QCOMPARE(h.p2.wbEnableMask(), quint8(0));
    QTRY_VERIFY(h.mediaTransport.isNull());
}

void TstDaemonMediaController::olderPeerKeepsLegacyContextAndCannotAcquireWideband()
{
    Harness h;
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, 60, false);
    QVERIFY(h.server.setDisplayBudgetLimits(
        {cost.charge.applicationBytesPerSecond,
         cost.charge.spectrumSampleUnitsPerSecond, 23}));
    h.enableWidebandSource();
    auto* station = new Test::LoopbackTransport(QStringLiteral("old-station"), this);
    auto* peer = new Test::LoopbackTransport(QStringLiteral("old-peer"), this);
    station->linkTo(peer);
    h.server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kRemoteWidebandSessionProtocolMinor - 1, 6, QStringLiteral("old-client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(h.server.token())));
    QTRY_VERIFY(h.server.mediaAvailable());
    QVERIFY(!h.server.remoteWidebandAvailable());
    const auto send = [&](const QJsonObject& payload) {
        SessionMessage message;
        message.kind = SessionMessageKind::MediaControl;
        message.mediaPayload = payload;
        peer->sendText(SessionMessages::encode(message));
    };
    send({{QStringLiteral("op"), QStringLiteral("start")},
          {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}});
    QTRY_VERIFY(h.mediaTransport);
    h.mediaTransport->becomeReady();
    auto request = subscription(41, 1, h.sliceId, h.radio.streamCentreHz(h.streamIndex));
    request.insert(QStringLiteral("extendedView"), true);
    send(request);
    QCoreApplication::processEvents();
    QCOMPARE(h.controller.activeEndpointCount(), 0);
    QCOMPARE(h.p2.wbEnableMask(), quint8(0));
    request.remove(QStringLiteral("extendedView"));
    send(request);
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QJsonObject overBudget = request;
    overBudget.insert(QStringLiteral("endpointId"), 42);
    send(overBudget);
    QTRY_VERIFY(([&] {
        for (const QByteArray& wire : peer->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)
                && message.kind == SessionMessageKind::MediaControl
                && message.mediaPayload.value(QStringLiteral("op")) == QLatin1String("rejected")
                && message.mediaPayload.value(QStringLiteral("endpointId")).toInteger() == 42) {
                return true;
            }
        }
        return false;
    })());
    QCOMPARE(h.controller.activeEndpointCount(), 1);
    for (const QByteArray& wire : peer->received()) {
        SessionMessage message;
        if (SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::MediaControl
            && message.mediaPayload.value(QStringLiteral("endpointId")).toInteger() == 42) {
            QVERIFY(message.mediaPayload.value(QStringLiteral("op"))
                    != QLatin1String("allocation-result"));
        }
    }
    QTRY_VERIFY(([&] {
        h.feedRadio();
        for (const auto& wire : peer->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)
                && message.kind == SessionMessageKind::MediaControl
                && message.mediaPayload.value(QStringLiteral("op")) == QStringLiteral("context")) {
                return message.mediaPayload.size() == 19
                    && !message.mediaPayload.contains(QStringLiteral("wideband"));
            }
        }
        return false;
    })());
    peer->closeLink(QStringLiteral("test complete"));
}

void TstDaemonMediaController::minorSevenPeerReceivesLegacyAudioContexts()
{
    OpusAudioEncoder encoder;
    if (!encoder.isReady()) {
        QSKIP("Opus encoder is unavailable in this build");
    }
    Harness h;
    // A GUI that says hello with minor 7 predates the audio status detail.
    // Whatever state Core is in, it must keep receiving the exact context
    // that GUI already parses, or its audio stops.
    auto* station = new Test::LoopbackTransport(QStringLiteral("minor7-station"), this);
    auto* peer = new Test::LoopbackTransport(QStringLiteral("minor7-peer"), this);
    station->linkTo(peer);
    h.server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kRemoteAudioStatusSessionProtocolMinor - 1, 0,
        QStringLiteral("minor-7 client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(h.server.token())));
    QTRY_VERIFY(h.server.mediaAvailable());
    QVERIFY(!h.server.remoteAudioStatusAvailable());
    const auto send = [&](const QJsonObject& payload) {
        SessionMessage message;
        message.kind = SessionMessageKind::MediaControl;
        message.mediaPayload = payload;
        peer->sendText(SessionMessages::encode(message));
    };
    send({{QStringLiteral("op"), QStringLiteral("start")},
          {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}});
    QTRY_VERIFY(h.mediaTransport);
    const qint64 ssrc = h.mediaTransport->startOptions.localAudioSsrc;
    QVERIFY(ssrc != 0);

    const auto latestContext = [&] { return receivedAudioContexts(*peer).constLast(); };
    const auto expectLegacy = [&](int count, quint32 revision, bool enabled) {
        QTRY_COMPARE(receivedAudioContexts(*peer).size(), count);
        const QJsonObject context = latestContext();
        QVERIFY2(hasLegacyAudioContextShape(context),
                 QJsonDocument(context).toJson(QJsonDocument::Compact).constData());
        QCOMPARE(context.value(QStringLiteral("connectionId")).toString(),
                 QLatin1String(kConnectionId));
        QCOMPARE(context.value(QStringLiteral("revision")).toInteger(), qint64{revision});
        QCOMPARE(context.value(QStringLiteral("enabled")).toBool(), enabled);
        QCOMPARE(context.value(QStringLiteral("ssrc")).toInteger(), ssrc);
        // What a minor-7 GUI accepts, and not what a minor-8 GUI accepts.
        QVERIFY(decodeRemoteAudioContext(context, false).has_value());
        QVERIFY(!decodeRemoteAudioContext(context, true).has_value());
    };

    // Asked for before the media peer is ready, then granted once it is.
    send(audioControl(1, true));
    expectLegacy(1, 1, false);
    if (QTest::currentTestFailed()) { return; }
    h.mediaTransport->becomeReady();
    expectLegacy(2, 1, true);
    if (QTest::currentTestFailed()) { return; }
    QCOMPARE(latestContext().value(QStringLiteral("firstSequence")).toInteger(), qint64{1});
    QCOMPARE(latestContext().value(QStringLiteral("firstTimestamp")).toInteger(), qint64{0});

    // The client turns audio off, and the station radio drops while it is off.
    send(audioControl(2, false));
    expectLegacy(3, 2, false);
    if (QTest::currentTestFailed()) { return; }
    h.radio.setConnectionStateForTest(ConnectionState::Disconnected);
    expectLegacy(4, 2, false);
    if (QTest::currentTestFailed()) { return; }

    // Asked for while the radio is offline, granted when it returns, and
    // withdrawn again when it drops with audio still wanted.
    send(audioControl(3, true));
    expectLegacy(5, 3, false);
    if (QTest::currentTestFailed()) { return; }
    h.radio.setConnectionStateForTest(ConnectionState::Connected);
    expectLegacy(6, 3, true);
    if (QTest::currentTestFailed()) { return; }
    h.radio.setConnectionStateForTest(ConnectionState::Disconnected);
    expectLegacy(7, 3, false);
    if (QTest::currentTestFailed()) { return; }
    peer->closeLink(QStringLiteral("test complete"));
}

void TstDaemonMediaController::minorEightAudioContextsCarryEncoderOrReason()
{
    OpusAudioEncoder encoder;
    if (!encoder.isReady()) {
        QSKIP("Opus encoder is unavailable in this build");
    }
    // The controller's sender is private; an identically built sender
    // reports the profile its encoder runs.
    const DaemonAudioSender reference(nullptr);
    const std::optional<OpusEncoderProfile> senderProfile = reference.encoderProfile();
    QVERIFY(senderProfile.has_value());
    const QJsonObject expectedEncoder = remoteAudioEncoderToJson(*senderProfile);
    QCOMPARE(expectedEncoder,
             (QJsonObject{{QStringLiteral("codec"), QStringLiteral("opus")},
                          {QStringLiteral("sampleRate"), 48000},
                          {QStringLiteral("channels"), 2},
                          {QStringLiteral("frameSamples"), 1920},
                          {QStringLiteral("targetBitrate"), 24000},
                          {QStringLiteral("audioBandwidthHz"), 8000}}));

    Harness h;
    h.establishSession();
    QVERIFY(h.client.agreedMinor() >= kRemoteAudioStatusSessionProtocolMinor);
    QVERIFY(h.server.remoteAudioStatusAvailable());
    QVERIFY(h.client.remoteAudioStatusAvailable());
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    QVERIFY(h.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("start")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}},
        h.client.sessionEpoch()));
    QTRY_VERIFY(h.mediaTransport);

    const auto contexts = [&controls] {
        QList<QJsonObject> found;
        for (const auto& call : controls) {
            const QJsonObject message = call.at(0).toJsonObject();
            if (message.value(QStringLiteral("op")) == QLatin1String("audio-context")) {
                found.append(message);
            }
        }
        return found;
    };
    const auto expectOn = [&](int count, quint32 revision) {
        QTRY_COMPARE(contexts().size(), count);
        const QJsonObject context = contexts().constLast();
        QCOMPARE(context.size(), 9);
        QVERIFY(context.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(context.value(QStringLiteral("revision")).toInteger(), qint64{revision});
        QCOMPARE(context.value(QStringLiteral("encoder")).toObject(), expectedEncoder);
        QVERIFY(!context.contains(QStringLiteral("reason")));
        const std::optional<RemoteAudioContextMessage> decoded =
            decodeRemoteAudioContext(context, true);
        QVERIFY(decoded.has_value());
        QVERIFY(decoded->encoder.has_value());
        QCOMPARE(*decoded->encoder, *senderProfile);
        QVERIFY(!decodeRemoteAudioContext(context, false).has_value());
    };
    const auto expectOff = [&](int count, quint32 revision, const char* reason) {
        QTRY_COMPARE(contexts().size(), count);
        const QJsonObject context = contexts().constLast();
        QCOMPARE(context.size(), 9);
        QVERIFY(!context.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(context.value(QStringLiteral("revision")).toInteger(), qint64{revision});
        QCOMPARE(context.value(QStringLiteral("reason")).toString(), QLatin1String(reason));
        QVERIFY(!context.contains(QStringLiteral("encoder")));
        QVERIFY(decodeRemoteAudioContext(context, true).has_value());
        QVERIFY(!decodeRemoteAudioContext(context, false).has_value());
    };
    const auto sendAudio = [&h](quint32 revision, bool enabled) {
        QVERIFY(h.client.sendMediaControl(audioControl(revision, enabled),
                                          h.client.sessionEpoch()));
    };

    // encoder-unavailable needs DaemonAudioSender::start() to fail with a
    // ready peer and a connected radio. That is unreachable here without a
    // production seam: the SSRC is never 0, RadioModel always owns an
    // AudioEngine and the default encoder always initialises.

    // Asked for before the media peer is ready, then granted once it is.
    sendAudio(1, true);
    expectOff(1, 1, "media-not-ready");
    if (QTest::currentTestFailed()) { return; }
    h.mediaTransport->becomeReady();
    expectOn(2, 1);
    if (QTest::currentTestFailed()) { return; }

    // The client's own choice outranks the radio: off, then the radio drops.
    sendAudio(2, false);
    expectOff(3, 2, "client-disabled");
    if (QTest::currentTestFailed()) { return; }
    h.radio.setConnectionStateForTest(ConnectionState::Disconnected);
    expectOff(4, 2, "client-disabled");
    if (QTest::currentTestFailed()) { return; }

    // Asked for while the radio is offline, granted when it returns, and
    // withdrawn again when it drops with audio still wanted.
    sendAudio(3, true);
    expectOff(5, 3, "radio-offline");
    if (QTest::currentTestFailed()) { return; }
    h.radio.setConnectionStateForTest(ConnectionState::Connected);
    expectOn(6, 3);
    if (QTest::currentTestFailed()) { return; }
    h.radio.setConnectionStateForTest(ConnectionState::Disconnected);
    expectOff(7, 3, "radio-offline");
    if (QTest::currentTestFailed()) { return; }
    h.finish();
}

// R-R3-23: nereusd's audio_bitrate reaches both the offer (the transport's
// start options, which set the SDP ceiling) and the encoder whose profile the
// minor-8 audio context reports.
void TstDaemonMediaController::configuredAudioBitrateReachesOfferAndContext()
{
    OpusAudioEncoder encoder;
    if (!encoder.isReady()) {
        QSKIP("Opus encoder is unavailable in this build");
    }
    Harness h;
    QCOMPARE(h.controller.audioTargetBitrate(), 24000);
    h.controller.setAudioTargetBitrate(48000);
    h.establishSession();
    QVERIFY(h.server.remoteAudioStatusAvailable());
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    QVERIFY(h.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("start")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}},
        h.client.sessionEpoch()));
    QTRY_VERIFY(h.mediaTransport);
    QCOMPARE(h.mediaTransport->startOptions.role, IMediaTransport::Role::Offerer);
    QCOMPARE(h.mediaTransport->startOptions.audioTargetBitrate, 48000);

    h.mediaTransport->becomeReady();
    QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
    const auto enabledContext = [&controls]() -> QJsonObject {
        for (const auto& call : controls) {
            const QJsonObject message = call.at(0).toJsonObject();
            if (message.value(QStringLiteral("op")) == QLatin1String("audio-context")
                && message.value(QStringLiteral("enabled")).toBool()) {
                return message;
            }
        }
        return {};
    };
    QTRY_VERIFY(!enabledContext().isEmpty());
    const std::optional<RemoteAudioContextMessage> decoded =
        decodeRemoteAudioContext(enabledContext(), true);
    QVERIFY(decoded.has_value());
    QVERIFY(decoded->encoder.has_value());
    QCOMPARE(decoded->encoder->targetBitrate, 48000);
    h.finish();
}

QTEST_MAIN(TstDaemonMediaController)
#include "tst_daemon_media_controller.moc"

// R-R3-01/R-R3-08: E1 owns a Wide engine. E2 on the same stream asks for a
// longer Wide FFT, moves to Fine, resizes Fine and leaves. None of that may
// renew E1's context or change the engine that feeds it.
void TstDaemonMediaController::sharedEngineKeepsOtherPanWhileNeighbourChurns()
{
    Harness h;
    h.establishSession();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);

    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(1, 1, h.sliceId, centre, QStringLiteral("wide"), 1024),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 1).isEmpty();
    })());
    const QJsonObject e1 = messageFor(controls, QStringLiteral("context"), 1);
    const qint64 e1Generation = e1.value(QStringLiteral("contextGeneration")).toInteger();
    const double e1Span = e1.value(QStringLiteral("spanHz")).toDouble();

    const auto e1Untouched = [&]() {
        QCOMPARE(messageCount(controls, QStringLiteral("context"), 1), 1);
        const QJsonObject latest = messageFor(controls, QStringLiteral("context"), 1);
        QCOMPARE(latest.value(QStringLiteral("contextGeneration")).toInteger(), e1Generation);
        QCOMPARE(latest.value(QStringLiteral("spanHz")).toDouble(), e1Span);
    };
    const auto waitForE2Revision = [&](int revision) {
        QTRY_VERIFY(([&] {
            h.feedRadio();
            return messageFor(controls, QStringLiteral("context"), 2)
                .value(QStringLiteral("revision")).toInt() == revision;
        })());
    };

    // A "wide" label with a longer size cannot lengthen E1's engine.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(2, 1, h.sliceId, centre, QStringLiteral("wide"), 4096),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    waitForE2Revision(1);
    // E2 shares E1's engine, so both crops cover the same bins.
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 2)
                 .value(QStringLiteral("spanHz")).toDouble(), e1Span);
    e1Untouched();

    // E2 moves to its own Fine engine, then resizes it.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(2, 2, h.sliceId, centre, QStringLiteral("fine"), 8192),
        h.client.sessionEpoch()));
    waitForE2Revision(2);
    e1Untouched();
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(2, 3, h.sliceId, centre, QStringLiteral("fine"), 16384),
        h.client.sessionEpoch()));
    waitForE2Revision(3);
    e1Untouched();

    QVERIFY(h.client.sendMediaControl(unsubscription(2), h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_COMPARE(h.controller.activeSourceCount(), 1);

    // E1 still paints from its original context after the churn: a keyframe
    // is honoured only for the endpoint's current context generation.
    QVERIFY(h.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("keyframe")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
        {QStringLiteral("endpointId"), 1},
        {QStringLiteral("contextGeneration"), e1Generation}},
        h.client.sessionEpoch()));
    h.mediaTransport->displays.clear();
    QTRY_VERIFY(([&] {
        h.feedRadio();
        for (const QByteArray& bytes : std::as_const(h.mediaTransport->displays)) {
            DisplayCodecDecoder fresh;
            const DisplayCodecDecodeResult decoded = fresh.decode(bytes);
            if (decoded.disposition == DisplayCodecDisposition::Accepted
                && decoded.frame.context.endpointId == 1
                && decoded.frame.context.contextGeneration
                    == static_cast<quint32>(e1Generation)) {
                return true;
            }
        }
        return false;
    })());
    e1Untouched();

    // The reverse: a pan that only shares E1's engine leaves, and E1 is
    // still untouched.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(3, 1, h.sliceId, centre, QStringLiteral("wide"), 4096),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageCount(controls, QStringLiteral("context"), 3) == 1;
    })());
    QCOMPARE(h.controller.spectrumGrant(3)->grantedFftSize, 1024);
    QCOMPARE(h.controller.spectrumGrant(3)->reason, SpectrumLimitReason::SharedEngine);
    QVERIFY(h.client.sendMediaControl(unsubscription(3), h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    for (int i = 0; i < 20; ++i) { h.feedRadio(); QTest::qWait(10); }
    e1Untouched();

    // E1 leaves. The pan that was held to E1's engine is now alone on it,
    // so it is granted its own request, and its renewed context says so.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(4, 1, h.sliceId, centre, QStringLiteral("wide"), 4096),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageCount(controls, QStringLiteral("context"), 4) == 1;
    })());
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 4)
                 .value(QStringLiteral("limit")).toString(), QStringLiteral("shared"));
    QVERIFY(h.client.sendMediaControl(unsubscription(1), h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageCount(controls, QStringLiteral("context"), 4) == 2;
    })());
    const QJsonObject upgraded = messageFor(controls, QStringLiteral("context"), 4);
    QCOMPARE(upgraded.value(QStringLiteral("grantedFftSize")).toInt(), 4096);
    QCOMPARE(upgraded.value(QStringLiteral("limit")).toString(), QStringLiteral("none"));
    QCOMPARE(h.controller.spectrumGrant(4)->grantedFftSize, 4096);
    QCOMPARE(h.controller.spectrumGrant(4)->reason, SpectrumLimitReason::None);
    h.finish();
}

// R-R3-01/R-R3-08: a neighbour's frame rate is not a reason to renew a pan.
// The engine runs at the highest rate its pans ask for; each endpoint keeps
// its own cadence, so no pan is held below the rate it requested and no pan
// is renewed when a neighbour joins, changes rate or leaves.
void TstDaemonMediaController::sharedEngineKeepsOtherPanAcrossFrameRates()
{
    Harness h;
    h.establishSession();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    const auto atFps = [&](quint32 endpointId, quint32 revision, int fps) {
        QJsonObject request = tieredSubscription(endpointId, revision, h.sliceId, centre,
                                                 QStringLiteral("wide"), 1024);
        request.insert(QStringLiteral("fps"), fps);
        return request;
    };
    const auto contextFor = [&](quint32 endpointId, int count) {
        QTRY_VERIFY(([&] {
            h.feedRadio();
            return messageCount(controls, QStringLiteral("context"), endpointId) >= count;
        })());
    };
    const auto settle = [&] {
        for (int i = 0; i < 20; ++i) { h.feedRadio(); QTest::qWait(10); }
    };

    // E1 at 30 fps alone, then E2 on the same engine at 60, then at 15,
    // then gone. E1 keeps one context and its bins throughout.
    QVERIFY(h.client.sendMediaControl(atFps(1, 1, 30), h.client.sessionEpoch()));
    contextFor(1, 1);
    const QJsonObject e1 = messageFor(controls, QStringLiteral("context"), 1);
    const auto e1Untouched = [&] {
        QCOMPARE(messageCount(controls, QStringLiteral("context"), 1), 1);
        const QJsonObject latest = messageFor(controls, QStringLiteral("context"), 1);
        QCOMPARE(latest.value(QStringLiteral("contextGeneration")),
                 e1.value(QStringLiteral("contextGeneration")));
        QCOMPARE(latest.value(QStringLiteral("traceSamples")),
                 e1.value(QStringLiteral("traceSamples")));
        QCOMPARE(latest.value(QStringLiteral("fps")).toInt(), 30);
    };
    QCOMPARE(h.controller.spectrumSourceFps(1), std::optional<int>(30));

    QVERIFY(h.client.sendMediaControl(atFps(2, 1, 60), h.client.sessionEpoch()));
    contextFor(2, 1);
    settle();
    e1Untouched();
    QCOMPARE(h.controller.spectrumSourceFps(2), std::optional<int>(60));
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 2)
                 .value(QStringLiteral("fps")).toInt(), 60);

    QVERIFY(h.client.sendMediaControl(atFps(2, 2, 15), h.client.sessionEpoch()));
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 2)
            .value(QStringLiteral("revision")).toInt() == 2;
    })());
    settle();
    e1Untouched();
    QCOMPARE(h.controller.spectrumSourceFps(1), std::optional<int>(30));

    QVERIFY(h.client.sendMediaControl(unsubscription(2), h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    settle();
    e1Untouched();

    // The reverse order: a background pan at 15 fps is first, a focused pan
    // at 60 joins. The focused pan's engine runs at its own rate, and the
    // background pan is not renewed when the focused pan arrives or leaves.
    QVERIFY(h.client.sendMediaControl(unsubscription(1), h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 0);
    QVERIFY(h.client.sendMediaControl(atFps(3, 1, 15), h.client.sessionEpoch()));
    contextFor(3, 1);
    QVERIFY(h.client.sendMediaControl(atFps(4, 1, 60), h.client.sessionEpoch()));
    contextFor(4, 1);
    settle();
    QCOMPARE(h.controller.spectrumSourceFps(4), std::optional<int>(60));
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 4)
                 .value(QStringLiteral("fps")).toInt(), 60);
    QCOMPARE(messageCount(controls, QStringLiteral("context"), 3), 1);
    QVERIFY(h.client.sendMediaControl(unsubscription(4), h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    settle();
    QCOMPARE(messageCount(controls, QStringLiteral("context"), 3), 1);
    QCOMPARE(h.controller.spectrumSourceFps(3), std::optional<int>(15));
    h.finish();
}

// R-R3-01/R-R3-08: the grant records what Core gave each request and why.
void TstDaemonMediaController::grantReportsLargestSizeSharedEngineAndSourceBins()
{
    Harness h;
    h.establishSession();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    const int largest = NereusSDR::FFTEngine::maximumFftSize();

    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(1, 1, h.sliceId, centre, QStringLiteral("wide"), 1024),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    auto grant = h.controller.spectrumGrant(1);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->requestedFftSize, 1024);
    QCOMPARE(grant->grantedFftSize, 1024);
    QCOMPARE(grant->grantedTier, FftTier::Wide);
    QCOMPARE(grant->requestedPixels, 128);
    QCOMPARE(grant->grantedPixels, 128);
    QCOMPARE(grant->reason, SpectrumLimitReason::None);
    QVERIFY(!h.controller.spectrumGrant(99).has_value());

    // Above the largest size on an engine another pan uses: the shared
    // engine is the limit that decides the grant.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(2, 1, h.sliceId, centre, QStringLiteral("wide"), largest * 2),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    grant = h.controller.spectrumGrant(2);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->requestedFftSize, largest * 2);
    QCOMPARE(grant->grantedFftSize, 1024);
    QCOMPARE(grant->reason, SpectrumLimitReason::SharedEngine);

    // Alone on its own Fine engine, the same request gets the largest size.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(3, 1, h.sliceId, centre, QStringLiteral("fine"), largest * 2),
        h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 3);
    grant = h.controller.spectrumGrant(3);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->grantedFftSize, largest);
    QCOMPARE(grant->grantedTier, FftTier::Fine);
    QCOMPARE(grant->reason, SpectrumLimitReason::LargestSize);

    // More pixels than the crop has source bins: granted the visible bins,
    // and the context carries exactly that many samples.
    QJsonObject wider = tieredSubscription(1, 2, h.sliceId, centre,
                                           QStringLiteral("wide"), 1024);
    wider.insert(QStringLiteral("pixels"), SpectrumEndpoint::kMaxPixels);
    QVERIFY(h.client.sendMediaControl(wider, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.spectrumGrant(1)->requestedPixels, SpectrumEndpoint::kMaxPixels);
    grant = h.controller.spectrumGrant(1);
    QCOMPARE(grant->grantedFftSize, 1024);
    QCOMPARE(grant->reason, SpectrumLimitReason::SourceBins);
    // 48 kHz of a 192 kHz, 1024-bin source: 256 bins plus the inclusive edge.
    QCOMPARE(grant->grantedPixels, 257);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 1)
            .value(QStringLiteral("revision")).toInt() == 2;
    })());
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 1)
                 .value(QStringLiteral("traceSamples")).toInt(), 257);
    QCOMPARE(h.controller.spectrumGrant(1)->grantedPixels, 257);
    h.finish();
}

// R-R3-08: the display budget charges the pixels Core grants, not the ones
// asked for, so a request clamped by its source bins fits a tight budget.
void TstDaemonMediaController::budgetChargesGrantedPixels()
{
    const SpectrumDisplayCost limit = *spectrumDisplayCost(128, 60, false);
    Harness h(DisplayBudgetLimits{limit.charge.applicationBytesPerSecond,
                                  limit.charge.spectrumSampleUnitsPerSecond, 7});
    h.establishSession();
    QVERIFY(h.server.displayBudgetAvailable());
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();

    QJsonObject request = subscription(70, 1, h.sliceId,
                                       h.radio.streamCentreHz(h.streamIndex));
    request.insert(QStringLiteral("pixels"), SpectrumEndpoint::kMaxPixels);
    request.insert(QStringLiteral("spanHz"), 18750.0); // about 100 source bins
    QVERIFY(h.client.sendMediaControl(request, h.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 70, 1).isEmpty());
    const QJsonObject result = allocationFor(controls, 70, 1);
    QCOMPARE(result.value(QStringLiteral("accepted")).toBool(), true);
    const auto grant = h.controller.spectrumGrant(70);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->reason, SpectrumLimitReason::SourceBins);
    QVERIFY(grant->grantedPixels > 0 && grant->grantedPixels < 128);
    const SpectrumDisplayCost charged = *spectrumDisplayCost(grant->grantedPixels, 60, false);
    QCOMPARE(result.value(QStringLiteral("spectrumSampleUnitsPerSecond")).toInteger(),
             static_cast<qint64>(charged.charge.spectrumSampleUnitsPerSecond));
    QCOMPARE(result.value(QStringLiteral("applicationBytesPerSecond")).toInteger(),
             static_cast<qint64>(charged.charge.applicationBytesPerSecond));
    h.finish();
}

// R-R3-01/R-R3-08/R-R3-37: under the display budget a minor 9 GUI holds
// the charge for the pixels it was granted. When the neighbour that held it
// to a smaller engine leaves, the pan gets its own FFT size but no pixels
// beyond that charge, and it is not told the receiver lacks detail.
void TstDaemonMediaController::regrantAfterNeighbourLeavesStaysWithinAdmittedCharge()
{
    Harness h(DisplayBudgetLimits{10'000'000, 10'000'000, 7});
    h.establishSession();
    QVERIFY(h.server.displayBudgetAvailable());
    QVERIFY(h.server.spectrumGrantAvailable());
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);

    QVERIFY(h.client.sendMediaControl(subscription(1, 1, h.sliceId, centre),
                                      h.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 1, 1).isEmpty());
    QJsonObject deep = tieredSubscription(2, 1, h.sliceId, centre,
                                          QStringLiteral("wide"), 4096);
    deep.insert(QStringLiteral("spanHz"), 6000.0);
    QVERIFY(h.client.sendMediaControl(deep, h.client.sessionEpoch()));
    QTRY_VERIFY(!allocationFor(controls, 2, 1).isEmpty());
    QVERIFY(allocationFor(controls, 2, 1).value(QStringLiteral("accepted")).toBool());
    const auto shared = h.controller.spectrumGrant(2);
    QVERIFY(shared.has_value());
    QCOMPARE(shared->reason, SpectrumLimitReason::SharedEngine);
    QCOMPARE(shared->grantedFftSize, 1024);
    const int admittedPixels = shared->grantedPixels;
    QVERIFY(admittedPixels > 0 && admittedPixels < 128);
    const SpectrumDisplayCost admitted = *spectrumDisplayCost(admittedPixels, 60, false);
    QCOMPARE(allocationFor(controls, 2, 1).value(QStringLiteral("spectrumSampleUnitsPerSecond"))
                 .toInteger(),
             static_cast<qint64>(admitted.charge.spectrumSampleUnitsPerSecond));

    QVERIFY(h.client.sendMediaControl({
        {QStringLiteral("op"), QStringLiteral("unsubscribe")},
        {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
        {QStringLiteral("endpointId"), 1},
        {QStringLiteral("revision"), 2}}, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return messageFor(controls, QStringLiteral("context"), 2)
            .value(QStringLiteral("grantedFftSize")).toInt() == 4096;
    })());
    const QJsonObject context = messageFor(controls, QStringLiteral("context"), 2);
    QCOMPARE(context.value(QStringLiteral("traceSamples")).toInt(), admittedPixels);
    QCOMPARE(context.value(QStringLiteral("limit")).toString(), QStringLiteral("none"));
    const auto regranted = h.controller.spectrumGrant(2);
    QCOMPARE(regranted->grantedFftSize, 4096);
    QCOMPARE(regranted->grantedPixels, admittedPixels);
    QCOMPARE(regranted->reason, SpectrumLimitReason::None);
    h.finish();
}

// R-R3-09: Core accepts only what the GUI's context parser accepts. Every
// refused request goes through the typed rejection path and leaves the live
// endpoint and its source exactly as they were.
void TstDaemonMediaController::outOfRangeRequestsAreRejectedAndLeaveEndpointUntouched()
{
    Harness h;
    h.establishSession();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    const QJsonObject live = subscription(1, 1, h.sliceId, centre);
    QVERIFY(h.client.sendMediaControl(live, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 1).isEmpty();
    })());
    const qint64 generation = messageFor(controls, QStringLiteral("context"), 1)
        .value(QStringLiteral("contextGeneration")).toInteger();

    const QList<std::pair<QString, QJsonValue>> invalid{
        {QStringLiteral("framesPerLine"), kMaxFramesPerLine + 1},
        {QStringLiteral("minDbm"), kMinDbmLimit - 1.0},
        {QStringLiteral("maxDbm"), kMaxDbmLimit + 1.0},
        {QStringLiteral("centreHz"), centre + 500000.0},
        {QStringLiteral("fps"), 0},
        {QStringLiteral("fps"), 61},
        {QStringLiteral("pixels"), 0},
        {QStringLiteral("pixels"), SpectrumEndpoint::kMaxPixels + 1},
        {QStringLiteral("spanHz"), 0.0},
        {QStringLiteral("spanHz"), -48000.0},
        {QStringLiteral("fftSize"), 3000},
        {QStringLiteral("fps"), QStringLiteral("60")},
        {QStringLiteral("minDbm"), 0.0}, // equal to maxDbm
    };
    quint32 endpointId = 10;
    for (const auto& [key, value] : invalid) {
        QJsonObject bad = subscription(endpointId, 1, h.sliceId, centre);
        bad.insert(key, value);
        QVERIFY(h.client.sendMediaControl(bad, h.client.sessionEpoch()));
        QTRY_VERIFY2(!messageFor(controls, QStringLiteral("rejected"), endpointId).isEmpty(),
                     qPrintable(key));
        QCOMPARE(h.controller.activeEndpointCount(), 1);
        QCOMPARE(h.controller.activeSourceCount(), 1);
        ++endpointId;

        // The same bad value as a replacement of the live endpoint.
        QJsonObject replacement = live;
        replacement.insert(QStringLiteral("revision"), static_cast<qint64>(endpointId));
        replacement.insert(key, value);
        const int rejectedBefore = messageCount(controls, QStringLiteral("rejected"), 1);
        QVERIFY(h.client.sendMediaControl(replacement, h.client.sessionEpoch()));
        QTRY_COMPARE(messageCount(controls, QStringLiteral("rejected"), 1), rejectedBefore + 1);
        QCOMPARE(h.controller.activeEndpointCount(), 1);
        QCOMPARE(h.controller.spectrumGrant(1)->requestedPixels, 128);
    }

    h.feedRadio(0.1875);
    QTest::qWait(20);
    QCOMPARE(messageCount(controls, QStringLiteral("context"), 1), 1);
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 1)
                 .value(QStringLiteral("contextGeneration")).toInteger(), generation);

    // The limits themselves are accepted, and the GUI accepts their context.
    QJsonObject edge = subscription(40, 1, h.sliceId, centre);
    edge.insert(QStringLiteral("framesPerLine"), kMaxFramesPerLine);
    edge.insert(QStringLiteral("minDbm"), kMinDbmLimit);
    edge.insert(QStringLiteral("maxDbm"), kMaxDbmLimit);
    QVERIFY(h.client.sendMediaControl(edge, h.client.sessionEpoch()));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    QVERIFY(messageFor(controls, QStringLiteral("rejected"), 40).isEmpty());
    h.finish();
}

// Ownership is pinned: a control from another epoch or a connection id that
// is not the session's media peer never reaches the endpoints.
void TstDaemonMediaController::staleEpochAndForeignConnectionLeaveEndpointsUntouched()
{
    Harness h;
    h.establishSession();
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    const quint64 epoch = h.client.sessionEpoch();
    QVERIFY(h.client.sendMediaControl(subscription(1, 1, h.sliceId, centre), epoch));
    QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return !messageFor(controls, QStringLiteral("context"), 1).isEmpty();
    })());
    const QJsonObject context = messageFor(controls, QStringLiteral("context"), 1);

    // Delivered as the station would, but stamped with a different epoch.
    QJsonObject replacement = subscription(1, 2, h.sliceId, centre, 4096);
    emit h.server.mediaControlReceived(replacement, epoch + 1);
    emit h.server.mediaControlReceived(unsubscription(1), epoch + 1);
    emit h.server.mediaControlReceived(subscription(2, 1, h.sliceId, centre), epoch + 1);
    if (epoch > 1) {
        emit h.server.mediaControlReceived(unsubscription(1), epoch - 1);
    }

    // A syntactically valid connection id that is not this session's peer.
    const QString foreign = QStringLiteral("99999999-2222-4333-8444-555555555555");
    replacement.insert(QStringLiteral("connectionId"), foreign);
    QVERIFY(h.client.sendMediaControl(replacement, epoch));
    QJsonObject foreignUnsubscribe = unsubscription(1);
    foreignUnsubscribe.insert(QStringLiteral("connectionId"), foreign);
    QVERIFY(h.client.sendMediaControl(foreignUnsubscribe, epoch));
    QJsonObject foreignNew = subscription(3, 1, h.sliceId, centre);
    foreignNew.insert(QStringLiteral("connectionId"), foreign);
    QVERIFY(h.client.sendMediaControl(foreignNew, epoch));

    // Positive control: the same direct delivery with the live epoch works.
    emit h.server.mediaControlReceived(subscription(4, 1, h.sliceId, centre), epoch);
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
    QVERIFY(!h.controller.spectrumGrant(2).has_value());
    QVERIFY(!h.controller.spectrumGrant(3).has_value());
    QCOMPARE(h.controller.spectrumGrant(1)->requestedFftSize, 1024);

    h.feedRadio(0.1875);
    QTest::qWait(20);
    QCOMPARE(messageCount(controls, QStringLiteral("context"), 1), 1);
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 1)
                 .value(QStringLiteral("contextGeneration")).toInteger(),
             context.value(QStringLiteral("contextGeneration")).toInteger());
    QVERIFY(messageFor(controls, QStringLiteral("rejected"), 1).isEmpty());
    h.finish();
}

// R-R3-09: a GUI from before minor 9 keeps receiving exactly today's
// spectrum context, with and without wideband, while Core still records
// the grant it made.
void TstDaemonMediaController::minorEightPeerReceivesTodaysSpectrumContext()
{
    Harness h;
    h.enableWidebandSource();
    auto* station = new Test::LoopbackTransport(QStringLiteral("minor8-station"), this);
    auto* peer = new Test::LoopbackTransport(QStringLiteral("minor8-peer"), this);
    station->linkTo(peer);
    h.server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kRemoteSpectrumGrantSessionProtocolMinor - 1, 0,
        QStringLiteral("minor-8 client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(h.server.token())));
    QTRY_VERIFY(h.server.mediaAvailable());
    QVERIFY(h.server.remoteWidebandAvailable());
    QVERIFY(!h.server.spectrumGrantAvailable());
    const auto send = [&](const QJsonObject& payload) {
        SessionMessage message;
        message.kind = SessionMessageKind::MediaControl;
        message.mediaPayload = payload;
        peer->sendText(SessionMessages::encode(message));
    };
    send({{QStringLiteral("op"), QStringLiteral("start")},
          {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}});
    QTRY_VERIFY(h.mediaTransport);
    h.mediaTransport->becomeReady();
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    send(subscription(81, 1, h.sliceId, centre));
    QJsonObject extended = subscription(82, 1, h.sliceId, centre);
    extended.insert(QStringLiteral("extendedView"), true);
    send(extended);
    QTRY_COMPARE(h.controller.activeEndpointCount(), 2);

    const auto contextFor = [&](quint32 endpointId) {
        QJsonObject latest;
        for (const QByteArray& wire : peer->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)
                && message.kind == SessionMessageKind::MediaControl
                && message.mediaPayload.value(QStringLiteral("op")) == QLatin1String("context")
                && message.mediaPayload.value(QStringLiteral("endpointId")).toInteger()
                    == qint64{endpointId}) {
                latest = message.mediaPayload;
            }
        }
        return latest;
    };
    QTRY_VERIFY(([&] {
        h.feedRadio();
        return !contextFor(81).isEmpty() && !contextFor(82).isEmpty();
    })());

    QStringList todaysKeys{
        QStringLiteral("op"), QStringLiteral("connectionId"), QStringLiteral("endpointId"),
        QStringLiteral("revision"), QStringLiteral("contextGeneration"),
        QStringLiteral("sourceStream"), QStringLiteral("sourceCentreHz"),
        QStringLiteral("sampleRateHz"), QStringLiteral("centreHz"), QStringLiteral("spanHz"),
        QStringLiteral("wideCentreHz"), QStringLiteral("wideSpanHz"),
        QStringLiteral("traceSamples"), QStringLiteral("waterfallSamples"),
        QStringLiteral("wideSamples"), QStringLiteral("minDbm"), QStringLiteral("maxDbm"),
        QStringLiteral("fps"), QStringLiteral("framesPerLine")};
    QCOMPARE(todaysKeys.size(), 19);
    for (const quint32 endpointId : {81u, 82u}) {
        const QJsonObject context = contextFor(endpointId);
        QStringList expected = todaysKeys;
        if (endpointId == 82) { expected.append(QStringLiteral("wideband")); }
        expected.sort();
        QCOMPARE(context.keys(), expected);
        // What a minor-8 GUI accepts, and not what a minor-9 GUI accepts.
        const std::optional<SpectrumContextMessage> decoded =
            decodeRemoteSpectrumContext(context, false);
        QVERIFY(decoded.has_value());
        QVERIFY(!decodeRemoteSpectrumContext(context, true).has_value());
        QCOMPARE(encodeRemoteSpectrumContext(*decoded, false), context);
        QVERIFY(h.controller.spectrumGrant(endpointId).has_value());
    }
    peer->closeLink(QStringLiteral("test complete"));
}

// R-R3-01/08: from minor 9 on, each context carries the grant Core recorded,
// including what limited it. The harness negotiates the current minor, which
// only has to have reached the grant minor; later minors keep the grant.
void TstDaemonMediaController::minorNineSpectrumContextsReportTheGrant()
{
    Harness h;
    h.establishSession();
    QCOMPARE(h.client.agreedMinor(), kSessionProtocolMinor);
    QVERIFY(h.client.agreedMinor() >= kRemoteSpectrumGrantSessionProtocolMinor);
    QVERIFY(h.server.spectrumGrantAvailable());
    QVERIFY(h.client.spectrumGrantAvailable());
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer();
    const double centre = h.radio.streamCentreHz(h.streamIndex);
    const int largest = NereusSDR::FFTEngine::maximumFftSize();

    const auto reported = [&](quint32 endpointId, quint32 revision)
        -> std::optional<SpectrumContextGrant> {
        const QJsonObject context = messageFor(controls, QStringLiteral("context"), endpointId);
        if (context.value(QStringLiteral("revision")).toInteger() != qint64{revision}) {
            return std::nullopt;
        }
        const std::optional<SpectrumContextMessage> decoded =
            decodeRemoteSpectrumContext(context, true);
        return decoded ? decoded->grant : std::nullopt;
    };
    const auto awaitReport = [&](quint32 endpointId, quint32 revision, int feedsPerPoll) {
        std::optional<SpectrumContextGrant> grant;
        // The caller verifies the result; QTRY cannot return a value.
        const bool arrived = QTest::qWaitFor([&] {
            for (int feed = 0; feed < feedsPerPoll; ++feed) { h.feedRadio(); }
            grant = reported(endpointId, revision);
            return grant.has_value();
        }, 10000);
        Q_UNUSED(arrived);
        return grant;
    };

    // Alone on its engine: nothing limits it.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(1, 1, h.sliceId, centre, QStringLiteral("wide"), 1024),
        h.client.sessionEpoch()));
    std::optional<SpectrumContextGrant> grant = awaitReport(1, 1, 1);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->grantedFftSize, 1024);
    QCOMPARE(grant->grantedTier, FftTier::Wide);
    QCOMPARE(grant->requestedPixels, 128);
    QCOMPARE(grant->grantedPixels, 128);
    QCOMPARE(grant->limit, SpectrumLimitReason::None);
    QCOMPARE(*grant, spectrumContextGrant(*h.controller.spectrumGrant(1)));

    // A larger request on the Wide engine E1 uses: the shared engine stands.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(2, 1, h.sliceId, centre, QStringLiteral("wide"), largest * 2),
        h.client.sessionEpoch()));
    grant = awaitReport(2, 1, 1);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->grantedFftSize, 1024);
    QCOMPARE(grant->limit, SpectrumLimitReason::SharedEngine);
    QCOMPARE(*grant, spectrumContextGrant(*h.controller.spectrumGrant(2)));

    // Alone on the Fine engine, above the largest size.
    QVERIFY(h.client.sendMediaControl(
        tieredSubscription(3, 1, h.sliceId, centre, QStringLiteral("fine"), largest * 2),
        h.client.sessionEpoch()));
    grant = awaitReport(3, 1, 64);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->grantedFftSize, largest);
    QCOMPARE(grant->grantedTier, FftTier::Fine);
    QCOMPARE(grant->limit, SpectrumLimitReason::LargestSize);
    QCOMPARE(*grant, spectrumContextGrant(*h.controller.spectrumGrant(3)));

    // More points than E1's crop has source bins.
    QJsonObject wider = tieredSubscription(1, 2, h.sliceId, centre, QStringLiteral("wide"), 1024);
    wider.insert(QStringLiteral("pixels"), SpectrumEndpoint::kMaxPixels);
    QVERIFY(h.client.sendMediaControl(wider, h.client.sessionEpoch()));
    grant = awaitReport(1, 2, 1);
    QVERIFY(grant.has_value());
    QCOMPARE(grant->requestedPixels, SpectrumEndpoint::kMaxPixels);
    QCOMPARE(grant->grantedPixels, 257);
    QCOMPARE(grant->limit, SpectrumLimitReason::SourceBins);
    QCOMPARE(messageFor(controls, QStringLiteral("context"), 1)
                 .value(QStringLiteral("traceSamples")).toInt(), 257);
    QCOMPARE(*grant, spectrumContextGrant(*h.controller.spectrumGrant(1)));
    QVERIFY(messageFor(controls, QStringLiteral("rejected"), 1).isEmpty());
    h.finish();
}
