// =================================================================
// tests/tst_daemon_display_keep.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test. R-DISP-HOLD.
//
// A display asked for again with only its dBm range changed is the same
// picture: the Core's averaged trace, peak hold, blobs and waterfall levels
// carry on into the new context. A display asked for again with another
// span is a new picture and starts again, as it always has.
//
// The Core is the real DaemonMediaController behind a real station session;
// what it sends is decoded as a client decodes it. The Core paces its
// frames on its own clock, so the test feeds the receiver until the frames
// it waits for have come; nothing is proved by a wait alone.
//
// Modification history (NereusSDR):
//   2026-10-10  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  A new picture on the same receiver
//                                    keeps its waterfall levels and noise
//                                    floor; another receiver carries
//                                    nothing. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtTest>

#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>
#include <optional>
#include <random>

#include "core/AppSettings.h"
#include "core/FFTEngine.h"
#include "core/HpsdrModel.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DisplayExtras.h"
#include "core/session/media/IMediaTransport.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr char kConnectionId[] = "11111111-2222-4333-8444-555555555555";
constexpr quint32 kEndpoint = 7;

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

// Both planes averaged in the log domain (the desktop's Log Recursive), so
// a restarted plane is far from a settled one for many frames.
QJsonObject plane()
{
    return {{QStringLiteral("detector"), 0},
            {QStringLiteral("averageMode"), 3},
            {QStringLiteral("averageAlpha"), 0.0}};
}

