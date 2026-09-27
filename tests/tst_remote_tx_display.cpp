// =================================================================
// tests/tst_remote_tx_display.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test.
//
// Remote-window parity Task 28 (R-R3-49, A11, R-R3-01, R-R3-08, R-R3-37,
// R-IOS-13): the Core sends the transmit analyzer's display, not the
// receiver's, for the pan hosting the transmitting slice while it is keyed,
// to a media peer that declared txDisplayVersion. Thetis shows the transmit
// analyzer on the transmitting receiver's display while keyed with display
// duplex off (console.cs:24281-24338 [v2.10.3.15], DisplayThread), and the
// display follows XIT while keyed (console.cs:22138-22150 [v2.10.3.15]).
//
// A real authenticated StationServer / StationClient session over the
// in-process loopback, a fake media transport, a real TxAnalyzer whose
// output signals stand in for its poll (the test seam), and the Core keyed
// through its own MoxController with the receive-only pre-check lifted (the
// pattern of tst_remote_peripherals). Nothing reaches a radio.
//
// Modification history (NereusSDR):
//   2026-09-26 : Created for parity Task 28 by J.J. Boyd (KG4VCF).
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-09-26 : Tasks 27-29 fix wave (R-R3-49): the rise pan holds the
//                 transmit display through a layout change and a rebinding
//                 while keyed; remote view changes reach the analyzer at
//                 most once per coalescing period. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-09-27 : Parity Task 31 (A11, R-R3-49): txDisplayVersion 3; a
//                 `duplex` endpoint keeps the receiver while keyed, and the
//                 field is read only from a peer that declared 3. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest>

// The test seam for a rebinding while keyed: TxSliceArbiter::flipTo moves
// the transmit flag without requestHandoff's unkey (a slice removed while
// keyed is the product path; this reaches the same edge directly).
#define private public
#include "core/TxSliceArbiter.h"
#undef private

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/TxAnalyzer.h"
#include "core/TxDisplayFeed.h"
#include "core/WdspEngine.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/TransmitStateFacade.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/RemoteSpectrumContext.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <numbers>
#include <optional>

using namespace NereusSDR;

namespace {

constexpr char kConnectionId[] = "22222222-3333-4444-8555-666666666666";
constexpr double kTxMinDbm = -80.0;
constexpr double kTxMaxDbm = 20.0;

class FakeTransport final : public IMediaTransport {
public:
    explicit FakeTransport(QObject* parent = nullptr) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { started = true; return true; }
    void stop() override { started = readyState = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray& bytes) override
    {
        if (!readyState) { return false; }
        displays.append(bytes);
        return true;
    }
    bool sendRtp(const QByteArray&) override { return readyState; }
    bool isReady() const override { return readyState; }
    bool losslessAudioNegotiated() const override { return false; }
    void becomeReady() { readyState = true; emit ready(); }

