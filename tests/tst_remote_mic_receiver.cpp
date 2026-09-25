// =================================================================
// tests/tst_remote_mic_receiver.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test file.
//
// iPhone app plan Task 36 (R-IOS-13; remote design sections 8.2 and 8.3):
// the microphone uplink at the Core. The feed gives the transmit pump
// silence until its buffer holds the 60 ms target, then the audio; a tone
// crosses at its level with no gap over 10 ms through 1 % loss (in-band FEC
// rebuilds each lost packet); a sender 200 ppm fast and one 200 ppm slow
// each run 10 minutes keyed with no starvation and no overflow; a key waits
// for the buffer and is answered not ready after 250 ms without audio; and
// starvation is signalled after 250 ms without audio while keyed.
//
// Timing uses an injected clock and scheduler, never sleeps. The 10-minute
// runs are 10 minutes of simulated time: the pump's blocks and the sender's
// packets are interleaved by their own clocks, as fast as the computer can.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original test for NereusSDR by J.J. Boyd (KG4VCF), iPhone
//               app plan Task 36 (R-IOS-13), with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include "core/session/media/MediaPeer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"
#include "core/session/media/RemoteMicReceiver.h"

#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <vector>

using namespace NereusSDR;

namespace {

constexpr quint32 kMicSsrc = 0x6d696301U;
constexpr int kBlock = RemoteMicConfig::kPumpBlockFrames;

// A scheduler and clock driven by the test.
struct FakeTime {
    qint64 nowMs = 0;
    std::multimap<qint64, std::function<void()>> due;

    RemoteMicReceiver::Clock clock()
    {
        return [this] { return nowMs; };
    }
    RemoteMicReceiver::Scheduler scheduler()
    {
        return [this](int ms, std::function<void()> fire) {
            due.emplace(nowMs + ms, std::move(fire));
        };
    }
    void advanceTo(qint64 ms)
    {
        while (!due.empty() && due.begin()->first <= ms) {
            auto next = due.begin();
            nowMs = next->first;
            std::function<void()> fire = std::move(next->second);
            due.erase(next);
            fire();
        }
        nowMs = ms;
    }
};

// A 1 kHz tone at `amplitude`, one 20 ms frame starting at sample `start`.
std::vector<float> toneFrame(qint64 start, float amplitude)
{
    std::vector<float> frame(RemoteMicConfig::kOpusFrameSamples);
    for (int i = 0; i < RemoteMicConfig::kOpusFrameSamples; ++i) {
        const double t = static_cast<double>(start + i) / RemoteMicConfig::kSampleRate;
        frame[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * t));
    }
    return frame;
}

double rms(const float* samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

// L16 microphone packet: mono duplicated to both channels, as the window
// sends it.
QByteArray l16Packet(float value, quint16 sequence, quint32 timestamp)
{
    QVector<float> stereo(PcmAudioCodecConfig::kPacketFrames * 2, value);
    return PcmAudioPacketiser{}.encode(stereo, sequence, timestamp, kMicSsrc).packet;
}

} // namespace

class TestRemoteMicReceiver : public QObject {
    Q_OBJECT

private slots:
    void theLineKeepsTheTransmitNumbers();
    void micSsrcIsDistinctFromTheCoresStreams();
    void feedGivesSilenceUntilTheTargetThenAudio();
    void feedOutOfUseDropsAudioAndEmptiesTheBuffer();
    void toneCrossesAtItsLevelWithinTheBuffersLatency();
    void onePercentLossLeavesNoGapOver10ms();
    void lossWithoutFecIsConcealedWithinOneFrame();
    void twoLostInARowAreConcealedThenRecovered();
    void latePacketsAreDroppedAndCounted();
    void losslessOnlyWhenTheLineAgreedIt();
    void senderOffTheRadiosClockRunsTenMinutesKeyed_data();
    void senderOffTheRadiosClockRunsTenMinutesKeyed();
    void keyWaitIsAnsweredOnceTheBufferFills();
    void keyWaitIsRefusedAfter250msWithoutAudio();
    void starvationIsSignalledOnlyWhileWatched();
};

