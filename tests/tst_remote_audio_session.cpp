// =================================================================
// tests/tst_remote_audio_session.cpp  (NereusSDR)
// =================================================================
// Real DTLS/SRTP daemon-to-remote audio exercise.  LoopbackTransport is used
// only for the authenticated control plane; both media peers use the default
// LibDataChannel transport and carry actual RTP/Opus over their local link.
// =================================================================

#include <QtTest>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/HpsdrModel.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/PacedAudioBus.h"
#include "gui/RemoteMediaController.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using namespace NereusSDR;

namespace {

constexpr int kFrames = 480;
constexpr double kPi = 3.14159265358979323846;

// Test-only control link, so compatibility can be exercised without any
// production test hook. With a hello minor set on both ends, a current Core
// and a current GUI each believe the other speaks that minor. A one-shot
// forge puts a forged copy of the next audio context on the wire just
// ahead of the real one.
class RewritingTransport final : public Test::LoopbackTransport {
public:
    using Forge = std::function<QJsonObject(const QJsonObject& real)>;

    RewritingTransport(const QString& description, std::optional<quint16> helloMinor)
        : LoopbackTransport(description), m_helloMinor(helloMinor) {}

    void sendText(const QByteArray& wire) override
    {
        const bool mayRewrite = (m_helloMinor && wire.contains("\"hello\""))
            || (forgeNextAudioContext && wire.contains("\"audio-context\""));
        SessionMessage message;
        if (mayRewrite && SessionMessages::decode(wire, &message)) {
            if (m_helloMinor && message.kind == SessionMessageKind::Hello) {
                ++rewrittenHellos;
                LoopbackTransport::sendText(SessionMessages::encode(SessionMessages::hello(
                    message.protocolMajor, *m_helloMinor, message.settingsSchemaVersion,
                    message.peerName)));
                return;
            }
            if (forgeNextAudioContext && message.kind == SessionMessageKind::MediaControl
                && message.mediaPayload.value(QStringLiteral("op"))
                    == QLatin1String("audio-context")) {
                SessionMessage forged = message;
                forged.mediaPayload = std::exchange(forgeNextAudioContext, {})(
                    message.mediaPayload);
                ++forgedContexts;
                LoopbackTransport::sendText(SessionMessages::encode(forged));
            }
        }
        LoopbackTransport::sendText(wire);
    }

    int rewrittenHellos = 0;
    int forgedContexts = 0;
    Forge forgeNextAudioContext;

private:
    std::optional<quint16> m_helloMinor;
};

// The encoder object a default Core announces.
QJsonObject defaultEncoderJson()
{
    return {{QStringLiteral("codec"), QStringLiteral("opus")},
            {QStringLiteral("sampleRate"), 48000},
            {QStringLiteral("channels"), 2},
            {QStringLiteral("frameSamples"), 1920},
            {QStringLiteral("targetBitrate"), 24000},
            {QStringLiteral("audioBandwidthHz"), 8000}};
}

// Every context the GUI accepted, captured when it said so.
struct AcceptedContexts {
    QList<RemoteAudioContextMessage> contexts;
    int signalsWithoutContext = 0;
    QMetaObject::Connection connection;

    explicit AcceptedContexts(RemoteMediaController& media)
    {
        connection = QObject::connect(
            &media, &RemoteMediaController::audioContextAccepted, &media, [this, &media] {
                if (const std::optional<RemoteAudioContextMessage> context =
                        media.acceptedAudioContext()) {
                    contexts.append(*context);
                } else {
                    ++signalsWithoutContext;
                }
            });
    }
    ~AcceptedContexts() { QObject::disconnect(connection); }
    AcceptedContexts(const AcceptedContexts&) = delete;
    AcceptedContexts& operator=(const AcceptedContexts&) = delete;

    bool any(const std::function<bool(const RemoteAudioContextMessage&)>& match) const
    {
        return std::any_of(contexts.cbegin(), contexts.cend(), match);
    }