    bool started{false};
    bool readyState{false};
    QList<QByteArray> displays;
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

QJsonObject subscription(quint32 endpointId, quint32 revision, int sliceId, double centreHz,
                         double spanHz = 48000.0, int fps = 60)
{
    return {{QStringLiteral("op"), QStringLiteral("subscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
            {QStringLiteral("revision"), static_cast<qint64>(revision)},
            {QStringLiteral("sliceId"), sliceId},
            {QStringLiteral("tier"), QStringLiteral("wide")},
            {QStringLiteral("fftSize"), 1024},
            {QStringLiteral("windowType"), static_cast<int>(WindowFunction::Hann)},
            {QStringLiteral("centreHz"), centreHz},
            {QStringLiteral("spanHz"), spanHz},
            {QStringLiteral("pixels"), 128},
            {QStringLiteral("fps"), fps},
            {QStringLiteral("framesPerLine"), 1},
            {QStringLiteral("trace"), plane()},
            {QStringLiteral("waterfall"), plane()},
            {QStringLiteral("minDbm"), -180.0},
            {QStringLiteral("maxDbm"), 0.0},
            {QStringLiteral("wideSpanFactor"), 0.0}};
}

QJsonObject withTxWindow(QJsonObject request, double minDbm = kTxMinDbm,
                         double maxDbm = kTxMaxDbm)
{
    request.insert(QStringLiteral("txMinDbm"), minDbm);
    request.insert(QStringLiteral("txMaxDbm"), maxDbm);
    return request;
}

QJsonObject unsubscription(quint32 endpointId)
{
    return {{QStringLiteral("op"), QStringLiteral("unsubscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)}};
}

QList<QJsonObject> messagesFor(const QSignalSpy& spy, const QString& op, quint32 endpointId)
{
    QList<QJsonObject> out;
    for (const auto& call : spy) {
        const QJsonObject message = call.at(0).toJsonObject();
        if (message.value(QStringLiteral("op")).toString() == op
            && static_cast<quint32>(message.value(QStringLiteral("endpointId")).toInteger())
                == endpointId) {
            out.append(message);
        }
    }
    return out;
}

std::optional<QJsonObject> lastContext(const QSignalSpy& spy, quint32 endpointId)
{
    const QList<QJsonObject> contexts = messagesFor(spy, QStringLiteral("context"), endpointId);
    if (contexts.isEmpty()) { return std::nullopt; }
    return contexts.last();
}

bool isTransmit(const std::optional<QJsonObject>& context)
{
    return context && context->value(QStringLiteral("transmit")).toBool();
}

quint32 packetEndpoint(const QByteArray& packet)
{
    return qFromBigEndian<quint32>(packet.constData() + 8);
}

quint32 packetGeneration(const QByteArray& packet)
{
    return qFromBigEndian<quint32>(packet.constData() + 12);
}

// The display packets for one endpoint, at one context generation or at any
// other.
int framesFor(const QList<QByteArray>& displays, quint32 endpointId,
              std::optional<quint32> generation, bool otherGenerations = false)
{
    int count = 0;
    for (const QByteArray& packet : displays) {
        if (!packet.startsWith("NSDC") || packetEndpoint(packet) != endpointId) {
            continue;
        }
        const bool match = !generation || packetGeneration(packet) == *generation;
        if (match != otherGenerations) {
            ++count;
        }
    }
    return count;
}

QVector<float> ramp(int count, float start, float step)
{
    QVector<float> out(count);
    for (int i = 0; i < count; ++i) {
        out[i] = start + step * static_cast<float>(i);
    }
    return out;
}

struct Harness {
    QTemporaryDir directory;
    AppSettings settings;
    RadioModel radio;
    std::unique_ptr<TxAnalyzer> analyzer;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy settingsProxy;
    StationClient client{&remote, &settingsProxy};
    QPointer<FakeTransport> mediaTransport;
    qint64 nowNs{0};
    bool realClock{true};
    QElapsedTimer realTimer;
    std::unique_ptr<DaemonMediaController> controller;
    int sliceId{-1};
    int spareSliceId{-1};

    explicit Harness(bool withAnalyzer = true,
                     std::optional<DisplayBudgetLimits> limits = std::nullopt)
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()))
    {
        realTimer.start();
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        sliceId = radio.addSlice();
        spareSliceId = radio.addSlice();
        if (withAnalyzer) {
            analyzer = std::make_unique<TxAnalyzer>(TxAnalyzer::kTxDispId);
            radio.setTxAnalyzer(analyzer.get());
        }
        controller = std::make_unique<DaemonMediaController>(
            &server, &radio, nullptr,
            [this](QObject* parent) -> IMediaTransport* {
                mediaTransport = new FakeTransport(parent);
                return mediaTransport;
            },
            [this] { return realClock ? realTimer.nsecsElapsed() : nowNs; });
        server.setMediaEnabled(true);
        if (limits) {
            QVERIFY(server.setDisplayBudgetLimits(*limits));
        }
    }
    ~Harness()
    {
        controller.reset();
        radio.setTxAnalyzer(nullptr);
    }

    SliceModel* slice() const { return radio.sliceById(sliceId); }
    SliceModel* spare() const { return radio.sliceById(spareSliceId); }
    TxDisplayFeed* feed() const { return radio.txDisplayFeed(); }

    void establishSession()
    {
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(server.mediaAvailable() && client.mediaAvailable());
    }

    bool start(bool declare, int version = 1)
    {
        QJsonObject start{{QStringLiteral("op"), QStringLiteral("start")},
                          {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}};
        if (declare) {
            start.insert(QStringLiteral("txDisplayVersion"), version);
        }
        return client.sendMediaControl(start, client.sessionEpoch());
    }

    void startReadyPeer(bool declare, int version = 1)
    {
        QVERIFY(start(declare, version));
        QTRY_VERIFY(mediaTransport);
        mediaTransport->becomeReady();
    }

    bool send(const QJsonObject& control)
    {
        return client.sendMediaControl(control, client.sessionEpoch());
    }

    void feedStream(int streamIndex)
    {
        QVERIFY(QMetaObject::invokeMethod(
            &radio, "rawIqDataForStream", Qt::DirectConnection, Q_ARG(int, streamIndex),
            Q_ARG(QVector<float>, syntheticIq(1026, 0.125))));
    }

    void feedSlice(int id)
    {
        SliceModel* model = radio.sliceById(id);
        QVERIFY(model);
        feedStream(model->streamIndex());
    }

    bool key(bool on)
    {
        MoxController* mox = radio.moxController();
        if (mox == nullptr) { return false; }
        // Today's Core is receive-only; lifting the pre-check stands in for
        // one that can transmit. The keying goes through the same path.
        mox->setMoxCheck({});
        mox->setMox(on);
        return true;
    }

    // The analyzer's poll, as the test seam: its two output signals.
    void emitPlanes(const QVector<float>& trace, const QVector<float>& waterfall)
    {
        emit analyzer->txFftReady(-1, trace);
        emit analyzer->txWaterfallReady(-1, waterfall);
    }

    void finish() { client.disconnectFromStation(QStringLiteral("test complete")); }
};

} // namespace

class TstRemoteTxDisplay : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void capabilityFollowsTheAnalyzerAndTheMinor();
    void contextCodecCarriesTransmitOnlyWhenNegotiated();
    void riseSendsTheTransmitDisplayAndFallResumesReceive();
    void xitWhileKeyedRenewsTheContext();
    void severalViewersShareTheGoverningView();
    void aSubscribeWhileKeyedMovesTheView();
    void olderPeerKeepsTodaysWire();
    void transmitWindowIsReadLikeTheReceiveWindow();
    void budgetPacesTransmitFramesLikeReceiveFrames();
    void aSliceMovingWhileKeyedLeavesTheRisePanTransmitting();
    void remoteViewChangesWhileKeyedAreCoalesced();
    void txStateCarriesTheHighSwrState();
    void aDuplexEndpointKeepsTheReceiverWhileKeyed();
    void duplexIsReadOnlyFromAVersionThreePeer();
};

void TstRemoteTxDisplay::initTestCase()
{
    // No radio is attached: the model's own not-connected notes are
    // expected here.
    QLoggingCategory::setFilterRules(QStringLiteral(
        "nereus.*.debug=false\nnereus.*.info=false\nnereussdr.*.info=false\n"
        "nereus.connection.warning=false\n"
        // The test window is a bare StationClient with no settings
        // migration or catalogue of its own; its notes about that are the
        // harness's, not this task's.
        "nereus.stationclient.warning=false"));
}

void TstRemoteTxDisplay::capabilityFollowsTheAnalyzerAndTheMinor()
{
    {
        Harness h;
        h.establishSession();
        QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
        QCOMPARE(h.server.txDisplayVersion(), 3);
        h.finish();
    }
    {
        // A Core without a TX analyzer sends 0, and refuses a start that
        // declares it: no peer starts.
        Harness h(/*withAnalyzer=*/false);
        h.establishSession();
        QTRY_VERIFY(h.client.capabilities().remoteMediaVersion >= 1);
        QCOMPARE(h.client.capabilities().txDisplayVersion, 0);
        QVERIFY(h.start(/*declare=*/true));
        QTest::qWait(100);
        QVERIFY(h.mediaTransport.isNull());
        QVERIFY(!h.controller->txDisplayNegotiated());
        h.finish();
    }
    // A peer below minor 11 is never told: the entry rides the minor-11
    // block only.
    StationCapabilities caps;
    caps.txDisplayVersion = 1;
    caps.radioIdentityEntries = false;
    for (const MirrorUpdate& update : caps.toUpdates()) {
        QVERIFY(update.name != "txDisplayVersion");
    }
    caps.radioIdentityEntries = true;
    bool found = false;
    QByteArray previous;
    for (const MirrorUpdate& update : caps.toUpdates()) {
        if (update.name == "txDisplayVersion") {
            found = true;
            // Appended after the last entry of the minor-11 block.
            QCOMPARE(previous, QByteArray("stationRadiosVersion"));
            QCOMPARE(update.value.toLongLong(), 1);
        }
        previous = update.name;
    }
    QVERIFY(found);
    QCOMPARE(StationCapabilities::fromUpdates(caps.toUpdates()).txDisplayVersion, 1);
}

void TstRemoteTxDisplay::contextCodecCarriesTransmitOnlyWhenNegotiated()
{
    SpectrumContextMessage message;
    message.connectionId = QLatin1String(kConnectionId);
    message.endpointId = 1;
    message.revision = 1;
    message.contextGeneration = 4;
    message.sourceCentreHz = 14'200'000.0;
    message.sampleRateHz = 96000.0;
    message.centreHz = 14'200'000.0;
    message.spanHz = 8000.0;
    message.traceSamples = 128;
    message.waterfallSamples = 128;
    message.minDbm = kTxMinDbm;
    message.maxDbm = kTxMaxDbm;
    message.fps = 15;
    message.framesPerLine = 1;
    SpectrumContextGrant grant;
    grant.grantedFftSize = 32768;
    grant.requestedPixels = 128;
    grant.grantedPixels = 128;
    message.grant = grant;

    // Without `transmit`: exactly today's 24 keys.
    const QJsonObject today = encodeRemoteSpectrumContext(message, true);
    QCOMPARE(today.size(), 24);
    QVERIFY(!today.contains(QStringLiteral("transmit")));
    QVERIFY(decodeRemoteSpectrumContext(today, true).has_value());
    QVERIFY(!decodeRemoteSpectrumContext(today, true, /*transmitNegotiated=*/true));

    message.transmit = true;
    const QJsonObject transmit = encodeRemoteSpectrumContext(message, true);
    QCOMPARE(transmit.size(), 25);
    QCOMPARE(transmit.value(QStringLiteral("transmit")).toBool(), true);
    const auto decoded = decodeRemoteSpectrumContext(transmit, true, true);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->transmit, std::optional<bool>(true));
    // A peer that did not declare refuses the extra key.
    QVERIFY(!decodeRemoteSpectrumContext(transmit, true).has_value());
    // Only a boolean.
    QJsonObject wrong = transmit;
    wrong.insert(QStringLiteral("transmit"), 1);
    QVERIFY(!decodeRemoteSpectrumContext(wrong, true, true).has_value());
    // Only on the grant shape.
    QVERIFY(!decodeRemoteSpectrumContext(transmit, false, true).has_value());
}

void TstRemoteTxDisplay::riseSendsTheTransmitDisplayAndFallResumesReceive()
{
    Harness h;
    QVERIFY(h.slice() && h.spare());
    QCOMPARE(h.radio.txBoundSlice(), h.slice());
    h.slice()->setPanKey(QStringLiteral("pan-a"));
    h.spare()->setPanKey(QStringLiteral("pan-b"));
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(/*declare=*/true);
    QVERIFY(h.controller->txDisplayNegotiated());

    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    const double spareCentre = h.radio.streamCentreHz(h.spare()->streamIndex());
    QVERIFY(h.send(withTxWindow(subscription(1, 1, h.sliceId, centre))));
    QVERIFY(h.send(withTxWindow(subscription(2, 1, h.spareSliceId, spareCentre))));
    // Receive first: a receive context (transmit false) and receive frames.
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        h.feedSlice(h.spareSliceId);
        return lastContext(controls, 1) && lastContext(controls, 2)
            && framesFor(h.mediaTransport->displays, 1, std::nullopt) > 0
            && framesFor(h.mediaTransport->displays, 2, std::nullopt) > 0;
    }());
    const QJsonObject receiveBefore = *lastContext(controls, 1);
    QCOMPARE(receiveBefore.value(QStringLiteral("transmit")).toBool(true), false);
    QCOMPARE(receiveBefore.size(), 25);
    const int endpoint2Contexts = messagesFor(controls, QStringLiteral("context"), 2).size();

