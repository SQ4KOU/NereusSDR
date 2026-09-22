// =================================================================
// tests/tst_daemon_media_controller.cpp  (NereusSDR)
// =================================================================
// R3 daemon media controller integration coverage over a real authenticated
// StationServer/StationClient control session. Synthetic I/Q is test-only;
// production reaches the source exclusively through RadioModel's tagged tap.
// =================================================================

#include <QtTest>

#include "core/AppSettings.h"
#include "core/FFTEngine.h"
#include "core/P2RadioConnection.h"
#include "core/DdcAssignment.h"
#include "core/WidebandFrameAccumulator.h"
#include "core/HpsdrModel.h"
#include "core/NoiseFloorEstimator.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/IMediaTransport.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <numbers>

using namespace NereusSDR;

namespace {

constexpr char kConnectionId[] = "11111111-2222-4333-8444-555555555555";

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
        if (!readyState) { return false; }
        displays.append(bytes);
        const auto callback = onDisplaySend;
        if (callback) { return callback(); }
        return true;
    }
    bool sendRtp(const QByteArray&) override { return readyState; }
    bool isReady() const override { return readyState; }
    void becomeReady() { readyState = true; emit ready(); }

    bool started{false};
    bool readyState{false};
    StartOptions startOptions{Role::Answerer, 0};
    QList<QByteArray> displays;
    std::function<bool()> onDisplaySend;
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
    QPointer<ClosingControlTransport> stationTransport;
    DaemonMediaController controller;
    int sliceId{-1};
    int spareSliceId{-1};
    int streamIndex{-1};

    Harness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, directory.path())
        , controller(&server, &radio, nullptr,
                     [this](QObject* parent) -> IMediaTransport* {
                         mediaTransport = new FakeTransport(parent);
                         return mediaTransport;
                     })
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
};

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
    QCOMPARE(context.size(), 19);
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
    QCOMPARE(context.size(), 20);
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

QTEST_MAIN(TstDaemonMediaController)
#include "tst_daemon_media_controller.moc"