    // One signal per accepted context: no context is reported twice.
    bool eachReportedOnce() const
    {
        for (qsizetype index = 1; index < contexts.size(); ++index) {
            if (contexts.at(index).connectionId == contexts.at(index - 1).connectionId
                && contexts.at(index).generation == contexts.at(index - 1).generation) {
                return false;
            }
        }
        return true;
    }
};

QList<QJsonObject> audioContexts(const QSignalSpy& controls)
{
    QList<QJsonObject> contexts;
    for (const auto& call : controls) {
        const QJsonObject message = call.at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == QLatin1String("audio-context")) {
            contexts.append(message);
        }
    }
    return contexts;
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

double channelEnergy(const QVector<float>& samples, int channel, int firstFrame = 0)
{
    double total = 0.0;
    for (int frame = firstFrame; frame * 2 + channel < samples.size(); ++frame) {
        const double sample = samples.at(frame * 2 + channel);
        total += sample * sample;
    }
    return total;
}

double correlation(const QVector<float>& samples, int firstFrame = 0)
{
    double left = 0.0;
    double right = 0.0;
    double cross = 0.0;
    for (int frame = firstFrame; frame * 2 + 1 < samples.size(); ++frame) {
        const double l = samples.at(frame * 2);
        const double r = samples.at(frame * 2 + 1);
        left += l * l;
        right += r * r;
        cross += l * r;
    }
    return left > 0.0 && right > 0.0 ? cross / std::sqrt(left * right) : 1.0;
}

double toneAmplitude(const QVector<float>& samples, int channel, double hz,
                     int firstFrame = 0)
{
    double cosine = 0.0;
    double sine = 0.0;
    int frames = 0;
    for (int frame = firstFrame; frame * 2 + channel < samples.size(); ++frame) {
        const double phase = 2.0 * kPi * hz * static_cast<double>(frame) / 48000.0;
        const double sample = samples.at(frame * 2 + channel);
        cosine += sample * std::cos(phase);
        sine += sample * std::sin(phase);
        ++frames;
    }
    return frames > 0 ? 2.0 * std::hypot(cosine, sine) / frames : 0.0;
}

struct Harness {
    QTemporaryDir directory;
    AppSettings settings;
    RadioModel station;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy settingsProxy;
    StationClient client{&remote, &settingsProxy};
    AudioEngine* stationAudio{nullptr};
    PacedAudioBus* remoteBus{nullptr};
    int sliceA{-1};
    int sliceB{-1};
    qint64 stationFrames{0};

    Harness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&station, settings, directory.path())
    {
        Q_ASSERT(directory.isValid());
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                    /*defaultRateHz=*/192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        stationAudio = station.audioEngine();
        Q_ASSERT(stationAudio != nullptr);
        stationAudio->masterMixForTest().setRampFrames(1);
        stationAudio->masterMixForTest().setSlewUpFrames(0);
        sliceA = station.addSlice();
        sliceB = station.addSlice();
        Q_ASSERT(sliceA >= 0 && sliceB >= 0);
        stationAudio->setSliceStreaming(sliceA, true);
        stationAudio->setSliceStreaming(sliceB, true);
        stationAudio->masterMixForTest().setSliceGain(sliceA, 0.60f, -0.95f);
        stationAudio->masterMixForTest().setSliceGain(sliceB, 0.45f, 0.95f);
        station.sliceById(sliceA)->setAudioPan(-0.95);
        station.sliceById(sliceB)->setAudioPan(0.95);
        server.setMediaEnabled(true);

        remote.setConnectionStateForTest(ConnectionState::Connected);
        auto bus = std::make_unique<PacedAudioBus>();
        remoteBus = bus.get();
        remote.audioEngine()->setSpeakersBusForTest(std::move(bus));
    }

    // helloMinor, when set, is what both ends announce, so both negotiate
    // down to it. forgeFirstContext, when set, puts one forged copy on the
    // wire ahead of Core's first audio context.
    void connectSession(std::optional<quint16> helloMinor = std::nullopt,
                        RewritingTransport::Forge forgeFirstContext = {})
    {
        auto* station = new RewritingTransport(QStringLiteral("station"), helloMinor);
        auto* clientEnd = new RewritingTransport(QStringLiteral("client"), helloMinor);
        station->forgeNextAudioContext = std::move(forgeFirstContext);
        stationLink = station;
        station->linkTo(clientEnd);
        client.startSession(clientEnd, server.token());
        server.acceptTransport(station);
        QTRY_VERIFY(server.mediaAvailable());
        if (helloMinor) {
            QCOMPARE(station->rewrittenHellos, 1);
            QCOMPARE(clientEnd->rewrittenHellos, 1);
            QCOMPARE(client.agreedMinor(), *helloMinor);
        }
    }

    QPointer<RewritingTransport> stationLink;