    // Rise.
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)));
    QVERIFY(h.controller->transmitDisplayActive(1));
    QVERIFY(!h.controller->transmitDisplayActive(2));
    const QJsonObject transmit = *lastContext(controls, 1);
    const double carrier = static_cast<double>(h.radio.txFrequencyForSlice(h.slice()));
    const TxDisplayView expectedView = TxAnalyzer::clampViewToBaseband(carrier, carrier,
                                                                       48000.0, 128);
    QCOMPARE(transmit.value(QStringLiteral("sourceCentreHz")).toDouble(), carrier);
    QCOMPARE(transmit.value(QStringLiteral("sampleRateHz")).toDouble(),
             double(WdspEngine::kTxDspSampleRate));
    QCOMPARE(transmit.value(QStringLiteral("centreHz")).toDouble(), expectedView.centreHz());
    QCOMPARE(transmit.value(QStringLiteral("spanHz")).toDouble(), expectedView.spanHz());
    QCOMPARE(transmit.value(QStringLiteral("traceSamples")).toInt(), 128);
    QCOMPARE(transmit.value(QStringLiteral("waterfallSamples")).toInt(), 128);
    QCOMPARE(transmit.value(QStringLiteral("wideSamples")).toInt(), 0);
    QCOMPARE(transmit.value(QStringLiteral("minDbm")).toDouble(), kTxMinDbm);
    QCOMPARE(transmit.value(QStringLiteral("maxDbm")).toDouble(), kTxMaxDbm);
    QCOMPARE(transmit.value(QStringLiteral("fps")).toInt(), h.analyzer->outputFps());
    QCOMPARE(transmit.value(QStringLiteral("grantedFftSize")).toInt(), h.analyzer->fftSize());
    QCOMPARE(transmit.value(QStringLiteral("limit")).toString(), QStringLiteral("none"));
    QVERIFY(decodeRemoteSpectrumContext(transmit, true, true).has_value());
    const quint32 txGeneration =
        static_cast<quint32>(transmit.value(QStringLiteral("contextGeneration")).toInteger());
    // The analyzer follows the governing endpoint's view.
    QCOMPARE(h.analyzer->spectrumWindowLowHz(), expectedView.lowHz);
    QCOMPARE(h.analyzer->spectrumWindowHighHz(), expectedView.highHz);
    QCOMPARE(h.analyzer->numPixels(), 128);

    // Frames decode to the pixels the analyzer emitted.
    const int receiveFramesAtRise = framesFor(h.mediaTransport->displays, 1, txGeneration,
                                              /*otherGenerations=*/true);
    const QVector<float> trace = ramp(128, -70.0f, 0.5f);
    const QVector<float> waterfall = ramp(128, 10.0f, -0.6f);
    QTRY_VERIFY([&] {
        h.emitPlanes(trace, waterfall);
        h.feedSlice(h.sliceId);
        h.feedSlice(h.spareSliceId);
        return framesFor(h.mediaTransport->displays, 1, txGeneration) >= 3;
    }());
    DisplayCodecDecoder decoder;
    bool decodedOne = false;
    const float step = float((kTxMaxDbm - kTxMinDbm) / 255.0);
    for (const QByteArray& packet : h.mediaTransport->displays) {
        if (!packet.startsWith("NSDC") || packetEndpoint(packet) != 1
            || packetGeneration(packet) != txGeneration) {
            continue;
        }
        const DisplayCodecDecodeResult result = decoder.decode(packet);
        QCOMPARE(result.disposition, DisplayCodecDisposition::Accepted);
        QCOMPARE(result.frame.traceDbm.size(), 128);
        QCOMPARE(result.frame.waterfallDbm.size(), 128);
        for (int i = 0; i < 128; ++i) {
            QVERIFY(std::abs(result.frame.traceDbm.at(i) - trace.at(i)) <= step);
            QVERIFY(std::abs(result.frame.waterfallDbm.at(i) - waterfall.at(i)) <= step);
        }
        decodedOne = true;
    }
    QVERIFY(decodedOne);
    // No receive frame reached the transmitting pan's endpoint while keyed.
    QCOMPARE(framesFor(h.mediaTransport->displays, 1, txGeneration, true), receiveFramesAtRise);
    // The other pan kept its receive frames and got no new context.
    const int spareFramesAtRise = framesFor(h.mediaTransport->displays, 2, std::nullopt);
    QTRY_VERIFY([&] {
        h.feedSlice(h.spareSliceId);
        return framesFor(h.mediaTransport->displays, 2, std::nullopt) > spareFramesAtRise;
    }());
    QCOMPARE(messagesFor(controls, QStringLiteral("context"), 2).size(), endpoint2Contexts);

    // Fall: a context with transmit false and the receive view it had
    // before the rise, then receive frames again.
    const int receiveFramesAtFall = framesFor(h.mediaTransport->displays, 1, txGeneration, true);
    QVERIFY(h.key(false));
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        const auto context = lastContext(controls, 1);
        return context && !isTransmit(context)
            && context->value(QStringLiteral("contextGeneration")).toInteger() > txGeneration;
    }());
    QVERIFY(!h.controller->transmitDisplayActive(1));
    const QJsonObject receiveAfter = *lastContext(controls, 1);
    for (const char* key : {"centreHz", "spanHz", "minDbm", "maxDbm", "sourceCentreHz",
                            "sampleRateHz", "traceSamples"}) {
        QCOMPARE(receiveAfter.value(QLatin1String(key)), receiveBefore.value(QLatin1String(key)));
    }
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return framesFor(h.mediaTransport->displays, 1, txGeneration, true)
            > receiveFramesAtFall;
    }());
    // The analyzer stopped and its clip cleared.
    QVERIFY(!h.analyzer->isRunning());
    QCOMPARE(h.analyzer->spectrumWindowLowHz(), 0);
    h.finish();
}

