// =================================================================
// tests/tst_daemon_audio_session.cpp  (NereusSDR)
// =================================================================
// Authenticated daemon audio control and RTP forwarding over the real station
// AudioEngine/MasterMixer path.  The transport is deliberately fake only at
// the network boundary.
// =================================================================

#include <QtTest>

#include "fakes/RecordingRtpSink.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/HpsdrModel.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "models/RadioModel.h"

#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include "fakes/UpgradedCoreToken.h"

#include <mutex>

using namespace NereusSDR;

namespace {

constexpr char kConnectionId[] = "11111111-2222-4333-8444-555555555555";
constexpr int kDspFrames = 64;

class FakeTransport final : public IMediaTransport {
public:
    explicit FakeTransport(QObject* parent = nullptr) : IMediaTransport(parent) {}

    bool start(const StartOptions& options) override { startOptions = options; started = true; return true; }
    void stop() override { started = readyState = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray&) override { return readyState; }
    bool sendRtp(const QByteArray& packet) override { return rtpSink->sendRtpAccepted(packet); }
    std::shared_ptr<MediaRtpSink> audioRtpSink() override
    {
        return giveAudioSink ? rtpSink : nullptr;
    }
    bool isReady() const override { return readyState; }
    void becomeReady() { readyState = true; emit ready(); }

    bool started{false};
    // False: a transport with no audio send for another thread, as the
    // interface's default is.
    bool giveAudioSink{true};
    // R-AUD-SEND: the Core sends audio from its sender thread, through the
    // sink; what the sink took is read under its lock.
    const std::shared_ptr<Test::RecordingRtpSink> rtpSink{
        std::make_shared<Test::RecordingRtpSink>()};
    std::atomic<bool>& readyState{rtpSink->ready};
    Test::SharedPacketList& rtpPackets{rtpSink->packets};
    StartOptions startOptions{Role::Answerer, 0};
    std::atomic<int>& rtpAttempts{rtpSink->attempts};
    std::atomic<bool>& acceptRtp{rtpSink->accept};
    // The transport is closing: it is no longer ready, the next packet
    // finds it so, and the transport then says it closed. From the sender
    // thread, so the signal reaches the peer on its own thread, as the real
    // transport's does.
    void closeOnNextRtp()
    {
        readyState = false;
        rtpSink->setHook([this](const QByteArray&) {
            if (!m_closeSignalled.exchange(true)) {
                QMetaObject::invokeMethod(this, [this] { emit closed(); }, Qt::QueuedConnection);
            }
        });
    }
    std::atomic<bool> m_closeSignalled{false};
};

QVector<float> stereoBlock(float left, float right)
{
    QVector<float> block(kDspFrames * 2);
    for (int frame = 0; frame < kDspFrames; ++frame) {
        block[frame * 2] = left;
        block[frame * 2 + 1] = right;
    }
    return block;
}

QJsonObject audioControl(quint32 revision, bool enabled)
{
    return {{QStringLiteral("op"), QStringLiteral("audio")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)},
            {QStringLiteral("revision"), static_cast<qint64>(revision)},
            {QStringLiteral("enabled"), enabled}};
}

// The "daemon audio diagnostics" lines logged while one of these lives.
class DiagnosticsLines final {
public:
    DiagnosticsLines()
    {
        s_lines = &m_lines;
        m_previous = qInstallMessageHandler(&DiagnosticsLines::handle);
    }
    ~DiagnosticsLines()
    {
        qInstallMessageHandler(m_previous);
        s_lines = nullptr;
    }
    DiagnosticsLines(const DiagnosticsLines&) = delete;
    DiagnosticsLines& operator=(const DiagnosticsLines&) = delete;
    QStringList lines() const
    {
        const std::lock_guard<std::mutex> lock(s_mutex);
        return m_lines;
    }

private:
    static void handle(QtMsgType, const QMessageLogContext&, const QString& message)
    {
        const std::lock_guard<std::mutex> lock(s_mutex);
        if (s_lines != nullptr && message.contains(QLatin1String("daemon audio diagnostics "))) {
            s_lines->append(message);
        }
    }
    static inline std::mutex s_mutex;
    static inline QStringList* s_lines = nullptr;
    QStringList m_lines;
    QtMessageHandler m_previous = nullptr;
};