    void feedMixedTone()
    {
        QVector<float> a(kFrames * 2);
        QVector<float> b(kFrames * 2);
        for (int frame = 0; frame < kFrames; ++frame) {
            const double time = static_cast<double>(stationFrames + frame) / 48000.0;
            const float first = static_cast<float>(0.22 * std::sin(2.0 * kPi * 617.0 * time));
            const float second = static_cast<float>(0.19 * std::sin(2.0 * kPi * 1579.0 * time));
            a[frame * 2] = a[frame * 2 + 1] = first;
            b[frame * 2] = b[frame * 2 + 1] = second;
        }
        stationFrames += kFrames;
        stationAudio->rxBlockReady(sliceA, a.constData(), kFrames);
        stationAudio->rxBlockReady(sliceB, b.constData(), kFrames);
    }
};

} // namespace

class TstRemoteAudioSession final : public QObject {
    Q_OBJECT

private slots:
    void encryptedAudioSurvivesMuteResumeAndSessionReconnect()
    {
        Harness h;
        // Keep controller destruction ahead of both AudioEngines: its remote
        // receiver owns worker callbacks and its peer owns libdatachannel.
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy remoteErrors(&remoteMedia, &RemoteMediaController::errorOccurred);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        AcceptedContexts accepted(remoteMedia);
        const std::optional<OpusEncoderProfile> coreProfile = OpusAudioEncoder().profile();
        QVERIFY(coreProfile.has_value());
        // The GUI logs the profile Core reported, not one it assumes.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral(
            "^Remote audio receiving: Opus 48000 Hz, 2 channels, 1920-sample frames, "
            "target 24000 bit/s, audio bandwidth 8000 Hz, context \\d+$")));

        QTimer source;
        source.setInterval(10);
        source.setTimerType(Qt::PreciseTimer);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        speaker.setTimerType(Qt::PreciseTimer);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(kFrames); });
        source.start();
        speaker.start();

        h.connectSession();
        QVERIFY(remoteMedia.audioDetailNegotiated());
        QTRY_VERIFY_WITH_TIMEOUT(!latestAudioContext(controls).isEmpty(), 15000);
        const QJsonObject initial = latestAudioContext(controls);
        // Minor 8: the eight keys plus the profile Core actually encodes with.
        QCOMPARE(initial.size(), 9);
        QCOMPARE(initial.value(QStringLiteral("encoder")).toObject(), defaultEncoderJson());
        QVERIFY(!initial.contains(QStringLiteral("reason")));
        QVERIFY(initial.value(QStringLiteral("enabled")).toBool());
        QVERIFY(initial.value(QStringLiteral("generation")).toInteger() > 0);
        const QString initialConnection = initial.value(QStringLiteral("connectionId")).toString();
        const quint32 initialGeneration = static_cast<quint32>(
            initial.value(QStringLiteral("generation")).toInteger());
        QTRY_VERIFY(remoteMedia.acceptedAudioContext().has_value());
        {
            const RemoteAudioContextMessage context = *remoteMedia.acceptedAudioContext();
            QCOMPARE(context.generation, initialGeneration);
            QVERIFY(context.enabled);
            QVERIFY(context.encoder.has_value());
            QCOMPARE(*context.encoder, *coreProfile);
            QVERIFY(!context.offReason.has_value());
        }

        const int initialHeardFrame = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(h.remoteBus->heard.size()
                                     >= (initialHeardFrame + 48000) * 2
                                 && channelEnergy(h.remoteBus->heard, 0, initialHeardFrame) > 2.0
                                 && channelEnergy(h.remoteBus->heard, 1, initialHeardFrame) > 2.0,
                                 15000);
        const int audibleStartFrame = initialHeardFrame;
        // The approved 24 kb/s stereo profile retains each panned program
        // with measured ~23 dB separation, not near-zero correlation. Check
        // the two known tones directly: each intended channel must retain at
        // least 18 dB (8x amplitude) over the same tone in the other channel.
        const double left617 = toneAmplitude(h.remoteBus->heard, 0, 617.0, audibleStartFrame);
        const double left1579 = toneAmplitude(h.remoteBus->heard, 0, 1579.0, audibleStartFrame);
        const double right617 = toneAmplitude(h.remoteBus->heard, 1, 617.0, audibleStartFrame);
        const double right1579 = toneAmplitude(h.remoteBus->heard, 1, 1579.0, audibleStartFrame);
        qInfo() << "Encrypted audio spectral amplitudes L617/L1579/R617/R1579"
                << left617 << left1579 << right617 << right1579;
        QVERIFY(left617 > right617 * 8.0);
        QVERIFY(right1579 > left1579 * 8.0);
        QVERIFY(std::abs(correlation(h.remoteBus->heard, audibleStartFrame)) < 0.95);
        QVERIFY(h.remoteBus->peakQueued > 0);
        QVERIFY(h.remoteBus->peakQueued <= 1440);
        QCOMPARE(remoteErrors.count(), 0);

        const double stationPanA = h.station.sliceById(h.sliceA)->audioPan();
        const double stationPanB = h.station.sliceById(h.sliceB)->audioPan();
        const int stationAfGainA = h.station.sliceById(h.sliceA)->afGain();
        const int stationAfGainB = h.station.sliceById(h.sliceB)->afGain();
        const bool stationMutedA = h.station.sliceById(h.sliceA)->muted();
        const bool stationMutedB = h.station.sliceById(h.sliceB)->muted();
        const int flushesBeforeMute = h.remoteBus->flushes;
        h.remote.audioEngine()->setMasterMuted(true);
        QTRY_VERIFY_WITH_TIMEOUT(!latestAudioContext(controls)
                                     .value(QStringLiteral("enabled")).toBool(), 5000);
        QCOMPARE(latestAudioContext(controls).size(), 9);
        QCOMPARE(latestAudioContext(controls).value(QStringLiteral("reason")).toString(),
                 QStringLiteral("client-disabled"));
        QVERIFY(!latestAudioContext(controls).contains(QStringLiteral("encoder")));
        QTRY_VERIFY(remoteMedia.acceptedAudioContext().has_value()
                    && !remoteMedia.acceptedAudioContext()->enabled);
        QVERIFY(remoteMedia.acceptedAudioContext()->offReason.has_value());
        QCOMPARE(*remoteMedia.acceptedAudioContext()->offReason,
                 RemoteAudioOffReason::ClientDisabled);
        QVERIFY(!remoteMedia.acceptedAudioContext()->encoder.has_value());
        QVERIFY(h.remoteBus->flushes > flushesBeforeMute);
        QVERIFY(h.remoteBus->outputPacing().has_value());
        QCOMPARE(h.remoteBus->outputPacing()->queuedFrames, 0);
        QCOMPARE(h.station.sliceById(h.sliceA)->audioPan(), stationPanA);
        QCOMPARE(h.station.sliceById(h.sliceB)->audioPan(), stationPanB);
        QCOMPARE(h.station.sliceById(h.sliceA)->afGain(), stationAfGainA);
        QCOMPARE(h.station.sliceById(h.sliceB)->afGain(), stationAfGainB);
        QCOMPARE(h.station.sliceById(h.sliceA)->muted(), stationMutedA);
        QCOMPARE(h.station.sliceById(h.sliceB)->muted(), stationMutedB);

        const int heardBeforeResume = h.remoteBus->heard.size() / 2;
        h.remote.audioEngine()->setMasterMuted(false);
        QTRY_VERIFY_WITH_TIMEOUT(latestAudioContext(controls)
                                     .value(QStringLiteral("enabled")).toBool(), 5000);
        const QJsonObject resumed = latestAudioContext(controls);
        QVERIFY(static_cast<quint32>(resumed.value(QStringLiteral("generation")).toInteger())
                > initialGeneration);
        QVERIFY(resumed.value(QStringLiteral("firstSequence")).isDouble());
        QVERIFY(resumed.value(QStringLiteral("firstTimestamp")).isDouble());
        QCOMPARE(resumed.value(QStringLiteral("encoder")).toObject(), defaultEncoderJson());
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBeforeResume) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBeforeResume) > 0.5,
                                 10000);

        h.client.disconnectFromStation(QStringLiteral("test reconnect"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.client.mediaAvailable(), 5000);
        // A retired media session forgets what it accepted.
        QTRY_VERIFY(!remoteMedia.acceptedAudioContext().has_value());
        const int heardBeforeReconnect = h.remoteBus->heard.size() / 2;
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(!latestAudioContext(controls).isEmpty()
                                 && latestAudioContext(controls)
                                        .value(QStringLiteral("connectionId")).toString()
                                        != initialConnection, 15000);
        const QJsonObject reconnected = latestAudioContext(controls);
        QVERIFY(reconnected.value(QStringLiteral("enabled")).toBool());
        QVERIFY(reconnected.value(QStringLiteral("ssrc")) != initial.value(QStringLiteral("ssrc")));
        QCOMPARE(reconnected.value(QStringLiteral("encoder")).toObject(), defaultEncoderJson());
        QTRY_VERIFY(remoteMedia.acceptedAudioContext().has_value()
                    && remoteMedia.acceptedAudioContext()->connectionId
                        == reconnected.value(QStringLiteral("connectionId")).toString());
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBeforeReconnect) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBeforeReconnect) > 0.5,
                                 15000);
        QCOMPARE(remoteErrors.count(), 0);
        // Every accepted context carried exactly the detail its state calls for.
        QCOMPARE(accepted.signalsWithoutContext, 0);
        QVERIFY(accepted.eachReportedOnce());
        QVERIFY(!accepted.contexts.isEmpty());
        for (const RemoteAudioContextMessage& context : accepted.contexts) {
            QCOMPARE(context.encoder.has_value(), context.enabled);
            QCOMPARE(context.offReason.has_value(), !context.enabled);
        }

        source.stop();
        speaker.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void minorSevenPeersKeepLegacyAudioContext()
    {
        Harness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy remoteErrors(&remoteMedia, &RemoteMediaController::errorOccurred);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        AcceptedContexts accepted(remoteMedia);
        // A minor-7 Core reports no profile; the GUI says so rather than
        // naming one it assumes.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral(
            "^Remote audio receiving: codec profile not reported by Core, context \\d+$")));

        QTimer source;
        source.setInterval(10);
        source.setTimerType(Qt::PreciseTimer);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        speaker.setTimerType(Qt::PreciseTimer);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(kFrames); });
        source.start();
        speaker.start();

        // Both ends agree minor 7: a Core and a GUI from before the audio
        // status detail. Audio must still start, on the eight-key context.
        // Core's first context is preceded by a forged copy in the minor-8
        // shape with a different first sequence; a minor-7 GUI must refuse
        // it without advancing its generation.
        h.connectSession(quint16{7}, [](const QJsonObject& real) {
            QJsonObject forged = real;
            forged.insert(QStringLiteral("encoder"), defaultEncoderJson());
            forged.insert(QStringLiteral("firstSequence"),
                          (real.value(QStringLiteral("firstSequence")).toInteger() + 1000)
                              % 65536);
            return forged;
        });
        if (QTest::currentTestFailed()) { return; }
        QVERIFY(!h.server.remoteAudioStatusAvailable());
        QVERIFY(!h.client.remoteAudioStatusAvailable());
        QVERIFY(!remoteMedia.audioDetailNegotiated());
        QTRY_VERIFY_WITH_TIMEOUT(latestAudioContext(controls)
                                     .value(QStringLiteral("enabled")).toBool(), 15000);
        QCOMPARE(latestAudioContext(controls).size(), 8);

        const int heardBefore = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBefore) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBefore) > 0.5,
                                 15000);

        h.remote.audioEngine()->setMasterMuted(true);
        QTRY_VERIFY_WITH_TIMEOUT(!latestAudioContext(controls)
                                     .value(QStringLiteral("enabled")).toBool(), 5000);
        QTRY_VERIFY(remoteMedia.acceptedAudioContext().has_value()
                    && !remoteMedia.acceptedAudioContext()->enabled);

        const QList<QJsonObject> received = audioContexts(controls);
        QCOMPARE(h.stationLink->forgedContexts, 1);
        QVERIFY(received.size() >= 3);
        // The forged copy reached the GUI, then the real one, same generation.
        QCOMPARE(received.at(0).size(), 9);
        QCOMPARE(received.at(0).value(QStringLiteral("generation")).toInteger(),
                 received.at(1).value(QStringLiteral("generation")).toInteger());
        for (qsizetype index = 1; index < received.size(); ++index) {
            QCOMPARE(received.at(index).size(), 8);
            QVERIFY(!received.at(index).contains(QStringLiteral("encoder")));
            QVERIFY(!received.at(index).contains(QStringLiteral("reason")));
        }
        QVERIFY(!accepted.contexts.isEmpty());
        QCOMPARE(qint64{accepted.contexts.constFirst().generation},
                 received.at(1).value(QStringLiteral("generation")).toInteger());
        QCOMPARE(qint64{accepted.contexts.constFirst().firstSequence},
                 received.at(1).value(QStringLiteral("firstSequence")).toInteger());
        QCOMPARE(accepted.signalsWithoutContext, 0);
        QVERIFY(accepted.eachReportedOnce());
        for (const RemoteAudioContextMessage& context : accepted.contexts) {
            QVERIFY(!context.encoder.has_value());
            QVERIFY(!context.offReason.has_value());
        }
        QCOMPARE(remoteErrors.count(), 0);

        source.stop();
        speaker.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void minorEightReportsWhyAudioIsOffAndRefusesForgedContexts()
    {
        Harness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy remoteErrors(&remoteMedia, &RemoteMediaController::errorOccurred);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        AcceptedContexts accepted(remoteMedia);

        QTimer source;
        source.setInterval(10);
        source.setTimerType(Qt::PreciseTimer);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        speaker.setTimerType(Qt::PreciseTimer);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(kFrames); });
        source.start();
        speaker.start();

        // Core's first context is preceded by a forged copy in the minor-7
        // shape with a different first sequence; a minor-8 GUI must refuse
        // it without advancing its generation.
        h.connectSession(std::nullopt, [](const QJsonObject& real) {
            QJsonObject forged = real;
            forged.remove(QStringLiteral("encoder"));
            forged.remove(QStringLiteral("reason"));
            forged.insert(QStringLiteral("firstSequence"),
                          (real.value(QStringLiteral("firstSequence")).toInteger() + 1000)
                              % 65536);
            return forged;
        });
        QVERIFY(h.server.remoteAudioStatusAvailable());
        QVERIFY(remoteMedia.audioDetailNegotiated());
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.acceptedAudioContext().has_value()
                                     && remoteMedia.acceptedAudioContext()->enabled, 15000);
        {
            const QList<QJsonObject> received = audioContexts(controls);
            QCOMPARE(h.stationLink->forgedContexts, 1);
            QVERIFY(received.size() >= 2);
            QCOMPARE(received.at(0).size(), 8);
            QCOMPARE(received.at(0).value(QStringLiteral("generation")).toInteger(),
                     received.at(1).value(QStringLiteral("generation")).toInteger());
            QVERIFY(!accepted.contexts.isEmpty());
            QCOMPARE(qint64{accepted.contexts.constFirst().generation},
                     received.at(1).value(QStringLiteral("generation")).toInteger());
            QCOMPARE(qint64{accepted.contexts.constFirst().firstSequence},
                     received.at(1).value(QStringLiteral("firstSequence")).toInteger());
        }
        const int heardBefore = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBefore) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBefore) > 0.5,
                                 15000);

        // The station radio drops with audio wanted. Core says why at once,
        // ahead of the GUI's own mirror of the radio state.
        h.station.setConnectionStateForTest(ConnectionState::Disconnected);
        QTRY_VERIFY_WITH_TIMEOUT(accepted.any([](const RemoteAudioContextMessage& context) {
            return !context.enabled
                && context.offReason == RemoteAudioOffReason::RadioOffline;
        }), 5000);
        // Once the GUI mirrors the offline radio it withdraws its own
        // request, and Core reports that choice.
        QTRY_VERIFY_WITH_TIMEOUT(!h.remote.isConnected(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(accepted.contexts.constLast().offReason
                                     == RemoteAudioOffReason::ClientDisabled, 5000);

        // The radio returns, and so does audio.
        const int heardBeforeReturn = h.remoteBus->heard.size() / 2;
        h.station.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_VERIFY_WITH_TIMEOUT(h.remote.isConnected(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(accepted.contexts.constLast().enabled, 10000);
        QVERIFY(accepted.contexts.constLast().encoder.has_value());
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBeforeReturn) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBeforeReturn) > 0.5,
                                 15000);

        QCOMPARE(accepted.signalsWithoutContext, 0);
        QVERIFY(accepted.eachReportedOnce());
        for (const RemoteAudioContextMessage& context : accepted.contexts) {
            QCOMPARE(context.encoder.has_value(), context.enabled);
            QCOMPARE(context.offReason.has_value(), !context.enabled);
        }
        QCOMPARE(remoteErrors.count(), 0);

        source.stop();
        speaker.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

QTEST_GUILESS_MAIN(TstRemoteAudioSession)
#include "tst_remote_audio_session.moc"