void TstRemoteTxDisplay::xitWhileKeyedRenewsTheContext()
{
    Harness h;
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(true);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    QVERIFY(h.send(withTxWindow(subscription(1, 1, h.sliceId, centre, 20000.0))));
    QTRY_VERIFY([&] { h.feedSlice(h.sliceId); return lastContext(controls, 1).has_value(); }());
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)));
    const double carrier = static_cast<double>(h.radio.txFrequencyForSlice(h.slice()));
    QCOMPARE(lastContext(controls, 1)->value(QStringLiteral("centreHz")).toDouble(), carrier);
    const qint64 firstGeneration =
        lastContext(controls, 1)->value(QStringLiteral("contextGeneration")).toInteger();

    // Row 16: XIT on while keyed renews the context at the new carrier.
    h.slice()->setXitHz(700);
    h.slice()->setXitEnabled(true);
    QTRY_VERIFY(lastContext(controls, 1)->value(QStringLiteral("contextGeneration")).toInteger()
                > firstGeneration);
    const QJsonObject renewed = *lastContext(controls, 1);
    QVERIFY(isTransmit(renewed));
    QCOMPARE(renewed.value(QStringLiteral("sourceCentreHz")).toDouble(), carrier + 700.0);
    QCOMPARE(renewed.value(QStringLiteral("centreHz")).toDouble(), carrier + 700.0);
    QCOMPARE(renewed.value(QStringLiteral("spanHz")).toDouble(), 20000.0);
    const quint32 generation =
        static_cast<quint32>(renewed.value(QStringLiteral("contextGeneration")).toInteger());
    // The frames follow it (the analyzer emits the view's pixels).
    const int samples = renewed.value(QStringLiteral("traceSamples")).toInt();
    QCOMPARE(samples, h.feed()->currentView().pixels);
    QTRY_VERIFY([&] {
        h.emitPlanes(ramp(samples, -60.0f, 0.1f), ramp(samples, -60.0f, 0.1f));
        return framesFor(h.mediaTransport->displays, 1, generation) > 0;
    }());

    // The transmit slice retuned while keyed: renewed again.
    const qint64 xitGeneration = generation;
    h.slice()->setFrequency(h.slice()->frequency() + 2000.0);
    QTRY_VERIFY(lastContext(controls, 1)->value(QStringLiteral("contextGeneration")).toInteger()
                > xitGeneration);
    QCOMPARE(lastContext(controls, 1)->value(QStringLiteral("sourceCentreHz")).toDouble(),
             static_cast<double>(h.radio.txFrequencyForSlice(h.slice())));
    QVERIFY(h.key(false));
    QTRY_VERIFY(!isTransmit(lastContext(controls, 1)) || !h.feed()->isKeyed());
    h.finish();
}