void TestRemoteMicReceiver::theLineKeepsTheTransmitNumbers()
{
    QCOMPARE(RemoteMicConfig::kTargetDepthMs, 60);
    QCOMPARE(RemoteMicConfig::kMaxDepthMs, 120);
    QCOMPARE(RemoteMicConfig::kStarvationMs, 250);
    QCOMPARE(RemoteMicConfig::kReadyDeadlineMs, 250);
    QCOMPARE(RemoteMicConfig::kTargetDepthFrames, 2880);
    QCOMPARE(RemoteMicConfig::kMaxDepthFrames, 5760);
    QCOMPARE(RemoteMicConfig::kOpusFrameSamples, 960);
    QCOMPARE(RemoteMicConfig::kOpusPayloadType, 111);
    QCOMPARE(RemoteMicConfig::kOpusBitrate, 24000);
}

// The line's SSRC is none of the ids the Core sends on, for any connection.
void TestRemoteMicReceiver::micSsrcIsDistinctFromTheCoresStreams()
{
    for (int i = 0; i < 64; ++i) {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const quint32 mic = MediaPeer::micAudioSsrcForConnection(id);
        QVERIFY(mic != 0);
        QVERIFY(!MediaPeer::receiverAudioSsrcsForConnection(id).contains(mic));
        QVERIFY(mic != MediaPeer::headphonesAudioSsrcForConnection(id));
        // The same id always gives the same SSRC.
        QCOMPARE(MediaPeer::micAudioSsrcForConnection(id), mic);
    }
    MediaPeer peer;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(peer.start(IMediaTransport::Role::Answerer, id));
    QVERIFY(peer.micAudioSsrc() != peer.audioSsrc());
    QCOMPARE(peer.micAudioSsrc(), quint32(0));
    peer.stop();
    MediaPeer micPeer;
    QVERIFY(micPeer.start(IMediaTransport::Role::Answerer, id,
                          IMediaTransport::kDefaultAudioTargetBitrate, false, false, false,
                          /*micLine=*/true));
    QCOMPARE(micPeer.micAudioSsrc(), MediaPeer::micAudioSsrcForConnection(id));
    QVERIFY(micPeer.micAudioSsrc() != micPeer.audioSsrc());
    micPeer.stop();
    QCOMPARE(micPeer.micAudioSsrc(), quint32(0));
}

void TestRemoteMicReceiver::feedGivesSilenceUntilTheTargetThenAudio()
{
    RemoteMicFeed feed;
    std::vector<float> out(kBlock, 9.0f);
    // Out of use: the pump keeps its own source.
    QVERIFY(!feed.pull(out.data(), kBlock));
    QCOMPARE(out.front(), 9.0f);

    feed.setInUse(true);
    std::vector<float> audio(RemoteMicConfig::kOpusFrameSamples, 0.5f);
    // 40 ms: below the 60 ms target, the pump hears silence.
    QVERIFY(feed.write(audio.data(), 960));
    QVERIFY(feed.write(audio.data(), 960));
    QVERIFY(feed.pull(out.data(), kBlock));
    QCOMPARE(rms(out.data(), kBlock), 0.0);
    QVERIFY(!feed.stats().started);
    // 80 ms written: the target is reached and the audio starts.
    QVERIFY(feed.write(audio.data(), 960));
    QVERIFY(feed.write(audio.data(), 960));
    QCOMPARE(feed.framesSinceInUse(), qint64(3840));
    bool heard = false;
    for (int i = 0; i < 30 && !heard; ++i) {
        QVERIFY(feed.pull(out.data(), kBlock));
        heard = rms(out.data(), kBlock) > 0.4;
    }
    QVERIFY(heard);
    QVERIFY(feed.stats().started);
    QVERIFY(feed.stats().fillFrames <= RemoteMicConfig::kMaxDepthFrames);
    QCOMPARE(feed.stats().overflows, 0);
}

// Out of use, the feed refuses audio and the pump's source is its own; a
// change of use empties the buffer, so nothing from before it is heard.
void TestRemoteMicReceiver::feedOutOfUseDropsAudioAndEmptiesTheBuffer()
{
    RemoteMicFeed feed;
    std::vector<float> audio(960, 0.5f);
    QVERIFY(!feed.write(audio.data(), 960));
    feed.setInUse(true);
    for (int i = 0; i < 5; ++i) {
        QVERIFY(feed.write(audio.data(), 960));
    }
    std::vector<float> out(kBlock);
    QVERIFY(feed.pull(out.data(), kBlock));
    QVERIFY(feed.stats().fillFrames > RemoteMicConfig::kTargetDepthFrames);

    // The key ends: the buffer is emptied, the pump's own source returns.
    feed.setInUse(false);
    QCOMPARE(feed.framesSinceInUse(), qint64(0));
    QVERIFY(!feed.write(audio.data(), 960));
    QVERIFY(!feed.pull(out.data(), kBlock));
    QCOMPARE(feed.stats().fillFrames, 0);
    QVERIFY(!feed.stats().started);

    // In use again: nothing from the last key is heard.
    feed.setInUse(true);
    QVERIFY(feed.pull(out.data(), kBlock));
    QCOMPARE(rms(out.data(), kBlock), 0.0);
    QCOMPARE(feed.stats().fillFrames, 0);
    QVERIFY(feed.stats().changes >= 3);
}