QJsonObject subscription(int sliceId, double centreHz, int revision, double minDbm,
                         double maxDbm, double spanHz)
{
    return {{QStringLiteral("op"), QStringLiteral("subscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("endpointId"), static_cast<qint64>(kEndpoint)},
            {QStringLiteral("revision"), revision},
            {QStringLiteral("sliceId"), sliceId},
            {QStringLiteral("tier"), QStringLiteral("wide")},
            {QStringLiteral("fftSize"), 1024},
            {QStringLiteral("windowType"), static_cast<int>(WindowFunction::Hann)},
            {QStringLiteral("centreHz"), centreHz},
            {QStringLiteral("spanHz"), spanHz},
            {QStringLiteral("pixels"), 128},
            {QStringLiteral("fps"), 30},
            {QStringLiteral("framesPerLine"), 1},
            {QStringLiteral("trace"), plane()},
            {QStringLiteral("waterfall"), plane()},
            {QStringLiteral("minDbm"), minDbm},
            {QStringLiteral("maxDbm"), maxDbm},
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

// What the Core sent for one context of the endpoint, as a client decodes it.
struct Seen {
    int frames = 0;
    QVector<float> firstTrace;
    QVector<float> lastTrace;
    std::optional<DisplayExtrasFrame> firstExtras;
    std::optional<DisplayExtrasFrame> lastExtras;
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
    DaemonMediaController controller;
    int sliceId{-1};
    int streamIndex{-1};
    // A second receiver, 40 dB quieter than the first, fed once it is added
    // (addSecondReceiver).
    int secondSliceId{-1};
    int secondStreamIndex{-1};
    std::minstd_rand noise{20261010U};
    // The decoder of the context now being sent, and everything decoded.
    DisplayCodecDecoder decoder;
    std::optional<DisplayCodecContext> decoding;
    QMap<quint32, Seen> seen;
    QList<quint32> generations;
    int cursor = 0;

    Harness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()))
        , controller(&server, &radio, nullptr, [this](QObject* parent) -> IMediaTransport* {
            mediaTransport = new FakeTransport(parent);
            return mediaTransport;
        })
    {
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        sliceId = radio.addSlice();
        SliceModel* const slice = radio.sliceById(sliceId);
        streamIndex = slice ? slice->streamIndex() : -1;
        server.setMediaEnabled(true);
    }

    bool start()
    {
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
        if (!client.sendMediaControl({{QStringLiteral("op"), QStringLiteral("start")},
                                      {QStringLiteral("connectionId"),
                                       QLatin1String(kConnectionId)}},
                                     client.sessionEpoch())) {
            return false;
        }
        if (!QTest::qWaitFor([this] { return !mediaTransport.isNull(); }, 10'000)) {
            return false;
        }
        mediaTransport->becomeReady();
        return true;
    }

    bool ask(int revision, double minDbm, double maxDbm, double spanHz)
    {
        return client.sendMediaControl(
            subscription(sliceId, radio.streamCentreHz(streamIndex), revision, minDbm, maxDbm,
                         spanHz),
            client.sessionEpoch());
    }

    bool addSecondReceiver()
    {
        // A slice on another pan and another band has a stream of its own.
        secondSliceId = radio.addSlice(QStringLiteral("pan-1"));
        SliceModel* const slice = radio.sliceById(secondSliceId);
        if (slice) { slice->setFrequency(7150000.0); }
        secondStreamIndex = slice ? slice->streamIndex() : -1;
        return secondStreamIndex >= 0 && secondStreamIndex != streamIndex;
    }

    // The same endpoint, asked for on the second receiver.
    bool askOnSecondReceiver(int revision, double minDbm, double maxDbm, double spanHz)
    {
        return client.sendMediaControl(
            subscription(secondSliceId, radio.streamCentreHz(secondStreamIndex), revision,
                         minDbm, maxDbm, spanHz),
            client.sessionEpoch());
    }

    // The receiver's samples: steady noise, so the picture has a floor.
    void feed()
    {
        QVector<float> iq(2048);
        std::uniform_real_distribution<float> sample(-1.0e-3f, 1.0e-3f);
        for (float& value : iq) { value = sample(noise); }
        QMetaObject::invokeMethod(&radio, "rawIqDataForStream", Qt::DirectConnection,
                                  Q_ARG(int, streamIndex), Q_ARG(QVector<float>, iq));
        if (secondStreamIndex >= 0) {
            for (float& value : iq) { value *= 0.01f; }
            QMetaObject::invokeMethod(&radio, "rawIqDataForStream", Qt::DirectConnection,
                                      Q_ARG(int, secondStreamIndex),
                                      Q_ARG(QVector<float>, iq));
        }
    }

    // Decodes what the Core has sent since the last call.
    void decodeSent()
    {
        const QList<QByteArray>& displays = mediaTransport->displays;
        for (; cursor < displays.size(); ++cursor) {
            const QByteArray& bytes = displays.at(cursor);
            if (bytes.startsWith("NSDX")) {
                if (!decoding) { continue; }
                const DisplayExtrasDecodeResult extras = decodeDisplayExtras(bytes, *decoding);
                if (!extras.accepted) { continue; }
                Seen& context = seen[decoding->contextGeneration];
                if (!context.firstExtras) { context.firstExtras = extras.frame; }
                context.lastExtras = extras.frame;
                continue;
            }
            DisplayCodecDecodeResult frame = decoder.decode(bytes);
            if (frame.disposition != DisplayCodecDisposition::Accepted) {
                // The keyframe of a new context starts a new decoder, as a
                // client's does when the context arrives.
                DisplayCodecDecoder fresh;
                frame = fresh.decode(bytes);
                if (frame.disposition != DisplayCodecDisposition::Accepted) { continue; }
                decoder = fresh;
            }
            decoding = frame.frame.context;
            const quint32 generation = frame.frame.context.contextGeneration;
            if (!seen.contains(generation)) { generations.append(generation); }
            Seen& context = seen[generation];
            if (context.frames == 0) { context.firstTrace = frame.frame.traceDbm; }
            context.lastTrace = frame.frame.traceDbm;
            ++context.frames;
        }
    }

    // Feeds the receiver, letting the Core send, until `done`.
    bool run(const std::function<bool()>& done, int timeoutMs = 60'000)
    {
        return QTest::qWaitFor([&] {
            feed();
            decodeSent();
            return done();
        }, timeoutMs);
    }

    void finish() { client.disconnectFromStation(QStringLiteral("test complete")); }
};

} // namespace

class TstDaemonDisplayKeep : public QObject {
    Q_OBJECT
private slots:
    void aDbmRangeAloneKeepsThePictureAndANewSpanRestartsIt()
    {
#ifndef HAVE_FFTW3
        QSKIP("FFTEngine has no FFTW3 backend in this build");
#endif
        Harness h;
        QVERIFY(h.start());
        const double span = 48000.0;
        QVERIFY(h.ask(1, -180.0, 0.0, span));
        QTRY_COMPARE(h.controller.activeEndpointCount(), 1);

        // A settled picture: five seconds of frames at a one second
        // averaging time, with the peak hold and the levels being sent.
        QVERIFY(h.run([&] {
            if (h.generations.isEmpty()) { return false; }
            const Seen& first = h.seen[h.generations.first()];
            return first.frames >= 150 && first.lastExtras && first.lastExtras->peakHoldDbm
                && first.lastExtras->waterfallLevelsDbm && first.lastExtras->peakBlobs;
        }));
        QCOMPARE(h.generations.size(), 1);
        const Seen settled = h.seen[h.generations.first()];
        const QVector<float> trace = settled.lastTrace;
        const QVector<float> hold = *settled.lastExtras->peakHoldDbm;
        const std::pair<float, float> levels = *settled.lastExtras->waterfallLevelsDbm;
        QCOMPARE(trace.size(), 128);
        // The trace grew from its seed: where it settled is far above where
        // it started, so a restart cannot pass for a kept picture.
        const double grown = mean(trace) - mean(settled.firstTrace);
        QVERIFY2(grown > 10.0, qPrintable(QStringLiteral("the trace grew %1 dB").arg(grown)));
        const DisplayExtrasProcessor* const extras = h.controller.displayExtrasForTest(kEndpoint);
        QVERIFY(extras);

        // The same picture, asked again for another dBm range.
        QVERIFY(h.ask(2, -170.0, 10.0, span));
        QVERIFY(h.run([&] {
            return h.generations.size() == 2 && h.seen[h.generations.last()].firstExtras;
        }));
        const Seen kept = h.seen[h.generations.last()];
        QCOMPARE(h.decoding->minDbm, -170.0f);
        QCOMPARE(h.decoding->maxDbm, 10.0f);
        // The new context's keyframe carries the settled trace.
        const double moved = meanDifference(kept.firstTrace, trace);
        QVERIFY2(moved <= 1.0,
                 qPrintable(QStringLiteral("the keyframe of the new context is %1 dB from the "
                                           "settled trace (mean %2 dBm after %3 dBm)")
                                .arg(moved).arg(mean(kept.firstTrace)).arg(mean(trace))));
        // The peak hold is still there, without its 500 ms wait after a
        // reset, and so are the blobs; the waterfall's levels carry on.
        QVERIFY2(kept.firstExtras->peakHoldDbm.has_value(),
                 "the peak hold was emptied by a new dBm range");
        QVERIFY(meanDifference(*kept.firstExtras->peakHoldDbm, hold) <= 1.0);
        QVERIFY(kept.firstExtras->peakBlobs.has_value());
        QCOMPARE(kept.firstExtras->peakBlobs->size(), settled.lastExtras->peakBlobs->size());
        QVERIFY(kept.firstExtras->waterfallLevelsDbm.has_value());
        QVERIFY(std::abs(kept.firstExtras->waterfallLevelsDbm->first - levels.first) <= 1.0f);
        QVERIFY(std::abs(kept.firstExtras->waterfallLevelsDbm->second - levels.second) <= 1.0f);
        QCOMPARE(h.controller.displayExtrasForTest(kEndpoint), extras);

        // A new picture: the same range, half the span. Everything starts
        // again, as it does today.
        QVERIFY(h.ask(3, -170.0, 10.0, span / 2.0));
        QVERIFY(h.run([&] {
            return h.generations.size() == 3 && h.seen[h.generations.last()].firstExtras;
        }));
        const Seen restarted = h.seen[h.generations.last()];
        const double fell = mean(trace) - mean(restarted.firstTrace);
        QVERIFY2(fell > 10.0,
                 qPrintable(QStringLiteral("a new span's first trace is %1 dB under the settled "
                                           "one").arg(fell)));
        QVERIFY2(!restarted.firstExtras->peakHoldDbm.has_value(),
                 "a new picture kept the old picture's peak hold");
        h.finish();
    }

    // The merge of main joined two rules in DaemonMediaController: main's
    // (the same picture keeps its processor) and this branch's (a display
    // asked again on the same receiver keeps its levels). Together: a new
    // picture on the same receiver gets a new processor that adopts the old
    // one's waterfall levels and noise floor, and starts the blobs and the
    // peak hold again; the same picture keeps everything; another receiver
    // carries nothing.
    void aNewPictureOnTheSameReceiverKeepsItsLevelsAndAnotherReceiverCarriesNothing()
    {
#ifndef HAVE_FFTW3
        QSKIP("FFTEngine has no FFTW3 backend in this build");
#endif
        Harness h;
        QVERIFY(h.addSecondReceiver());
        QVERIFY(h.start());
        const double span = 48000.0;
        QVERIFY(h.ask(1, -180.0, 0.0, span));
        QTRY_COMPARE(h.controller.activeEndpointCount(), 1);
        const auto settledOn = [&h](int contexts) {
            if (h.generations.size() != contexts) { return false; }
            const Seen& last = h.seen[h.generations.last()];
            return last.frames >= 150 && last.lastExtras && last.lastExtras->peakHoldDbm
                && last.lastExtras->waterfallLevelsDbm && last.lastExtras->noiseFloorDbm
                && last.lastExtras->peakBlobs;
        };
        const auto firstExtrasOf = [&h](int contexts) {
            return h.generations.size() == contexts && h.seen[h.generations.last()].firstExtras;
        };
        QVERIFY(h.run([&] { return settledOn(1); }));
        const Seen settled = h.seen[h.generations.last()];
        const std::pair<float, float> levels = *settled.lastExtras->waterfallLevelsDbm;
        const float floor = *settled.lastExtras->noiseFloorDbm;
        const DisplayExtrasProcessor* const first = h.controller.displayExtrasForTest(kEndpoint);
        QVERIFY(first);
        QVERIFY(first->agcRunLevel().has_value());

        // A zoom: half the span on the same receiver. A new picture.
        QVERIFY(h.ask(2, -180.0, 0.0, span / 2.0));
        QVERIFY(h.run([&] { return firstExtrasOf(2); }));
        const Seen zoomed = h.seen[h.generations.last()];
        // The picture did start again: the first trace is far under the
        // settled one, so levels made from it alone would be far lower too.
        QVERIFY(mean(settled.lastTrace) - mean(zoomed.firstTrace) > 10.0);
        // The levels and the noise floor carry on. The AGC takes a
        // twentieth of the way to each new row, and the first row of the
        // new picture is some 60 dB down, so the carried low level is about
        // 3 dB lower on the first frame; one started again would be some
        // 60 dB lower.
        QVERIFY(zoomed.firstExtras->waterfallLevelsDbm.has_value());
        QVERIFY2(std::abs(zoomed.firstExtras->waterfallLevelsDbm->first - levels.first) <= 10.0f,
                 qPrintable(QStringLiteral("the low level went from %1 to %2 on a zoom")
                                .arg(levels.first)
                                .arg(zoomed.firstExtras->waterfallLevelsDbm->first)));
        QCOMPARE(zoomed.firstExtras->waterfallLevelsDbm->second, levels.second);
        QVERIFY2(zoomed.firstExtras->noiseFloorDbm.has_value(),
                 "the noise floor started again on a zoom");
        QVERIFY2(std::abs(*zoomed.firstExtras->noiseFloorDbm - floor) <= 3.0f,
                 qPrintable(QStringLiteral("the noise floor went from %1 to %2 on a zoom")
                                .arg(floor).arg(*zoomed.firstExtras->noiseFloorDbm)));
        // The blobs and the peak hold start again, in a new processor.
        QVERIFY2(!zoomed.firstExtras->peakHoldDbm.has_value(),
                 "a new picture kept the old picture's peak hold");
        // Steady noise has no peaks to find, so the blobs are proved by
        // their processor: it is a new one, and its detector with it.
        const DisplayExtrasProcessor* const second = h.controller.displayExtrasForTest(kEndpoint);
        QVERIFY(second);
        QVERIFY(second != first);

        // The zoomed picture settles; then the same picture is asked again
        // for another dBm range: main's rule, everything stays.
        QVERIFY(h.run([&] { return settledOn(2); }));
        const Seen zoomSettled = h.seen[h.generations.last()];
        const std::pair<float, float> zoomLevels = *zoomSettled.lastExtras->waterfallLevelsDbm;
        QVERIFY(h.ask(3, -170.0, 10.0, span / 2.0));
        QVERIFY(h.run([&] { return firstExtrasOf(3); }));
        const Seen kept = h.seen[h.generations.last()];
        QCOMPARE(h.controller.displayExtrasForTest(kEndpoint), second);
        QVERIFY2(kept.firstExtras->peakHoldDbm.has_value(),
                 "the peak hold was emptied by a new dBm range");
        QVERIFY(kept.firstExtras->peakBlobs.has_value());
        QVERIFY(kept.firstExtras->waterfallLevelsDbm.has_value());
        QVERIFY(std::abs(kept.firstExtras->waterfallLevelsDbm->first - zoomLevels.first) <= 1.0f);
        QVERIFY(meanDifference(kept.firstTrace, zoomSettled.lastTrace) <= 1.0);

        // The endpoint asked for on the second receiver, 40 dB quieter:
        // nothing of the first receiver's comes with it.
        QVERIFY(h.run([&] { return settledOn(3); }));
        const std::pair<float, float> before =
            *h.seen[h.generations.last()].lastExtras->waterfallLevelsDbm;
        const float floorBefore = *h.seen[h.generations.last()].lastExtras->noiseFloorDbm;
        QVERIFY(h.askOnSecondReceiver(4, -170.0, 10.0, span / 2.0));
        QVERIFY(h.run([&] { return firstExtrasOf(4); }));
        const Seen other = h.seen[h.generations.last()];
        const DisplayExtrasProcessor* const third = h.controller.displayExtrasForTest(kEndpoint);
        QVERIFY(third);
        QVERIFY(third != second);
        QVERIFY(!other.firstExtras->peakHoldDbm.has_value());
        QVERIFY(other.firstExtras->waterfallLevelsDbm.has_value());
        QVERIFY2(other.firstExtras->waterfallLevelsDbm->first < before.first - 20.0f,
                 qPrintable(QStringLiteral("the first receiver's low level %1 came to the "
                                           "second receiver (%2)")
                                .arg(before.first)
                                .arg(other.firstExtras->waterfallLevelsDbm->first)));
        if (other.firstExtras->noiseFloorDbm) {
            QVERIFY2(*other.firstExtras->noiseFloorDbm < floorBefore - 20.0f,
                     qPrintable(QStringLiteral("the first receiver's noise floor %1 came to "
                                               "the second receiver (%2)")
                                    .arg(floorBefore).arg(*other.firstExtras->noiseFloorDbm)));
        }
        h.finish();
    }
};

QTEST_MAIN(TstDaemonDisplayKeep)
#include "tst_daemon_display_keep.moc"