void TstRemoteTxDisplay::severalViewersShareTheGoverningView()
{
    Harness h;
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(true);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    QVERIFY(h.send(withTxWindow(subscription(1, 1, h.sliceId, centre, 40000.0))));
    QVERIFY(h.send(withTxWindow(subscription(3, 1, h.sliceId, centre, 12000.0))));
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return lastContext(controls, 1) && lastContext(controls, 3);
    }());
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)) && isTransmit(lastContext(controls, 3)));
    // The lower id governs; the other is told it shares the governing view.
    QTRY_COMPARE(lastContext(controls, 3)->value(QStringLiteral("limit")).toString(),
                 QStringLiteral("shared"));
    QCOMPARE(lastContext(controls, 1)->value(QStringLiteral("limit")).toString(),
             QStringLiteral("none"));
    QCOMPARE(lastContext(controls, 3)->value(QStringLiteral("spanHz")).toDouble(), 40000.0);
    QCOMPARE(lastContext(controls, 3)->value(QStringLiteral("centreHz")).toDouble(),
             lastContext(controls, 1)->value(QStringLiteral("centreHz")).toDouble());

    // The governing one leaves: the other governs, with its own view.
    QVERIFY(h.send(unsubscription(1)));
    QTRY_COMPARE(lastContext(controls, 3)->value(QStringLiteral("limit")).toString(),
                 QStringLiteral("none"));
    QCOMPARE(lastContext(controls, 3)->value(QStringLiteral("spanHz")).toDouble(), 12000.0);
    QVERIFY(isTransmit(lastContext(controls, 3)));
    QCOMPARE(h.analyzer->spectrumWindowHighHz() - h.analyzer->spectrumWindowLowHz(), 12000);

    // A local viewer (a desktop window hosting the Core) governs over it.
    const int local = h.feed()->addViewer(h.feed()->currentView().carrierHz, 30000.0, 900, true);
    QTRY_COMPARE(lastContext(controls, 3)->value(QStringLiteral("limit")).toString(),
                 QStringLiteral("shared"));
    QCOMPARE(lastContext(controls, 3)->value(QStringLiteral("spanHz")).toDouble(), 30000.0);
    h.feed()->removeViewer(local);
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    h.finish();
}

void TstRemoteTxDisplay::aSubscribeWhileKeyedMovesTheView()
{
    Harness h;
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(true);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    QVERIFY(h.send(withTxWindow(subscription(1, 1, h.sliceId, centre, 20000.0))));
    QTRY_VERIFY([&] { h.feedSlice(h.sliceId); return lastContext(controls, 1).has_value(); }());
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)));
    const double carrier = static_cast<double>(h.radio.txFrequencyForSlice(h.slice()));

    // A pan and zoom on the transmitting pan: the governing view moves the
    // analyzer, and the endpoint gets a context for its new revision.
    QVERIFY(h.send(withTxWindow(subscription(1, 2, h.sliceId, carrier + 3000.0, 10000.0),
                                -90.0, 30.0)));
    // A change held for the coalescing period follows with its own context.
    QTRY_VERIFY(lastContext(controls, 1)->value(QStringLiteral("revision")).toInteger() == 2
                && lastContext(controls, 1)->value(QStringLiteral("centreHz")).toDouble()
                    == carrier + 3000.0);
    const QJsonObject moved = *lastContext(controls, 1);
    QVERIFY(isTransmit(moved));
    QCOMPARE(moved.value(QStringLiteral("centreHz")).toDouble(), carrier + 3000.0);
    QCOMPARE(moved.value(QStringLiteral("spanHz")).toDouble(), 10000.0);
    QCOMPARE(moved.value(QStringLiteral("minDbm")).toDouble(), -90.0);
    QCOMPARE(moved.value(QStringLiteral("maxDbm")).toDouble(), 30.0);
    QCOMPARE(h.analyzer->spectrumWindowLowHz(), -2000);
    QCOMPARE(h.analyzer->spectrumWindowHighHz(), 8000);
    // Past the baseband: clamped.
    QVERIFY(h.send(withTxWindow(subscription(1, 3, h.sliceId, carrier + 45000.0, 20000.0))));
    QTRY_VERIFY(lastContext(controls, 1)->value(QStringLiteral("revision")).toInteger() == 3
                && lastContext(controls, 1)->value(QStringLiteral("centreHz")).toDouble()
                    == carrier + 38000.0);
    QCOMPARE(h.analyzer->spectrumWindowHighHz(), 48000);
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    h.finish();
}

void TstRemoteTxDisplay::olderPeerKeepsTodaysWire()
{
    Harness h;
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(/*declare=*/false);
    QVERIFY(!h.controller->txDisplayNegotiated());
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    // The transmit window is not this peer's to send: a subscribe of any
    // other shape, which a session without the display budget ignores
    // (the budget wire answers it as a request the Core cannot read).
    QVERIFY(h.send(withTxWindow(subscription(9, 1, h.sliceId, centre))));
    QTest::qWait(100);
    QCOMPARE(h.controller->activeEndpointCount(), 0);

    QVERIFY(h.send(subscription(1, 1, h.sliceId, centre)));
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return lastContext(controls, 1).has_value()
            && framesFor(h.mediaTransport->displays, 1, std::nullopt) > 0;
    }());
    const QJsonObject before = *lastContext(controls, 1);
    // Today's 24 keys, exactly as encoded without the field.
    QCOMPARE(before.size(), 24);
    QVERIFY(!before.contains(QStringLiteral("transmit")));
    const auto decoded = decodeRemoteSpectrumContext(before, true);
    QVERIFY(decoded.has_value());
    QCOMPARE(encodeRemoteSpectrumContext(*decoded, true), before);
    const int contexts = messagesFor(controls, QStringLiteral("context"), 1).size();

    // Keyed: receive frames keep coming and no transmit context is sent.
    QVERIFY(h.key(true));
    QTRY_VERIFY(h.feed()->isKeyed());
    const int framesAtRise = framesFor(h.mediaTransport->displays, 1, std::nullopt);
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        h.emitPlanes(ramp(128, -60.0f, 0.1f), ramp(128, -60.0f, 0.1f));
        return framesFor(h.mediaTransport->displays, 1, std::nullopt) > framesAtRise + 2;
    }());
    QCOMPARE(messagesFor(controls, QStringLiteral("context"), 1).size(), contexts);
    QVERIFY(!h.controller->transmitDisplayActive(1));
    QCOMPARE(h.feed()->viewerCount(), 0);
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    h.finish();
}