namespace {

// One run of the uplink: `packets` 20 ms frames of a 1 kHz tone from the
// app's encoder, the ones `lose` picks left out, the pump taking a block at
// a time between packets. Returns what the pump heard from its first block.
struct UplinkRun {
    std::vector<float> heard;
    int firstBlockHeard = -1;
    int lost = 0;
    RemoteMicReceiver::Stats receiver;
    RemoteMicFeed::Stats feed;
};

// The signal of frame k: a steady 1 kHz tone, or the tone keyed in 350 ms
// bursts with 150 ms between them at -26 dB, as speech comes in syllables
// (Opus codes in-band FEC only for frames its voice detector calls active).
enum class ToneShape { Steady, Syllables };

UplinkRun runTone(int packets, float amplitude,
                  const std::function<bool(int k, const QByteArray& next)>& lose,
                  ToneShape shape = ToneShape::Steady)
{
    UplinkRun run;
    FakeTime time;
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed, nullptr, time.clock(), time.scheduler());
    RemoteMicEncoder encoder;
    if (!encoder.isReady() || !receiver.start(kMicSsrc, false)) {
        return run;
    }
    feed.setInUse(true);
    // Encoded one ahead, so a loss can be chosen by what the next packet
    // carries.
    std::vector<QByteArray> encoded;
    for (int k = 0; k < packets + 1; ++k) {
        std::vector<float> frame =
            toneFrame(static_cast<qint64>(k) * RemoteMicConfig::kOpusFrameSamples, amplitude);
        if (shape == ToneShape::Syllables && (k % 25) >= 17) {
            for (float& sample : frame) {
                sample *= 0.05f;
            }
        }
        encoded.push_back(encoder.encode(frame.data(), static_cast<quint16>(k),
                                         static_cast<quint32>(k * RemoteMicConfig::kOpusFrameSamples),
                                         kMicSsrc));
    }
    const int blocksPerPacket = RemoteMicConfig::kOpusFrameSamples / kBlock;   // 15
    std::vector<float> pumped(kBlock);
    int block = 0;
    for (int k = 0; k < packets; ++k) {
        if (lose(k, encoded[static_cast<size_t>(k) + 1])) {
            ++run.lost;
        } else {
            receiver.submit(encoded[static_cast<size_t>(k)]);
        }
        for (int b = 0; b < blocksPerPacket; ++b, ++block) {
            feed.pull(pumped.data(), kBlock);
            if (run.firstBlockHeard < 0 && rms(pumped.data(), kBlock) > 0.01) {
                run.firstBlockHeard = block;
            }
            run.heard.insert(run.heard.end(), pumped.begin(), pumped.end());
        }
        time.advanceTo(time.nowMs + 20);
    }
    run.receiver = receiver.stats();
    run.feed = feed.stats();
    return run;
}

// The longest run, in ms, of 1 ms windows from `from` on whose RMS is below
// `floor`.
int longestRunBelow(const std::vector<float>& audio, size_t from, double floor)
{
    constexpr int kWindow = RemoteMicConfig::kSampleRate / 1000;
    int run = 0;
    int longest = 0;
    for (size_t start = from; start + kWindow <= audio.size(); start += kWindow) {
        if (rms(audio.data() + start, kWindow) < floor) {
            longest = std::max(longest, ++run);
        } else {
            run = 0;
        }
    }
    return longest;
}

bool carriesFec(const QByteArray& packet)
{
    AudioRtpPacket parsed;
    if (parseAudioRtp(packet, RemoteMicConfig::kOpusPayloadType, parsed)
        != OpusAudioCodecStatus::Accepted) {
        return false;
    }
    return opusPacketCarriesFec(parsed.payload);
}

} // namespace