// One field of a diagnostics line, as its text.
QString diagnosticsField(const QString& line, const QString& name)
{
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral(" %1=(\\S+)").arg(name)).match(line);
    return match.hasMatch() ? match.captured(1) : QString();
}

QJsonObject latestAudioContext(const QSignalSpy& controls)
{
    for (auto it = controls.crbegin(); it != controls.crend(); ++it) {
        const QJsonObject message = it->at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == QLatin1String("audio-context")) {
            return message;
        }
    }
    return {};
}

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
    AudioEngine* engine{nullptr};
    bool transportsGiveAudioSink{true};
    int sliceA{-1};
    int sliceB{-1};

    Harness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()))
        , controller(&server, &radio, nullptr,
                     [this](QObject* parent) -> IMediaTransport* {
                         mediaTransport = new FakeTransport(parent);
                         mediaTransport->giveAudioSink = transportsGiveAudioSink;
                         return mediaTransport;
                     })
    {
        Q_ASSERT(directory.isValid());
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        engine = radio.audioEngine();
        Q_ASSERT(engine != nullptr);
        engine->masterMixForTest().setRampFrames(1);
        engine->masterMixForTest().setSlewUpFrames(0);
        sliceA = radio.addSlice();
        sliceB = radio.addSlice();
        Q_ASSERT(sliceA >= 0 && sliceB >= 0);
        engine->setSliceStreaming(sliceA, true);
        engine->setSliceStreaming(sliceB, true);
        server.setMediaEnabled(true);
    }

    void establishAndReady()
    {
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(server.mediaAvailable());
        // R-R3-49: sendMediaControl() needs this client's side of the
        // handshake, which lands a queued delivery after the Core's.
        QTRY_VERIFY(client.mediaAvailable());
        QVERIFY(client.sendMediaControl({
            {QStringLiteral("op"), QStringLiteral("start")},
            {QStringLiteral("connectionId"), QLatin1String(kConnectionId)}},
            client.sessionEpoch()));
        QTRY_VERIFY(mediaTransport);
        mediaTransport->becomeReady();
    }

    void feedMixed(int frames, float a = 0.20f, float b = 0.30f)
    {
        const QVector<float> left = stereoBlock(a, a);
        const QVector<float> right = stereoBlock(b, b);
        for (int delivered = 0; delivered < frames; delivered += kDspFrames) {
            engine->rxBlockReady(sliceA, left.constData(), kDspFrames);
            engine->rxBlockReady(sliceB, right.constData(), kDspFrames);
        }
        // R-AUD-SEND: no DSP callback follows the last one here; see
        // DaemonAudioSource::handOverPendingForTest().
        controller.handOverPendingAudioForTest();
    }
};

} // namespace