void TstRemoteTxDisplay::transmitWindowIsReadLikeTheReceiveWindow()
{
    Harness h;
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(true);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    const QString unreadable = QStringLiteral("The Core could not read this display request.");
    struct Case {
        quint32 id;
        QJsonObject request;
    };
    QJsonObject text = withTxWindow(subscription(16, 1, h.sliceId, centre));
    text.insert(QStringLiteral("txMaxDbm"), QStringLiteral("20"));
    const QList<Case> cases{
        {11, withTxWindow(subscription(11, 1, h.sliceId, centre), 20.0, 20.0)},
        {12, withTxWindow(subscription(12, 1, h.sliceId, centre), 30.0, 20.0)},
        {13, withTxWindow(subscription(13, 1, h.sliceId, centre), -401.0, 20.0)},
        {14, withTxWindow(subscription(14, 1, h.sliceId, centre), -80.0, 100.5)},
        {16, text},
    };
    for (const Case& refused : cases) {
        QVERIFY(h.send(refused.request));
        QTRY_VERIFY2(!messagesFor(controls, QStringLiteral("rejected"), refused.id).isEmpty(),
                     qPrintable(QString::number(refused.id)));
        QCOMPARE(messagesFor(controls, QStringLiteral("rejected"), refused.id).last()
                     .value(QStringLiteral("reason")).toString(),
                 unreadable);
    }
    QCOMPARE(h.controller->activeEndpointCount(), 0);
    // One edge alone is a subscribe of another shape: ignored without the
    // display budget, as any other shape is.
    QJsonObject onlyMin = subscription(15, 1, h.sliceId, centre);
    onlyMin.insert(QStringLiteral("txMinDbm"), -80.0);
    QVERIFY(h.send(onlyMin));
    QTest::qWait(100);
    QCOMPARE(h.controller->activeEndpointCount(), 0);
    // The edges themselves are accepted.
    QVERIFY(h.send(withTxWindow(subscription(17, 1, h.sliceId, centre), -400.0, 100.0)));
    QTRY_COMPARE(h.controller->activeEndpointCount(), 1);
    // Without the window a declaring peer's transmit display uses the
    // receive window.
    QVERIFY(h.send(subscription(18, 1, h.sliceId, centre)));
    QTRY_COMPARE(h.controller->activeEndpointCount(), 2);
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return lastContext(controls, 17) && lastContext(controls, 18);
    }());
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 17)) && isTransmit(lastContext(controls, 18)));
    QCOMPARE(lastContext(controls, 17)->value(QStringLiteral("minDbm")).toDouble(), -400.0);
    QCOMPARE(lastContext(controls, 18)->value(QStringLiteral("minDbm")).toDouble(), -180.0);
    QCOMPARE(lastContext(controls, 18)->value(QStringLiteral("maxDbm")).toDouble(), 0.0);
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    h.finish();
}

void TstRemoteTxDisplay::budgetPacesTransmitFramesLikeReceiveFrames()
{
    // A budget session whose share gives the pan 5 frames a second: the
    // transmit display goes at 5, not the analyzer's 15, and each frame
    // stays within the endpoint's admitted cost (the controller warns and
    // drops a frame that does not; failOnWarning catches it).
    constexpr int kFps = 5;
    const SpectrumDisplayCost cost = *spectrumDisplayCost(128, kFps, false);
    Harness h(true, DisplayBudgetLimits{cost.charge.applicationBytesPerSecond,
                                        cost.charge.spectrumSampleUnitsPerSecond, 3});
    QTest::failOnWarning(QRegularExpression(QStringLiteral("exceeded admitted display cost")));
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QVERIFY(h.server.displayBudgetAvailable());
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(true);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    QVERIFY(h.send(withTxWindow(subscription(1, 1, h.sliceId, centre, 48000.0, kFps))));
    QTRY_VERIFY(!messagesFor(controls, QStringLiteral("allocation-result"), 1).isEmpty());
    QVERIFY(messagesFor(controls, QStringLiteral("allocation-result"), 1).last()
                .value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY([&] { h.feedSlice(h.sliceId); return lastContext(controls, 1).has_value(); }());
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)));
    QCOMPARE(lastContext(controls, 1)->value(QStringLiteral("fps")).toInt(), kFps);
    const quint32 generation = static_cast<quint32>(
        lastContext(controls, 1)->value(QStringLiteral("contextGeneration")).toInteger());

    // Two seconds of the analyzer at 15 frames a second on the Core's own
    // clock, the sender ticking every 5 ms of it.
    h.realClock = false;
    h.nowNs = h.realTimer.nsecsElapsed();
    const int before = framesFor(h.mediaTransport->displays, 1, generation);
    for (int poll = 0; poll < 30; ++poll) {
        h.emitPlanes(ramp(128, -60.0f, 0.2f), ramp(128, -50.0f, 0.2f));
        for (int tick = 0; tick < 13; ++tick) {
            h.nowNs += 5'128'205; // 1/15 s over 13 ticks
            QCoreApplication::processEvents();
            QTest::qWait(1);
        }
    }
    const int sent = framesFor(h.mediaTransport->displays, 1, generation) - before;
    QVERIFY2(sent >= 8 && sent <= 11, qPrintable(QString::number(sent)));
    for (const QByteArray& packet : h.mediaTransport->displays) {
        if (packet.startsWith("NSDC") && packetEndpoint(packet) == 1) {
            QVERIFY(packet.size() <= qsizetype(cost.maximumFrameBytes));
        }
    }
    h.realClock = true;
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    h.finish();
}