// A 1 kHz tone from the app's encoder reaches the pump at its level, within
// the buffer's latency, with no gap at all.
void TestRemoteMicReceiver::toneCrossesAtItsLevelWithinTheBuffersLatency()
{
    constexpr float kAmplitude = 0.3f;
    const UplinkRun run = runTone(1000, kAmplitude, [](int, const QByteArray&) { return false; });
    QVERIFY2(run.firstBlockHeard >= 0, "the tone never reached the pump");
    QCOMPARE(run.feed.underflows, 0);
    QCOMPARE(run.feed.overflows, 0);
    // Within the buffer's latency: the pump starts once it holds 60 ms
    // (three 20 ms packets after the first), plus the codec's own delay.
    const double latencyMs =
        run.firstBlockHeard * 1000.0 * kBlock / RemoteMicConfig::kSampleRate;
    QVERIFY2(latencyMs <= RemoteMicConfig::kTargetDepthMs + 30.0,
             qPrintable(QString::number(latencyMs)));
    // At its level: the settled output's RMS is the tone's within 1 dB.
    const size_t settled = static_cast<size_t>(run.firstBlockHeard + 200) * kBlock;
    const double expected = kAmplitude / std::sqrt(2.0);
    const double measured = rms(run.heard.data() + settled,
                                static_cast<int>(run.heard.size() - settled));
    const double errorDb = 20.0 * std::log10(measured / expected);
    QVERIFY2(std::abs(errorDb) < 1.0, qPrintable(QString::number(errorDb)));
    QCOMPARE(longestRunBelow(run.heard, settled, expected / 2.0), 0);
}

// 1 % of the packets lost: each is rebuilt from the next packet's in-band
// FEC when it arrives, so against the same stream without loss the audio
// never falls 6 dB, let alone for 10 ms, and keeps its level. The tone comes
// in syllables because Opus codes FEC only for frames its voice detector
// calls active; a steady tone stops being active within a second (measured
// with this tree's libopus: 39 of 1000 steady-tone packets carry FEC, 720
// of 1000 in syllables). The lost packets are ones whose next packet
// carries FEC, as speech's do; lossWithoutFecIsConcealedWithinOneFrame
// covers the others.
void TestRemoteMicReceiver::onePercentLossLeavesNoGapOver10ms()
{
    constexpr float kAmplitude = 0.3f;
    constexpr int kPackets = 1000;
    const UplinkRun reference = runTone(kPackets, kAmplitude,
        [](int, const QByteArray&) { return false; }, ToneShape::Syllables);
    int nextLoss = 11;
    const UplinkRun run = runTone(kPackets, kAmplitude,
        [&nextLoss](int k, const QByteArray& next) {
            if (k >= nextLoss && carriesFec(next)) {
                nextLoss = k + 95;   // about 1 %, never two in a row
                return true;
            }
            return false;
        },
        ToneShape::Syllables);
    QVERIFY2(run.lost >= 8, qPrintable(QString::number(run.lost)));
    QCOMPARE(run.receiver.recoveredPackets, quint64(run.lost));
    QCOMPARE(run.receiver.concealedPackets, quint64(0));
    QCOMPARE(run.receiver.framesWritten, quint64(kPackets) * RemoteMicConfig::kOpusFrameSamples);
    QCOMPARE(run.feed.underflows, 0);
    QCOMPARE(run.firstBlockHeard, reference.firstBlockHeard);
    QCOMPARE(run.heard.size(), reference.heard.size());

    // Where the stream without loss has the tone, the lossy one keeps it
    // within 6 dB; a stretch that does not is a gap.
    constexpr int kWindow = RemoteMicConfig::kSampleRate / 1000;
    const double toneRms = kAmplitude / std::sqrt(2.0);
    int gap = 0;
    int longest = 0;
    const size_t settled = static_cast<size_t>(run.firstBlockHeard + 200) * kBlock;
    for (size_t start = settled; start + kWindow <= run.heard.size(); start += kWindow) {
        const double want = rms(reference.heard.data() + start, kWindow);
        const double got = rms(run.heard.data() + start, kWindow);
        if (want >= toneRms / 2.0 && got < want / 2.0) {
            longest = std::max(longest, ++gap);
        } else {
            gap = 0;
        }
    }
    QVERIFY2(longest <= 10, qPrintable(QString::number(longest)));
    const double levelDb = 20.0 * std::log10(
        rms(run.heard.data() + settled, static_cast<int>(run.heard.size() - settled))
        / rms(reference.heard.data() + settled, static_cast<int>(reference.heard.size() - settled)));
    QVERIFY2(std::abs(levelDb) < 0.5, qPrintable(QString::number(levelDb)));
}

