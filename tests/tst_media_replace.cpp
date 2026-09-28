// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_media_replace.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 29 (R-IOS-16; the pairing design, section 5.4; the
// remote media control document, "Replacing the media connection";
// mediaReplaceVersion 1): media moves to a new peer connection with no
// gap and no repeat.
//
//   - The Core (DaemonMediaController, transports fake only at the
//     network boundary): a replacement is refused unless it replaces the
//     current, ready peer and the radio is idle; once the new peer is
//     ready every audio packet goes out on both with the same sequence
//     number and timestamp and each peer's own SSRC; kReplaceOverlapMs
//     later the new peer takes over, the Core says `replace`, the old peer
//     sends nothing more and closes after kReplaceDrainMs; a new peer that
//     closes before that leaves the current one untouched.
//   - Dual receive (RtpDuplicateFilter with the window's AudioJitterBuffer,
//     on a simulated clock): a switch from a slow, lossy relay path to a
//     fast direct one plays every interval once, none concealed and none
//     twice.
//   - End to end over the real DTLS/SRTP session (the Core's and the
//     window's media controllers, real Opus): the window replaces its
//     media while listening; the audio heard has no gap over 40 ms and no
//     repeat, the session signs in and snapshots nothing new, and no new
//     audio context restarts playback.
//
// No real audio device is opened (the harness's paced bus stands in for
// the speakers).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <map>
#include <memory>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/HpsdrModel.h"
#include "core/MoxController.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/AudioJitterBuffer.h"
#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/MediaPeer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/DualPathAudio.h"
#include "core/session/media/RtpDuplicateFilter.h"
#include "core/session/PathRacer.h"
#include "OperatorWording.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/RemoteAudioSessionHarness.h"
#include "fakes/UpgradedCoreToken.h"
#include "gui/RemoteMediaController.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "RealtimeTestLoad.h"
#include "OperatorWording.h"

using namespace NereusSDR;

namespace {

constexpr char kFirst[] = "11111111-2222-4333-8444-555555555555";
constexpr char kSecond[] = "66666666-7777-4888-9999-aaaaaaaaaaaa";
constexpr int kDspFrames = 64;

class FakeTransport final : public IMediaTransport {
public:
    explicit FakeTransport(QObject* parent = nullptr) : IMediaTransport(parent) {}

    bool start(const StartOptions& options) override
    {
        startOptions = options;
        started = true;
        return true;
    }
    void stop() override
    {
        if (!stopped) {
            stopped = true;
            readyState = false;
        }
    }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray&) override { return readyState; }
    bool sendRtp(const QByteArray& packet) override
    {
        if (!readyState) {
            return false;
        }
        rtpPackets.append(packet);
        return true;
    }
    bool isReady() const override { return readyState; }
    std::optional<MediaIcePath> selectedPath() const override { return path; }
    void becomeReady()
    {
        readyState = true;
        emit ready();
    }
    void close() { emit closed(); }

    bool started{false};
    bool stopped{false};
    bool readyState{false};
    StartOptions startOptions{Role::Answerer, 0};
    std::optional<MediaIcePath> path;
    QList<QByteArray> rtpPackets;
};

quint32 ssrcOf(const QByteArray& packet)
{
    return qFromBigEndian<quint32>(packet.constData() + 8);
}
quint32 timestampOf(const QByteArray& packet)
{
    return qFromBigEndian<quint32>(packet.constData() + 4);
}
quint16 sequenceOf(const QByteArray& packet)
{
    return qFromBigEndian<quint16>(packet.constData() + 2);
}

QVector<float> stereoBlock(float left, float right)
{
    QVector<float> block(kDspFrames * 2);
    for (int frame = 0; frame < kDspFrames; ++frame) {
        block[frame * 2] = left;
        block[frame * 2 + 1] = right;
    }
    return block;
}

QJsonObject control(const QString& op, const QString& connectionId)
{
    return {{QStringLiteral("op"), op}, {QStringLiteral("connectionId"), connectionId}};
}

QJsonObject replaceControl(const QString& connectionId, const QString& replaces)
{
    return {{QStringLiteral("op"), QStringLiteral("replace")},
            {QStringLiteral("connectionId"), connectionId},
            {QStringLiteral("replaces"), replaces}};
}

QList<QJsonObject> controlsNamed(const QSignalSpy& spy, const QString& op)
{
    QList<QJsonObject> out;
    for (const QList<QVariant>& call : spy) {
        const QJsonObject message = call.at(0).toJsonObject();
        if (message.value(QStringLiteral("op")) == op) {
            out.append(message);
        }
    }
    return out;
}

// The Core's media controller with its transports fake at the network, as
// tst_daemon_audio_session stands it up; each peer it makes is kept.
struct CoreHarness {
    QTemporaryDir directory;
    AppSettings settings;
    RadioModel radio;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy settingsProxy;
    StationClient client{&remote, &settingsProxy};
    QList<QPointer<FakeTransport>> transports;
    DaemonMediaController controller;
    AudioEngine* engine{nullptr};
    int slice{-1};