void TstRemoteTxDisplay::aSliceMovingWhileKeyedLeavesTheRisePanTransmitting()
{
    Harness h;
    h.slice()->setPanKey(QStringLiteral("pan-a"));
    h.spare()->setPanKey(QStringLiteral("pan-b"));
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(true);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    const double spareCentre = h.radio.streamCentreHz(h.spare()->streamIndex());
    quint32 revision = 1;
    QVERIFY(h.send(withTxWindow(subscription(1, revision, h.sliceId, centre))));
    QVERIFY(h.send(withTxWindow(subscription(2, revision, h.spareSliceId, spareCentre))));
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        h.feedSlice(h.spareSliceId);
        return lastContext(controls, 1) && lastContext(controls, 2);
    }());
    // Both endpoints ask again, and the Core answers both at the new
    // revision: every reconcile queued before has run by then.
    const auto resubscribeBoth = [&] {
        ++revision;
        QVERIFY(h.send(withTxWindow(subscription(1, revision, h.sliceId, centre))));
        QVERIFY(h.send(withTxWindow(subscription(2, revision, h.spareSliceId, spareCentre))));
        QTRY_VERIFY([&] {
            h.feedSlice(h.sliceId);
            h.feedSlice(h.spareSliceId);
            const auto one = lastContext(controls, 1);
            const auto two = lastContext(controls, 2);
            return one && two
                && one->value(QStringLiteral("revision")).toInteger() == revision
                && two->value(QStringLiteral("revision")).toInteger() == revision;
        }());
    };

    // A layout change while keyed rehomes the slices (the transmit slice to
    // pan-b, the other to pan-a): pan-a, where the window shows the
    // transmit display, keeps it until the fall.
    QVERIFY(h.key(true));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)));
    h.slice()->setPanKey(QStringLiteral("pan-b"));
    h.spare()->setPanKey(QStringLiteral("pan-a"));
    resubscribeBoth();
    QVERIFY(h.controller->transmitDisplayActive(2));
    QVERIFY(!h.controller->transmitDisplayActive(1));
    QVERIFY(isTransmit(lastContext(controls, 2)));
    QVERIFY(!isTransmit(lastContext(controls, 1)));
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.controller->transmitDisplayActive(2));
    QVERIFY(!h.controller->transmitDisplayActive(1));

    // The next key records the transmit slice's pan now (pan-b). A
    // rebinding to the other slice (on pan-a) while keyed leaves it there.
    QVERIFY(h.key(true));
    QTRY_VERIFY(h.controller->transmitDisplayActive(1));
    QVERIFY(!h.controller->transmitDisplayActive(2));
    TxSliceArbiter* arbiter = h.radio.txSliceArbiter();
    QVERIFY(arbiter);
    arbiter->flipTo(h.spare());
    QCOMPARE(h.radio.txBoundSlice(), h.spare());
    QVERIFY(h.feed()->isKeyed());
    resubscribeBoth();
    QVERIFY(h.controller->transmitDisplayActive(1));
    QVERIFY(!h.controller->transmitDisplayActive(2));
    QVERIFY(isTransmit(lastContext(controls, 1)));
    QVERIFY(!isTransmit(lastContext(controls, 2)));
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    QTRY_VERIFY(!h.controller->transmitDisplayActive(1));
    h.finish();
}

void TstRemoteTxDisplay::remoteViewChangesWhileKeyedAreCoalesced()
{
    Harness h;
    TxDisplayFeed* feed = h.feed();
    QVERIFY(feed);
    QVERIFY(h.key(true));
    QTRY_VERIFY(feed->isKeyed());
    const double carrier = feed->currentView().carrierHz;
    const int remote = feed->addViewer(carrier, 20000.0, 400, /*local=*/false);
    QVERIFY(feed->isGoverning(remote));
    QTRY_VERIFY(h.analyzer->setAnalyzerCount() > 0);  // the rise's own

    // A drag from a remote window: twenty moves as fast as they come. The
    // first reaches the analyzer at once (one SetAnalyzer, window and
    // pixel count together); the rest are held.
    QElapsedTimer drag;
    drag.start();
    const int before = h.analyzer->setAnalyzerCount();
    for (int step = 1; step <= 20; ++step) {
        feed->updateViewer(remote, carrier + 1000.0 * step, 20000.0, 400 + step);
    }
    const qint64 dragMs = drag.elapsed();
    QVERIFY2(dragMs < TxDisplayFeed::kRemoteViewCoalesceMs,
             qPrintable(QStringLiteral("drag took %1 ms").arg(dragMs)));
    QVERIFY(h.analyzer->setAnalyzerCount() - before <= 1);
    // The last move always follows, once, when the period ends.
    QTRY_COMPARE(h.analyzer->spectrumWindowLowHz(), 20000 - 10000);
    QCOMPARE(h.analyzer->spectrumWindowHighHz(), 20000 + 10000);
    QCOMPARE(h.analyzer->numPixels(), 420);
    QCOMPARE(feed->currentView().centreHz(), carrier + 20000.0);
    QVERIFY(h.analyzer->setAnalyzerCount() - before <= 2);

    // A local viewer's changes are never held.
    const int local = feed->addViewer(carrier, 10000.0, 300, /*local=*/true);
    const int localBefore = h.analyzer->setAnalyzerCount();
    for (int step = 1; step <= 5; ++step) {
        feed->updateViewer(local, carrier + 1000.0 * step, 10000.0, 300);
        QCOMPARE(feed->currentView().centreHz(), carrier + 1000.0 * step);
    }
    QCOMPARE(h.analyzer->setAnalyzerCount() - localBefore, 5);
    feed->removeViewer(local);
    feed->removeViewer(remote);
    QVERIFY(h.key(false));
    QTRY_VERIFY(!feed->isKeyed());
}