// A lost packet whose next carries no FEC (a steady tone past Opus's voice
// detector) is concealed: the timeline is kept and the tone goes on, faded,
// for no longer than the one lost 20 ms frame.
void TestRemoteMicReceiver::lossWithoutFecIsConcealedWithinOneFrame()
{
    constexpr float kAmplitude = 0.3f;
    int nextLoss = 200;
    const UplinkRun run = runTone(1000, kAmplitude,
        [&nextLoss](int k, const QByteArray& next) {
            if (k >= nextLoss && !carriesFec(next)) {
                nextLoss = k + 100;
                return true;
            }
            return false;
        });
    QVERIFY(run.lost >= 5);
    QCOMPARE(run.receiver.concealedPackets, quint64(run.lost));
    QCOMPARE(run.receiver.framesWritten, quint64(1000) * RemoteMicConfig::kOpusFrameSamples);
    QCOMPARE(run.feed.underflows, 0);
    const size_t settled = static_cast<size_t>(run.firstBlockHeard + 200) * kBlock;
    const double expected = kAmplitude / std::sqrt(2.0);
    const int gapMs = longestRunBelow(run.heard, settled, expected / 4.0);
    QVERIFY2(gapMs <= 20, qPrintable(QString::number(gapMs)));
}

void TestRemoteMicReceiver::twoLostInARowAreConcealedThenRecovered()
{
    FakeTime time;
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed, nullptr, time.clock(), time.scheduler());
    RemoteMicEncoder encoder;
    QVERIFY(receiver.start(kMicSsrc, false));
    feed.setInUse(true);
    for (int k = 0; k < 10; ++k) {
        const std::vector<float> frame = toneFrame(k * 960, 0.3f);
        const QByteArray packet = encoder.encode(frame.data(), static_cast<quint16>(k),
                                                 static_cast<quint32>(k * 960), kMicSsrc);
        if (k != 4 && k != 5) {
            receiver.submit(packet);
        }
    }
    // Packet 4 by loss concealment, packet 5 from packet 6 (its FEC while
    // the voice detector calls the tone active, concealment otherwise):
    // every frame is written, so the buffer keeps its timeline.
    QVERIFY(receiver.stats().concealedPackets >= 1);
    QCOMPARE(receiver.stats().concealedPackets + receiver.stats().recoveredPackets, quint64(2));
    QCOMPARE(receiver.stats().framesWritten, quint64(10 * 960));
}

void TestRemoteMicReceiver::latePacketsAreDroppedAndCounted()
{
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed);
    RemoteMicEncoder encoder;
    QVERIFY(receiver.start(kMicSsrc, false));
    feed.setInUse(true);
    std::vector<QByteArray> packets;
    for (int k = 0; k < 6; ++k) {
        const std::vector<float> frame = toneFrame(k * 960, 0.3f);
        packets.push_back(encoder.encode(frame.data(), static_cast<quint16>(k),
                                         static_cast<quint32>(k * 960), kMicSsrc));
    }
    receiver.submit(packets[0]);
    receiver.submit(packets[1]);
    receiver.submit(packets[3]);   // 2 is missing: recovered from 3's FEC
    receiver.submit(packets[2]);   // too late now
    receiver.submit(packets[3]);   // a repeat
    receiver.submit(packets[4]);
    QCOMPARE(receiver.stats().latePackets, quint64(2));
    QCOMPARE(receiver.stats().recoveredPackets, quint64(1));
    QCOMPARE(receiver.stats().framesWritten, quint64(5 * 960));
    // Another SSRC is refused.
    const std::vector<float> frame = toneFrame(0, 0.3f);
    receiver.submit(encoder.encode(frame.data(), 5, 5 * 960, kMicSsrc + 1));
    QCOMPARE(receiver.stats().rejectedPackets, quint64(1));
}