    CoreHarness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&radio, settings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()))
        , controller(&server, &radio, nullptr,
                     [this](QObject* parent) -> IMediaTransport* {
                         auto* transport = new FakeTransport(parent);
                         transports.append(transport);
                         return transport;
                     })
    {
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        engine = radio.audioEngine();
        engine->masterMixForTest().setRampFrames(1);
        engine->masterMixForTest().setSlewUpFrames(0);
        slice = radio.addSlice();
        engine->setSliceStreaming(slice, true);
        server.setMediaEnabled(true);
    }

    bool establishWithAudio()
    {
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        if (!QTest::qWaitFor([this] { return server.mediaAvailable() && client.mediaAvailable(); },
                             10000)) {
            qWarning() << "media never available";
            return false;
        }
        const bool sent = client.sendMediaControl(
            control(QStringLiteral("start"), QLatin1String(kFirst)), client.sessionEpoch());
        if (!QTest::qWaitFor([this] { return !transports.isEmpty(); }, 5000)) {
            qWarning() << "no transport; start sent" << sent;
            return false;
        }
        transports.first()->becomeReady();
        client.sendMediaControl({{QStringLiteral("op"), QStringLiteral("audio")},
                                 {QStringLiteral("connectionId"), QLatin1String(kFirst)},
                                 {QStringLiteral("revision"), 1},
                                 {QStringLiteral("enabled"), true}},
                                client.sessionEpoch());
        return QTest::qWaitFor([this] { return controller.audioDiagnostics().activeContext; },
                               5000);
    }

    void feed(int blocks)
    {
        const QVector<float> tone = stereoBlock(0.25f, 0.25f);
        for (int b = 0; b < blocks; ++b) {
            for (int delivered = 0; delivered < DaemonAudioSource::kBlockFrames;
                 delivered += kDspFrames) {
                engine->rxBlockReady(slice, tone.constData(), kDspFrames);
            }
        }
    }
};

} // namespace

