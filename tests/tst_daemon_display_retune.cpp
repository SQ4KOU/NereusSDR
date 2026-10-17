// =================================================================
// tests/tst_daemon_display_retune.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test. R-DISP-RETUNE.
//
// A remote pan's picture carries on through a retune. When the receiver's
// stream center moves under a pan that still sees it, or the pan is asked
// for again at another center, the Core sends a new context that opens with
// a keyframe, and the averaged trace goes on from where it was. The peak
// hold and the blobs empty; the noise floor and the waterfall levels
// continue. A pan the retune left outside the receiver is retired, as
// before. While the operator tunes, frames keep coming.
//
// The Core is the real DaemonMediaController behind a real station session,
// on a clock the test holds. Every source frame is published by the test at
// a time of its choosing and every sender tick is run by the test, so
// nothing here waits for a frame and nothing is proved by a wait.
//
// Modification history (NereusSDR):
//   2026-10-11  J.J. Boyd / KG4VCF  Third wave: the Core states the picture
//                                    each context continues, only to a
//                                    client that declared the value.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>

#include <algorithm>

#include <cmath>
#include <functional>
#include <optional>

#include "core/AppSettings.h"
#include "core/FFTEngine.h"
#include "core/HpsdrModel.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DaemonSpectrumSource.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DisplayExtras.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr char kConnectionId[] = "11111111-2222-4333-8444-555555555555";
constexpr quint32 kEndpoint = 7;
constexpr quint32 kNarrowEndpoint = 8;
constexpr int kFftSize = 1024;
constexpr int kFps = 30;
constexpr int kPixels = 128;
constexpr double kSpanHz = 48000.0;
constexpr qint64 kStartNs = 10'000'000'000;
constexpr qint64 kPeriodNs = 1'000'000'000 / kFps;
// The receiver runs at 192 kHz, so one bin of the 1024 is 187.5 Hz.
constexpr double kBinHz = 192000.0 / kFftSize;
// Where the test's carrier sits in the source before any move.
constexpr int kCarrierBin = 500;

class FakeTransport final : public IMediaTransport {
public:
    explicit FakeTransport(QObject* parent = nullptr) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { return true; }
    void stop() override { readyState = false; }
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
    void becomeReady() { readyState = true; emit ready(); }

    bool readyState{false};
    QList<QByteArray> displays;
};

bool isKeyframe(const QByteArray& frame)
{
    return frame.size() > 5 && (static_cast<quint8>(frame.at(5)) & 0x01) != 0;
}

// Both planes averaged in the log domain (the desktop's Log Recursive), so
// a restarted plane is far from a settled one for many frames.
QJsonObject plane()
{
    return {{QStringLiteral("detector"), 0},
            {QStringLiteral("averageMode"), 3},
            {QStringLiteral("averageAlpha"), 0.0}};
}