// L16 on the line only from a connection that agreed it (the desktop
// window's lossless uplink); its mono is the mean of the two channels.
void TestRemoteMicReceiver::losslessOnlyWhenTheLineAgreedIt()
{
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed);
    QVERIFY(receiver.start(kMicSsrc, false));
    feed.setInUse(true);
    receiver.submit(l16Packet(0.25f, 1, 0));
    QCOMPARE(receiver.stats().rejectedPackets, quint64(1));
    QCOMPARE(receiver.stats().framesWritten, quint64(0));

    receiver.setLosslessNegotiated(true);
    for (quint16 s = 2; s < 2 + 20; ++s) {
        receiver.submit(l16Packet(0.25f, s, s * 192U));
    }
    // One lost 4 ms packet is 4 ms of silence; the timeline is kept.
    receiver.submit(l16Packet(0.25f, 23, 23 * 192U));
    QCOMPARE(receiver.stats().concealedPackets, quint64(1));
    QCOMPARE(receiver.stats().framesWritten, quint64(22 * 192));
    std::vector<float> out(kBlock);
    bool heard = false;
    for (int i = 0; i < 40 && !heard; ++i) {
        QVERIFY(feed.pull(out.data(), kBlock));
        heard = std::abs(out[10] - 0.25f) < 0.01f;
    }
    QVERIFY(heard);
}

void TestRemoteMicReceiver::senderOffTheRadiosClockRunsTenMinutesKeyed_data()
{
    QTest::addColumn<double>("ppm");
    QTest::newRow("sender-200ppm-fast") << 200.0;
    QTest::newRow("sender-200ppm-slow") << -200.0;
}

// A sender 200 ppm off the radio's clock, either way, keyed for 10 minutes
// of simulated time: the rate matcher follows it, so the buffer never runs
// empty (no starvation, no underflow) and never overflows.
void TestRemoteMicReceiver::senderOffTheRadiosClockRunsTenMinutesKeyed()
{
    QFETCH(double, ppm);
    FakeTime time;
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed, nullptr, time.clock(), time.scheduler());
    QSignalSpy starved(&receiver, &RemoteMicReceiver::starved);
    QVERIFY(receiver.start(kMicSsrc, true));
    feed.setInUse(true);
    receiver.setWatching(true);

    // L16 keeps 10 minutes cheap; the clocks, not the codec, are under test.
    constexpr double kSeconds = 600.0;
    const double packetSeconds = PcmAudioCodecConfig::kPacketFrames
        / static_cast<double>(RemoteMicConfig::kSampleRate) / (1.0 + ppm * 1e-6);
    const double blockSeconds = kBlock / static_cast<double>(RemoteMicConfig::kSampleRate);
    double nextPacket = 0.0;
    quint16 sequence = 0;
    quint32 timestamp = 0;
    std::vector<float> out(kBlock);
    int minFillAfterStart = RemoteMicConfig::kMaxDepthFrames;
    int maxFill = 0;
    const qint64 blocks = static_cast<qint64>(kSeconds / blockSeconds);
    for (qint64 b = 0; b < blocks; ++b) {
        const double now = static_cast<double>(b) * blockSeconds;
        while (nextPacket <= now) {
            receiver.submit(l16Packet(0.2f, sequence++, timestamp));
            timestamp += PcmAudioCodecConfig::kPacketFrames;
            nextPacket += packetSeconds;
        }
        QVERIFY(feed.pull(out.data(), kBlock));
        const RemoteMicFeed::Stats stats = feed.stats();
        if (stats.started && b > blocks / 100) {
            minFillAfterStart = std::min(minFillAfterStart, stats.fillFrames);
        }
        maxFill = std::max(maxFill, stats.fillFrames);
        const qint64 ms = static_cast<qint64>(now * 1000.0);
        if (ms != time.nowMs) {
            time.advanceTo(ms);
        }
    }
    const RemoteMicFeed::Stats stats = feed.stats();
    QVERIFY(stats.started);
    QCOMPARE(stats.underflows, 0);
    QCOMPARE(stats.overflows, 0);
    QCOMPARE(starved.count(), 0);
    QVERIFY2(minFillAfterStart > 0, qPrintable(QString::number(minFillAfterStart)));
    QVERIFY2(maxFill <= RemoteMicConfig::kMaxDepthFrames, qPrintable(QString::number(maxFill)));
    // The matcher corrected the right way: a fast sender is slowed
    // (fewer frames out than in), a slow one sped up.
    QVERIFY2(ppm > 0 ? stats.ratio < 1.0 : stats.ratio > 1.0,
             qPrintable(QString::number(stats.ratio, 'g', 10)));
    qInfo().noquote() << QStringLiteral("ppm %1: ratio %2, fill %3..%4 frames at the end %5")
                             .arg(ppm).arg(stats.ratio, 0, 'g', 10)
                             .arg(minFillAfterStart).arg(maxFill).arg(stats.fillFrames);
}