class TstDaemonAudioSession final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        OpusAudioEncoder encoder;
        if (!encoder.isReady()) {
            QSKIP("Opus encoder is unavailable in this build");
        }
    }

    void enabledPauseResumePublishesFreshContextsAndContinuousRtpBases()
    {
        Harness h;
        h.establishAndReady();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
        QTRY_VERIFY(!latestAudioContext(controls).isEmpty());
        const QJsonObject enabled = latestAudioContext(controls);
        // A minor-8 session: the eight keys plus the encoder profile.
        QCOMPARE(enabled.size(), 9);
        QVERIFY(enabled.value(QStringLiteral("encoder")).isObject());
        QVERIFY(!enabled.contains(QStringLiteral("reason")));
        QCOMPARE(enabled.value(QStringLiteral("revision")).toInteger(), qint64{1});
        QVERIFY(enabled.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(static_cast<quint32>(enabled.value(QStringLiteral("ssrc")).toInteger()),
                 h.mediaTransport->startOptions.localAudioSsrc);

        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_COMPARE(h.mediaTransport->rtpPackets.size(), 1);
        OpusAudioDecoder decoder;
        const auto first = decoder.decodeRtp(h.mediaTransport->rtpPackets.constFirst(),
                                             h.mediaTransport->startOptions.localAudioSsrc);
        QCOMPARE(first.status, OpusAudioCodecStatus::Accepted);

        QVERIFY(h.client.sendMediaControl(audioControl(2, false), h.client.sessionEpoch()));
        QTRY_VERIFY(latestAudioContext(controls).value(QStringLiteral("revision")).toInteger() == 2);
        const QJsonObject paused = latestAudioContext(controls);
        QVERIFY(!paused.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(paused.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("client-disabled"));
        QCOMPARE(paused.value(QStringLiteral("firstSequence")).toInteger(),
                 qint64{static_cast<quint16>(first.sequence + 1)});
        QCOMPARE(paused.value(QStringLiteral("firstTimestamp")).toInteger(),
                 qint64{first.timestamp + DaemonAudioSource::kBlockFrames});

        QVERIFY(h.client.sendMediaControl(audioControl(3, true), h.client.sessionEpoch()));
        QTRY_VERIFY(latestAudioContext(controls).value(QStringLiteral("revision")).toInteger() == 3);
        const QJsonObject resumed = latestAudioContext(controls);
        QVERIFY(resumed.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(resumed.value(QStringLiteral("firstSequence")).toInteger(),
                 paused.value(QStringLiteral("firstSequence")).toInteger());
        QCOMPARE(resumed.value(QStringLiteral("firstTimestamp")).toInteger(),
                 paused.value(QStringLiteral("firstTimestamp")).toInteger());
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_COMPARE(h.mediaTransport->rtpPackets.size(), 2);
        const auto second = decoder.decodeRtp(h.mediaTransport->rtpPackets.constLast(),
                                              h.mediaTransport->startOptions.localAudioSsrc);
        QCOMPARE(second.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(second.sequence, static_cast<quint16>(first.sequence + 1));
        QCOMPARE(second.timestamp, static_cast<quint32>(first.timestamp
                                                          + DaemonAudioSource::kBlockFrames));
    }

    void rejectsMalformedOrStaleAudioAndReconcilesRadioDisconnect()
    {
        Harness h;
        h.establishAndReady();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        QVERIFY(h.client.sendMediaControl(audioControl(5, true), h.client.sessionEpoch()));
        QTRY_VERIFY(!latestAudioContext(controls).isEmpty());
        const int acceptedContexts = controls.count();

        QJsonObject malformed = audioControl(6, false);
        malformed.insert(QStringLiteral("extra"), true);
        QVERIFY(h.client.sendMediaControl(malformed, h.client.sessionEpoch()));
        QVERIFY(h.client.sendMediaControl(audioControl(5, false), h.client.sessionEpoch()));
        QTest::qWait(20);
        QCOMPARE(controls.count(), acceptedContexts);
        QVERIFY(!h.client.sendMediaControl(audioControl(7, false), h.client.sessionEpoch() + 1));

        h.radio.setConnectionStateForTest(ConnectionState::LinkLost);
        QTRY_VERIFY(!latestAudioContext(controls).value(QStringLiteral("enabled")).toBool());
        const QJsonObject disconnected = latestAudioContext(controls);
        QCOMPARE(disconnected.value(QStringLiteral("revision")).toInteger(), qint64{5});
        QCOMPARE(disconnected.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("radio-offline"));
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTest::qWait(20);
        QVERIFY(h.mediaTransport->rtpPackets.isEmpty());
        h.radio.setConnectionStateForTest(ConnectionState::Disconnected);
        QTRY_VERIFY(!latestAudioContext(controls).value(QStringLiteral("enabled")).toBool());

        h.radio.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_VERIFY(latestAudioContext(controls).value(QStringLiteral("enabled")).toBool());
        const QJsonObject reconnected = latestAudioContext(controls);
        QVERIFY(reconnected.value(QStringLiteral("generation")).toInteger()
                > disconnected.value(QStringLiteral("generation")).toInteger());
    }

    void audioDiagnosticsCountTransportAcceptanceAndKeepFinalSnapshot()
    {
        Harness h;
        h.establishAndReady();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
        QTRY_VERIFY(!latestAudioContext(controls).isEmpty());
        const QJsonObject context = latestAudioContext(controls);

        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_VERIFY(h.controller.audioDiagnostics().sendAccepted == std::uint64_t{1});
        const auto accepted = h.controller.audioDiagnostics();
        QVERIFY(accepted.activeContext);
        QCOMPARE(accepted.contextGeneration,
                 static_cast<quint32>(context.value(QStringLiteral("generation")).toInteger()));
        QCOMPARE(accepted.revision, quint32{1});
        QCOMPARE(accepted.sender.source.capturedValidRateFrames,
                 std::uint64_t{DaemonAudioSource::kBlockFrames});
        QCOMPARE(accepted.sender.source.sourceDropEvents, std::uint64_t{0});
        QCOMPARE(accepted.sender.consumedBlocks, std::uint64_t{1});
        QCOMPARE(accepted.sender.encodedPackets, std::uint64_t{1});
        QCOMPARE(accepted.sender.encodeFailures, std::uint64_t{0});
        QVERIFY(accepted.sender.hasLastEmittedPacket);
        QCOMPARE(accepted.sendAttempts, std::uint64_t{1});
        QCOMPARE(accepted.sendAccepted, std::uint64_t{1});
        QCOMPARE(accepted.sendRejected, std::uint64_t{0});
        QCOMPARE(accepted.sendInFlight, std::uint64_t{0});
        QCOMPARE(accepted.sendUnresolvedAtRetirement, std::uint64_t{0});
        QCOMPARE(accepted.sendAttempts, accepted.sendAccepted + accepted.sendRejected
                 + accepted.sendInFlight + accepted.sendUnresolvedAtRetirement);

        h.mediaTransport->acceptRtp = false;
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_VERIFY(h.controller.audioDiagnostics().sendRejected == std::uint64_t{1});
        const auto rejected = h.controller.audioDiagnostics();
        QCOMPARE(rejected.sendAttempts, std::uint64_t{2});
        QCOMPARE(rejected.sendAccepted, std::uint64_t{1});
        QCOMPARE(rejected.sendRejected, std::uint64_t{1});
        QCOMPARE(rejected.sendInFlight, std::uint64_t{0});
        QCOMPARE(rejected.sendUnresolvedAtRetirement, std::uint64_t{0});
        QCOMPARE(rejected.sendAttempts, rejected.sendAccepted + rejected.sendRejected
                 + rejected.sendInFlight + rejected.sendUnresolvedAtRetirement);
        QCOMPARE(h.mediaTransport->rtpAttempts.load(), 2);
        QCOMPARE(h.mediaTransport->rtpPackets.size(), 1);

        QVERIFY(h.client.sendMediaControl(audioControl(2, false), h.client.sessionEpoch()));
        QTRY_VERIFY(!latestAudioContext(controls).value(QStringLiteral("enabled")).toBool());
        const auto stopped = h.controller.audioDiagnostics();
        QVERIFY(!stopped.activeContext);
        QCOMPARE(stopped.sendAttempts, rejected.sendAttempts);
        QCOMPARE(stopped.sendAccepted, rejected.sendAccepted);
        QCOMPARE(stopped.sendRejected, rejected.sendRejected);
        QCOMPARE(stopped.sendInFlight, std::uint64_t{0});
        QCOMPARE(stopped.sendUnresolvedAtRetirement,
                 rejected.sendUnresolvedAtRetirement);
        QCOMPARE(stopped.sendAttempts, stopped.sendAccepted + stopped.sendRejected
                 + stopped.sendInFlight + stopped.sendUnresolvedAtRetirement);
        QCOMPARE(stopped.sender.source.capturedValidRateFrames,
                 rejected.sender.source.capturedValidRateFrames);
        QCOMPARE(stopped.sender.source.sourceDropEvents,
                 rejected.sender.source.sourceDropEvents);
        QCOMPARE(stopped.sender.consumedBlocks, rejected.sender.consumedBlocks);
        QCOMPARE(stopped.sender.encodedPackets, rejected.sender.encodedPackets);
        QCOMPARE(stopped.sender.lastEmittedSequence,
                 rejected.sender.lastEmittedSequence);
        QCOMPARE(stopped.sender.lastEmittedTimestamp,
                 rejected.sender.lastEmittedTimestamp);
    }

    // R-AUD-SEND: a transport with no audio send for the sender thread.
    // Audio then goes nowhere, and the Core says so: once for the
    // connection, however often its routes are brought up to date after.
    void aTransportWithNoAudioSinkIsWarnedAboutOnce()
    {
        const QRegularExpression warning(
            QStringLiteral("receive audio cannot be sent on media connection %1")
                .arg(QLatin1String(kConnectionId)));
        Harness h;
        h.transportsGiveAudioSink = false;
        h.establishAndReady();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        // The one line, which must come; any further one fails the test.
        QTest::ignoreMessage(QtWarningMsg, warning);
        QTest::failOnWarning(warning);
        QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
        QTRY_VERIFY(h.controller.audioDiagnostics().activeContext);
        // Off and on again: the routes are brought up to date each time.
        QVERIFY(h.client.sendMediaControl(audioControl(2, false), h.client.sessionEpoch()));
        QTRY_VERIFY(latestAudioContext(controls).value(QStringLiteral("revision")).toInteger() == 2);
        QVERIFY(h.client.sendMediaControl(audioControl(3, true), h.client.sessionEpoch()));
        QTRY_VERIFY(latestAudioContext(controls).value(QStringLiteral("revision")).toInteger() == 3);
        // Nothing is sent, and nothing is counted against the transport.
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_VERIFY(h.controller.audioDiagnostics().sender.encodedPackets == std::uint64_t{1});
        const auto diagnostics = h.controller.audioDiagnostics();
        QCOMPARE(diagnostics.sendAttempts, std::uint64_t{0});
        QCOMPARE(diagnostics.sendRejected, std::uint64_t{0});
        QVERIFY(h.mediaTransport->rtpPackets.isEmpty());
    }

    // R-AUD-SEND: the diagnostics line says how evenly the sender thread
    // sent: the longest gap between two packets since the context started
    // and since the previous line, in milliseconds to one decimal, after
    // sendUnresolvedAtRetirement. Reading the diagnostics for another
    // purpose does not take the second figure; a line that is logged does,
    // so one gap is reported as "since the previous line" exactly once.
    void theDiagnosticsLineReportsTheLongestSendGap()
    {
        constexpr int kLineDeadlineMs = 20'000;
        const QString none = QStringLiteral("-1");
        const QString sinceStart = QStringLiteral("sendGapMaxMs");
        const QString sinceLast = QStringLiteral("sendGapMaxSinceLastMs");
        Harness h;
        h.establishAndReady();
        DiagnosticsLines log;

        // A context with one packet: no gap was measured, in any line.
        QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
        QTRY_VERIFY(h.controller.audioDiagnostics().activeContext);
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_COMPARE(h.mediaTransport->rtpPackets.size(), 1);
        QVERIFY(h.client.sendMediaControl(audioControl(2, false), h.client.sessionEpoch()));
        QTRY_VERIFY(!h.controller.audioDiagnostics().activeContext);
        const QStringList single = log.lines();
        QVERIFY(!single.isEmpty());
        QVERIFY(single.constLast().contains(QLatin1String("diagnostics final ")));
        for (const QString& line : single) {
            QVERIFY2(line.contains(QLatin1String(
                         " sendUnresolvedAtRetirement=0 sendGapMaxMs=-1 sendGapMaxSinceLastMs=-1 "
                         "hasLastPacket=")),
                     qPrintable(line));
        }

        // A context with two packets has one gap.
        const qsizetype firstOfPair = single.size();
        QVERIFY(h.client.sendMediaControl(audioControl(3, true), h.client.sessionEpoch()));
        QTRY_VERIFY(h.controller.audioDiagnostics().activeContext);
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_COMPARE(h.mediaTransport->rtpPackets.size(), 2);
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_COMPARE(h.mediaTransport->rtpPackets.size(), 3);
        // Read for another purpose, more than once: that takes nothing.
        const qint64 gapUs = h.controller.audioDiagnostics().sender.longestSendGapUs;
        QVERIFY(gapUs >= 0);
        QCOMPARE(h.controller.audioDiagnostics().sender.longestSendGapUs, gapUs);
        const QString expected = QString::number(double(gapUs) / 1000.0, 'f', 1);
        QVERIFY2(QRegularExpression(QStringLiteral("^\\d+\\.\\d$")).match(expected).hasMatch(),
                 qPrintable(expected));

        // A periodic line after the gap, then the final one. Nothing was
        // sent between them, so the final line has no gap since the last.
        const qsizetype afterTheGap = log.lines().size();
        QTRY_VERIFY_WITH_TIMEOUT(log.lines().size() > afterTheGap, kLineDeadlineMs);
        QVERIFY(h.client.sendMediaControl(audioControl(4, false), h.client.sessionEpoch()));
        QTRY_VERIFY(!h.controller.audioDiagnostics().activeContext);
        const QStringList lines = log.lines();
        QVERIFY(lines.constLast().contains(QLatin1String("diagnostics final ")));
        int linesWithTheGapSinceLast = 0;
        for (qsizetype i = firstOfPair; i < lines.size(); ++i) {
            const QString& line = lines.at(i);
            QVERIFY2(line.contains(QRegularExpression(QStringLiteral(
                         " sendUnresolvedAtRetirement=\\d+ sendGapMaxMs=\\S+ "
                         "sendGapMaxSinceLastMs=\\S+ hasLastPacket="))),
                     qPrintable(line));
            const QString last = diagnosticsField(line, sinceLast);
            if (last == expected) {
                ++linesWithTheGapSinceLast;
            } else {
                QVERIFY2(last == none, qPrintable(line));
            }
            if (i >= afterTheGap) {
                QCOMPARE(diagnosticsField(line, sinceStart), expected);
            }
        }
        QCOMPARE(linesWithTheGapSinceLast, 1);
        QCOMPARE(diagnosticsField(lines.at(afterTheGap), sinceLast), expected);
        QCOMPARE(diagnosticsField(lines.constLast(), sinceLast), none);
        QCOMPARE(h.controller.audioDiagnostics().sender.longestSendGapUs, gapUs);
    }

    // R-AUD-SEND: a ready transport that will not take a packet. That is a
    // transport fault to a reader of the diagnostics: one attempt, rejected.
    void audioDiagnosticsCountAReadyTransportsRefusalAsRejected()
    {
        Harness h;
        h.establishAndReady();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
        QTRY_VERIFY(h.controller.audioDiagnostics().activeContext);

        h.mediaTransport->acceptRtp = false;
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTRY_COMPARE(h.mediaTransport->rtpAttempts.load(), 1);
        QTRY_VERIFY(h.controller.audioDiagnostics().sendInFlight == std::uint64_t{0}
                    && h.controller.audioDiagnostics().sendAttempts == std::uint64_t{1});
        const auto refused = h.controller.audioDiagnostics();
        QCOMPARE(refused.sendAttempts, std::uint64_t{1});
        QCOMPARE(refused.sendAccepted, std::uint64_t{0});
        QCOMPARE(refused.sendRejected, std::uint64_t{1});
        QCOMPARE(refused.sendUnresolvedAtRetirement, std::uint64_t{0});
    }

    // R-AUD-SEND: a send that meets a closing transport. (Before the sender
    // thread the send ran on the controller's thread, the close retired the
    // peer inside it, and the attempt stayed unresolved. The send now runs
    // on the sender thread: the transport is no longer ready when the
    // packet comes, so nothing is asked of it, and the close reaches the
    // controller on its own thread afterwards.) No attempt, nothing
    // rejected, nothing unresolved at the retirement, and the counts add up.
    void audioDiagnosticsDoNotCountASendThatMeetsAClosingTransport()
    {
        Harness h;
        h.establishAndReady();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        QVERIFY(h.client.sendMediaControl(audioControl(1, true), h.client.sessionEpoch()));
        QTRY_VERIFY(!latestAudioContext(controls).isEmpty());
        QTRY_VERIFY(h.controller.audioDiagnostics().activeContext);

        // The next packet finds the transport not ready and the transport
        // says it closed. Keep no raw fixture pointer after feeding audio:
        // closing retires the peer, and the transport with it.
        const std::shared_ptr<Test::RecordingRtpSink> sink = h.mediaTransport->rtpSink;
        h.mediaTransport->closeOnNextRtp();
        h.feedMixed(DaemonAudioSource::kBlockFrames);

        QTRY_VERIFY(!h.controller.audioDiagnostics().activeContext);
        const auto retired = h.controller.audioDiagnostics();
        // The packet reached the sink, which answered "not ready".
        QCOMPARE(sink->attempts.load(), 1);
        QCOMPARE(sink->packets.size(), 0);
        QCOMPARE(retired.sender.encodedPackets, std::uint64_t{1});
        QCOMPARE(retired.sendAttempts, std::uint64_t{0});
        QCOMPARE(retired.sendAccepted, std::uint64_t{0});
        QCOMPARE(retired.sendRejected, std::uint64_t{0});
        QCOMPARE(retired.sendInFlight, std::uint64_t{0});
        QCOMPARE(retired.sendUnresolvedAtRetirement, std::uint64_t{0});
        QCOMPARE(retired.sendAttempts, retired.sendAccepted + retired.sendRejected
                 + retired.sendInFlight + retired.sendUnresolvedAtRetirement);
    }
};

QTEST_MAIN(TstDaemonAudioSession)
#include "tst_daemon_audio_session.moc"