void TstRemoteTxDisplay::txStateCarriesTheHighSwrState()
{
    // The Core side: TransmitState reads RadioModel's SWR protection, the
    // values the local window's setHighSwrOverlay is handed.
    RadioModel radio;
    TransmitState state;
    state.bind(&radio);
    QSignalSpy changed(&state, &TransmitState::swrChanged);
    QVERIFY(!state.highSwr());
    QVERIFY(!state.swrWindBackLatched());
    // A reflected power high enough to trip the protection, fold-back on.
    radio.swrProt().setEnabled(true);
    radio.swrProt().setWindBackEnabled(true);
    for (int sample = 0; sample < 50 && !radio.swrProt().highSwr(); ++sample) {
        radio.swrProt().ingest(50.0f, 30.0f, /*tuneActive=*/false);
    }
    QVERIFY(radio.swrProt().highSwr());
    QCOMPARE(state.highSwr(), true);
    QCOMPARE(state.swrWindBackLatched(), radio.swrProt().windBackLatched());
    QVERIFY(!changed.isEmpty());
    // Protection off clears it.
    radio.swrProt().setEnabled(false);
    radio.swrProt().ingest(50.0f, 30.0f, false);
    QVERIFY(!state.highSwr());
    QVERIFY(!state.swrWindBackLatched());
    // The window side: a plain apply of the Core's values.
    TransmitState window;
    QSignalSpy windowChanged(&window, &TransmitState::swrChanged);
    QVERIFY(window.applyStationValue("highSwr", true));
    QVERIFY(window.applyStationValue("swrWindBackLatched", true));
    QVERIFY(window.highSwr());
    QVERIFY(window.swrWindBackLatched());
    QCOMPARE(windowChanged.count(), 2);
    QVERIFY(window.applyStationValue("highSwr", true));
    QCOMPARE(windowChanged.count(), 2);
    window.clearStationValues();
    QVERIFY(!window.highSwr());
    QVERIFY(!window.swrWindBackLatched());
    QCOMPARE(windowChanged.count(), 3);
    // Both are properties of the mirrored class, appended.
    const QMetaObject* meta = &TransmitState::staticMetaObject;
    const int highSwr = meta->indexOfProperty("highSwr");
    const int latched = meta->indexOfProperty("swrWindBackLatched");
    QVERIFY(highSwr > meta->indexOfProperty("stopEpoch"));
    QCOMPARE(latched, highSwr + 1);
    state.unbind();
}

// Parity Task 31 (A11, R-R3-49): txDisplayVersion 3. An endpoint whose
// subscribe carries `duplex` true is no viewer while keyed: it keeps its
// receive frames and its contexts say `transmit` false (Thetis DisplayThread,
// console.cs:24281-24338 [v2.10.3.15], `if (bLocalMox && !_display_duplex)`).
// A subscribe while keyed swaps it at once, both ways.
void TstRemoteTxDisplay::aDuplexEndpointKeepsTheReceiverWhileKeyed()
{
    Harness h;
    h.slice()->setPanKey(QStringLiteral("pan-a"));
    h.establishSession();
    QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
    QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
    h.startReadyPeer(/*declare=*/true, /*version=*/3);
    const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
    QJsonObject duplex = withTxWindow(subscription(1, 1, h.sliceId, centre));
    duplex.insert(QStringLiteral("duplex"), true);
    QVERIFY(h.send(duplex));
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return lastContext(controls, 1).has_value();
    }());
    QVERIFY(h.controller->endpointDuplex(1));

    QVERIFY(h.key(true));
    QTRY_VERIFY(h.feed()->isKeyed());
    QTest::qWait(100);
    QVERIFY(!h.controller->transmitDisplayActive(1));
    QVERIFY(!isTransmit(lastContext(controls, 1)));
    QCOMPARE(h.feed()->viewerCount(), 0);
    // Receive frames keep coming at the receive context's generation.
    const quint32 receiveGeneration = static_cast<quint32>(
        lastContext(controls, 1)->value(QStringLiteral("contextGeneration")).toInteger());
    const int before = framesFor(h.mediaTransport->displays, 1, receiveGeneration);
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return framesFor(h.mediaTransport->displays, 1, receiveGeneration) > before + 2;
    }());

    // DUP off while keyed: the transmit display at once.
    QVERIFY(h.send(withTxWindow(subscription(1, 2, h.sliceId, centre))));
    QTRY_VERIFY(isTransmit(lastContext(controls, 1)));
    QVERIFY(h.controller->transmitDisplayActive(1));
    QVERIFY(!h.controller->endpointDuplex(1));
    // And on again: back to the receiver.
    duplex.insert(QStringLiteral("revision"), 3);
    QVERIFY(h.send(duplex));
    QTRY_VERIFY(!h.controller->transmitDisplayActive(1));
    QTRY_VERIFY([&] {
        h.feedSlice(h.sliceId);
        return lastContext(controls, 1)
            && !isTransmit(lastContext(controls, 1));
    }());
    QCOMPARE(h.feed()->viewerCount(), 0);
    QVERIFY(h.key(false));
    QTRY_VERIFY(!h.feed()->isKeyed());
    h.finish();
}

// `duplex` is read only from a peer whose start declared 3, and only as a
// boolean: otherwise it is a subscribe of another shape, ignored without the
// display budget as any other shape is (the transmit window's one-edge case).
// `false` is the same as absent.
void TstRemoteTxDisplay::duplexIsReadOnlyFromAVersionThreePeer()
{
    {
        Harness h;
        h.establishSession();
        QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
        h.startReadyPeer(/*declare=*/true, /*version=*/1);
        const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
        QJsonObject duplex = withTxWindow(subscription(1, 1, h.sliceId, centre));
        duplex.insert(QStringLiteral("duplex"), true);
        QVERIFY(h.send(duplex));
        QTest::qWait(150);
        QCOMPARE(h.controller->activeEndpointCount(), 0);
        // The same request without it is taken.
        QVERIFY(h.send(withTxWindow(subscription(2, 1, h.sliceId, centre))));
        QTRY_COMPARE(h.controller->activeEndpointCount(), 1);
        h.finish();
    }
    {
        Harness h;
        h.establishSession();
        QTRY_COMPARE(h.client.capabilities().txDisplayVersion, 3);
        h.startReadyPeer(/*declare=*/true, /*version=*/3);
        const double centre = h.radio.streamCentreHz(h.slice()->streamIndex());
        QJsonObject wrong = withTxWindow(subscription(2, 1, h.sliceId, centre));
        wrong.insert(QStringLiteral("duplex"), 1);
        QVERIFY(h.send(wrong));
        QTest::qWait(150);
        QCOMPARE(h.controller->activeEndpointCount(), 0);
        QJsonObject off = withTxWindow(subscription(3, 1, h.sliceId, centre));
        off.insert(QStringLiteral("duplex"), false);
        QVERIFY(h.send(off));
        QTRY_COMPARE(h.controller->activeEndpointCount(), 1);
        QVERIFY(!h.controller->endpointDuplex(3));
        QJsonObject on = withTxWindow(subscription(4, 1, h.sliceId, centre));
        on.insert(QStringLiteral("duplex"), true);
        QVERIFY(h.send(on));
        QTRY_COMPARE(h.controller->activeEndpointCount(), 2);
        QVERIFY(h.controller->endpointDuplex(4));
        h.finish();
    }
}

QTEST_MAIN(TstRemoteTxDisplay)
#include "tst_remote_tx_display.moc"
