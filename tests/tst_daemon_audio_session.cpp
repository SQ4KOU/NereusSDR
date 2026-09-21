// =================================================================
// tests/tst_daemon_audio_session.cpp  (NereusSDR)
// =================================================================
// Authenticated daemon audio control and RTP forwarding over the real station
// AudioEngine/MasterMixer path.  The transport is deliberately fake only at
// the network boundary.
// =================================================================

#include <QtTest>

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
#include <QTemporaryDir>

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
    bool sendRtp(const QByteArray& packet) override
    {
        if (!readyState) { return false; }
        rtpPackets.append(packet);
        return true;
    }
    bool isReady() const override { return readyState; }
    void becomeReady() { readyState = true; emit ready(); }

    bool started{false};
    bool readyState{false};
    StartOptions startOptions{Role::Answerer, 0};
    QList<QByteArray> rtpPackets;
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
    int sliceA{-1};
    int sliceB{-1};

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
        QCOMPARE(enabled.size(), 8);
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

        h.radio.setConnectionStateForTest(ConnectionState::Disconnected);
        QTRY_VERIFY(!latestAudioContext(controls).value(QStringLiteral("enabled")).toBool());
        const QJsonObject disconnected = latestAudioContext(controls);
        QCOMPARE(disconnected.value(QStringLiteral("revision")).toInteger(), qint64{5});
        h.feedMixed(DaemonAudioSource::kBlockFrames);
        QTest::qWait(20);
        QVERIFY(h.mediaTransport->rtpPackets.isEmpty());

        h.radio.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_VERIFY(latestAudioContext(controls).value(QStringLiteral("enabled")).toBool());
        const QJsonObject reconnected = latestAudioContext(controls);
        QVERIFY(reconnected.value(QStringLiteral("generation")).toInteger()
                > disconnected.value(QStringLiteral("generation")).toInteger());
    }
};

QTEST_MAIN(TstDaemonAudioSession)
#include "tst_daemon_audio_session.moc"