QJsonObject subscription(quint32 endpointId, int sliceId, double centerHz, int revision,
                         double spanHz)
{
    return {{QStringLiteral("op"), QStringLiteral("subscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
            {QStringLiteral("revision"), revision},
            {QStringLiteral("sliceId"), sliceId},
            {QStringLiteral("tier"), QStringLiteral("wide")},
            {QStringLiteral("fftSize"), kFftSize},
            {QStringLiteral("windowType"), static_cast<int>(WindowFunction::Hann)},
            {QStringLiteral("centreHz"), centerHz},
            {QStringLiteral("spanHz"), spanHz},
            {QStringLiteral("pixels"), kPixels},
            {QStringLiteral("fps"), kFps},
            {QStringLiteral("framesPerLine"), 1},
            {QStringLiteral("trace"), plane()},
            {QStringLiteral("waterfall"), plane()},
            {QStringLiteral("minDbm"), -180.0},
            {QStringLiteral("maxDbm"), 0.0},
            {QStringLiteral("wideSpanFactor"), 0.0},
            // Every display extra, the averaging timed over a second.
            {QStringLiteral("peakBlobs"),
             QJsonObject{{QStringLiteral("count"), 3}, {QStringLiteral("holdMs"), 500},
                         {QStringLiteral("fallDbPerSec"), 6.0},
                         {QStringLiteral("insideOnly"), false}}},
            {QStringLiteral("activePeakHold"),
             QJsonObject{{QStringLiteral("enabled"), true}, {QStringLiteral("holdMs"), 2000},
                         {QStringLiteral("fallDbPerSec"), 6.0}}},
            {QStringLiteral("noiseFloor"),
             QJsonObject{{QStringLiteral("enabled"), true}, {QStringLiteral("shiftDb"), 0.0},
                         {QStringLiteral("fastAttack"), false}}},
            {QStringLiteral("waterfallLevels"),
             QJsonObject{{QStringLiteral("mode"), QStringLiteral("agc")},
                         {QStringLiteral("lowDbm"), -122.0},
                         {QStringLiteral("highDbm"), -62.0},
                         {QStringLiteral("offsetDb"), 0}}},
            {QStringLiteral("normalize"), false},
            {QStringLiteral("calibrationOffsetDb"), 0.0},
            {QStringLiteral("averageTimeMs"), 1000}};
}

// The receiver's spectrum: a flat floor and one carrier, `carrierBin` being
// where the carrier falls in the source. A carrier that stays at one radio
// frequency moves down the source by as many bins as the center moves up.
QVector<float> spectrum(int carrierBin)
{
    QVector<float> bins(kFftSize, 1.0e-9f);
    if (carrierBin >= 0 && carrierBin < kFftSize) {
        bins[carrierBin] = 1.0e-5f;
    }
    return bins;
}

double mean(const QVector<float>& values)
{
    double sum = 0.0;
    for (float value : values) { sum += value; }
    return values.isEmpty() ? 0.0 : sum / values.size();
}

double meanDifference(const QVector<float>& left, const QVector<float>& right)
{
    if (left.size() != right.size() || left.isEmpty()) { return 1.0e9; }
    double sum = 0.0;
    for (int i = 0; i < left.size(); ++i) { sum += std::abs(left.at(i) - right.at(i)); }
    return sum / left.size();
}

int peakIndex(const QVector<float>& values)
{
    int index = -1;
    for (int i = 0; i < values.size(); ++i) {
        if (index < 0 || values.at(i) > values.at(index)) { index = i; }
    }
    return index;
}

// What the Core sent for one context of an endpoint, as a client decodes it.
struct Seen {
    int frames = 0;
    bool firstIsKeyframe = false;
    QVector<float> firstTrace;
    QVector<float> lastTrace;
    std::optional<DisplayExtrasFrame> firstExtras;
    std::optional<DisplayExtrasFrame> lastExtras;
};

// One endpoint's decoder and everything decoded for it.
struct Watched {
    DisplayCodecDecoder decoder;
    std::optional<DisplayCodecContext> decoding;
    QMap<quint32, Seen> seen;
    QList<quint32> generations;
    int frames = 0;
};

struct Harness {
    QTemporaryDir directory;
    AppSettings settings;
    RadioModel radio;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy settingsProxy;
    StationClient client{&remote, &settingsProxy};
    QPointer<FakeTransport> mediaTransport;
    qint64 nowNs{0};
    QPointer<QTimer> sender;
    DaemonMediaController controller;
    int sliceId{-1};
    int streamIndex{-1};
    DaemonSpectrumSource* source{nullptr};
    MediaSourceKey key;
    QMap<quint32, Watched> watched;
    int cursor = 0;
    // The calendar: source frame k at kStartNs + k periods, sender tick j
    // at kStartNs + j x 5 ms. At the same instant the frame comes first.
    qint64 frame = 0;
    qint64 tick = 1;
    int published = 0;
    int refused = 0;

    Harness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()))
        , controller(&server, &radio, nullptr,
                     [this](QObject* parent) -> IMediaTransport* {
                         mediaTransport = new FakeTransport(parent);
                         return mediaTransport;
                     },
                     [this] { return nowNs; })
    {
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        sliceId = radio.addSlice();
        SliceModel* const slice = radio.sliceById(sliceId);
        streamIndex = slice ? slice->streamIndex() : -1;
        server.setMediaEnabled(true);
        nowNs = kStartNs;
    }

    // `declarePicture`: the media start declares spectrumPictureVersion, as
    // a client that takes the Core's picture value does (third wave).
    bool start(bool declarePicture = false)
    {
        if (!connectSession()) { return false; }
        QJsonObject startControl{{QStringLiteral("op"), QStringLiteral("start")},
                                 {QStringLiteral("connectionId"),
                                  QLatin1String(kConnectionId)}};
        if (declarePicture) {
            startControl.insert(QStringLiteral("spectrumPictureVersion"), 1);
        }
        if (!client.sendMediaControl(startControl, client.sessionEpoch())) {
            return false;
        }
        if (!QTest::qWaitFor([this] { return !mediaTransport.isNull(); }, 10'000)) {
            return false;
        }
        mediaTransport->becomeReady();
        return true;
    }

    // The station session, up to media being available; no media start.
    bool connectSession()
    {
        // The sender timer is run by hand: production keeps starting it, so
        // a long interval leaves its timeout slot to the test alone.
        for (QTimer* timer : controller.findChildren<QTimer*>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (timer->interval() == static_cast<int>(kDisplaySenderIntervalMs)) {
                sender = timer;
                timer->setInterval(60'000);
            }
        }
        if (!sender) { return false; }
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        if (!QTest::qWaitFor([this] {
                return server.mediaAvailable() && client.mediaAvailable();
            }, 10'000)) {
            return false;
        }
        return true;
    }

    bool ask(quint32 endpointId, double centerHz, int revision, double spanHz = kSpanHz)
    {
        return client.sendMediaControl(
            subscription(endpointId, sliceId, centerHz, revision, spanHz),
            client.sessionEpoch());
    }

    bool askFor(const QJsonObject& request)
    {
        return client.sendMediaControl(request, client.sessionEpoch());
    }

    // The source the Core made for the pans, once its engine has taken its
    // configuration (the one thing here that happens on another thread).
    bool findSource()
    {
        source = &controller.sharedSpectrum()->source();
        const QList<MediaSourceKey> keys = source->activeSources();
        if (keys.size() != 1) { return false; }
        key = keys.constFirst();
        return QTest::qWaitFor([this] { return source->isActive(key); }, 10'000);
    }

    void sendTick()
    {
        sender->stop();
        QMetaObject::invokeMethod(sender, "timeout", Qt::DirectConnection);
    }

    // Runs the calendar up to, not including, `endNs`. `bins` is the
    // source's spectrum for each frame; `beforeFrame`, when given, runs at
    // each frame's instant before that frame is published.
    void run(qint64 endNs, const std::function<QVector<float>()>& bins,
             const std::function<void()>& beforeFrame = {})
    {
        for (;;) {
            const qint64 frameNs = kStartNs + frame * kPeriodNs;
            const qint64 tickNs = kStartNs + tick * kDisplaySenderIntervalNs;
            const qint64 next = std::min(frameNs, tickNs);
            if (next >= endNs) { break; }
            nowNs = next;
            if (frameNs == next) {
                if (beforeFrame) { beforeFrame(); }
                if (source->publishFrameForTest(key, frameNs, bins())) {
                    ++published;
                } else {
                    ++refused;
                }
                ++frame;
            } else {
                sendTick();
                ++tick;
            }
        }
        decodeSent();
    }

    qint64 nextFrameNs() const { return kStartNs + frame * kPeriodNs; }

    // Runs the calendar a frame period at a time, letting the station
    // session deliver what the client sent, until the endpoint has been
    // sent frames of `contexts` contexts, then two periods more. Bounded in
    // calendar time, not in waiting: ten seconds of frames.
    bool runUntilContexts(quint32 endpointId, int contexts,
                          const std::function<QVector<float>()>& bins)
    {
        constexpr int kMaxPeriods = 10 * kFps;
        for (int period = 0; period < kMaxPeriods; ++period) {
            if (watched[endpointId].generations.size() >= contexts) {
                run(nextFrameNs() + 2 * kPeriodNs, bins);
                return true;
            }
            QCoreApplication::processEvents();
            run(nextFrameNs() + kPeriodNs, bins);
        }
        return false;
    }

    // Decodes what the Core has sent since the last call.
    void decodeSent()
    {
        const QList<QByteArray>& displays = mediaTransport->displays;
        for (; cursor < displays.size(); ++cursor) {
            const QByteArray& bytes = displays.at(cursor);
            if (bytes.size() < 12) { continue; }
            const quint32 endpointId = qFromBigEndian<quint32>(bytes.constData() + 8);
            Watched& endpoint = watched[endpointId];
            if (bytes.startsWith("NSDX")) {
                if (!endpoint.decoding) { continue; }
                const DisplayExtrasDecodeResult extras =
                    decodeDisplayExtras(bytes, *endpoint.decoding);
                if (!extras.accepted) { continue; }
                Seen& context = endpoint.seen[endpoint.decoding->contextGeneration];
                if (!context.firstExtras) { context.firstExtras = extras.frame; }
                context.lastExtras = extras.frame;
                continue;
            }
            DisplayCodecDecodeResult frameResult = endpoint.decoder.decode(bytes);
            if (frameResult.disposition != DisplayCodecDisposition::Accepted) {
                // The keyframe of a new context starts a new decoder, as a
                // client's does when the context arrives.
                DisplayCodecDecoder fresh;
                frameResult = fresh.decode(bytes);
                if (frameResult.disposition != DisplayCodecDisposition::Accepted) { continue; }
                endpoint.decoder = fresh;
            }
            endpoint.decoding = frameResult.frame.context;
            const quint32 generation = frameResult.frame.context.contextGeneration;
            if (!endpoint.seen.contains(generation)) { endpoint.generations.append(generation); }
            Seen& context = endpoint.seen[generation];
            if (context.frames == 0) {
                context.firstTrace = frameResult.frame.traceDbm;
                context.firstIsKeyframe = isKeyframe(bytes);
            }
            context.lastTrace = frameResult.frame.traceDbm;
            ++context.frames;
            ++endpoint.frames;
        }
    }

    void finish() { client.disconnectFromStation(QStringLiteral("test complete")); }
};

QJsonObject lastMessage(const QSignalSpy& messages, const QString& op, quint32 endpointId)
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

// Every message of one kind the client was sent for an endpoint, in order.
QList<QJsonObject> allMessages(const QSignalSpy& messages, const QString& op,
                               quint32 endpointId)
{
    QList<QJsonObject> found;
    for (const QList<QVariant>& entry : messages) {
        const QJsonObject message = entry.at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == op
            && static_cast<quint32>(message.value(QStringLiteral("endpointId")).toInteger())
                == endpointId) {
            found.append(message);
        }
    }
    return found;
}

// What a settled picture looked like just before something moved.
struct Settled {
    quint32 generation = 0;
    QVector<float> trace;
    QVector<float> firstTrace;
    QVector<float> hold;
    float noiseFloorDbm = 0.0f;
    std::pair<float, float> levels;
    const DisplayExtrasProcessor* extras = nullptr;
};

} // namespace

class TstDaemonDisplayRetune : public QObject {
    Q_OBJECT
private:
    // Five seconds of frames at a one second averaging time, with the peak
    // hold, the blobs, the noise floor and the levels all being sent.
    static void settle(Harness& h, Settled& settled)
    {
        const double center = h.radio.streamCentreHz(h.streamIndex);
        QVERIFY(h.ask(kEndpoint, center, 1));
        QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
        QVERIFY(h.findSource());
        h.run(kStartNs + 5 * kFps * kPeriodNs, [] { return spectrum(kCarrierBin); });
        QCOMPARE(h.refused, 0);
        const Watched& pan = h.watched[kEndpoint];
        QCOMPARE(pan.generations.size(), 1);
        const Seen& first = pan.seen[pan.generations.first()];
        QVERIFY2(first.frames >= 140, qPrintable(QString::number(first.frames)));
        QVERIFY(first.firstIsKeyframe);
        QVERIFY(first.lastExtras.has_value());
        QVERIFY(first.lastExtras->peakHoldDbm.has_value());
        QVERIFY(first.lastExtras->peakBlobs.has_value());
        QVERIFY(!first.lastExtras->peakBlobs->isEmpty());
        QVERIFY(first.lastExtras->noiseFloorDbm.has_value());
        QVERIFY(first.lastExtras->waterfallLevelsDbm.has_value());
        settled.generation = pan.generations.first();
        settled.trace = first.lastTrace;
        settled.firstTrace = first.firstTrace;
        settled.hold = *first.lastExtras->peakHoldDbm;
        settled.noiseFloorDbm = *first.lastExtras->noiseFloorDbm;
        settled.levels = *first.lastExtras->waterfallLevelsDbm;
        settled.extras = h.controller.displayExtrasForTest(kEndpoint);
        QVERIFY(settled.extras);
        QCOMPARE(settled.trace.size(), kPixels);
        // The trace grew from its seed: where it settled is far above where
        // it started, so a restart cannot pass for a picture that carried on.
        const double grown = mean(settled.trace) - mean(settled.firstTrace);
        QVERIFY2(grown > 10.0, qPrintable(QStringLiteral("the trace grew %1 dB").arg(grown)));
    }

    // The first frames of the context that followed `settled`: a keyframe
    // carrying the settled trace, no peak hold and no blobs, the noise
    // floor and the waterfall levels where they were, the same processor.
    static void carriedOn(Harness& h, const Settled& settled, double traceToleranceDb)
    {
        const Watched& pan = h.watched[kEndpoint];
        QCOMPARE(pan.generations.size(), 2);
        QVERIFY(pan.generations.last() != settled.generation);
        const Seen& moved = pan.seen[pan.generations.last()];
        QVERIFY(moved.frames >= 1);
        QVERIFY2(moved.firstIsKeyframe, "the moved picture did not open with a keyframe");
        const double difference = meanDifference(moved.firstTrace, settled.trace);
        QVERIFY2(difference <= traceToleranceDb,
                 qPrintable(QStringLiteral("the keyframe of the moved picture is %1 dB from the "
                                           "settled trace (mean %2 dBm after %3 dBm)")
                                .arg(difference).arg(mean(moved.firstTrace))
                                .arg(mean(settled.trace))));
        QVERIFY(moved.firstExtras.has_value());
        QVERIFY2(!moved.firstExtras->peakHoldDbm.has_value(),
                 "the moved picture kept the peak hold of the old frequencies");
        QVERIFY2(!moved.firstExtras->peakBlobs.has_value()
                     || moved.firstExtras->peakBlobs->isEmpty(),
                 "the moved picture kept the blobs of the old frequencies");
        QVERIFY2(moved.firstExtras->noiseFloorDbm.has_value(),
                 "the noise floor started again on a moved picture");
        QVERIFY2(std::abs(*moved.firstExtras->noiseFloorDbm - settled.noiseFloorDbm) <= 1.0f,
                 qPrintable(QStringLiteral("noise floor %1 dBm after %2 dBm")
                                .arg(*moved.firstExtras->noiseFloorDbm)
                                .arg(settled.noiseFloorDbm)));
        QVERIFY2(moved.firstExtras->waterfallLevelsDbm.has_value(),
                 "the waterfall levels started again on a moved picture");
        QVERIFY(std::abs(moved.firstExtras->waterfallLevelsDbm->first - settled.levels.first)
                <= 1.0f);
        QVERIFY(std::abs(moved.firstExtras->waterfallLevelsDbm->second - settled.levels.second)
                <= 1.0f);
        QCOMPARE(h.controller.displayExtrasForTest(kEndpoint), settled.extras);
    }

    // Third wave: one pan taken through every kind of context the Core
    // sends, by a client that did or did not declare the picture value.
    // The contexts it was sent, in order: a first one, the same picture (a
    // new dBm range), that picture moved by a re-ask and by a retune, a new
    // picture (another span), a new picture (the trace now holds its
    // maximum), that held maximum moved, and its dBm range alone.
    static void everyKindOfContext(bool declare, QList<QJsonObject>& contexts)
    {
        Harness h;
        QVERIFY(h.start(declare));
        QCOMPARE(h.client.capabilities().spectrumPictureVersion, 1);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        const double center = h.radio.streamCentreHz(h.streamIndex);
        const auto bins = [] { return spectrum(kCarrierBin); };
        QVERIFY(h.ask(kEndpoint, center, 1));
        QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
        QVERIFY(h.findSource());
        QVERIFY(h.runUntilContexts(kEndpoint, 1, bins));

        QJsonObject request = subscription(kEndpoint, h.sliceId, center, 2, kSpanHz);
        request.insert(QStringLiteral("minDbm"), -170.0);
        QVERIFY(h.askFor(request));
        QVERIFY(h.runUntilContexts(kEndpoint, 2, bins));

        const double moved = center + 8 * kBinHz;
        request.insert(QStringLiteral("centreHz"), moved);
        request.insert(QStringLiteral("revision"), 3);
        QVERIFY(h.askFor(request));
        QVERIFY(h.runUntilContexts(kEndpoint, 3, bins));

        QVERIFY(h.radio.requestStreamCentre(h.sliceId, center + 256 * kBinHz));
        QVERIFY(h.runUntilContexts(kEndpoint, 4, bins));

        request.insert(QStringLiteral("spanHz"), kSpanHz / 2.0);
        request.insert(QStringLiteral("revision"), 4);
        QVERIFY(h.askFor(request));
        QVERIFY(h.runUntilContexts(kEndpoint, 5, bins));

        request.insert(QStringLiteral("trace"),
                       QJsonObject{{QStringLiteral("detector"), 0},
                                   {QStringLiteral("averageMode"), -1},
                                   {QStringLiteral("averageAlpha"), 0.0}});
        request.insert(QStringLiteral("revision"), 5);
        QVERIFY(h.askFor(request));
        QVERIFY(h.runUntilContexts(kEndpoint, 6, bins));

        request.insert(QStringLiteral("centreHz"), moved + 8 * kBinHz);
        request.insert(QStringLiteral("revision"), 6);
        QVERIFY(h.askFor(request));
        QVERIFY(h.runUntilContexts(kEndpoint, 7, bins));

        request.insert(QStringLiteral("maxDbm"), -10.0);
        request.insert(QStringLiteral("revision"), 7);
        QVERIFY(h.askFor(request));
        QVERIFY(h.runUntilContexts(kEndpoint, 8, bins));

        QCOMPARE(h.refused, 0);
        QVERIFY(lastMessage(controls, QStringLiteral("rejected"), kEndpoint).isEmpty());
        QTRY_COMPARE(allMessages(controls, QStringLiteral("context"), kEndpoint).size(), 8);
        contexts = allMessages(controls, QStringLiteral("context"), kEndpoint);
        h.finish();
    }

private slots:
    void initTestCase()
    {
#ifndef HAVE_FFTW3
        QSKIP("FFTEngine has no FFTW3 backend in this build");
#endif
    }

    // The stream center moves under a pan that still sees the receiver, and
    // away from a narrow pan that then sees none of it.
    void aRetuneUnderAPanCarriesItsPictureOn()
    {
        Harness h;
        QVERIFY(h.start());
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        Settled settled;
        settle(h, settled);
        if (QTest::currentTestFailed()) { return; }

        // A second, narrow pan near the lower edge of the receiver.
        const double center = h.radio.streamCentreHz(h.streamIndex);
        QVERIFY(h.ask(kNarrowEndpoint, center - 90000.0, 1, 1000.0));
        QTRY_COMPARE(h.controller.activeEndpointCount(), 2);
        h.run(h.nextFrameNs() + 10 * kPeriodNs, [] { return spectrum(kCarrierBin); });
        QCOMPARE(h.refused, 0);
        QVERIFY(h.watched[kNarrowEndpoint].frames >= 1);
        // Joining did not disturb the first pan.
        QCOMPARE(h.watched[kEndpoint].generations.size(), 1);
        const QVector<float> before = h.watched[kEndpoint].seen[settled.generation].lastTrace;
        const float floorBefore =
            *h.watched[kEndpoint].seen[settled.generation].lastExtras->noiseFloorDbm;
        Settled atMove = settled;
        atMove.trace = before;
        atMove.noiseFloorDbm = floorBefore;
        atMove.levels =
            *h.watched[kEndpoint].seen[settled.generation].lastExtras->waterfallLevelsDbm;
        const quint64 sourceGeneration = h.source->inputQueueDiagnostics(h.key).generation;
        const int narrowFrames = h.watched[kNarrowEndpoint].frames;

        // The receiver is retuned 48 kHz up: 256 bins exactly, so a carrier
        // that stays put on the air stays on its pixel.
        constexpr int kMoveBins = 256;
        QVERIFY(h.radio.requestStreamCentre(h.sliceId, center + kMoveBins * kBinHz));
        QCOMPARE(h.radio.streamCentreHz(h.streamIndex), center + kMoveBins * kBinHz);
        // The source was relabeled, not restarted: it publishes at once.
        QVERIFY2(h.source->isActive(h.key), "the retune restarted the spectrum source");
        QVERIFY(h.source->inputQueueDiagnostics(h.key).generation > sourceGeneration);
        // The wide pan stays; the narrow pan is retired.
        QCOMPARE(h.controller.activeEndpointCount(), 1);
        QVERIFY(h.controller.displayExtrasForTest(kEndpoint) != nullptr);

        h.run(h.nextFrameNs() + 4 * kPeriodNs,
              [] { return spectrum(kCarrierBin - kMoveBins); });
        QCOMPARE(h.refused, 0);
        carriedOn(h, atMove, 1.0);
        if (QTest::currentTestFailed()) { return; }
        // The carrier is where it was on the screen.
        const Watched& pan = h.watched[kEndpoint];
        QCOMPARE(peakIndex(pan.seen[pan.generations.last()].firstTrace), peakIndex(before));
        // The context the Core announced for it is at the new center.
        QTRY_VERIFY(!lastMessage(controls, QStringLiteral("context"), kEndpoint).isEmpty());
        QTRY_COMPARE(static_cast<quint32>(lastMessage(controls, QStringLiteral("context"),
                                                      kEndpoint)
                                              .value(QStringLiteral("contextGeneration"))
                                              .toInteger()),
                     pan.generations.last());
        QCOMPARE(lastMessage(controls, QStringLiteral("context"), kEndpoint)
                     .value(QStringLiteral("sourceCentreHz")).toDouble(),
                 center + kMoveBins * kBinHz);
        QVERIFY(lastMessage(controls, QStringLiteral("rejected"), kEndpoint).isEmpty());

        // The narrow pan was told why it went, and got nothing more.
        QTRY_VERIFY(!lastMessage(controls, QStringLiteral("rejected"), kNarrowEndpoint).isEmpty());
        QCOMPARE(lastMessage(controls, QStringLiteral("rejected"), kNarrowEndpoint)
                     .value(QStringLiteral("reason")).toString(),
                 QString::fromLatin1(kRetireReasonSourceRetune));
        QCOMPARE(h.watched[kNarrowEndpoint].frames, narrowFrames);

        // Half a second on, the peak hold and the blobs are back, measured
        // on the moved picture.
        h.run(h.nextFrameNs() + 30 * kPeriodNs,
              [] { return spectrum(kCarrierBin - kMoveBins); });
        QCOMPARE(pan.generations.size(), 2);
        const Seen& later = pan.seen[pan.generations.last()];
        QVERIFY(later.lastExtras->peakHoldDbm.has_value());
        QVERIFY(later.lastExtras->peakBlobs.has_value());
        h.finish();
    }

    // The pan is asked for again with only its center moved, as a window
    // does when its view follows a tune. The receiver did not move.
    void aPanAskedAgainAtAnotherCenterCarriesItsPictureOn()
    {
        Harness h;
        QVERIFY(h.start());
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        Settled settled;
        settle(h, settled);
        if (QTest::currentTestFailed()) { return; }
        const double center = h.radio.streamCentreHz(h.streamIndex);
        const quint64 sourceGeneration = h.source->inputQueueDiagnostics(h.key).generation;

        // Eight bins up: the carrier moves four pixels down the screen.
        constexpr int kMoveBins = 8;
        QVERIFY(h.ask(kEndpoint, center + kMoveBins * kBinHz, 2));
        // The request crosses the station session's event loop; the calendar
        // goes on meanwhile, and the Core answers with a new context.
        QVERIFY(h.runUntilContexts(kEndpoint, 2, [] { return spectrum(kCarrierBin); }));
        // The source is the one it was.
        QCOMPARE(h.source->inputQueueDiagnostics(h.key).generation, sourceGeneration);
        QVERIFY(h.source->isActive(h.key));
        QCOMPARE(h.controller.activeEndpointCount(), 1);
        QCOMPARE(h.refused, 0);
        // The averaged energy is not shifted: the first frame of the moved
        // picture is one averaging step from the settled trace.
        carriedOn(h, settled, 1.0);
        if (QTest::currentTestFailed()) { return; }
        QVERIFY(lastMessage(controls, QStringLiteral("rejected"), kEndpoint).isEmpty());
        // The context the Core announced for it is at the new center.
        QTRY_COMPARE(static_cast<quint32>(lastMessage(controls, QStringLiteral("context"),
                                                      kEndpoint)
                                              .value(QStringLiteral("contextGeneration"))
                                              .toInteger()),
                     h.watched[kEndpoint].generations.last());
        const QJsonObject context = lastMessage(controls, QStringLiteral("context"), kEndpoint);
        QVERIFY2(std::abs(context.value(QStringLiteral("centreHz")).toDouble()
                          - (center + kMoveBins * kBinHz)) <= kBinHz,
                 qPrintable(QString::number(context.value(QStringLiteral("centreHz")).toDouble(),
                                            'f', 1)));
        QCOMPARE(context.value(QStringLiteral("sourceCentreHz")).toDouble(), center);

        // Four seconds later the trace has followed the carrier to its new
        // pixel.
        h.run(h.nextFrameNs() + 120 * kPeriodNs, [] { return spectrum(kCarrierBin); });
        const Watched& pan = h.watched[kEndpoint];
        QCOMPARE(pan.generations.size(), 2);
        const Seen& later = pan.seen[pan.generations.last()];
        QVERIFY(peakIndex(later.lastTrace) < peakIndex(settled.trace));

        // Another span is a new picture, and that still starts again.
        QVERIFY(h.ask(kEndpoint, center + kMoveBins * kBinHz, 3, kSpanHz / 2.0));
        QVERIFY(h.runUntilContexts(kEndpoint, 3, [] { return spectrum(kCarrierBin); }));
        QCOMPARE(pan.generations.size(), 3);
        const Seen& restarted = pan.seen[pan.generations.last()];
        const double fell = mean(later.lastTrace) - mean(restarted.firstTrace);
        QVERIFY2(fell > 10.0,
                 qPrintable(QStringLiteral("a new span's first trace is %1 dB under the settled "
                                           "one").arg(fell)));
        h.finish();
    }

    // Third wave: the Core states which picture each context continues, to a
    // client that declared the value, and sends every other client exactly
    // the context it parses today.
    void theCoreStatesThePictureOnlyToAClientThatDeclaredIt()
    {
        QList<QJsonObject> plain;
        everyKindOfContext(false, plain);
        if (QTest::currentTestFailed()) { return; }
        QList<QJsonObject> stated;
        everyKindOfContext(true, stated);
        if (QTest::currentTestFailed()) { return; }
        QCOMPARE(plain.size(), 8);
        QCOMPARE(stated.size(), 8);
        const QString key = QStringLiteral("pictureGeneration");
        QList<quint32> generation;
        QList<quint32> picture;
        for (int index = 0; index < 8; ++index) {
            // Today's context: the 19 keys and the five of the grant report.
            QCOMPARE(plain.at(index).size(), 24);
            QVERIFY(!plain.at(index).contains(key));
            QCOMPARE(stated.at(index).size(), 25);
            QVERIFY(stated.at(index).value(key).isDouble());
            // And the same bytes as the declared one without its one field.
            QJsonObject stripped = stated.at(index);
            stripped.remove(key);
            QCOMPARE(QJsonDocument(plain.at(index)).toJson(QJsonDocument::Compact),
                     QJsonDocument(stripped).toJson(QJsonDocument::Compact));
            generation.append(static_cast<quint32>(
                stated.at(index).value(QStringLiteral("contextGeneration")).toInteger()));
            picture.append(static_cast<quint32>(stated.at(index).value(key).toInteger()));
            if (index > 0) { QVERIFY(generation.at(index) != generation.at(index - 1)); }
        }
        // A first context starts a picture.
        QCOMPARE(picture.at(0), generation.at(0));
        // The same picture, and that picture moved twice: still the first.
        QCOMPARE(picture.at(1), generation.at(0));
        QCOMPARE(picture.at(2), generation.at(0));
        QCOMPARE(picture.at(3), generation.at(0));
        // Another span: a new picture.
        QCOMPARE(picture.at(4), generation.at(4));
        // Another averaging mode: a new picture.
        QCOMPARE(picture.at(5), generation.at(5));
        // A held maximum moved: a new picture.
        QCOMPARE(picture.at(6), generation.at(6));
        // Its dBm range alone: the same picture.
        QCOMPARE(picture.at(7), generation.at(6));
    }

    // Third wave: the start's declaration is taken only at version 1 and
    // only from a client the Core told the capability, which is one whose
    // hello declared the feature.
    void aStartMayDeclareThePictureOnlyAsTheCoreOffersIt()
    {
        const auto startWith = [](Harness& h, const QJsonValue& version) {
            QJsonObject control{{QStringLiteral("op"), QStringLiteral("start")},
                                {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}};
            if (!version.isUndefined()) {
                control.insert(QStringLiteral("spectrumPictureVersion"), version);
            }
            return h.client.sendMediaControl(control, h.client.sessionEpoch());
        };
        {
            Harness h;
            QVERIFY(h.connectSession());
            QCOMPARE(h.client.capabilities().spectrumPictureVersion, 1);
            for (const QJsonValue& bad : {QJsonValue(2), QJsonValue(0), QJsonValue(true),
                                          QJsonValue(QStringLiteral("1")), QJsonValue(1.5)}) {
                QVERIFY(startWith(h, bad));
                QTest::qWait(50);
                QVERIFY(h.mediaTransport.isNull());
            }
            QVERIFY(startWith(h, 1));
            QTRY_VERIFY(!h.mediaTransport.isNull());
            h.finish();
        }
        {
            // A client that did not declare the feature in its hello: it is
            // told no capability, and its start may not declare the value.
            Harness h;
            h.client.withholdFeatureForTest(QByteArrayLiteral("spectrumPicture"));
            QVERIFY(h.connectSession());
            QCOMPARE(h.client.capabilities().spectrumPictureVersion, 0);
            QVERIFY(startWith(h, 1));
            QTest::qWait(50);
            QVERIFY(h.mediaTransport.isNull());
            QVERIFY(startWith(h, QJsonValue(QJsonValue::Undefined)));
            QTRY_VERIFY(!h.mediaTransport.isNull());
            h.finish();
        }
    }

    // The operator tunes: the receiver's center moves once every frame
    // period for two seconds. The pan goes on getting its frames, at the
    // rate it had, and its picture does not start again.
    void tuningDoesNotStarveThePan()
    {
        Harness h;
        QVERIFY(h.start());
        Settled settled;
        settle(h, settled);
        if (QTest::currentTestFailed()) { return; }
        const double center = h.radio.streamCentreHz(h.streamIndex);
        // Two seconds of frames: sixty periods.
        constexpr qint64 kMeasureNs = 2 * kFps * kPeriodNs;

        // Two seconds standing still.
        const int framesAtStart = h.watched[kEndpoint].frames;
        h.run(h.nextFrameNs() + kMeasureNs, [] { return spectrum(kCarrierBin); });
        const int still = h.watched[kEndpoint].frames - framesAtStart;
        QCOMPARE(h.refused, 0);
        QCOMPARE(h.watched[kEndpoint].generations.size(), 1);
        const QVector<float> before = h.watched[kEndpoint].seen[settled.generation].lastTrace;

        // Two seconds tuning, 100 Hz a frame.
        const int framesBeforeTuning = h.watched[kEndpoint].frames;
        const int publishedBeforeTuning = h.published;
        int moves = 0;
        int restarts = 0;
        h.run(h.nextFrameNs() + kMeasureNs, [] { return spectrum(kCarrierBin); }, [&] {
            ++moves;
            if (!h.radio.requestStreamCentre(h.sliceId, center + moves * 100.0)
                || !h.source->isActive(h.key)) {
                ++restarts;
            }
        });
        const int tuning = h.watched[kEndpoint].frames - framesBeforeTuning;
        qInfo().noquote() << QStringLiteral(
            "R-DISP-RETUNE tuning: %1 frames in two seconds standing still, %2 in two seconds "
            "with %3 center moves (%4 source frames published, %5 refused, %6 restarts)")
            .arg(still).arg(tuning).arg(moves).arg(h.published - publishedBeforeTuning)
            .arg(h.refused).arg(restarts);
        QCOMPARE(moves, 60);
        QCOMPARE(restarts, 0);
        QCOMPARE(h.refused, 0);
        QCOMPARE(still, 60);
        QVERIFY2(tuning >= still,
                 qPrintable(QStringLiteral("%1 frames while tuning, %2 standing still")
                                .arg(tuning).arg(still)));
        QCOMPARE(h.controller.activeEndpointCount(), 1);
        // Every move made a context, each opening with a keyframe on the
        // trace that was there, not on a restarted one.
        const Watched& pan = h.watched[kEndpoint];
        QCOMPARE(pan.generations.size(), 1 + moves);
        for (int index = 1; index < pan.generations.size(); ++index) {
            const Seen& context = pan.seen[pan.generations.at(index)];
            QVERIFY(context.firstIsKeyframe);
            const double difference = meanDifference(context.firstTrace, before);
            QVERIFY2(difference <= 1.0,
                     qPrintable(QStringLiteral("context %1 opened %2 dB from the settled trace")
                                    .arg(index).arg(difference)));
        }
        QCOMPARE(h.controller.displayExtrasForTest(kEndpoint), settled.extras);
        h.finish();
    }

    // A measurement, with what it found pinned. A source frame waits at its
    // endpoint for the sender's next tick (every 5 ms), and a retune ends the
    // old context and drops the frame waiting under the old center
    // (DaemonMediaController::reconcileSource). So a move made after a frame
    // arrived and before the tick that would have sent it costs that frame.
    // Here every move is made in that gap, the worst case, on a link with no
    // display budget and on one paced to exactly the pan's admitted charge.
    void framesLostToAMoveMadeBeforeTheSenderTick_data()
    {
        QTest::addColumn<bool>("paced");
        QTest::addColumn<int>("framesPerMove");
        QTest::newRow("no budget, a move after every frame") << false << 1;
        QTest::newRow("no budget, a move after every third frame") << false << 3;
        QTest::newRow("paced, a move after every frame") << true << 1;
        QTest::newRow("paced, a move after every third frame") << true << 3;
    }

    void framesLostToAMoveMadeBeforeTheSenderTick()
    {
        QFETCH(bool, paced);
        QFETCH(int, framesPerMove);
        Harness h;
        if (paced) {
            // The least the Core admits this pan at: its own charge, the
            // display extras it asks for included.
            DisplayExtrasRequest extras;
            QVERIFY(parseDisplayExtrasRequest(
                subscription(kEndpoint, 0, 14.0e6, 1, kSpanHz), extras));
            const auto cost = displayCostWithExtras(kPixels, kFps, false, extras.sections());
            QVERIFY(cost.has_value());
            QVERIFY(h.server.setDisplayBudgetLimits(
                {cost->charge.applicationBytesPerSecond,
                 cost->charge.spectrumSampleUnitsPerSecond, 1}));
        }
        QVERIFY(h.start());
        QCOMPARE(h.server.displayBudgetLimits().has_value(), paced);
        Settled settled;
        settle(h, settled);
        if (QTest::currentTestFailed()) { return; }
        const double center = h.radio.streamCentreHz(h.streamIndex);
        constexpr qint64 kMeasureNs = 2 * kFps * kPeriodNs;

        struct Counted {
            int published = 0;
            int delivered = 0;
            int moves = 0;
            qint64 longestWaitNs = 0;
            qint64 waitedNs = 0;
        };
        // Runs the calendar for two seconds. With `moving`, the center moves
        // straight after every `framesPerMove`th frame is published, before
        // any sender tick.
        const auto measure = [&](bool moving) {
            Counted counted;
            const qint64 endNs = h.nextFrameNs() + kMeasureNs;
            qint64 arrivedNs = 0;
            for (;;) {
                const qint64 frameNs = kStartNs + h.frame * kPeriodNs;
                const qint64 tickNs = kStartNs + h.tick * kDisplaySenderIntervalNs;
                const qint64 next = std::min(frameNs, tickNs);
                if (next >= endNs) { break; }
                h.nowNs = next;
                if (frameNs == next) {
                    if (h.source->publishFrameForTest(h.key, frameNs,
                                                      spectrum(kCarrierBin))) {
                        ++counted.published;
                        arrivedNs = frameNs;
                    }
                    ++h.frame;
                    if (moving && counted.published % framesPerMove == 0) {
                        ++counted.moves;
                        h.radio.requestStreamCentre(h.sliceId, center + counted.moves * 100.0);
                    }
                    continue;
                }
                const qsizetype sentBefore = h.mediaTransport->displays.size();
                h.sendTick();
                ++h.tick;
                const QList<QByteArray>& displays = h.mediaTransport->displays;
                for (qsizetype index = sentBefore; index < displays.size(); ++index) {
                    const QByteArray& bytes = displays.at(index);
                    if (bytes.size() < 12 || bytes.startsWith("NSDX")
                        || qFromBigEndian<quint32>(bytes.constData() + 8) != kEndpoint) {
                        continue;
                    }
                    ++counted.delivered;
                    const qint64 waitNs = tickNs - arrivedNs;
                    counted.waitedNs += waitNs;
                    counted.longestWaitNs = std::max(counted.longestWaitNs, waitNs);
                }
            }
            h.decodeSent();
            return counted;
        };

        const Counted still = measure(false);
        const Counted tuning = measure(true);
        const auto meanWaitMs = [](const Counted& counted) {
            return counted.delivered == 0
                ? 0.0 : counted.waitedNs / 1.0e6 / counted.delivered;
        };
        qInfo().noquote() << QStringLiteral(
            "R-DISP-RETUNE frames lost per move (%1): standing still %2 of %3 frames "
            "delivered, a frame waiting %4 ms for its tick on average and %5 ms at most; "
            "with %6 moves, each made before the tick, %7 of %8 delivered (%9 lost)")
            .arg(QString::fromLatin1(QTest::currentDataTag()))
            .arg(still.delivered).arg(still.published)
            .arg(meanWaitMs(still), 0, 'f', 2).arg(still.longestWaitNs / 1.0e6, 0, 'f', 2)
            .arg(tuning.moves).arg(tuning.delivered).arg(tuning.published)
            .arg(tuning.published - tuning.delivered);
        QCOMPARE(still.published, 60);
        QCOMPARE(still.delivered, still.published);
        QCOMPARE(tuning.published, 60);
        QCOMPARE(tuning.moves, 60 / framesPerMove);
        // Each move cost exactly the frame it found waiting, and no other.
        QCOMPARE(tuning.delivered, tuning.published - tuning.moves);
        QCOMPARE(h.controller.activeEndpointCount(), 1);
        h.finish();
    }
};

QTEST_MAIN(TstDaemonDisplayRetune)
#include "tst_daemon_display_retune.moc"
