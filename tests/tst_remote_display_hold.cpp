// =================================================================
// tests/tst_remote_display_hold.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test. R-DISP-HOLD.
//
// A remote window's spectrum picture restarted by itself during receive:
// the grid follower moved the dBm scale 2 dB, the window asked the Core for
// the new range, the Core answered with a fresh endpoint whose averaged
// trace grew again from its seed, the regrowing trace pulled the pan's
// noise floor estimate down, and the grid follower moved the scale again.
//
// Two rules close the loop, and these tests hold both from the window's
// side: the window holds the dBm range it asked for while what the pan
// needs lies inside it (so the grid follower's moves ask nothing), and a
// context for the same picture blanks nothing the pan has drawn or
// measured.
//
// The planner is run by planNowForTest on a clock the test moves; nothing
// here is proved by waiting.
//
// R-DISP-RETUNE adds the third kind of context: the same picture moved in
// frequency (a retune, a pan drag). The drawn trace, the waterfall history
// and the noise floor carry on; the peak hold and the blobs empty.
//
// Modification history (NereusSDR):
//   2026-10-12  J.J. Boyd / KG4VCF  Display-only fixtures withhold remoteTx
//                                    so no PC microphone preview is opened.
//                                    AI-assisted via OpenAI Codex.
//   2026-10-11  J.J. Boyd / KG4VCF  The two fast spin tests set Sizing to
//                                    Fixed, the engines their frame counts
//                                    were measured on (R-DSP-59).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  The mini display's "more bins than
//                                    points" row sets Sizing to Fixed, so
//                                    its pan stays on the wide engine now
//                                    that Auto plans a pan's size from
//                                    its span (R-DSP-59).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  The fast spin tests end each step on the
//                                    frames it waits for, not on a count of
//                                    frames in a fixed time, which fell
//                                    under load.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  R-DISP-RETUNE (third wave): the Core
//                                    states the picture; what a pan and a
//                                    mini display draw through a fast spin,
//                                    over a link that delays the frames as
//                                    it delays the answers; a Core that
//                                    states no picture.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  R-DISP-RETUNE: the moved picture tests.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-10  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtTest>

#include <QApplication>
#include <QJsonObject>
#include <QMetaMethod>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <random>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ClarityController.h"
#include "core/ControlRanges.h"
#include "core/HpsdrModel.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/IMediaTransport.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "gui/PanClarityRegistry.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/RemoteMediaController.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

// The dBm window's headroom and whole-dB rounding, as the rule states them.
constexpr double kHeadroomDb = 10.0;

// The fast spin tests: how long a step waits for a frame the Core is to send,
// or the window is to draw. A step ends on those frames, not on how many of
// them a fixed wait happened to hold. The Core makes no frame within 5 ms of
// the one before it (FFTEngine::processFrame), so when its spectrum thread
// is kept waiting and then takes several blocks of samples at once, the
// frames due inside that catch-up are never made. Among the other tests of a
// parallel run that left steps with no frame of the spin to deliver: a count
// of frames drawn in 30 steps, pinned at 40, came out at 34 and at 38.
// (A quiet Mac, three runs: the pan drew 57, 57 and 58 of the 71 frames of
// the spin's contexts that reached the window in 30 steps; the mini display
// was given 22, 23 and 27 in 16 steps. Before the window drew what it was
// sent, both were 0.)
constexpr int kSpinFrameWaitMs = 10'000;

class DisplayTransport final : public IMediaTransport {
public:
    explicit DisplayTransport(QObject* parent) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { return true; }
    void stop() override { active = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    // One spectrum frame, by the endpoint and the context generation in its
    // envelope.
    struct Frame {
        quint32 endpoint = 0;
        quint32 generation = 0;
    };
    static std::optional<Frame> frameOf(const QByteArray& packet)
    {
        if (packet.size() < 16 || !packet.startsWith("NSDC")) { return std::nullopt; }
        return Frame{qFromBigEndian<quint32>(packet.constData() + 8),
                     qFromBigEndian<quint32>(packet.constData() + 12)};
    }
    bool sendDisplay(const QByteArray& packet) override
    {
        if (!active) { return false; }
        if (const std::optional<Frame> frame = frameOf(packet)) { sent.append(*frame); }
        // A closed gate loses the packet, as a lossy link does.
        if (!other || !gateOpen) { return true; }
        // A link with delay: the packet waits, in order, to be released.
        if (holding) { held.append(packet); return true; }
        other->deliver(packet);
        return true;
    }
    // Everything held up to now is what the next releaseMarked() delivers.
    void mark() { marked = held.size(); }
    // Delivers, in order, what was held when mark() was called; returns the
    // spectrum frames among it.
    QList<Frame> releaseMarked()
    {
        QList<Frame> delivered;
        const qsizetype count = std::min(marked, held.size());
        const QList<QByteArray> due = held.mid(0, count);
        held.remove(0, count);
        marked = 0;
        for (const QByteArray& packet : due) {
            if (const std::optional<Frame> frame = frameOf(packet)) { delivered.append(*frame); }
            if (other) { other->deliver(packet); }
        }
        return delivered;
    }
    QList<Frame> releaseAll()
    {
        holding = false;
        mark();
        return releaseMarked();
    }
    // The context generation of the newest spectrum frame this end was given
    // to send for an endpoint, held or not.
    std::optional<quint32> newestSent(quint32 endpoint) const
    {
        for (auto it = sent.crbegin(); it != sent.crend(); ++it) {
            if (it->endpoint == endpoint) { return it->generation; }
        }
        return std::nullopt;
    }
    bool sendRtp(const QByteArray&) override { return active; }
    bool isReady() const override { return active; }
    void activate() { active = true; emit ready(); }
    void deliver(const QByteArray& packet) { emit displayReceived(packet); }
    bool active = false;
    bool gateOpen = true;
    bool holding = false;
    qsizetype marked = 0;
    QList<QByteArray> held;
    // Every spectrum frame this end was given to send, held or not.
    QList<Frame> sent;
    QPointer<DisplayTransport> other;
};

// The frequency of the strongest signal in a row of samples drawn across
// centerHz +- spanHz / 2.
std::optional<double> strongestHz(const QVector<float>& samples, double centerHz, double spanHz)
{
    if (samples.isEmpty() || spanHz <= 0.0) { return std::nullopt; }
    const float top = *std::max_element(samples.cbegin(), samples.cend());
    double sum = 0.0;
    int counted = 0;
    for (int i = 0; i < samples.size(); ++i) {
        if (samples.at(i) >= top - 0.01f) { sum += i; ++counted; }
    }
    return centerHz - spanHz / 2.0 + (sum / counted + 0.5) * spanHz / samples.size();
}

struct Ask {
    double minDbm = 0.0;
    double maxDbm = 0.0;
    bool mini = false;
};

struct Range {
    double minDbm = 0.0;
    double maxDbm = 0.0;
};

// What the pan needs coded: its scale joined with the waterfall levels in
// force (the levels Clarity set, or the stored ones).
Range neededBy(const SpectrumWidget* widget)
{
    const double panHigh = double(widget->refLevel());
    const double panLow = panHigh - double(widget->dynamicRange());
    const bool runtime = widget->clarityActive();
    const double low = runtime ? double(widget->wfActiveLowThreshold())
                               : double(widget->wfLowThreshold());
    const double high = runtime ? double(widget->wfActiveHighThreshold())
                                : double(widget->wfHighThreshold());
    return {std::min(panLow, low), std::max(panHigh, high)};
}

// One window, with or without a Core's media controller behind the station.
// Without one the station only receives what the window asks (the asks are
// counted as they leave the window); with one the Core answers with contexts
// and frames.
struct Fixture {
    QTemporaryDir dir;
    AppSettings settings;
    RadioModel station;
    std::unique_ptr<StationServer> server;
    QPointer<DisplayTransport> coreMedia;
    std::unique_ptr<DaemonMediaController> core;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy proxy;
    StationClient client{&remote, &proxy};
    PanadapterStack stack;
    PanadapterApplet* pan = nullptr;
    SpectrumWidget* widget = nullptr;
    QPointer<DisplayTransport> windowMedia;
    std::unique_ptr<RemoteMediaController> window;
    qint64 now = 1'000;
    QList<Ask> asks;
    // The station's end of the session link, to hold what the Core sends.
    QPointer<Test::LoopbackTransport> stationLink;
    // Called inside the send of each mini display ask.
    std::function<void()> onMiniAsk;
    // Called inside the send of each pan ask.
    std::function<void()> onPanAsk;
    int sliceId = -1;
    int stream = -1;
    double centre = 0.0;
    std::minstd_rand noise{20261010U};
    // A carrier this far from the receiver's center, when set.
    std::optional<double> toneOffsetHz;
    double tonePhase = 0.0;

    // statesPicture false: the window declares no spectrumPicture, so the
    // Core is to it one that states no picture (an older Core).
    explicit Fixture(bool withCore, bool statesPicture = true)
        : settings(dir.filePath(QStringLiteral("station.settings")))
    {
        // These display fixtures offer no microphone line or keying verbs.
        client.withholdFeatureForTest(QByteArrayLiteral("remoteTx"));
        if (!statesPicture) {
            client.withholdFeatureForTest(QByteArrayLiteral("spectrumPicture"));
        }
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        sliceId = station.addSlice();
        SliceModel* const slice = station.sliceById(sliceId);
        stream = slice ? slice->streamIndex() : -1;
        centre = stream >= 0 ? station.streamCentreHz(stream) : 0.0;
        server = std::make_unique<StationServer>(
            &station, settings, NereusSDR::Test::seedUpgradedCoreToken(dir.path()));
        server->setMediaEnabled(true);
        if (withCore) {
            server->setDisplayBudgetLimits({10'000'000, 10'000'000, 1});
            core = std::make_unique<DaemonMediaController>(
                server.get(), &station, nullptr, [this](QObject* owner) -> IMediaTransport* {
                    coreMedia = new DisplayTransport(owner);
                    return coreMedia;
                });
        }
        remote.audioEngine()->setMasterMuted(true); // The fixture opens no speaker.
        pan = stack.addPanadapter(QStringLiteral("pan"));
        stack.setActivePan(QStringLiteral("pan"));
        pan->addSlice(sliceId);
        pan->setActiveSliceIndex(sliceId);
        widget = pan->spectrumWidget();
        widget->setExtendedViewAllowed(false);
        widget->setDisplayWindowPreservingHistory(centre, 96000);
        widget->setVfoFrequency(centre);
        widget->setConnectionState(ConnectionState::Connected);
        widget->setDispNormalize(false);
        widget->setWfUseSpectrumMinMax(false);
        widget->setWfAgcEnabled(false);
        widget->setWaterfallNFAGCEnabled(false);
        widget->setAdjustGridMinToNoiseFloor(false);
        widget->setDbmRange(-140.0f, -40.0f);
        widget->setWfLowThreshold(-130.0f);
        widget->setWfHighThreshold(-60.0f);
        stack.resize(600, 700);
        stack.show();
    }