// A key waits for the buffer: answered ready as soon as the feed holds its
// 60 ms, and never twice.
void TestRemoteMicReceiver::keyWaitIsAnsweredOnceTheBufferFills()
{
    FakeTime time;
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed, nullptr, time.clock(), time.scheduler());
    RemoteMicEncoder encoder;
    QVERIFY(receiver.start(kMicSsrc, false));
    feed.setInUse(true);
    QList<bool> answers;
    receiver.awaitReady([&answers](bool ready) { answers.append(ready); });
    QVERIFY(receiver.isWaiting());
    for (int k = 0; k < 3; ++k) {
        QVERIFY(answers.isEmpty());
        const std::vector<float> frame = toneFrame(k * 960, 0.3f);
        receiver.submit(encoder.encode(frame.data(), static_cast<quint16>(k),
                                       static_cast<quint32>(k * 960), kMicSsrc));
        time.advanceTo(time.nowMs + 20);
    }
    QCOMPARE(answers, QList<bool>{true});
    QVERIFY(!receiver.isWaiting());
    time.advanceTo(time.nowMs + 1000);
    QCOMPARE(answers, QList<bool>{true});

    // Already full: answered at once.
    receiver.awaitReady([&answers](bool ready) { answers.append(ready); });
    QCOMPARE(answers, (QList<bool>{true, true}));
}

void TestRemoteMicReceiver::keyWaitIsRefusedAfter250msWithoutAudio()
{
    FakeTime time;
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed, nullptr, time.clock(), time.scheduler());
    QVERIFY(receiver.start(kMicSsrc, false));
    feed.setInUse(true);
    QList<bool> answers;
    receiver.awaitReady([&answers](bool ready) { answers.append(ready); });
    time.advanceTo(249);
    QVERIFY(answers.isEmpty());
    time.advanceTo(250);
    QCOMPARE(answers, QList<bool>{false});
    QVERIFY(!receiver.isWaiting());

    // A cancelled wait is never answered.
    receiver.awaitReady([&answers](bool ready) { answers.append(ready); });
    receiver.cancelWait();
    time.advanceTo(1000);
    QCOMPARE(answers, QList<bool>{false});
}

void TestRemoteMicReceiver::starvationIsSignalledOnlyWhileWatched()
{
    FakeTime time;
    RemoteMicFeed feed;
    RemoteMicReceiver receiver(&feed, nullptr, time.clock(), time.scheduler());
    QSignalSpy starved(&receiver, &RemoteMicReceiver::starved);
    QVERIFY(receiver.start(kMicSsrc, true));
    feed.setInUse(true);

    // Not watched: silence is not starvation.
    time.advanceTo(1000);
    QCOMPARE(starved.count(), 0);

    receiver.setWatching(true);
    receiver.submit(l16Packet(0.1f, 1, 0));
    time.advanceTo(1249);
    QCOMPARE(starved.count(), 0);
    time.advanceTo(1250);
    QCOMPARE(starved.count(), 1);
    QCOMPARE(starved.at(0).at(0).toBool(), true);
    QVERIFY(receiver.isStarved());

    // Audio again ends it.
    receiver.submit(l16Packet(0.1f, 2, 192));
    QCOMPARE(starved.count(), 2);
    QCOMPARE(starved.at(1).at(0).toBool(), false);

    // Steady audio: no starvation.
    for (quint16 s = 3; s < 200; ++s) {
        time.advanceTo(time.nowMs + 4);
        receiver.submit(l16Packet(0.1f, s, s * 192U));
    }
    QCOMPARE(starved.count(), 2);

    // Starved again, then the key ends: the starvation ends with it.
    time.advanceTo(time.nowMs + 300);
    QCOMPARE(starved.count(), 3);
    receiver.setWatching(false);
    QCOMPARE(starved.count(), 4);
    QCOMPARE(starved.at(3).at(0).toBool(), false);
    time.advanceTo(time.nowMs + 1000);
    QCOMPARE(starved.count(), 4);
}

QTEST_GUILESS_MAIN(TestRemoteMicReceiver)
#include "tst_remote_mic_receiver.moc"
