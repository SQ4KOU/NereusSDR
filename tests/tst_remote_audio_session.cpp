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
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/PacedAudioBus.h"
#include "gui/RemoteMediaController.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QSignalSpy>
#include <QTimer>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

using namespace NereusSDR;

namespace {

constexpr int kFrames = 480;
constexpr double kPi = 3.14159265358979323846;

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

    void connectSession()
    {
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(server.mediaAvailable());
    }

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
        QTRY_VERIFY_WITH_TIMEOUT(!latestAudioContext(controls).isEmpty(), 15000);
        const QJsonObject initial = latestAudioContext(controls);
        QCOMPARE(initial.size(), 8);
        QVERIFY(initial.value(QStringLiteral("enabled")).toBool());
        QVERIFY(initial.value(QStringLiteral("generation")).toInteger() > 0);
        const QString initialConnection = initial.value(QStringLiteral("connectionId")).toString();
        const quint32 initialGeneration = static_cast<quint32>(
            initial.value(QStringLiteral("generation")).toInteger());

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
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBeforeResume) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBeforeResume) > 0.5,
                                 10000);

        h.client.disconnectFromStation(QStringLiteral("test reconnect"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.client.mediaAvailable(), 5000);
        const int heardBeforeReconnect = h.remoteBus->heard.size() / 2;
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(!latestAudioContext(controls).isEmpty()
                                 && latestAudioContext(controls)
                                        .value(QStringLiteral("connectionId")).toString()
                                        != initialConnection, 15000);
        const QJsonObject reconnected = latestAudioContext(controls);
        QVERIFY(reconnected.value(QStringLiteral("enabled")).toBool());
        QVERIFY(reconnected.value(QStringLiteral("ssrc")) != initial.value(QStringLiteral("ssrc")));
        QTRY_VERIFY_WITH_TIMEOUT(channelEnergy(h.remoteBus->heard, 0, heardBeforeReconnect) > 0.5
                                 && channelEnergy(h.remoteBus->heard, 1, heardBeforeReconnect) > 0.5,
                                 15000);
        QCOMPARE(remoteErrors.count(), 0);

        source.stop();
        speaker.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

QTEST_GUILESS_MAIN(TstRemoteAudioSession)
#include "tst_remote_audio_session.moc"