class TstMediaReplace final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        OpusAudioEncoder encoder;
        if (!encoder.isReady()) {
            QSKIP("Opus encoder is unavailable in this build");
        }
    }
    // The load when a real-time case failed (R-R3-21, R-R3-40).
    void cleanup() { NereusSDR::RealtimeTestLoad::printLoadAverageIfFailed(); }

    // ── The Core ──────────────────────────────────────────────────────

    // Every audio packet goes out on both peers once the new one is ready,
    // with the same sequence number and timestamp and each peer's own
    // SSRC; the new peer then takes over and the Core says so.
    void theCoreSendsOnBothThenTheNewPeerTakesOver()
    {
        CoreHarness h;
        QVERIFY(h.establishWithAudio());
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        FakeTransport* first = h.transports.first();
        h.feed(2);
        QTRY_VERIFY(first->rtpPackets.size() >= 2);
        const quint32 firstSsrc = first->startOptions.localAudioSsrc;
        QCOMPARE(ssrcOf(first->rtpPackets.constLast()), firstSsrc);

        QVERIFY(h.client.sendMediaControl(replaceControl(QLatin1String(kSecond),
                                                         QLatin1String(kFirst)),
                                          h.client.sessionEpoch()));
        QTRY_COMPARE(h.transports.size(), 2);
        FakeTransport* second = h.transports.at(1);
        QVERIFY(second->started);
        QCOMPARE(second->startOptions.role, IMediaTransport::Role::Offerer);
        const quint32 secondSsrc = second->startOptions.localAudioSsrc;
        QVERIFY(secondSsrc != 0 && secondSsrc != firstSsrc);
        // Not yet ready: audio goes on the current peer alone.
        const int before = first->rtpPackets.size();
        h.feed(1);
        QTRY_COMPARE(first->rtpPackets.size(), before + 1);
        QVERIFY(second->rtpPackets.isEmpty());

        second->becomeReady();
        h.feed(3);
        QTRY_VERIFY(second->rtpPackets.size() >= 3);
        // The same packets on both: sequence and timestamp alike, SSRC each
        // peer's own.
        const QByteArray onFirst = first->rtpPackets.constLast();
        const QByteArray onSecond = second->rtpPackets.constLast();
        QCOMPARE(sequenceOf(onSecond), sequenceOf(onFirst));
        QCOMPARE(timestampOf(onSecond), timestampOf(onFirst));
        QCOMPARE(ssrcOf(onFirst), firstSsrc);
        QCOMPARE(ssrcOf(onSecond), secondSsrc);
        QCOMPARE(onSecond.mid(12), onFirst.mid(12));

        // kReplaceOverlapMs later: the new peer takes over, `replace` says so.
        QTRY_COMPARE_WITH_TIMEOUT(controlsNamed(controls, QStringLiteral("replace")).size(), 1,
                                  DaemonMediaController::kReplaceOverlapMs + 3000);
        const QJsonObject done = controlsNamed(controls, QStringLiteral("replace")).first();
        QCOMPARE(done.value(QStringLiteral("connectionId")).toString(), QLatin1String(kSecond));
        QCOMPARE(done.value(QStringLiteral("replaces")).toString(), QLatin1String(kFirst));
        QCOMPARE(done.size(), 3);
        // Nothing more on the old peer; the new one carries on, under its
        // own SSRC, the timeline unbroken.
        const int firstCount = first->rtpPackets.size();
        const QByteArray lastOnSecond = second->rtpPackets.constLast();
        const int secondBefore = second->rtpPackets.size();
        h.feed(3);
        QTRY_COMPARE(second->rtpPackets.size(), secondBefore + 3);
        QTest::qWait(50);
        QCOMPARE(first->rtpPackets.size(), firstCount);
        QCOMPARE(ssrcOf(second->rtpPackets.constLast()), secondSsrc);
        QCOMPARE(sequenceOf(second->rtpPackets.constLast()),
                 static_cast<quint16>(sequenceOf(lastOnSecond) + 3));
        // No whole-peer refusal went out for either peer, and no new audio
        // context restarted playback.
        QVERIFY(controlsNamed(controls, QStringLiteral("rejected")).isEmpty());
        // (The first context, generation 1, may still have been on its way
        // when the spy began.)
        for (const QJsonObject& context : controlsNamed(controls, QStringLiteral("audio-context"))) {
            QCOMPARE(context.value(QStringLiteral("generation")).toInt(), 1);
            QCOMPARE(context.value(QStringLiteral("connectionId")).toString(),
                     QLatin1String(kFirst));
        }
        // Operations now name the new connection.
        const qsizetype contextsBefore =
            controlsNamed(controls, QStringLiteral("audio-context")).size();
        h.client.sendMediaControl({{QStringLiteral("op"), QStringLiteral("audio")},
                                   {QStringLiteral("connectionId"), QLatin1String(kSecond)},
                                   {QStringLiteral("revision"), 2},
                                   {QStringLiteral("enabled"), false}},
                                  h.client.sessionEpoch());
        QTRY_VERIFY(controlsNamed(controls, QStringLiteral("audio-context")).size() > contextsBefore);
        QCOMPARE(controlsNamed(controls, QStringLiteral("audio-context")).constLast()
                     .value(QStringLiteral("connectionId")).toString(),
                 QLatin1String(kSecond));
        // The old peer closes after draining.
        const QPointer<FakeTransport> firstLeft = h.transports.first();
        QTRY_VERIFY_WITH_TIMEOUT(firstLeft.isNull() || firstLeft->stopped,
                                 DaemonMediaController::kReplaceDrainMs + 3000);
    }

    // Refused, and the current peer untouched: a replacement naming a peer
    // that is not the current one, the current id itself, a second one
    // while one is under way.
    void aReplacementOfAnotherPeerIsRefused()
    {
        CoreHarness h;
        QVERIFY(h.establishWithAudio());
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        const QString other = QStringLiteral("99999999-8888-4777-8666-555555555555");
        h.client.sendMediaControl(replaceControl(QLatin1String(kSecond), other),
                                  h.client.sessionEpoch());
        h.client.sendMediaControl(replaceControl(QLatin1String(kFirst), QLatin1String(kFirst)),
                                  h.client.sessionEpoch());
        QTRY_COMPARE(controlsNamed(controls, QStringLiteral("rejected")).size(), 2);
        for (const QJsonObject& refusal : controlsNamed(controls, QStringLiteral("rejected"))) {
            QCOMPARE(refusal.value(QStringLiteral("endpointId")).toInt(), 0);
            QCOMPARE(refusal.value(QStringLiteral("reason")).toString(),
                     QStringLiteral("The Core did not move audio and display: that connection "
                                    "is not the current one."));
        }
        QCOMPARE(h.transports.size(), 1);
        QVERIFY(!h.transports.first()->stopped);
    }

    void legacyRelayMediaRefusesOverlapWithoutDisturbingAudio()
    {
        CoreHarness h;
        QVERIFY(h.establishWithAudio());
        FakeTransport* const first = h.transports.first();
        MediaIcePath relayPath;
        relayPath.remoteAddress = QStringLiteral("127.0.0.1");
        relayPath.ownedLoopbackShim = true;
        first->path = relayPath;
        h.feed(4);
        const qsizetype sentBefore = first->rtpPackets.size();
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        h.client.sendMediaControl(replaceControl(QLatin1String(kSecond), QLatin1String(kFirst)),
                                  h.client.sessionEpoch());
        QTRY_COMPARE(controlsNamed(controls, QStringLiteral("rejected")).size(), 1);
        QCOMPARE(h.transports.size(), 1);
        QVERIFY(!first->stopped);
        h.feed(4);
        QVERIFY(first->rtpPackets.size() > sentBefore);
    }

    // Nothing moves while the radio is on the air (the media document's
    // "Transmit"): refused in plain words, and the key untouched.
    void noReplacementWhileKeyed()
    {
        CoreHarness h;
        QVERIFY(h.establishWithAudio());
        MoxController* mox = h.radio.moxController();
        mox->setTimerIntervals(0, 0, 0, 0, 0, 0);
        h.radio.transmitModel().setMicSourceLocked(false);
        h.radio.transmitModel().setMicSource(MicSource::Radio);
        if (SliceModel* slice = h.radio.sliceById(h.slice)) {
            slice->setDspMode(DSPMode::USB);
            slice->setFrequency(14200000.0);
        }
        h.server.setRemoteTransmitAllowed(true);
        mox->setMox(true);
        QTRY_VERIFY(mox->state() != MoxState::Rx);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        h.client.sendMediaControl(replaceControl(QLatin1String(kSecond), QLatin1String(kFirst)),
                                  h.client.sessionEpoch());
        QTRY_COMPARE(controlsNamed(controls, QStringLiteral("rejected")).size(), 1);
        QCOMPARE(controlsNamed(controls, QStringLiteral("rejected")).first()
                     .value(QStringLiteral("reason")).toString(),
                 QStringLiteral("The Core did not move audio and display: the radio is "
                                "transmitting."));
        QCOMPARE(h.transports.size(), 1);
        QVERIFY(mox->state() != MoxState::Rx);
        mox->setMox(false);
        QTRY_COMPARE(mox->state(), MoxState::Rx);
    }

    // Task 29 fix wave (review Important 2): MOX released but its unkey
    // delay still running (MOX off, the state not yet receive): a
    // replacement is refused as while keyed.
    void noReplacementDuringTheUnkeyDelay()
    {
        CoreHarness h;
        QVERIFY(h.establishWithAudio());
        MoxController* mox = h.radio.moxController();
        mox->setTimerIntervals(0, 0, 0, 0, 0, 0);
        h.radio.transmitModel().setMicSourceLocked(false);
        h.radio.transmitModel().setMicSource(MicSource::Radio);
        if (SliceModel* slice = h.radio.sliceById(h.slice)) {
            slice->setDspMode(DSPMode::USB);
            slice->setFrequency(14200000.0);
        }
        h.server.setRemoteTransmitAllowed(true);
        mox->setMox(true);
        QTRY_VERIFY(mox->state() != MoxState::Rx);
        QTRY_VERIFY(mox->isMox());
        mox->setTimerIntervals(3000, 3000, 3000, 3000, 3000, 3000);
        mox->setMox(false);
        QTRY_VERIFY(!mox->isMox());
        QVERIFY(mox->state() != MoxState::Rx);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        h.client.sendMediaControl(replaceControl(QLatin1String(kSecond), QLatin1String(kFirst)),
                                  h.client.sessionEpoch());
        QTRY_COMPARE(controlsNamed(controls, QStringLiteral("rejected")).size(), 1);
        QCOMPARE(controlsNamed(controls, QStringLiteral("rejected")).first()
                     .value(QStringLiteral("reason")).toString(),
                 QStringLiteral("The Core did not move audio and display: the radio is "
                                "transmitting."));
        QCOMPARE(h.transports.size(), 1);
        QVERIFY(mox->state() != MoxState::Rx);
        QTRY_COMPARE_WITH_TIMEOUT(mox->state(), MoxState::Rx, 20000);
    }

    // A new peer that closes before it takes over is dropped with the
    // whole-peer refusal for its own id; the current peer carries on.
    void aNewPeerThatClosesLeavesTheCurrentOne()
    {
        CoreHarness h;
        QVERIFY(h.establishWithAudio());
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        h.client.sendMediaControl(replaceControl(QLatin1String(kSecond), QLatin1String(kFirst)),
                                  h.client.sessionEpoch());
        QTRY_COMPARE(h.transports.size(), 2);
        h.transports.at(1)->close();
        QTRY_COMPARE(controlsNamed(controls, QStringLiteral("rejected")).size(), 1);
        const QJsonObject refusal = controlsNamed(controls, QStringLiteral("rejected")).first();
        QCOMPARE(refusal.value(QStringLiteral("connectionId")).toString(), QLatin1String(kSecond));
        QCOMPARE(refusal.value(QStringLiteral("reason")).toString(),
                 QString::fromLatin1(kMediaPeerClosedReason));
        FakeTransport* first = h.transports.first();
        QVERIFY(!first->stopped);
        const int before = first->rtpPackets.size();
        h.feed(1);
        QTRY_COMPARE(first->rtpPackets.size(), before + 1);
        QVERIFY(controlsNamed(controls, QStringLiteral("replace")).isEmpty());
    }

    // ── Dual receive ──────────────────────────────────────────────────

    // The window's jitter queue fed from two paths across a switch, on a
    // simulated clock: a relay path of 150 ms with 2 % loss and jitter up
    // to 60 ms, then from the replacement's ready a direct path of 20 ms
    // with 1 % loss, both carrying the same packets for the overlap, then
    // the direct path alone, merged by DualPathAudio as the window merges
    // them. Every interval plays once or is skipped once: none concealed
    // that one path carried, none twice, and never two in a row skipped.
    void twoPathsAcrossASwitchPlayEachIntervalOnce()
    {
        constexpr qint64 kMs = 1'000'000;
        constexpr qint64 kPacketNs = AudioJitterBuffer::kDefaultPacketDurationNs;
        constexpr int kFramesPerPacket = AudioJitterBuffer::kDefaultPacketFrames;
        constexpr int kPackets = 400;         // 16 s of audio
        constexpr int kReadyAt = 60;          // the replacement is ready
        constexpr quint32 kSsrc = 0xAAAA;
        const int doneAt = kReadyAt + DaemonMediaController::kReplaceOverlapMs / 40;
        AudioJitterBuffer jitter;
        jitter.reset(0);
        qint64 now = 0;
        DualPathAudio dual([&jitter, &now](const QByteArray& packet) {
            jitter.insert(packet, qFromBigEndian<quint32>(packet.constData() + 4), now);
        });
        struct Arrival {
            quint32 timestamp;
            bool fromNew;
        };
        std::multimap<qint64, Arrival> arrivals;
        quint32 lcg = 12345;
        const auto random = [&lcg] {
            lcg = lcg * 1103515245u + 12345u;
            return (lcg >> 8) % 10000;
        };
        for (int i = 0; i < kPackets; ++i) {
            const qint64 sent = i * kPacketNs;
            const quint32 timestamp = static_cast<quint32>(i * kFramesPerPacket);
            const bool onRelay = i < doneAt;
            const bool onDirect = i >= kReadyAt;
            if (onRelay && random() >= 200) {
                arrivals.emplace(sent + 150 * kMs + static_cast<qint64>(random() % 60) * kMs,
                                 Arrival{timestamp, false});
            }
            if (onDirect && random() >= 100) {
                arrivals.emplace(sent + 20 * kMs, Arrival{timestamp, true});
            }
        }
        QSet<quint32> arrived;
        for (const auto& [at, arrival] : arrivals) {
            arrived.insert(arrival.timestamp);
        }
        // The Core's `replace` reaches the window over the direct control
        // path just after it stops sending on the relay.
        const qint64 doneNs = doneAt * kPacketNs + 20 * kMs;
        bool doneSeen = false;
        QList<quint32> played;
        QList<quint32> concealed;
        auto next = arrivals.cbegin();
        const qint64 end = kPackets * kPacketNs + 800 * kMs;
        const auto packetFor = [](quint32 timestamp) {
            QByteArray packet(20, 'x');
            packet[0] = char(0x80);
            qToBigEndian<quint32>(timestamp, packet.data() + 4);
            qToBigEndian<quint32>(kSsrc, packet.data() + 8);
            return packet;
        };
        for (now = 0; now < end; now += kMs) {
            const qint64 nowMs = now / kMs;
            if (!doneSeen && now >= kReadyAt * kPacketNs + 20 * kMs && !dual.active()) {
                dual.start(nowMs);
            }
            if (!doneSeen && now >= doneNs) {
                dual.oldPathDone(nowMs);
                doneSeen = true;
            }
            for (; next != arrivals.cend() && next->first <= now; ++next) {
                dual.submit(packetFor(next->second.timestamp), next->second.fromNew, nowMs);
            }
            dual.tick(nowMs);
            while (const std::optional<AudioJitterBuffer::Playout> out = jitter.takeReady(now)) {
                (out->concealed() ? concealed : played).append(out->timestamp);
            }
            for (const QByteArray& shed : jitter.takeShedPackets()) {
                Q_UNUSED(shed);
            }
        }
        QVERIFY(!dual.active());
        QVERIFY(dual.duplicatesDropped() > 0);
        // Nothing twice, in order.
        const QSet<quint32> unique(played.cbegin(), played.cend());
        QCOMPARE(unique.size(), played.size());
        for (qsizetype i = 1; i < played.size(); ++i) {
            QVERIFY(played.at(i) > played.at(i - 1));
        }
        // Nothing one path carried was concealed.
        for (const quint32 timestamp : std::as_const(concealed)) {
            QVERIFY2(!arrived.contains(timestamp),
                     qPrintable(QStringLiteral("interval %1 concealed though it arrived")
                                    .arg(timestamp / kFramesPerPacket)));
        }
        // The lower delay reached one skipped interval at a time: never two
        // in a row that arrived.
        const QSet<quint32> heard = unique + QSet<quint32>(concealed.cbegin(), concealed.cend());
        int run = 0;
        int longest = 0;
        for (int i = 1; i < kPackets; ++i) {
            const quint32 timestamp = static_cast<quint32>(i * kFramesPerPacket);
            run = !heard.contains(timestamp) && arrived.contains(timestamp) ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        QVERIFY2(longest <= 1, qPrintable(QStringLiteral("%1 intervals in a row skipped")
                                               .arg(longest)));
        // The lead eased to nothing: the direct path's own delay again.
        QCOMPARE(dual.leadMs(), qint64{0});
    }

    // DualPathAudio alone: the new path's packet waits for the old copy,
    // which sets the schedule; a packet the old path never brings goes on
    // at its arrival plus the lead; the lead eases once the old path is
    // done.
    // Task 29 fix wave (review Minor 11): packets the new path holds go
    // on in the order they came, across the RTP timestamp's wrap.
    void heldPacketsGoOnInArrivalOrderAcrossTheWrap()
    {
        QList<quint32> delivered;
        DualPathAudio dual([&delivered](const QByteArray& packet) {
            delivered.append(qFromBigEndian<quint32>(packet.constData() + 4));
        });
        const auto packet = [](quint32 timestamp) {
            QByteArray bytes(20, 'x');
            qToBigEndian<quint32>(timestamp, bytes.data() + 4);
            qToBigEndian<quint32>(7, bytes.data() + 8);
            return bytes;
        };
        dual.start(0);
        const QList<quint32> sent{0xFFFFF880u, 0xFFFFFC40u, 0x00000000u, 0x000003C0u};
        for (const quint32 timestamp : sent) {
            dual.submit(packet(timestamp), true, 0);
        }
        QVERIFY(delivered.isEmpty());
        // No old copy and no lead known: all due at once, in arrival order.
        dual.tick(DualPathAudio::kMaxWaitMs);
        QCOMPARE(delivered, sent);
    }

    void theNewPathWaitsForTheOldAndThenLeadsNothing()
    {
        QList<QPair<quint32, qint64>> delivered;
        qint64 now = 0;
        DualPathAudio dual([&delivered, &now](const QByteArray& packet) {
            delivered.append({qFromBigEndian<quint32>(packet.constData() + 4), now});
        });
        const auto packet = [](quint32 timestamp) {
            QByteArray bytes(20, 'x');
            qToBigEndian<quint32>(timestamp, bytes.data() + 4);
            qToBigEndian<quint32>(7, bytes.data() + 8);
            return bytes;
        };
        dual.start(0);
        // New first at 0, old at 130: delivered at 130, from the old.
        dual.submit(packet(1920), true, now);
        QVERIFY(delivered.isEmpty());
        now = 130;
        dual.submit(packet(1920), false, now);
        QCOMPARE(delivered.size(), 1);
        QCOMPARE(delivered.first().second, qint64{130});
        QCOMPARE(dual.leadMs(), qint64{130});
        // A packet the old path lost: at its arrival plus the lead.
        now = 200;
        dual.submit(packet(3840), true, now);
        now = 329;
        dual.tick(now);
        QCOMPARE(delivered.size(), 1);
        now = 330;
        dual.tick(now);
        QCOMPARE(delivered.size(), 2);
        // Its late old copy is dropped.
        now = 360;
        dual.submit(packet(3840), false, now);
        QCOMPARE(delivered.size(), 2);
        QCOMPARE(dual.duplicatesDropped(), quint64{2});
        // Done: the lead eases one interval every kEaseIntervalMs.
        dual.oldPathDone(now);
        now += DualPathAudio::kEaseIntervalMs;
        dual.tick(now);
        QCOMPARE(dual.leadMs(), qint64{130 - DualPathAudio::kEaseStepMs});
        for (int i = 0; i < 4; ++i) {
            now += DualPathAudio::kEaseIntervalMs;
            dual.tick(now);
        }
        QCOMPARE(dual.leadMs(), qint64{0});
        QVERIFY(!dual.active());
        // A new path that is gone drops what it held.
        dual.start(now);
        dual.submit(packet(99 * 1920), true, now);
        QCOMPARE(dual.held(), 1);
        dual.newPathGone();
        QCOMPARE(dual.held(), 0);
        QVERIFY(!dual.active());
    }

    // The Core's refusals of a replacement are in plain words.
    void theRefusalsAreInPlainWords()
    {
        for (const char* text :
             {"The Core did not move audio and display: that connection is not the current one.",
              "The Core did not move audio and display: the radio is transmitting."}) {
            QVERIFY2(OperatorWording::isPlain(QLatin1String(text)), text);
        }
    }

    // RtpDuplicateFilter alone: a timestamp is taken once per stream, the
    // streams apart, and it forgets beyond its window.
    void theFilterTakesEachTimestampOncePerStream()
    {
        RtpDuplicateFilter filter;
        QVERIFY(filter.admit(1, 1920));
        QVERIFY(!filter.admit(1, 1920));
        QVERIFY(filter.admit(2, 1920));
        for (int i = 2; i < 2 + RtpDuplicateFilter::kWindow; ++i) {
            QVERIFY(filter.admit(1, static_cast<quint32>(i * 1920)));
        }
        // 1920 has left the window.
        QVERIFY(filter.admit(1, 1920));
        QCOMPARE(filter.dropped(), quint64{1});
        QByteArray packet(12, '\0');
        qToBigEndian<quint32>(7, packet.data() + 4);
        qToBigEndian<quint32>(9, packet.data() + 8);
        QVERIFY(filter.admit(packet));
        QVERIFY(!filter.admit(packet));
        QVERIFY(filter.admit(QByteArray(4, 'x')));
    }

    // ── End to end ────────────────────────────────────────────────────

    // Over the real DTLS/SRTP session with real Opus: the window replaces
    // its media connection while listening. The session signs in and
    // snapshots nothing new, no new audio context restarts playback, the
    // copies are dropped before the jitter queue, and the tone heard has
    // no gap over 40 ms.
    void aReplacementWhileListeningHasNoGapAndNoRepeat()
    {
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy controls(&h.client, &StationClient::mediaControlReceived);
        QSignalSpy handshakes(&h.client, &StationClient::handshakeComplete);
        QSignalSpy authenticated(&h.server, &StationServer::clientAuthenticated);
        QTimer source;
        source.setInterval(10);
        source.setTimerType(Qt::PreciseTimer);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        speaker.setTimerType(Qt::PreciseTimer);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        h.connectSession();
        QVERIFY(h.client.capabilities().mediaReplaceVersion >= 1);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 15000);
        // A second of steady audio first.
        const int steadyFrom = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(h.remoteBus->heard.size() / 2 >= steadyFrom + 48000, 10000);

        const QString firstId = remoteMedia.mediaConnectionId();
        const int handshakesBefore = handshakes.size();
        const int authBefore = authenticated.size();
        const int contextsBefore = [&controls] {
            int n = 0;
            for (const QList<QVariant>& call : controls) {
                if (call.at(0).toJsonObject().value(QStringLiteral("op"))
                    == QLatin1String("audio-context")) {
                    ++n;
                }
            }
            return n;
        }();
        const RemoteAudioReceiverTelemetry before = remoteMedia.audioTelemetry();
        const int switchFrom = h.remoteBus->heard.size() / 2;
        QVERIFY(remoteMedia.replaceConnection());
        QVERIFY(remoteMedia.replacingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(!remoteMedia.replacingConnection()
                                     && remoteMedia.mediaConnectionId() != firstId, 20000);
        // Past the old connection's drain, with audio still coming.
        const int doneAt = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(h.remoteBus->heard.size() / 2
                                     >= doneAt + 48 * (DaemonMediaController::kReplaceDrainMs
                                                       + 1000),
                                 15000);
        const RemoteAudioReceiverTelemetry after = remoteMedia.audioTelemetry();
        source.stop();
        speaker.stop();

        // Copies came on both connections and were dropped before the
        // queue: no duplicate and no late copy reached it.
        QVERIFY(remoteMedia.duplicateAudioDropped() > 0);
        QCOMPARE(after.duplicatePackets, before.duplicatePackets);
        QCOMPARE(after.latePackets, before.latePackets);
        // At most one interval (40 ms) concealed across the move.
        QVERIFY2(after.concealedPackets - before.concealedPackets <= 1,
                 qPrintable(QStringLiteral("concealed %1")
                                .arg(after.concealedPackets - before.concealedPackets)));
        // The tone heard has no silent run over 40 ms (4 blocks of 10 ms).
        const QVector<float>& heard = h.remoteBus->heard;
        int silentRun = 0;
        int longest = 0;
        for (int frame = switchFrom; (frame + 480) * 2 <= heard.size(); frame += 480) {
            double energy = 0.0;
            for (int i = 0; i < 480; ++i) {
                const double l = heard.at((frame + i) * 2);
                energy += l * l;
            }
            silentRun = energy / 480.0 < 1e-6 ? silentRun + 1 : 0;
            longest = std::max(longest, silentRun);
        }
        QVERIFY2(longest <= 4, qPrintable(QStringLiteral("silent for %1 ms").arg(longest * 10)));
        // No sign-in, no snapshot, no new audio context.
        QCOMPARE(handshakes.size(), handshakesBefore);
        QCOMPARE(authenticated.size(), authBefore);
        int contextsAfter = 0;
        for (const QList<QVariant>& call : controls) {
            if (call.at(0).toJsonObject().value(QStringLiteral("op"))
                == QLatin1String("audio-context")) {
                ++contextsAfter;
            }
        }
        QCOMPARE(contextsAfter, contextsBefore);
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A delayed packet from the old connection after promotion must still
    // meet the overlap filter, even when its scheduling phase has ended.
    void lateOldRtpAfterPromotionNeverReachesTheJitterQueue()
    {
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController media(&h.client, &h.remote, nullptr);
        DaemonMediaController daemon(&h.server, &h.station);
        QTimer source;
        source.setInterval(10);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(media.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 15000);
        MediaPeer* const old = media.findChild<MediaPeer*>();
        QVERIFY(old);
        QPointer<MediaPeer> retired(old);
        const quint32 oldSsrc = old->audioSsrc();
        QVERIFY(media.replaceConnection());
        MediaPeer* next = nullptr;
        for (MediaPeer* peer : media.findChildren<MediaPeer*>()) {
            if (peer != old) { next = peer; break; }
        }
        QVERIFY(next);
        QSignalSpy nextRtp(next, &MediaPeer::rtpReceived);
        QTRY_VERIFY_WITH_TIMEOUT(!media.replacingConnection(), 20000);
        nextRtp.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!nextRtp.isEmpty(), 5000);
        QByteArray late;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto& call : nextRtp) {
                const QByteArray packet = call.at(0).toByteArray();
                if (packet.size() >= 12 && ssrcOf(packet) == next->audioSsrc()) {
                    late = packet;
                    return true;
                }
            }
            return false;
        }(), 5000);
        QTest::qWait(120); // any held new-path packet has entered the filter
        QVERIFY(late.size() >= 12);
        qToBigEndian<quint32>(oldSsrc, late.data() + 8);
        const quint64 filteredBefore = media.duplicateAudioDropped();
        const quint64 jitterBefore = media.audioTelemetry().duplicatePackets;
        emit old->rtpReceived(late);
        QCOMPARE(media.duplicateAudioDropped(), filteredBefore + 1);
        QTest::qWait(80);
        QCOMPARE(media.audioTelemetry().duplicatePackets, jitterBefore);
        QTRY_VERIFY_WITH_TIMEOUT(retired.isNull(),
                                 DaemonMediaController::kReplaceDrainMs + 3000);
        source.stop();
        speaker.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A second path move is remembered while the first old peer drains.
    // It must start only after that owner is released, then use the current
    // session path rather than the earlier request's ICE snapshot.
    void anotherMoveWaitsForRetirementAndReleasesTheOldPeer()
    {
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController media(&h.client, &h.remote, nullptr);
        DaemonMediaController daemon(&h.server, &h.station);
        QTimer source;
        source.setInterval(10);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(media.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 15000);
        QPointer<MediaPeer> first(media.findChild<MediaPeer*>());
        QVERIFY(first);
        const QString firstId = media.mediaConnectionId();
        QVERIFY(media.replaceConnection());
        QTRY_VERIFY_WITH_TIMEOUT(!media.replacingConnection()
                                     && media.mediaConnectionId() != firstId, 20000);
        const QString secondId = media.mediaConnectionId();
        QVERIFY(first);
        emit h.client.pathChanged();
        QVERIFY(media.replacePending());
        QVERIFY(!media.replaceConnection());
        QVERIFY(!media.replacingConnection());
        QCOMPARE(media.findChildren<MediaPeer*>().size(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(first.isNull(),
                                 DaemonMediaController::kReplaceDrainMs + 3000);
        QTRY_VERIFY_WITH_TIMEOUT(media.replacingConnection(), 5000);
        // The replacement and current peer are the only GUI owners now.
        QCOMPARE(media.findChildren<MediaPeer*>().size(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!media.replacingConnection()
                                     && media.mediaConnectionId() != secondId, 20000);
        QVERIFY(!media.replacePending());
        // A fourth request during the next drain is discarded on stop.
        emit h.client.pathChanged();
        QVERIFY(media.replacePending());
        source.stop();
        speaker.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
        QVERIFY(!media.replacePending());
        QTest::qWait(DaemonMediaController::kReplaceDrainMs + 100);
        QVERIFY(media.mediaConnectionId().isEmpty());
        QVERIFY(!media.replacingConnection());
        QVERIFY(media.findChildren<MediaPeer*>().isEmpty());
    }

    // Task 29 fix wave (review Important 1): the session moves to a better
    // path right after it signs in, before its media connection is ready,
    // as the race's standby does at snapshot.complete. Media follows the
    // move by itself once it is ready (a replacement kept pending), while
    // listening, with no silent run over 40 ms and no repeat.
    void mediaFollowsAMoveThatCameBeforeItWasReady()
    {
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy moved(&h.client, &StationClient::pathChanged);
        QSignalSpy handshakes(&h.client, &StationClient::handshakeComplete);
        QTimer source;
        source.setInterval(10);
        source.setTimerType(Qt::PreciseTimer);
        connect(&source, &QTimer::timeout, &source, [&h] { h.feedMixedTone(); });
        QTimer speaker;
        speaker.setInterval(10);
        speaker.setTimerType(Qt::PreciseTimer);
        connect(&speaker, &QTimer::timeout, &speaker, [&h] { h.remoteBus->render(480); });
        source.start();
        speaker.start();
        // The better path, ready before the session signs in: a second
        // connection to the same Core with its hello read. The session
        // moves there the moment it signs in, before media is ready (the
        // media controller starts media from the same signal, first).
        auto* stationB = new Test::LoopbackTransport(QStringLiteral("station B"));
        auto* clientB = new Test::LoopbackTransport(QStringLiteral("client B"));
        stationB->linkTo(clientB);
        QList<QByteArray> onB;
        const QMetaObject::Connection helloSpy =
            connect(clientB, &SessionTransport::textReceived, clientB,
                    [&onB](const QByteArray& wire) { onB.append(wire); });
        h.server.acceptTransport(stationB);
        QTRY_VERIFY(!onB.isEmpty());
        disconnect(helloSpy);
        QString firstId;
        bool movedBeforeReady = false;
        connect(&h.client, &StationClient::handshakeComplete, &h.client, [&] {
            firstId = remoteMedia.mediaConnectionId();
            movedBeforeReady = h.client.moveSessionForTest(clientB, PathRacer::ThisNetwork);
        });
        connect(&h.client, &StationClient::pathChanged, &h.client, [&] {
            movedBeforeReady = movedBeforeReady
                && remoteMedia.audioStatus().state != RemoteAudioStatus::State::Playing;
        });
        h.connectSession();
        QVERIFY(h.client.capabilities().mediaReplaceVersion >= 1);
        QVERIFY(!firstId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(moved.size(), 1, 10000);
        QVERIFY(movedBeforeReady);

        // Media follows: a new connection takes over once the first was
        // ready, and nothing stays pending.
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.audioStatus().state
                                     == RemoteAudioStatus::State::Playing, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.mediaConnectionId() != firstId
                                     && !remoteMedia.replacingConnection(), 20000);
        QVERIFY(!remoteMedia.replacePending());
        // Past the old connection's drain, with audio still coming.
        const int doneAt = h.remoteBus->heard.size() / 2;
        QTRY_VERIFY_WITH_TIMEOUT(h.remoteBus->heard.size() / 2
                                     >= doneAt + 48 * (DaemonMediaController::kReplaceDrainMs
                                                       + 1000),
                                 15000);
        const RemoteAudioReceiverTelemetry after = remoteMedia.audioTelemetry();
        source.stop();
        speaker.stop();
        QVERIFY(remoteMedia.duplicateAudioDropped() > 0);
        QCOMPARE(after.duplicatePackets, quint64(0));
        // From the first tone heard on, no silent run over 40 ms.
        const QVector<float>& heard = h.remoteBus->heard;
        int first = -1;
        int silentRun = 0;
        int longest = 0;
        for (int frame = 0; (frame + 480) * 2 <= heard.size(); frame += 480) {
            double energy = 0.0;
            for (int i = 0; i < 480; ++i) {
                const double l = heard.at((frame + i) * 2);
                energy += l * l;
            }
            const bool silent = energy / 480.0 < 1e-6;
            if (first < 0) {
                if (!silent) {
                    first = frame;
                }
                continue;
            }
            silentRun = silent ? silentRun + 1 : 0;
            longest = std::max(longest, silentRun);
        }
        QVERIFY(first >= 0);
        QVERIFY2(longest <= 4, qPrintable(QStringLiteral("silent for %1 ms").arg(longest * 10)));
        // One sign-in, the session moved once.
        QCOMPARE(handshakes.size(), 1);
        QCOMPARE(h.client.pathSwitches(), 1);
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

QTEST_MAIN(TstMediaReplace)
#include "tst_media_replace.moc"