    bool start()
    {
        if (!QTest::qWaitForWindowExposed(&stack)) { return false; }
        window = std::make_unique<RemoteMediaController>(
            &client, &remote, &stack, nullptr,
            [this](QObject* owner) -> IMediaTransport* {
                windowMedia = new DisplayTransport(owner);
                return windowMedia;
            },
            [this] { return now; });
        stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        // Each ask is recorded as it leaves the window, inside the send.
        QObject::connect(clientLink, &Test::LoopbackTransport::outboundText, clientLink,
                         [this](const QByteArray& wire) { record(wire); });
        client.startSession(clientLink, server->token());
        server->acceptTransport(stationLink);
        if (!QTest::qWaitFor([this] {
                return client.mediaAvailable() && windowMedia && (!core || coreMedia);
            }, 10'000)) {
            return false;
        }
        if (core) {
            coreMedia->other = windowMedia;
            coreMedia->activate();
        }
        windowMedia->activate();
        return QTest::qWaitFor([this] { return panAsks() >= 1; }, 10'000)
            && !window->micLineOpen() && !remote.audioEngine()->hasCaptureDemand()
            && remote.audioEngine()->captureHelperProcessIdForTest() == 0;
    }

    void record(const QByteArray& wire)
    {
        if (!wire.contains("\"op\":\"subscribe\"")) { return; }
        static const QRegularExpression minimum(
            QStringLiteral("\"minDbm\":(-?[0-9.eE+-]+)"));
        static const QRegularExpression maximum(
            QStringLiteral("\"maxDbm\":(-?[0-9.eE+-]+)"));
        const QString text = QString::fromUtf8(wire);
        Ask ask;
        ask.minDbm = minimum.match(text).captured(1).toDouble();
        ask.maxDbm = maximum.match(text).captured(1).toDouble();
        ask.mini = wire.contains("\"displayRole\":\"mini\"");
        asks.append(ask);
        if (ask.mini && onMiniAsk) { onMiniAsk(); }
        if (!ask.mini && onPanAsk) { onPanAsk(); }
    }

    int panAsks() const
    {
        return int(std::count_if(asks.cbegin(), asks.cend(),
                                 [](const Ask& ask) { return !ask.mini; }));
    }
    int miniAsks() const { return int(asks.size()) - panAsks(); }
    Ask lastAsk(bool mini = false) const
    {
        for (auto it = asks.crbegin(); it != asks.crend(); ++it) {
            if (it->mini == mini) { return *it; }
        }
        return {};
    }

    // A range that has held for longer than a frame period, planned: the
    // first tick sees it, the second finds it has held and asks if it must.
    void plan()
    {
        now += 60;
        window->planNowForTest();
        now += 60;
        window->planNowForTest();
    }

    // The receiver's samples: noise, so the trace has a floor of its own
    // well inside the dBm window (a silent receiver's floor is the window's
    // lower edge, which moves with the window).
    void feed()
    {
        QVector<float> iq(2048);
        std::uniform_real_distribution<float> sample(-1.0e-3f, 1.0e-3f);
        for (float& value : iq) { value = sample(noise); }
        if (toneOffsetHz) {
            const double advance = 2.0 * std::numbers::pi * *toneOffsetHz / 192000.0;
            for (int n = 0; n < iq.size() / 2; ++n) {
                iq[2 * n] += float(0.02 * std::cos(tonePhase));
                iq[2 * n + 1] += float(0.02 * std::sin(tonePhase));
                tonePhase += advance;
            }
            tonePhase = std::fmod(tonePhase, 2.0 * std::numbers::pi);
        }
        QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                                  Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
    }

    // Feeds the receiver until `done`, letting the Core and the window run.
    bool run(const std::function<bool()>& done, int timeoutMs = 20'000)
    {
        return QTest::qWaitFor([&] { feed(); return done(); }, timeoutMs);
    }

    // The window has accepted the context that answers its latest ask.
    bool contextFollowsLastAsk() const
    {
        const Ask ask = lastAsk();
        const DisplayCodecContext& codec = widget->remoteCodecForTest();
        return codec.traceSamples != 0 && double(codec.minDbm) == ask.minDbm
            && double(codec.maxDbm) == ask.maxDbm;
    }

    void finish() { client.disconnectFromStation(QStringLiteral("test complete")); }
};

// Counts the window's "Core noise floor received" lines, which it logs once
// each time its received state goes from not received to received.
int g_noiseFloorReceivedLines = 0;
QtMessageHandler g_previousHandler = nullptr;

void countNoiseFloorReceived(QtMsgType type, const QMessageLogContext& context,
                             const QString& message)
{
    if (message.contains(QLatin1String("Core noise floor received for Clarity"))) {
        ++g_noiseFloorReceivedLines;
    }
    if (g_previousHandler) { g_previousHandler(type, context, message); }
}

} // namespace

class TstRemoteDisplayHold : public QObject
{
    Q_OBJECT

private:
    // Brings a pan with a Core behind it to a settled picture: a trace
    // averaged over a second, a scale placed around the trace's own noise
    // floor, and a noise floor estimate that has stopped moving.
    static bool settlePicture(Fixture& f, QSignalSpy& frames)
    {
        f.widget->setSpectrumAveraging(SpectrumAveraging::LogRecursive);
        f.widget->setSpectrumAverageTimeMs(1000);
        f.widget->setNoiseFloorFastAttack(false);
        if (!f.start()) { return false; }
        if (!f.run([&] { return f.contextFollowsLastAsk() && frames.size() >= 60; })) {
            return false;
        }
        // The estimate climbs from its start value over many frames: the
        // scale is placed only once it has stopped, or it would be placed
        // around a floor the trace does not have and the trace would sit
        // above the range the Core codes.
        if (!steadyFloor(f, frames)) { return false; }
        // The scale around the floor the trace really has.
        const float floor = std::round(f.widget->nfLerpAverageForTest());
        f.widget->setDbmRange(floor - 30.0f, floor + 70.0f);
        f.widget->setWfLowThreshold(floor - 20.0f);
        f.widget->setWfHighThreshold(floor + 40.0f);
        f.plan();
        if (!f.run([&] { return f.contextFollowsLastAsk(); })) { return false; }
        return steadyFloor(f, frames);
    }

    // The estimate holds within a tenth of a dB over 30 frames.
    static bool steadyFloor(Fixture& f, QSignalSpy& frames)
    {
        float mark = f.widget->nfLerpAverageForTest();
        qsizetype markFrames = frames.size();
        return f.run([&] {
            if (frames.size() - markFrames < 30) { return false; }
            const float value = f.widget->nfLerpAverageForTest();
            const bool steady = std::abs(value - mark) < 0.1f;
            mark = value;
            markFrames = frames.size();
            return steady;
        }, 60'000);
    }

private slots:
    void initTestCase()
    {
        const QString profile = QStringLiteral("remote-display-hold-%1")
                                    .arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // The fault. The grid follower is on and noise floors move the grid
    // 2 dB at a time, down 8 dB and back. After the first accepted display
    // the window asks the Core nothing.
    void theGridFollowerMovingTheScaleAsksTheCoreNothing()
    {
        Fixture f(/*withCore=*/true);
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        QVERIFY(f.start());
        QVERIFY(f.run([&] { return f.contextFollowsLastAsk() && frames.size() >= 5; }));
        // The grid follower as the operator has it: the grid's minimum 5 dB
        // under the noise floor, the range kept.
        f.widget->setNFOffsetGridFollow(-5);
        f.widget->setMaintainNFAdjustDelta(true);
        f.widget->setAdjustGridMinToNoiseFloor(true);
        f.widget->testApplyNoiseFloor(-115.0f);
        QCOMPARE(f.widget->gridMin(), -120);
        f.plan();
        QVERIFY(f.run([&] { return f.contextFollowsLastAsk(); }));
        const int accepted = f.panAsks();

        int moves = 0;
        const auto move = [&](float floorDbm) {
            const int before = f.widget->gridMin();
            f.widget->testApplyNoiseFloor(floorDbm);
            QCOMPARE(f.widget->gridMin(), before + (floorDbm < float(before + 5) ? -2 : 2));
            ++moves;
            f.plan();
            // The Core answers whatever was asked before the next move.
            QVERIFY(f.run([&] { return f.contextFollowsLastAsk(); }));
        };
        for (float floor = -117.0f; floor >= -123.0f; floor -= 2.0f) { move(floor); }
        for (float floor = -121.0f; floor <= -115.0f; floor += 2.0f) { move(floor); }
        QCOMPARE(moves, 8);
        QCOMPARE(f.widget->gridMin(), -120);
        QVERIFY2(f.panAsks() == accepted,
                 qPrintable(QStringLiteral("%1 asks for 8 grid moves of 2 dB")
                                .arg(f.panAsks() - accepted)));
        f.finish();
    }

    // The hold's edges. A first ask carries what the pan needs plus the
    // headroom on whole dB; a move inside the hold asks nothing; a move
    // past it asks exactly once, for a range that holds what the pan needs,
    // and that range is then held.
    void aMovePastTheHoldAsksOnceAndTheNewRangeIsHeld()
    {
        Fixture f(/*withCore=*/false);
        QVERIFY(f.start());
        // The pan shows -140..-40 and the waterfall -130..-60.
        QCOMPARE(f.panAsks(), 1);
        QCOMPARE(f.lastAsk().minDbm, -150.0);
        QCOMPARE(f.lastAsk().maxDbm, -30.0);

        // Inside the hold: to the edge of it, each way.
        f.widget->setDbmRange(-150.0f, -50.0f);
        f.plan();
        f.widget->setDbmRange(-130.0f, -30.0f);
        f.plan();
        f.widget->setDbmRange(-133.5f, -47.25f);
        f.plan();
        QCOMPARE(f.panAsks(), 1);

        // Past it by a quarter of a dB: once, with headroom, on whole dB.
        f.widget->setDbmRange(-150.25f, -50.25f);
        f.plan();
        QCOMPARE(f.panAsks(), 2);
        QCOMPARE(f.lastAsk().minDbm, -161.0);
        QCOMPARE(f.lastAsk().maxDbm, -40.0);
        // Held: planning again, and moving inside it, asks nothing.
        f.plan();
        f.widget->setDbmRange(-158.0f, -58.0f);
        f.plan();
        f.widget->setDbmRange(-150.25f, -50.25f);
        f.plan();
        QCOMPARE(f.panAsks(), 2);

        // Far wider than the pan needs: an edge twice the headroom outside
        // it is kept, an edge more than that is narrowed, once.
        f.widget->setDbmRange(-141.0f, -50.0f);
        f.plan();
        QCOMPARE(f.panAsks(), 2);
        f.widget->setDbmRange(-140.5f, -50.0f);
        f.plan();
        QCOMPARE(f.panAsks(), 3);
        QCOMPARE(f.lastAsk().minDbm, -151.0);
        QCOMPARE(f.lastAsk().maxDbm, -40.0);
        f.plan();
        QCOMPARE(f.panAsks(), 3);
        // The waterfall's levels are part of what the pan needs: with the
        // scale drawn in to -120, a waterfall at -135 keeps the range.
        f.widget->setWfLowThreshold(-135.0f);
        f.widget->setDbmRange(-120.0f, -50.0f);
        f.plan();
        QCOMPARE(f.panAsks(), 3);

        // An operator's drag of the scale past the hold: nothing while it
        // moves (each step comes before the last has held a frame period),
        // one ask when it stops.
        for (int step = 1; step <= 40; ++step) {
            f.widget->setDbmRange(-120.0f - float(step), -50.0f - float(step));
            f.now += 10;
            f.window->planNowForTest();
        }
        QCOMPARE(f.panAsks(), 3);
        f.plan();
        QCOMPARE(f.panAsks(), 4);
        QCOMPARE(f.lastAsk().minDbm, -170.0);
        QCOMPARE(f.lastAsk().maxDbm, -50.0);
        f.plan();
        QCOMPARE(f.panAsks(), 4);

        // The link's limits stand: the range never leaves -400..100 dBm.
        f.widget->setWfHighThreshold(95.0f);
        f.plan();
        QCOMPARE(f.lastAsk().maxDbm, 100.0);
        f.finish();
    }

    // A long seeded walk of the scale and of the waterfall levels set at
    // run time. Every ask holds what the pan needs with no more than the
    // headroom (and the rounding) to spare, and no ask comes before an edge
    // of what the pan needs has travelled a headroom less the rounding from
    // where it stood at the ask before: the range cannot creep, grow without
    // bound or alternate.
    void aLongWalkNeverAsksMoreOftenThanItsExcursionAllows()
    {
        Fixture f(/*withCore=*/false);
        f.remote.setClarityEnabled(true);
        PanClarityRegistry registry(&f.remote);
        registry.registerPan(QStringLiteral("pan"), f.widget, nullptr);
        SliceModel* const slice = f.station.sliceById(f.sliceId);
        registry.bindRemote(QStringLiteral("pan"), f.widget, slice,
                            {f.stream, slice->streamEpoch(), f.centre, 192000}, 1, 1, 1);
        ClarityController* const clarity = registry.controllerForPan(QStringLiteral("pan"));
        QVERIFY(clarity);
        double levelLow = -125.0;
        double levelHigh = -65.0;
        emit clarity->waterfallThresholdsChanged(float(levelLow), float(levelHigh));
        QVERIFY(f.widget->clarityActive());
        QVERIFY(f.start());
        QCOMPARE(f.panAsks(), 1);

        double panLow = -140.0;
        double panHigh = -40.0;
        Range atLastAsk = neededBy(f.widget);
        int counted = f.panAsks();
        std::mt19937 random(0x4e53U);
        std::uniform_int_distribution<int> which(0, 3);
        // Quarter-dB steps of up to 3 dB with a slow drift, so the walk
        // both dithers inside the hold and leaves it.
        std::uniform_int_distribution<int> step(-12, 12);
        constexpr int kSteps = 2000;
        for (int index = 0; index < kSteps; ++index) {
            const double delta = step(random) * 0.25 + (index % 400 < 200 ? -0.05 : 0.05);
            switch (which(random)) {
            case 0: panLow = std::clamp(panLow + delta, -220.0, panHigh - 20.0); break;
            case 1: panHigh = std::clamp(panHigh + delta, panLow + 20.0, 20.0); break;
            case 2: levelLow = std::clamp(levelLow + delta, -220.0, levelHigh - 10.0); break;
            default: levelHigh = std::clamp(levelHigh + delta, levelLow + 10.0, 20.0); break;
            }
            f.widget->setDbmRange(float(panLow), float(panHigh));
            emit clarity->waterfallThresholdsChanged(float(levelLow), float(levelHigh));
            f.plan();
            const Range needed = neededBy(f.widget);
            const Ask held = f.lastAsk();
            // What is held always holds what the pan needs.
            QVERIFY2(held.minDbm <= needed.minDbm && held.maxDbm >= needed.maxDbm,
                     qPrintable(QStringLiteral("step %1: %2..%3 does not hold %4..%5")
                                    .arg(index).arg(held.minDbm).arg(held.maxDbm)
                                    .arg(needed.minDbm).arg(needed.maxDbm)));
            // And is never more than twice the headroom wider on a side.
            QVERIFY(held.minDbm >= needed.minDbm - 2.0 * kHeadroomDb);
            QVERIFY(held.maxDbm <= needed.maxDbm + 2.0 * kHeadroomDb);
            if (f.panAsks() == counted) { continue; }
            QCOMPARE(f.panAsks(), counted + 1);
            counted = f.panAsks();
            // A new range: whole dB, the headroom outside what is needed.
            QCOMPARE(held.minDbm, std::floor(needed.minDbm - kHeadroomDb));
            QCOMPARE(held.maxDbm, std::ceil(needed.maxDbm + kHeadroomDb));
            // An edge travelled at least the headroom less the rounding.
            const double travelled = std::max(std::abs(needed.minDbm - atLastAsk.minDbm),
                                              std::abs(needed.maxDbm - atLastAsk.maxDbm));
            QVERIFY2(travelled >= kHeadroomDb - 1.0,
                     qPrintable(QStringLiteral("step %1: asked after %2 dB").arg(index)
                                    .arg(travelled)));
            atLastAsk = needed;
        }
        // The walk left the hold many times, and dithered far more often.
        QVERIFY2(counted > 5 && counted < kSteps / 10,
                 qPrintable(QStringLiteral("%1 asks in %2 steps").arg(counted).arg(kSteps)));
        f.finish();
    }

    // The mini display's range follows the same rule: the grid follower's
    // 2 dB moves ask nothing, a move past the hold asks once.
    void theMiniDisplayHoldsItsRangeTheSameWay()
    {
        Fixture f(/*withCore=*/false);
        QVERIFY(f.start());
        f.window->setMiniDisplaySlices({f.sliceId});
        f.plan();
        QCOMPARE(f.miniAsks(), 1);
        QCOMPARE(f.lastAsk(true).minDbm, -150.0);
        QCOMPARE(f.lastAsk(true).maxDbm, -30.0);

        f.widget->setNFOffsetGridFollow(-5);
        f.widget->setMaintainNFAdjustDelta(true);
        f.widget->setAdjustGridMinToNoiseFloor(true);
        for (float floor = -137.0f; floor >= -143.0f; floor -= 2.0f) {
            f.widget->testApplyNoiseFloor(floor);
            f.plan();
        }
        for (float floor = -141.0f; floor <= -135.0f; floor += 2.0f) {
            f.widget->testApplyNoiseFloor(floor);
            f.plan();
        }
        QCOMPARE(f.widget->gridMin(), -140);
        QVERIFY2(f.miniAsks() == 1,
                 qPrintable(QStringLiteral("%1 mini asks for 8 grid moves of 2 dB")
                                .arg(f.miniAsks() - 1)));
        QCOMPARE(f.panAsks(), 1);

        f.widget->testApplyNoiseFloor(-150.0f);
        f.plan();
        QCOMPARE(f.widget->gridMin(), -155);
        QCOMPARE(f.miniAsks(), 2);
        QCOMPARE(f.lastAsk(true).minDbm, -165.0);
        QCOMPARE(f.lastAsk(true).maxDbm, -45.0);
        f.plan();
        QCOMPARE(f.miniAsks(), 2);
        f.finish();
    }

    // A context for the same picture (the dBm range alone changed): the
    // painted trace and the peak hold are there before its keyframe
    // arrives and the pan's noise floor estimate is untouched; frames of the
    // new context then draw. A context for a new picture (a zoom) empties
    // the peak hold, as it does today.
    void aSamePictureContextBlanksNothingAndANewPictureDoes()
    {
        Fixture f(/*withCore=*/true);
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        f.widget->setActivePeakHoldEnabled(true);
        QVERIFY(settlePicture(f, frames));
        QVERIFY(f.run([&] { return f.widget->activePeakHoldActive(); }));
        const int samples = f.widget->remoteCodecForTest().traceSamples;
        QVERIFY(samples > 0);
        QCOMPARE(int(f.widget->activePeakHoldPeaksForTest().size()), samples);

        // The Core's frames stop reaching the window: what the window shows
        // after the next context is what it kept, not a new keyframe.
        // Frames it had already received are still drawn at their times,
        // those of the old context at the frequency they were captured at
        // (R-R3-21), so the pixels may be those of any of them: what is
        // proved is that the trace, the peak hold and the noise floor
        // estimate are all still there.
        f.coreMedia->gateOpen = false;
        const QVector<float> painted = f.widget->renderedPixels();
        const quint32 generation = f.widget->remoteCodecForTest().contextGeneration;
        const int asked = f.panAsks();
        QVERIFY(!painted.isEmpty());

        // The dBm range moves past the hold: the same picture, asked again.
        f.widget->setDbmRange(f.widget->refLevel() - f.widget->dynamicRange() - 15.0f,
                              f.widget->refLevel() - 15.0f);
        f.plan();
        QCOMPARE(f.panAsks(), asked + 1);
        QVERIFY(f.run([&] { return f.contextFollowsLastAsk(); }));
        QVERIFY(f.widget->remoteCodecForTest().contextGeneration != generation);
        QVERIFY2(f.widget->remoteContextKeptPicture(),
                 "the window took a context for the same picture as a new picture");
        QCOMPARE(f.widget->renderedPixels().size(), painted.size());
        QVERIFY2(int(f.widget->activePeakHoldPeaksForTest().size()) == samples,
                 "the peak hold was emptied by a context for the same picture");
        QVERIFY(f.widget->activePeakHoldActive());

        // The new context's frames draw.
        f.coreMedia->gateOpen = true;
        const qsizetype before10 = frames.size();
        QVERIFY(f.run([&] { return frames.size() >= before10 + 10; }));
        QVERIFY(f.widget->activePeakHoldActive());

        // A new picture: the operator zooms in.
        f.coreMedia->gateOpen = false;
        const quint32 before = f.widget->remoteCodecForTest().contextGeneration;
        const int beforeAsks = f.panAsks();
        f.widget->setDisplayWindowPreservingHistory(f.centre, 48000);
        f.plan();
        QVERIFY(f.run([&] {
            f.plan();
            return f.panAsks() > beforeAsks && f.contextFollowsLastAsk()
                && f.widget->remoteCodecForTest().contextGeneration != before;
        }));
        QCOMPARE(int(f.widget->activePeakHoldPeaksForTest().size()), 0);
        QVERIFY(!f.widget->activePeakHoldActive());
        f.finish();
    }

    // The loop, closed. A grid move past the hold asks exactly once, and the
    // pan's noise floor estimate stays within 1 dB of where it was: the
    // averaged trace carried on at the Core, so there is nothing for the
    // grid follower to chase.
    void aMovePastTheHoldLeavesTheNoiseFloorWhereItWas()
    {
        Fixture f(/*withCore=*/true);
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        QVERIFY(settlePicture(f, frames));
        const float floorBefore = f.widget->nfLerpAverageForTest();
        const int asked = f.panAsks();

        f.widget->setDbmRange(f.widget->refLevel() - f.widget->dynamicRange() - 15.0f,
                              f.widget->refLevel() - 15.0f);
        f.plan();
        QCOMPARE(f.panAsks(), asked + 1);
        float lowest = floorBefore;
        float highest = floorBefore;
        qsizetype seen = frames.size();
        qsizetype sinceContext = -1;
        QVERIFY(f.run([&] {
            if (frames.size() != seen) {
                seen = frames.size();
                lowest = std::min(lowest, f.widget->nfLerpAverageForTest());
                highest = std::max(highest, f.widget->nfLerpAverageForTest());
            }
            if (sinceContext < 0 && f.contextFollowsLastAsk()) { sinceContext = frames.size(); }
            // Two seconds of frames of the new context.
            return sinceContext >= 0 && frames.size() - sinceContext >= 60;
        }, 60'000));
        f.plan();
        QCOMPARE(f.panAsks(), asked + 1);
        QVERIFY2(floorBefore - lowest <= 1.0f && highest - floorBefore <= 1.0f,
                 qPrintable(QStringLiteral("the noise floor estimate was %1 dBm and ranged "
                                           "%2..%3 dBm after the ask")
                                .arg(floorBefore).arg(lowest).arg(highest)));
        f.finish();
    }

    // R-DISP-RETUNE. The operator drags the pan 12 kHz: the same span, the
    // same sample counts, another center. The Core's averaged trace carried
    // on, so before the keyframe of the new context arrives the pan still
    // has a trace and its waterfall history, its noise floor estimate has
    // not moved and the noise floor is still "received"; the peak hold,
    // held per pixel, is empty. A zoom is still a new picture: it empties
    // the peak hold as well and the noise floor is awaited again.
    void aMovedPictureCarriesOnAndEmptiesOnlyThePeakHold()
    {
        Fixture f(/*withCore=*/true);
        PanClarityRegistry registry(&f.remote);
        registry.registerPan(QStringLiteral("pan"), f.widget, nullptr);
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        f.widget->setActivePeakHoldEnabled(true);
        QVERIFY(settlePicture(f, frames));
        QVERIFY(f.run([&] { return f.widget->activePeakHoldActive(); }));
        const int samples = f.widget->remoteCodecForTest().traceSamples;
        QVERIFY(samples > 0);

        // The window logs the Core's noise floor once when it first arrives.
        g_noiseFloorReceivedLines = 0;
        g_previousHandler = qInstallMessageHandler(countNoiseFloorReceived);
        const auto restore = qScopeGuard([] { qInstallMessageHandler(g_previousHandler); });
        f.window->setClarityRegistry(&registry);
        QVERIFY(f.run([&] { return g_noiseFloorReceivedLines == 1; }));
        f.widget->tickWaterfallForTest();
        QVERIFY(f.widget->waterfallHistoryRowsForTest() > 0);

        // The Core's frames stop reaching the window: what it shows after
        // the context is what it kept, not a keyframe.
        f.coreMedia->gateOpen = false;
        const float floorBefore = f.widget->nfLerpAverageForTest();
        const int historyBefore = f.widget->waterfallHistoryRowsForTest();
        const quint32 generation = f.widget->remoteCodecForTest().contextGeneration;
        const int asked = f.panAsks();
        QVERIFY(!f.widget->renderedPixels().isEmpty());

        // 12 kHz is a whole number of bins at any FFT size, so the Core
        // grants the same sample counts.
        f.widget->setDisplayWindowPreservingHistory(f.centre + 12000.0, 96000);
        QVERIFY(f.run([&] {
            f.plan();
            return f.panAsks() > asked
                && f.widget->remoteCodecForTest().contextGeneration != generation
                && f.widget->remoteCodecForTest().traceSamples != 0;
        }));
        QVERIFY(!f.widget->remoteContextKeptPicture());
        QCOMPARE(int(f.widget->remoteCodecForTest().traceSamples), samples);
        QVERIFY2(f.widget->renderedPixels().size() == samples,
                 "the trace was blanked by a context for the picture moved");
        QVERIFY2(f.widget->waterfallHistoryRowsForTest() >= historyBefore,
                 "the waterfall history was dropped by a context for the picture moved");
        QCOMPARE(int(f.widget->activePeakHoldPeaksForTest().size()), 0);
        QVERIFY(!f.widget->activePeakHoldActive());
        QVERIFY2(std::abs(f.widget->nfLerpAverageForTest() - floorBefore) <= 1.0f,
                 qPrintable(QStringLiteral("the noise floor estimate went from %1 to %2 dBm")
                                .arg(floorBefore).arg(f.widget->nfLerpAverageForTest())));

        // The new context's frames draw, the estimate stays where it was and
        // the noise floor was never awaited again.
        f.coreMedia->gateOpen = true;
        float lowest = floorBefore;
        float highest = floorBefore;
        const qsizetype mark = frames.size();
        qsizetype seen = mark;
        QVERIFY(f.run([&] {
            if (frames.size() != seen) {
                seen = frames.size();
                lowest = std::min(lowest, f.widget->nfLerpAverageForTest());
                highest = std::max(highest, f.widget->nfLerpAverageForTest());
            }
            return frames.size() >= mark + 60;
        }, 60'000));
        QVERIFY2(floorBefore - lowest <= 1.0f && highest - floorBefore <= 1.0f,
                 qPrintable(QStringLiteral("the noise floor estimate was %1 dBm and ranged "
                                           "%2..%3 dBm after the move")
                                .arg(floorBefore).arg(lowest).arg(highest)));
        QVERIFY2(g_noiseFloorReceivedLines == 1,
                 "the noise floor was awaited again after a context for the picture moved");
        QCOMPARE(f.widget->remoteContextPicture(),
                 SpectrumWidget::RemoteContextPicture::Moved);
        QVERIFY(f.run([&] { return f.widget->activePeakHoldActive(); }));

        // A new picture: the operator zooms in.
        const quint32 before = f.widget->remoteCodecForTest().contextGeneration;
        const int beforeAsks = f.panAsks();
        f.widget->setDisplayWindowPreservingHistory(f.centre + 12000.0, 48000);
        QVERIFY(f.run([&] {
            f.plan();
            return f.panAsks() > beforeAsks
                && f.widget->remoteCodecForTest().contextGeneration != before
                && f.widget->remoteCodecForTest().traceSamples != 0
                && g_noiseFloorReceivedLines == 2;
        }));
        QCOMPARE(f.widget->remoteContextPicture(),
                 SpectrumWidget::RemoteContextPicture::New);
        f.finish();
    }

    // R-DISP-RETUNE. The mini display is centered on its slice, so every
    // tuning step asks for its picture moved. It keeps its painted frame
    // until the frames of the new center arrive; taking the mini display
    // away still blanks it.
    void theMiniDisplayKeepsItsFrameThroughATuningStep()
    {
        Fixture f(/*withCore=*/true);
        QVERIFY(f.start());
        QSignalSpy miniFrames(f.window.get(), &RemoteMediaController::miniDisplayFrame);
        QSignalSpy unavailable(f.window.get(), &RemoteMediaController::miniDisplayUnavailable);
        f.window->setMiniDisplaySlices({f.sliceId});
        QVERIFY(f.run([&] { f.plan(); return miniFrames.size() >= 5; }));
        const double shownCenter = miniFrames.last().at(3).toDouble();
        const int miniAsked = f.miniAsks();
        unavailable.clear();

        SliceModel* const slice = f.remote.sliceById(f.sliceId);
        QVERIFY(slice);
        slice->setFrequency(slice->frequency() + 3000.0);
        QVERIFY(f.run([&] {
            f.plan();
            return f.miniAsks() > miniAsked && !miniFrames.isEmpty()
                && miniFrames.last().at(3).toDouble() != shownCenter;
        }));
        QVERIFY2(unavailable.isEmpty(),
                 qPrintable(QStringLiteral("the mini display was blanked %1 times by a "
                                           "tuning step").arg(unavailable.size())));

        f.window->setMiniDisplaySlices({});
        QTRY_VERIFY(!unavailable.isEmpty());
        f.finish();
    }

    // R-DISP-RETUNE. A tuning wheel over a link: each step is made before
    // the Core's context for the step before it has arrived. The window
    // ignores a context whose request has since changed and asks again, so
    // the ask for the second step, and for the third, is made while the
    // first is still unanswered. The mini display keeps its painted frame
    // through all of them. A step that is not a move of the same picture
    // (here another detector) made in that same state still blanks it.
    void theMiniDisplayKeepsItsFrameThroughStepsFasterThanTheLink()
    {
        Fixture f(/*withCore=*/true);
        QVERIFY(f.start());
        QSignalSpy miniFrames(f.window.get(), &RemoteMediaController::miniDisplayFrame);
        QSignalSpy unavailable(f.window.get(), &RemoteMediaController::miniDisplayUnavailable);
        f.window->setMiniDisplaySlices({f.sliceId});
        QVERIFY(f.run([&] { f.plan(); return miniFrames.size() >= 5; }));
        unavailable.clear();
        SliceModel* const slice = f.remote.sliceById(f.sliceId);
        QVERIFY(slice);
        const double start = slice->frequency();
        // 3 kHz is a whole number of bins at any FFT size.
        constexpr double kStepHz = 3000.0;

        // Step one. Its ask leaves; what the Core answers waits on the link.
        f.stationLink->setHoldsOutgoing(true);
        int asked = f.miniAsks();
        slice->setFrequency(start + kStepHz);
        f.plan();
        QCOMPARE(f.miniAsks(), asked + 1);
        for (int turn = 0; turn < 10; ++turn) { f.feed(); QTest::qWait(10); }

        // Step two, before the context of step one. The context then
        // arrives, is ignored for the request that has changed, and the
        // window asks for step two; the answer to that waits as well.
        slice->setFrequency(start + 2.0 * kStepHz);
        f.onMiniAsk = [&f] { f.stationLink->setHoldsOutgoing(true); };
        f.stationLink->setHoldsOutgoing(false);
        QVERIFY(f.run([&] { f.plan(); return f.miniAsks() == asked + 2; }));
        f.onMiniAsk = nullptr;
        for (int turn = 0; turn < 10; ++turn) { f.feed(); QTest::qWait(10); }
        QVERIFY2(unavailable.isEmpty(),
                 "the mini display was blanked by a second step made before the "
                 "first step's context");

        // Step three, before the context of step two.
        slice->setFrequency(start + 3.0 * kStepHz);
        f.stationLink->setHoldsOutgoing(false);
        const double binTolerance = 50.0;
        QVERIFY(f.run([&] {
            f.plan();
            return f.miniAsks() >= asked + 3 && !miniFrames.isEmpty()
                && std::abs(miniFrames.last().at(3).toDouble()
                            - (start + 3.0 * kStepHz)) < binTolerance;
        }));
        QCOMPARE(f.miniAsks(), asked + 3);
        QVERIFY2(unavailable.isEmpty(),
                 qPrintable(QStringLiteral("the mini display was blanked %1 times by three "
                                           "steps faster than the link")
                                .arg(unavailable.size())));

        // The same state once more: a step whose answer waits on the link.
        f.stationLink->setHoldsOutgoing(true);
        asked = f.miniAsks();
        slice->setFrequency(start + 4.0 * kStepHz);
        f.plan();
        QCOMPARE(f.miniAsks(), asked + 1);
        for (int turn = 0; turn < 10; ++turn) { f.feed(); QTest::qWait(10); }
        QVERIFY(unavailable.isEmpty());
        // Now a new picture is asked. The window predicts nothing at the
        // ask: the mini display is blanked when the Core's context says the
        // picture is a new one.
        f.widget->setSpectrumDetector(SpectrumDetector::Average);
        f.stationLink->setHoldsOutgoing(false);
        QVERIFY(f.run([&] { f.plan(); return f.miniAsks() >= asked + 2; }));
        QVERIFY2(f.run([&] { f.plan(); return !unavailable.isEmpty(); }),
                 "a new picture asked while a moved one was awaited did not blank "
                 "the mini display");
        f.finish();
    }

    // R-DISP-RETUNE. The mini display's 20 kHz is no whole number of FFT
    // bins, and a tuning step that is no whole number of bins either moves
    // it over them, so the Core's crop is a bin wider at one step and a bin
    // narrower at another. It asked for the same 20 kHz each time: the same
    // picture moved, and it keeps its painted frame.
    void theMiniDisplayKeepsItsFrameWhenTheCropChangesByABin_data()
    {
        QTest::addColumn<QString>("panHzPerBin");
        // The pan at exactly its stored size (Sizing: Fixed), so it stays on
        // the receiver's wide engine whatever its span. The mini display has
        // the fine engine to itself, at the size it asks, and its 20 kHz
        // holds more bins than its 1 024 points.
        QTest::newRow("moreBinsThanPoints") << QString();
        // The pan on the fine engine at 8 192, which the mini display then
        // shares. Its 20 kHz is 853.33 bins there, fewer than the points it
        // asks for, so its points are its bins: the same at every step,
        // though the bins its crop touches are not.
        QTest::newRow("fewerBinsThanPoints") << QStringLiteral("23.4375");
    }

    void theMiniDisplayKeepsItsFrameWhenTheCropChangesByABin()
    {
        QFETCH(QString, panHzPerBin);
        constexpr int kMiniPointsAsked = 1024;
        const QString hzPerBinKey = QLatin1String(ControlRanges::kDisplayHzPerBinTargetKey);
        const QString sizingModeKey = QLatin1String(ControlRanges::kDisplayFftSizingModeKey);
        const auto restore = qScopeGuard([&] {
            AppSettings::instance().remove(hzPerBinKey);
            AppSettings::instance().remove(sizingModeKey);
        });
        if (panHzPerBin.isEmpty()) {
            // In Auto, the default, a pan plans its size from its span and
            // can take the fine engine itself, which the mini display would
            // then share at that size.
            AppSettings::instance().setValue(
                sizingModeKey, QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
        } else {
            AppSettings::instance().setValue(hzPerBinKey, panHzPerBin);
        }
        Fixture f(/*withCore=*/true);
        QVERIFY(f.start());
        QSignalSpy miniFrames(f.window.get(), &RemoteMediaController::miniDisplayFrame);
        QSignalSpy unavailable(f.window.get(), &RemoteMediaController::miniDisplayUnavailable);
        f.window->setMiniDisplaySlices({f.sliceId});
        QVERIFY(f.run([&] { f.plan(); return miniFrames.size() >= 5; }));
        unavailable.clear();
        // The row is the case it names.
        const qsizetype points = miniFrames.last().at(1).value<QVector<float>>().size();
        QVERIFY2(panHzPerBin.isEmpty() ? points == kMiniPointsAsked
                                       : points > 0 && points < kMiniPointsAsked,
                 qPrintable(QString::number(points)));
        SliceModel* const slice = f.remote.sliceById(f.sliceId);
        QVERIFY(slice);

        int changedByOneBin = 0;
        QStringList seen;
        for (int step = 0; step < 8; ++step) {
            const double spanBefore = miniFrames.last().at(4).toDouble();
            const int asked = f.miniAsks();
            // 3 005 Hz is no whole number of bins at any FFT size in use.
            const double target = slice->frequency() + 3005.0;
            slice->setFrequency(target);
            QVERIFY(f.run([&] {
                f.plan();
                return f.miniAsks() > asked && !miniFrames.isEmpty()
                    && std::abs(miniFrames.last().at(3).toDouble() - target) < 50.0;
            }));
            const double change = miniFrames.last().at(4).toDouble() - spanBefore;
            seen.append(QString::number(change, 'f', 4));
            if (change != 0.0) {
                // One bin: the receiver's sample rate over a power of two.
                const double sizes = std::log2(192000.0 / std::abs(change));
                QVERIFY2(std::abs(sizes - std::round(sizes)) < 1.0e-9,
                         qPrintable(seen.join(QStringLiteral(", "))));
                ++changedByOneBin;
            }
            QVERIFY2(unavailable.isEmpty(),
                     qPrintable(QStringLiteral("step %1 blanked the mini display (its span "
                                               "changed by, in Hz: %2)")
                                    .arg(step).arg(seen.join(QStringLiteral(", ")))));
            QCOMPARE(miniFrames.last().at(1).value<QVector<float>>().size(), points);
        }
        // The case under test did happen: the Core's bin count changed.
        QVERIFY2(changedByOneBin >= 1, qPrintable(seen.join(QStringLiteral(", "))));
        f.finish();
    }

    // R-DISP-RETUNE. The operator drags the pan as a hand does: by an amount
    // that is no whole number of FFT bins, keeping the width the pan has.
    // The Core's crop is cut at bin edges, so the span it answers with
    // changes by a bin as the view's alignment to the bins changes. That is
    // still the same picture moved: the Core carried its averaged trace on,
    // and the window says so. A change of width is a new picture.
    void aPanDraggedByAFractionOfABinIsAMovedPicture()
    {
        Fixture f(/*withCore=*/true);
        QVERIFY(f.start());
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        QVERIFY(f.run([&] { return f.contextFollowsLastAsk() && frames.size() >= 5; }));
        const double binHz = f.widget->binWidthHz();
        QVERIFY(binHz > 0.0);

        int moved = 0;
        int changedByOneBin = 0;
        QStringList seen;
        for (int drag = 0; drag < 6; ++drag) {
            const quint32 generation = f.widget->remoteCodecForTest().contextGeneration;
            const int asked = f.panAsks();
            const double spanBefore = f.widget->bandwidth();
            // 12 010 Hz is 256.2 bins of 46.875 Hz and no whole number of
            // any finer bin either. There and back, so the view stays
            // inside what the receiver covers.
            const double dragHz = drag % 2 == 0 ? 12010.0 : -12010.0;
            f.widget->setDisplayWindowPreservingHistory(
                f.widget->centerFrequency() + dragHz, f.widget->bandwidth());
            QVERIFY(f.run([&] {
                f.plan();
                return f.panAsks() > asked
                    && f.widget->remoteCodecForTest().contextGeneration != generation
                    && f.widget->remoteCodecForTest().traceSamples != 0
                    && f.contextFollowsLastAsk();
            }));
            const double change = f.widget->bandwidth() - spanBefore;
            seen.append(QStringLiteral("%1 bins").arg(change / binHz, 0, 'f', 3));
            if (std::abs(std::abs(change) - binHz) < 1.0e-6) { ++changedByOneBin; }
            QVERIFY2(std::abs(change) < 1.5 * binHz,
                     qPrintable(QStringLiteral("drag %1 changed the pan's width by %2 Hz "
                                               "(spans changed by: %3)")
                                    .arg(drag).arg(change).arg(seen.join(QStringLiteral(", ")))));
            if (f.widget->remoteContextPicture()
                == SpectrumWidget::RemoteContextPicture::Moved) { ++moved; }
        }
        // The case under test did happen: the Core's bin count changed by one.
        QVERIFY2(changedByOneBin >= 1, qPrintable(seen.join(QStringLiteral(", "))));
        QVERIFY2(moved == 6,
                 qPrintable(QStringLiteral("%1 of 6 drags were a moved picture (spans "
                                           "changed by: %2)")
                                .arg(moved).arg(seen.join(QStringLiteral(", ")))));
        // The pan is as wide as it was asked to be, to within the bin edges.
        QVERIFY2(std::abs(f.widget->bandwidth() - 96000.0) <= 2.0 * binHz,
                 qPrintable(QStringLiteral("the pan is %1 Hz wide after six drags")
                                .arg(f.widget->bandwidth(), 0, 'f', 3)));

        // A real change of width, three bins: a new picture.
        const quint32 generation = f.widget->remoteCodecForTest().contextGeneration;
        const int asked = f.panAsks();
        f.widget->setDisplayWindowPreservingHistory(
            f.widget->centerFrequency(), f.widget->bandwidth() - 3.0 * binHz);
        QVERIFY(f.run([&] {
            f.plan();
            return f.panAsks() > asked
                && f.widget->remoteCodecForTest().contextGeneration != generation
                && f.widget->remoteCodecForTest().traceSamples != 0
                && f.contextFollowsLastAsk();
        }));
        QCOMPARE(f.widget->remoteContextPicture(),
                 SpectrumWidget::RemoteContextPicture::New);
        f.finish();
    }

    // R-DISP-RETUNE (third wave). What the main pan draws while it is moved
    // faster than the link answers (a tuning wheel with the pan following
    // its receiver): thirty steps of 1 kHz, each made before the Core's
    // context for the step before it has arrived.
    //
    // The link delays the Core's frames as it delays its answers: a frame
    // the Core sent before it heard a step reaches the window after the
    // context of the step before, as it does over a real link. (With the
    // frames undelayed the Core is always one context ahead of the window
    // and no frame can ever be one of a context the window knows.)
    //
    // A step is not over until the Core has sent a frame of the context it
    // made for it, so every step after the first delivers one of the step
    // before, on any machine (kSpinFrameWaitMs).
    //
    // The Core states the picture: every context of the spin continues the
    // picture the pan shows, so the window takes each for drawing though its
    // request has since changed, and draws its frames at the frequencies
    // they were captured at. The carrier in them stays at its frequency on
    // the moving pan. None of that is an accepted answer: the pan's own
    // context is the one from before the spin until the wheel stops, and
    // the window asks once a step, as it did.
    //
    // The Core states no picture: the window can draw none of them (the rule
    // before this one) and the context that ends the spin is a new picture.
    //
    // Every step a new picture (the width changes with it): none of those
    // contexts continues the picture shown, and none is drawn.
    void whatAPanDrawsWhileItIsMovedFasterThanTheLinkAnswers_data()
    {
        QTest::addColumn<bool>("statesPicture");
        QTest::addColumn<bool>("newPictureEachStep");
        QTest::newRow("the Core states the picture") << true << false;
        QTest::newRow("the Core states no picture") << false << false;
        QTest::newRow("every step is a new picture") << true << true;
    }

    void whatAPanDrawsWhileItIsMovedFasterThanTheLinkAnswers()
    {
        QFETCH(bool, statesPicture);
        QFETCH(bool, newPictureEachStep);
        // The pan at exactly its stored size (Sizing: Fixed), 4 096 on the
        // receiver's wide engine, as the floors were measured: the Core
        // there makes a frame from every 4 096 samples fed. In Auto, the
        // default, this pan takes the fine engine at 8 192, which makes a
        // frame from every 6 400 (30 a second of the receiver's time), so
        // the same feed gives about two thirds of the frames a step.
        const QString sizingModeKey = QLatin1String(ControlRanges::kDisplayFftSizingModeKey);
        const auto restore = qScopeGuard([&] { AppSettings::instance().remove(sizingModeKey); });
        AppSettings::instance().setValue(
            sizingModeKey, QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
        Fixture f(/*withCore=*/true, statesPicture);
        f.toneOffsetHz = 20000.0;
        QVERIFY(f.start());
        QCOMPARE(f.client.capabilities().spectrumPictureVersion, statesPicture ? 1 : 0);
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        QSignalSpy drawn(f.window.get(), &RemoteMediaController::displayFrameReceived);
        QVERIFY(f.run([&] { return f.contextFollowsLastAsk() && frames.size() >= 30; }));
        const auto carrierHz = [&f] {
            return strongestHz(f.widget->renderedPixels(), f.widget->centerFrequency(),
                               f.widget->bandwidth());
        };
        const std::optional<double> carrierAtStart = carrierHz();
        QVERIFY(carrierAtStart);
        const double startCenter = f.widget->centerFrequency();
        const double width = f.widget->bandwidth();
        const double pixelHz = width / f.widget->renderedPixels().size();
        QVERIFY2(std::abs(std::abs(*carrierAtStart - f.centre) - 20000.0) <= 2.0 * pixelHz,
                 qPrintable(QStringLiteral("the carrier is drawn %1 Hz from the receiver's "
                                           "center").arg(*carrierAtStart - f.centre)));
        // Toward the carrier, so it stays in the pan.
        const double direction = *carrierAtStart >= startCenter ? 1.0 : -1.0;
        constexpr double kStepHz = 1000.0;
        constexpr int kSteps = 30;

        const quint32 generationBefore = f.widget->remoteCodecForTest().contextGeneration;
        const int askedBefore = f.panAsks();
        const qsizetype sentBefore = f.coreMedia->sent.size();
        const qsizetype drawnBefore = drawn.size();
        // The pan is the only display here: every frame is its endpoint's.
        QVERIFY(!f.coreMedia->sent.isEmpty());
        const quint32 panEndpoint = f.coreMedia->sent.last().endpoint;
        const bool drawsTheSpin = statesPicture && !newPictureEachStep;
        int contextsAccepted = 0;
        int stepsBlank = 0;
        int stepsWithAFrame = 0;
        int stepsGivenAFrameOfTheSpin = 0;
        qsizetype deliveredOfTheOldContext = 0;
        qsizetype deliveredOfTheSpin = 0;
        double worstCarrierPixels = 0.0;

        // The link from here on: what the Core answers waits from each ask,
        // and so do its frames.
        f.coreMedia->holding = true;
        f.onPanAsk = [&f] {
            f.stationLink->setHoldsOutgoing(true);
            f.coreMedia->mark();
        };
        for (int step = 1; step <= kSteps; ++step) {
            // The context of the Core's newest frame: the one it made for
            // the step before.
            const std::optional<quint32> newestAtStep = f.coreMedia->newestSent(panEndpoint);
            const double stepWidth = newPictureEachStep && step % 2 == 1 ? width - 600.0 : width;
            f.widget->setDisplayWindowPreservingHistory(
                startCenter + direction * step * kStepHz, stepWidth);
            // The answer to the step before arrives now, after this step,
            // and the window asks for this one.
            f.stationLink->setHoldsOutgoing(false);
            QVERIFY2(f.run([&] { f.plan(); return f.panAsks() >= askedBefore + step; }, 10'000),
                     qPrintable(QStringLiteral("step %1 was not asked").arg(step)));
            // Then the frames the Core sent before it heard of this step.
            const qsizetype drawnAtStep = drawn.size();
            qsizetype givenOfTheSpin = 0;
            for (const DisplayTransport::Frame& frame : f.coreMedia->releaseMarked()) {
                if (frame.generation == generationBefore) {
                    ++deliveredOfTheOldContext;
                } else {
                    ++deliveredOfTheSpin;
                    ++givenOfTheSpin;
                }
            }
            for (int turn = 0; turn < 8; ++turn) { f.feed(); QTest::qWait(10); }
            if (givenOfTheSpin > 0) {
                ++stepsGivenAFrameOfTheSpin;
                // A frame of a context that continues the picture is drawn,
                // however long this machine takes over it.
                if (drawsTheSpin) {
                    QVERIFY2(f.run([&] { return drawn.size() != drawnAtStep; }, kSpinFrameWaitMs),
                             qPrintable(QStringLiteral("step %1 was given %2 frame(s) of the "
                                                       "spin's contexts and drew none")
                                            .arg(step).arg(givenOfTheSpin)));
                }
            }
            // The step ends when the Core has sent a frame of the context it
            // made for this step: the next step has one to deliver, however
            // few frames the Core makes on this machine.
            QVERIFY2(f.run([&] { return f.coreMedia->newestSent(panEndpoint) != newestAtStep; },
                           kSpinFrameWaitMs),
                     qPrintable(QStringLiteral("the Core sent no frame of the context for step %1")
                                    .arg(step)));
            if (drawn.size() != drawnAtStep) { ++stepsWithAFrame; }
            if (f.widget->remoteCodecForTest().contextGeneration != generationBefore) {
                ++contextsAccepted;
            }
            const std::optional<double> carrier = carrierHz();
            if (!carrier) {
                ++stepsBlank;
            } else {
                worstCarrierPixels = std::max(worstCarrierPixels,
                    std::abs(*carrier - *carrierAtStart)
                        / (f.widget->bandwidth() / f.widget->renderedPixels().size()));
            }
        }
        const int asksInSpin = f.panAsks() - askedBefore;
        const qsizetype drawnInSpin = drawn.size() - drawnBefore;
        qsizetype sentInSpin = 0;
        for (qsizetype i = sentBefore; i < f.coreMedia->sent.size(); ++i) {
            if (f.coreMedia->sent.at(i).generation != generationBefore) { ++sentInSpin; }
        }
        // Frames of the context from before the spin are drawn as they were
        // before it; what is drawn beyond them is of the spin's contexts.
        const qsizetype drawnOfTheSpin = std::max<qsizetype>(0, drawnInSpin - deliveredOfTheOldContext);

        // The wheel stops: the last answer arrives and the pan catches up.
        f.onPanAsk = nullptr;
        f.stationLink->setHoldsOutgoing(false);
        f.coreMedia->releaseAll();
        const qsizetype framesAtStop = frames.size();
        QVERIFY(f.run([&] {
            f.plan();
            return f.widget->remoteCodecForTest().contextGeneration != generationBefore
                && f.contextFollowsLastAsk() && frames.size() > framesAtStop;
        }));
        const SpectrumWidget::RemoteContextPicture ending = f.widget->remoteContextPicture();
        qInfo().noquote() << QStringLiteral(
            "R-DISP-RETUNE fast spin (%1): %2 steps of %3 Hz, %4 asks, %5 contexts accepted "
            "during the spin; the Core sent %6 frames of the spin's contexts, %7 reached the "
            "window before the wheel stopped and at least %8 were drawn (%9 frames drawn in "
            "all, %10 delivered of the context from before the spin); %11 steps drew a frame, "
            "%12 steps showed a blank pan; the carrier was drawn at most %13 pixels from its "
            "frequency; the pan ended on a %14 picture")
            .arg(QString::fromUtf8(QTest::currentDataTag()))
            .arg(kSteps).arg(kStepHz).arg(asksInSpin).arg(contextsAccepted)
            .arg(sentInSpin).arg(deliveredOfTheSpin).arg(drawnOfTheSpin).arg(drawnInSpin)
            .arg(deliveredOfTheOldContext).arg(stepsWithAFrame).arg(stepsBlank)
            .arg(worstCarrierPixels, 0, 'f', 2)
            .arg(ending == SpectrumWidget::RemoteContextPicture::Moved ? QStringLiteral("moved")
                 : ending == SpectrumWidget::RemoteContextPicture::Same ? QStringLiteral("same")
                 : QStringLiteral("new"));
        QCOMPARE(asksInSpin, kSteps);
        QCOMPARE(contextsAccepted, 0);
        QCOMPARE(stepsBlank, 0);
        QVERIFY2(worstCarrierPixels <= 2.0,
                 qPrintable(QStringLiteral("the carrier was drawn %1 pixels from its frequency")
                                .arg(worstCarrierPixels, 0, 'f', 2)));
        // Every step after the first delivered a frame of the context the
        // Core made for the step before it.
        QCOMPARE(stepsGivenAFrameOfTheSpin, kSteps - 1);
        if (drawsTheSpin) {
            // Each of those steps drew one (verified step by step above).
            QVERIFY2(stepsWithAFrame >= stepsGivenAFrameOfTheSpin,
                     qPrintable(QStringLiteral("%1 steps drew a frame of the %2 that were given "
                                               "one of the spin's contexts")
                                    .arg(stepsWithAFrame).arg(stepsGivenAFrameOfTheSpin)));
            QCOMPARE(ending, SpectrumWidget::RemoteContextPicture::Moved);
        } else {
            QVERIFY2(drawnInSpin <= deliveredOfTheOldContext,
                     qPrintable(QStringLiteral("%1 frames were drawn and %2 were of the "
                                               "context the pan had")
                                    .arg(drawnInSpin).arg(deliveredOfTheOldContext)));
            QCOMPARE(ending, SpectrumWidget::RemoteContextPicture::New);
        }
        f.finish();
    }

    // R-DISP-RETUNE (third wave). The same for the mini display, which is
    // centered on its slice: sixteen tuning steps of 500 Hz, each made before
    // the context for the step before it has arrived, over the link that
    // delays frames as it delays answers.
    //
    // The Core states the picture: the mini display is never blanked, and it
    // is given the frames of each context at that context's own center and
    // span, the carrier in them at its frequency. The Core states no
    // picture: it is blanked at every ask and given no frame until the
    // wheel stops.
    void whatAMiniDisplayIsGivenWhileItsSliceIsTunedFasterThanTheLinkAnswers_data()
    {
        QTest::addColumn<bool>("statesPicture");
        QTest::newRow("the Core states the picture") << true;
        QTest::newRow("the Core states no picture") << false;
    }

    void whatAMiniDisplayIsGivenWhileItsSliceIsTunedFasterThanTheLinkAnswers()
    {
        QFETCH(bool, statesPicture);
        // Sizing: Fixed, as the pan's fast spin above and for its reason:
        // the pan stays on the wide engine and the mini display has the fine
        // engine to itself, as the floors were measured. In Auto it shares
        // the pan's fine engine at 8 192 and is given fewer frames a step.
        const QString sizingModeKey = QLatin1String(ControlRanges::kDisplayFftSizingModeKey);
        const auto restore = qScopeGuard([&] { AppSettings::instance().remove(sizingModeKey); });
        AppSettings::instance().setValue(
            sizingModeKey, QLatin1String(ControlRanges::kDisplayFftSizingModeFixedToken));
        Fixture f(/*withCore=*/true, statesPicture);
        f.toneOffsetHz = 6000.0;
        QVERIFY(f.start());
        QSignalSpy miniFrames(f.window.get(), &RemoteMediaController::miniDisplayFrame);
        QSignalSpy unavailable(f.window.get(), &RemoteMediaController::miniDisplayUnavailable);
        // The pan's frames come first: the mini display's endpoint is the
        // other one.
        QVERIFY(f.run([&f] { return !f.coreMedia->sent.isEmpty(); }));
        const quint32 panEndpoint = f.coreMedia->sent.last().endpoint;
        f.window->setMiniDisplaySlices({f.sliceId});
        QVERIFY(f.run([&] { f.plan(); return miniFrames.size() >= 30; }));
        std::optional<quint32> miniEndpoint;
        for (const DisplayTransport::Frame& frame : f.coreMedia->sent) {
            if (frame.endpoint != panEndpoint) { miniEndpoint = frame.endpoint; }
        }
        QVERIFY(miniEndpoint);
        SliceModel* const slice = f.remote.sliceById(f.sliceId);
        QVERIFY(slice);
        const double start = slice->frequency();
        const auto carrierOf = [](const QList<QVariant>& frame) {
            return strongestHz(frame.at(1).value<QVector<float>>(), frame.at(3).toDouble(),
                               frame.at(4).toDouble());
        };
        const auto sampleHzOf = [](const QList<QVariant>& frame) {
            return frame.at(4).toDouble() / frame.at(1).value<QVector<float>>().size();
        };
        const std::optional<double> carrierAtStart = carrierOf(miniFrames.last());
        QVERIFY(carrierAtStart);
        QVERIFY2(std::abs(std::abs(*carrierAtStart - f.centre) - 6000.0)
                     <= 2.0 * sampleHzOf(miniFrames.last()),
                 qPrintable(QStringLiteral("the carrier is drawn %1 Hz from the receiver's "
                                           "center").arg(*carrierAtStart - f.centre)));
        const double direction = *carrierAtStart >= start ? 1.0 : -1.0;
        const double centerBefore = miniFrames.last().at(3).toDouble();
        constexpr double kStepHz = 500.0;
        constexpr int kSteps = 16;
        // The Core's crop is cut at bin edges: a center is a step's to within
        // a bin.
        constexpr double kBinToleranceHz = 50.0;

        unavailable.clear();
        const int askedBefore = f.miniAsks();
        const qsizetype sentBefore = f.coreMedia->sent.size();
        const std::optional<quint32> generationBefore = f.coreMedia->newestSent(*miniEndpoint);
        int framesAtAnEarlierStep = 0;
        int framesElsewhere = 0;
        int stepsWithAFrame = 0;
        int stepsGivenAFrameOfTheSpin = 0;
        double worstCarrierSamples = 0.0;
        f.coreMedia->holding = true;
        f.onMiniAsk = [&f] {
            f.stationLink->setHoldsOutgoing(true);
            f.coreMedia->mark();
        };
        for (int step = 1; step <= kSteps; ++step) {
            // The context of the Core's newest frame for the mini display:
            // the one it made for the step before.
            const std::optional<quint32> newestAtStep = f.coreMedia->newestSent(*miniEndpoint);
            slice->setFrequency(start + direction * step * kStepHz);
            f.stationLink->setHoldsOutgoing(false);
            QVERIFY2(f.run([&] { f.plan(); return f.miniAsks() >= askedBefore + step; }, 10'000),
                     qPrintable(QStringLiteral("step %1 was not asked").arg(step)));
            const qsizetype framesAtStep = miniFrames.size();
            qsizetype givenOfTheSpin = 0;
            for (const DisplayTransport::Frame& frame : f.coreMedia->releaseMarked()) {
                if (frame.endpoint == *miniEndpoint && frame.generation != generationBefore) {
                    ++givenOfTheSpin;
                }
            }
            for (int turn = 0; turn < 8; ++turn) { f.feed(); QTest::qWait(10); }
            if (givenOfTheSpin > 0) {
                ++stepsGivenAFrameOfTheSpin;
                // A frame of a context that continues the picture is given
                // to the mini display, however long this machine takes.
                if (statesPicture) {
                    QVERIFY2(f.run([&] { return miniFrames.size() != framesAtStep; },
                                   kSpinFrameWaitMs),
                             qPrintable(QStringLiteral("step %1 delivered %2 frame(s) of the "
                                                       "spin's contexts and the mini display "
                                                       "was given none")
                                            .arg(step).arg(givenOfTheSpin)));
                }
            }
            // The step ends when the Core has sent the mini display a frame
            // of the context it made for this step: the next step has one
            // to deliver, however few frames the Core makes on this machine.
            QVERIFY2(f.run([&] { return f.coreMedia->newestSent(*miniEndpoint) != newestAtStep; },
                           kSpinFrameWaitMs),
                     qPrintable(QStringLiteral("the Core sent the mini display no frame of "
                                               "the context for step %1").arg(step)));
            if (miniFrames.size() != framesAtStep) { ++stepsWithAFrame; }
            for (qsizetype i = framesAtStep; i < miniFrames.size(); ++i) {
                const QList<QVariant>& frame = miniFrames.at(i);
                // Its own frequencies: the center of a step asked before
                // this one, never the center the slice now has.
                const double stepsAlong =
                    (frame.at(3).toDouble() - centerBefore) / (direction * kStepHz);
                const double nearest = std::round(stepsAlong);
                if (nearest >= 1.0 && nearest <= double(step - 1)
                    && std::abs(stepsAlong - nearest) * kStepHz <= kBinToleranceHz) {
                    ++framesAtAnEarlierStep;
                } else {
                    ++framesElsewhere;
                }
                if (const std::optional<double> carrier = carrierOf(frame)) {
                    worstCarrierSamples = std::max(worstCarrierSamples,
                        std::abs(*carrier - *carrierAtStart) / sampleHzOf(frame));
                }
            }
        }
        const int asksInSpin = f.miniAsks() - askedBefore;
        const qsizetype blankedInSpin = unavailable.size();
        qsizetype sentInSpin = 0;
        for (qsizetype i = sentBefore; i < f.coreMedia->sent.size(); ++i) { ++sentInSpin; }

        // The wheel stops.
        f.onMiniAsk = nullptr;
        f.stationLink->setHoldsOutgoing(false);
        f.coreMedia->releaseAll();
        const double last = start + direction * kSteps * kStepHz;
        QVERIFY(f.run([&] {
            f.plan();
            return !miniFrames.isEmpty()
                && std::abs(miniFrames.last().at(3).toDouble() - last) <= kBinToleranceHz;
        }));
        qInfo().noquote() << QStringLiteral(
            "R-DISP-RETUNE fast spin, mini display (%1): %2 steps of %3 Hz, %4 asks, blanked "
            "%5 times; given %6 frames at the center of an earlier step and %7 elsewhere, of "
            "%8 frames the Core sent for both displays in the spin; %9 steps gave a frame; "
            "the carrier at most %10 samples from its frequency")
            .arg(QString::fromUtf8(QTest::currentDataTag()))
            .arg(kSteps).arg(kStepHz).arg(asksInSpin).arg(blankedInSpin)
            .arg(framesAtAnEarlierStep).arg(framesElsewhere).arg(sentInSpin)
            .arg(stepsWithAFrame).arg(worstCarrierSamples, 0, 'f', 2);
        QCOMPARE(asksInSpin, kSteps);
        QCOMPARE(framesElsewhere, 0);
        // Every step after the first delivered a frame of the context the
        // Core made for the step before it.
        QCOMPARE(stepsGivenAFrameOfTheSpin, kSteps - 1);
        if (statesPicture) {
            QCOMPARE(blankedInSpin, 0);
            // Each of those steps gave the mini display one (verified step
            // by step above), at the center of an earlier step.
            QVERIFY2(stepsWithAFrame >= stepsGivenAFrameOfTheSpin,
                     qPrintable(QStringLiteral("%1 steps gave a frame of the %2 that delivered "
                                               "one of the spin's contexts")
                                    .arg(stepsWithAFrame).arg(stepsGivenAFrameOfTheSpin)));
            QVERIFY2(framesAtAnEarlierStep >= stepsGivenAFrameOfTheSpin,
                     qPrintable(QStringLiteral("%1 frames were given in the spin")
                                    .arg(framesAtAnEarlierStep)));
            QVERIFY2(worstCarrierSamples <= 2.0,
                     qPrintable(QStringLiteral("the carrier was %1 samples from its frequency")
                                    .arg(worstCarrierSamples, 0, 'f', 2)));
            QVERIFY2(unavailable.isEmpty(), "the mini display was blanked when the wheel stopped");
        } else {
            QVERIFY2(blankedInSpin >= kSteps,
                     qPrintable(QStringLiteral("the mini display was blanked %1 times in %2 asks")
                                    .arg(blankedInSpin).arg(kSteps)));
            QCOMPARE(framesAtAnEarlierStep, 0);
        }
        f.finish();
    }

    // R-DISP-RETUNE (third wave). A Core that states no picture: the pan
    // keeps the rule it had before the moved picture. A context that differs
    // only in its dBm range is the picture shown; a drag, which that Core
    // may well have carried on, is a new picture. The window does not guess.
    void aPanOfACoreThatStatesNoPictureKeepsOnlyTheSamePicture()
    {
        Fixture f(/*withCore=*/true, /*statesPicture=*/false);
        QVERIFY(f.start());
        QSignalSpy frames(f.widget, &SpectrumWidget::spectrumFrameRendered);
        QVERIFY(f.run([&] { return f.contextFollowsLastAsk() && frames.size() >= 30; }));
        QCOMPARE(f.client.capabilities().spectrumPictureVersion, 0);

        quint32 generation = f.widget->remoteCodecForTest().contextGeneration;
        int asked = f.panAsks();
        f.widget->setDbmRange(f.widget->refLevel() - f.widget->dynamicRange() - 15.0f,
                              f.widget->refLevel() - 15.0f);
        QVERIFY(f.run([&] {
            f.plan();
            return f.panAsks() > asked && f.contextFollowsLastAsk()
                && f.widget->remoteCodecForTest().contextGeneration != generation;
        }));
        QCOMPARE(f.widget->remoteContextPicture(), SpectrumWidget::RemoteContextPicture::Same);

        // 12 kHz is a whole number of bins at every FFT size in use, so the
        // Core answers with the span it answered before.
        generation = f.widget->remoteCodecForTest().contextGeneration;
        asked = f.panAsks();
        const double spanBefore = f.widget->bandwidth();
        f.widget->setDisplayWindowPreservingHistory(f.widget->centerFrequency() + 12000.0,
                                                    f.widget->bandwidth());
        QVERIFY(f.run([&] {
            f.plan();
            return f.panAsks() > asked
                && f.widget->remoteCodecForTest().contextGeneration != generation
                && f.widget->remoteCodecForTest().traceSamples != 0
                && f.contextFollowsLastAsk();
        }));
        QCOMPARE(f.widget->bandwidth(), spanBefore);
        QCOMPARE(f.widget->remoteContextPicture(), SpectrumWidget::RemoteContextPicture::New);
        f.finish();
    }

    // R-DISP-RETUNE (third wave). A Core that states no picture: the mini
    // display is blanked by a tuning step, as it was before the moved
    // picture, and shows the frames of the new center when they come.
    void aMiniDisplayOfACoreThatStatesNoPictureIsBlankedByATuningStep()
    {
        Fixture f(/*withCore=*/true, /*statesPicture=*/false);
        QVERIFY(f.start());
        QSignalSpy miniFrames(f.window.get(), &RemoteMediaController::miniDisplayFrame);
        QSignalSpy unavailable(f.window.get(), &RemoteMediaController::miniDisplayUnavailable);
        f.window->setMiniDisplaySlices({f.sliceId});
        QVERIFY(f.run([&] { f.plan(); return miniFrames.size() >= 5; }));
        const double shownCenter = miniFrames.last().at(3).toDouble();
        const int miniAsked = f.miniAsks();
        unavailable.clear();

        SliceModel* const slice = f.remote.sliceById(f.sliceId);
        QVERIFY(slice);
        slice->setFrequency(slice->frequency() + 3000.0);
        QVERIFY(f.run([&] {
            f.plan();
            return f.miniAsks() > miniAsked && !miniFrames.isEmpty()
                && miniFrames.last().at(3).toDouble() != shownCenter;
        }));
        QVERIFY2(!unavailable.isEmpty(),
                 "a tuning step did not blank the mini display of a Core that states no "
                 "picture");
        f.finish();
    }
};

namespace {

// The R-DISP-RETUNE functions: what a pan and a mini display keep, draw and
// are given when the picture moves. NEREUS_DISPLAY_HOLD_GROUP=retune runs
// exactly these; =rest runs every other test function (R-DISP-HOLD); unset
// runs everything. The two are separate ctest entries, as the remote media
// controller's audio functions are, so neither carries the other's time
// against the 120 s limit: together they take about 130 s on a quiet Mac. A
// name here that is not a test function stops the run, so a rename cannot
// quietly drop a test from both entries.
const QStringList kRetuneFunctions{
    QStringLiteral("aMovedPictureCarriesOnAndEmptiesOnlyThePeakHold"),
    QStringLiteral("theMiniDisplayKeepsItsFrameThroughATuningStep"),
    QStringLiteral("theMiniDisplayKeepsItsFrameThroughStepsFasterThanTheLink"),
    QStringLiteral("theMiniDisplayKeepsItsFrameWhenTheCropChangesByABin"),
    QStringLiteral("aPanDraggedByAFractionOfABinIsAMovedPicture"),
    QStringLiteral("whatAPanDrawsWhileItIsMovedFasterThanTheLinkAnswers"),
    QStringLiteral("whatAMiniDisplayIsGivenWhileItsSliceIsTunedFasterThanTheLinkAnswers"),
    QStringLiteral("aPanOfACoreThatStatesNoPictureKeepsOnlyTheSamePicture"),
    QStringLiteral("aMiniDisplayOfACoreThatStatesNoPictureIsBlankedByATuningStep"),
};

// The test functions QtTest would run: private slots with no arguments,
// other than the four fixtures and the _data functions.
QStringList testFunctions(const QMetaObject* meta)
{
    QStringList names;
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        const QString name = QString::fromLatin1(method.name());
        if (method.methodType() != QMetaMethod::Slot
            || method.access() != QMetaMethod::Private || method.parameterCount() != 0
            || name.endsWith(QStringLiteral("_data"))
            || name == QStringLiteral("initTestCase") || name == QStringLiteral("cleanupTestCase")
            || name == QStringLiteral("init") || name == QStringLiteral("cleanup")) {
            continue;
        }
        names.append(name);
    }
    return names;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    TstRemoteDisplayHold test;
    QTEST_SET_MAIN_SOURCE_PATH
    QStringList arguments = app.arguments();
    const QString group = qEnvironmentVariable("NEREUS_DISPLAY_HOLD_GROUP");
    // A run that names its own functions (or options) runs as asked.
    if (!group.isEmpty() && arguments.size() == 1) {
        const QStringList all = testFunctions(test.metaObject());
        for (const QString& name : kRetuneFunctions) {
            if (!all.contains(name)) {
                qCritical("kRetuneFunctions names %s, which is not a test function",
                          qPrintable(name));
                return 1;
            }
        }
        if (group == QStringLiteral("retune")) {
            arguments += kRetuneFunctions;
        } else if (group == QStringLiteral("rest")) {
            for (const QString& name : all) {
                if (!kRetuneFunctions.contains(name)) {
                    arguments += name;
                }
            }
        } else {
            qCritical("NEREUS_DISPLAY_HOLD_GROUP must be retune or rest, not %s",
                      qPrintable(group));
            return 1;
        }
    }
    return QTest::qExec(&test, arguments);
}

#include "tst_remote_display_hold.moc"
