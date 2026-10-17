// =================================================================
// tests/tst_device_rate_matcher.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original deterministic acceptance test for the
// already-attributed DeviceRateMatcher (R-AUD-15, V-SW-5).  It drives the
// matcher's writer and reader on two simulated clocks, with no timer and no
// device, and compares its ratio with WDSP's own rmatchV fed the same
// clocks.  Each simulation is single-threaded and deterministic; the two
// independent drift simulations of each offset (ours and rmatchV) run on
// worker threads, and the threads only run them side by side to keep the
// wall time down.  No simulation shares state with another.
//
// Modification history (NereusSDR):
//   2026-10-08: native audio plan Task 2 (R-AUD-15, V-SW-5). J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan Task 6 (R-AUD-15): a flush leaves
//               nothing of the earlier audio. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-09: early-review fix wave (R-AUD-15): a restart sizes afresh,
//               and the fade-in after a dry run follows its padding. J.J.
//               Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10: a writer of whole packets (R-AUD-15, bench regression):
//               its size holds a packet, and with the room rule it takes
//               steady packets, batches and a stall with no overrun while
//               it matches the clocks. J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
//   2026-10-12: the packet writer's harness says when each packet was
//               handed over and how long it waited, as the receiver does;
//               after a stall the backlog is played down at the matcher's
//               0.5% and nothing is left standing (R-AUD-15). J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-13: A BLOCK writer resumes after repeated intentional gaps
//               without treating the continuously running reader as clock
//               drift. J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-16: the drift offsets and the hiatus cases are data rows, run
//               as ctest entries of their own beside each other (CI speed
//               round); the same simulations and checks. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include "TestFunctionGroups.h"
#include "core/audio/AudioDelayParts.h"
#include "core/audio/DeviceRateMatcher.h"
#include "core/audio/MatcherRing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#ifdef HAVE_WDSP
extern "C" {
// Exact declarations from Thetis Project Files/Source/wdsp/rmatch.h:117-135
// (third_party/wdsp/src/rmatch.h), as RemoteAudioRateMatcher.cpp declares them.
void* create_rmatchV(int in_size, int out_size, int nom_inrate, int nom_outrate,
                     int ringsize, double var);
void destroy_rmatchV(void* ptr);
void xrmatchOUT(void* b, double* out);
void xrmatchIN(void* b, double* in);
void getRMatchDiags(void* b, int* underflows, int* overflows, double* var,
                    int* ringsize, int* nring);
}
#endif

using namespace NereusSDR;

namespace {

constexpr int kRate = 48000;
constexpr int kWriteFrames = 64;
constexpr int kReadFrames = 128;
constexpr double kSineAmplitude = 0.5;
constexpr int kSinePeriodFrames = 48;   // 1 kHz at 48 kHz
// Twice the 1 kHz sine's own largest sample-to-sample step.
const double kMaxAllowedStep = 2.0 * kSineAmplitude * 2.0 * std::numbers::pi * 1000.0 / kRate;

// The clock offsets of the ten-minute drift runs: reader fast, then slow.
// The rows of driftMatchesRmatch and blockWriterReacquiresRealDriftAfterHiatus,
// and main() builds the ctest entries from the same list.
constexpr std::array<int, 2> kDriftOffsets{200, -200};

QString driftTag(int ppm)
{
    return QString::asprintf("%+d ppm", ppm);
}

// blockWriterRecoversAfterHiatus's rows, read by its _data function and by
// main(), which spreads them over ctest entries.
struct HiatusRow {
    QString tag;
    int callbackFrames = 0;
    double warmupSeconds = 0.0;
    int delayMs = 0;
};

QList<HiatusRow> hiatusRows()
{
    QList<HiatusRow> rows;
    for (int callback : {64, 128, 256, 512}) {
        for (double warmup : {2.8, 40.0}) {
            rows.append({QString::asprintf("callback%d-warm%.1f", callback, warmup), callback, warmup, 0});
        }
    }
    rows.append({QStringLiteral("callback512-warm400"), 512, 400.0, 0});
    rows.append({QStringLiteral("callback128-fixed20"), 128, 40.0, 20});
    return rows;
}

struct SineSource {
    std::array<float, kSinePeriodFrames> sinTable{};
    std::uint64_t frame = 0;

    SineSource()
    {
        for (int i = 0; i < kSinePeriodFrames; ++i) {
            const double phase = 2.0 * std::numbers::pi * i / kSinePeriodFrames;
            sinTable[static_cast<std::size_t>(i)] = static_cast<float>(kSineAmplitude * std::sin(phase));
        }
    }

    void fill(float* interleaved, int frames)
    {
        for (int f = 0; f < frames; ++f) {
            const std::size_t i = static_cast<std::size_t>((frame + static_cast<std::uint64_t>(f))
                                                           % kSinePeriodFrames);
            interleaved[2 * f + 0] = sinTable[i];
            interleaved[2 * f + 1] = -sinTable[i];
        }
        frame += static_cast<std::uint64_t>(frames);
    }
};

// Two clocks on one integer time line.  In units of
// 1 / (48000 * (1e6 + ppm)) s the writer's n-th 64-frame event is at
// n * 64 * (1e6 + ppm) and the reader's m-th 128-frame event, 48 kHz times
// (1 + ppm / 1e6), at m * 128 * 1e6.  A tie runs the writer first.
class ClockSim {
public:
    struct Options {
        int ppm = 0;
        bool reference = false;          // feed WDSP's rmatchV instead of ours
        double readerSkipAtSeconds = -1;  // the reader skips 50 ms of calls here
        double writerBurstAtSeconds = -1; // the writer delivers 100 ms at once here
        double writerHiatusAtSeconds = -1; // source time continues; no writes for 5 s
    };

    explicit ClockSim(const Options& options)
        : m_options(options)
        , m_matcher(DeviceRateMatcher::Config{})
        , m_writeBuffer(static_cast<std::size_t>(kWriteFrames) * 2)
        , m_burstBuffer(static_cast<std::size_t>(kRate / 10) * 2)
        , m_readBuffer(static_cast<std::size_t>(kReadFrames) * 2)
    {
        m_reader = m_matcher.makeReader();
#ifdef HAVE_WDSP
        if (options.reference) {
            m_reference = create_rmatchV(kWriteFrames, kReadFrames, kRate, kRate, 0, 1.0);
            m_referenceIn.assign(static_cast<std::size_t>(kWriteFrames) * 2, 0.0);
            m_referenceOut.assign(static_cast<std::size_t>(kReadFrames) * 2, 0.0);
        }
#endif
        const std::int64_t unitsPerSecond = static_cast<std::int64_t>(kRate) * (1'000'000 + options.ppm);
        m_unitsPerSecond = unitsPerSecond;
        if (options.readerSkipAtSeconds >= 0.0) {
            m_skipStart = static_cast<std::int64_t>(options.readerSkipAtSeconds * unitsPerSecond);
            m_skipEnd = m_skipStart + unitsPerSecond / 20;   // 50 ms
        }
        if (options.writerBurstAtSeconds >= 0.0) {
            m_burstAt = static_cast<std::int64_t>(options.writerBurstAtSeconds * unitsPerSecond);
        }
    }

    ~ClockSim()
    {
#ifdef HAVE_WDSP
        if (m_reference != nullptr) {
            destroy_rmatchV(m_reference);
        }
#endif
    }

    ClockSim(const ClockSim&) = delete;
    ClockSim& operator=(const ClockSim&) = delete;

    DeviceRateMatcher& matcher() { return m_matcher; }
    double maxStep() const { return m_maxStep; }
    std::uint64_t reads() const { return m_readerEvent; }

    // Runs every event before `seconds`.  When `sampleFromSeconds` is not
    // negative, the ratios are averaged from then on (per write).
    void runUntil(double seconds, double sampleFromSeconds = -1.0)
    {
        const std::int64_t end = static_cast<std::int64_t>(seconds * m_unitsPerSecond);
        const std::int64_t sampleFrom = sampleFromSeconds < 0.0
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(sampleFromSeconds * m_unitsPerSecond);
        const std::int64_t writerPeriod = static_cast<std::int64_t>(kWriteFrames) * (1'000'000 + m_options.ppm);
        const std::int64_t readerPeriod = static_cast<std::int64_t>(kReadFrames) * 1'000'000;
        while (true) {
            const std::int64_t nextWriter = m_writerEvent * writerPeriod;
            std::int64_t nextReader = static_cast<std::int64_t>(m_readerEvent) * readerPeriod;
            if (nextReader >= m_skipStart && nextReader < m_skipEnd) {
                nextReader = m_skipEnd;   // skipped, then made up at once
            }
            if (nextWriter >= end && nextReader >= end) {
                break;
            }
            if (nextWriter <= nextReader) {
                writeEvent(nextWriter, nextWriter >= sampleFrom);
            } else {
                readEvent();
            }
        }
    }

    double meanRatio() const { return m_ratioSamples ? m_ratioSum / m_ratioSamples : 0.0; }

private:
    bool feedsOurs() const { return !m_options.reference; }

    void writeEvent(std::int64_t time, bool sample)
    {
        const std::int64_t nowNs = m_writerEvent * kWriteFrames * 1'000'000'000LL / kRate;
        if (m_burstAt >= 0 && time >= m_burstAt) {
            m_burstAt = -1;
            const int frames = kRate / 10;
            m_source.fill(m_burstBuffer.data(), frames);
            m_matcher.write(m_burstBuffer.data(), frames, nowNs);
        }
        m_source.fill(m_writeBuffer.data(), kWriteFrames);
        const bool inHiatus = m_options.writerHiatusAtSeconds >= 0.0
            && time >= static_cast<std::int64_t>(m_options.writerHiatusAtSeconds * m_unitsPerSecond)
            && time < static_cast<std::int64_t>((m_options.writerHiatusAtSeconds + 5.0) * m_unitsPerSecond);
        if (feedsOurs() && !inHiatus) {
            m_matcher.write(m_writeBuffer.data(), kWriteFrames, nowNs);
        }
#ifdef HAVE_WDSP
        if (m_reference != nullptr) {
            for (int i = 0; i < kWriteFrames * 2; ++i) {
                m_referenceIn[static_cast<std::size_t>(i)] = m_writeBuffer[static_cast<std::size_t>(i)];
            }
            xrmatchIN(m_reference, m_referenceIn.data());
        }
#endif
        if (sample) {
            if (feedsOurs()) {
                m_ratioSum += m_matcher.ratio();
            }
#ifdef HAVE_WDSP
            if (m_reference != nullptr) {
                int underflows = 0;
                int overflows = 0;
                int ringsize = 0;
                int nring = 0;
                double var = 0.0;
                getRMatchDiags(m_reference, &underflows, &overflows, &var, &ringsize, &nring);
                m_ratioSum += var;
            }
#endif
            ++m_ratioSamples;
        }
        ++m_writerEvent;
    }

    void readEvent()
    {
        if (feedsOurs()) {
            m_reader.read(m_readBuffer.data(), kReadFrames);
            for (int f = 0; f < kReadFrames; ++f) {
                for (int c = 0; c < 2; ++c) {
                    const float value = m_readBuffer[static_cast<std::size_t>(2 * f + c)];
                    m_maxStep = std::max(m_maxStep, std::abs(static_cast<double>(value) - m_last[c]));
                    m_last[c] = value;
                }
            }
        }
#ifdef HAVE_WDSP
        if (m_reference != nullptr) {
            xrmatchOUT(m_reference, m_referenceOut.data());
        }
#endif
        ++m_readerEvent;
    }

    Options m_options;
    DeviceRateMatcher m_matcher;
    MatcherReader m_reader;
    SineSource m_source;
    std::vector<float> m_writeBuffer;
    std::vector<float> m_burstBuffer;
    std::vector<float> m_readBuffer;
#ifdef HAVE_WDSP
    void* m_reference = nullptr;
    std::vector<double> m_referenceIn;
    std::vector<double> m_referenceOut;
#endif
    std::int64_t m_unitsPerSecond = 0;
    std::int64_t m_writerEvent = 0;
    std::uint64_t m_readerEvent = 0;
    std::int64_t m_skipStart = std::numeric_limits<std::int64_t>::max();
    std::int64_t m_skipEnd = std::numeric_limits<std::int64_t>::max();
    std::int64_t m_burstAt = -1;
    double m_last[2] = {0.0, 0.0};
    double m_maxStep = 0.0;
    double m_ratioSum = 0.0;
    std::uint64_t m_ratioSamples = 0;
};

} // namespace

class TestDeviceRateMatcher : public QObject {
    Q_OBJECT

private slots:
    void ringLayoutAndAttach();
    void sizesAndReadout();
    void manualSizeAndRates();
    void callerMemory();
    void driftMatchesRmatch_data();
    void driftMatchesRmatch();
    void forcedDryRunStepsUpOnce();
    void blockWriterRecoversAfterHiatus_data();
    void blockWriterRecoversAfterHiatus();
    void blockWriterReacquiresRealDriftAfterHiatus_data();
    void blockWriterReacquiresRealDriftAfterHiatus();
    void forcedOverrunStaysSmooth();
    void flushRestartAndFade();
    void flushLeavesNothingOfTheEarlierAudio();
    void restartSizesAfresh();
    void dryRunFadeInFollowsItsPadding();
    void packetWriterSizeHoldsAPacket_data();
    void packetWriterSizeHoldsAPacket();
    void packetWriterTakesBurstsAndMatchesTheClocks_data();
    void packetWriterTakesBurstsAndMatchesTheClocks();
};

void TestDeviceRateMatcher::ringLayoutAndAttach()
{
    QCOMPARE(kMatcherRingMagic, 0x4E524D52u);
    QCOMPARE(kMatcherNoSkip, std::numeric_limits<std::uint64_t>::max());
    const std::size_t bytes = matcherRingBytes(256, 2);
    QCOMPARE(bytes, sizeof(MatcherRingHeader) + 256 * 2 * sizeof(float));

    auto memory = std::make_unique<std::byte[]>(bytes);
    QVERIFY(constructMatcherRing(memory.get(), 255, 2, 10) == nullptr);   // not a power of two
    MatcherRingHeader* ring = constructMatcherRing(memory.get(), 256, 2, 10);
    QVERIFY(ring != nullptr);
    QCOMPARE(ring->magic, kMatcherRingMagic);
    QCOMPARE(ring->capacityFrames, 256u);
    QCOMPARE(ring->channels, 2u);
    QCOMPARE(ring->slewFrames, 10u);
    QCOMPARE(ring->skipTo.load(), kMatcherNoSkip);
    QCOMPARE(ring->written.load(), std::uint64_t{0});

    QCOMPARE(attachMatcherRing(memory.get(), bytes), ring);
    QVERIFY(attachMatcherRing(memory.get(), bytes - 4) == nullptr);
    QVERIFY(attachMatcherRing(nullptr, bytes) == nullptr);
    ring->channels = 1;
    QVERIFY(attachMatcherRing(memory.get(), bytes) == nullptr);
    ring->channels = 2;
    ring->magic = 0;
    QVERIFY(attachMatcherRing(memory.get(), bytes) == nullptr);
}

void TestDeviceRateMatcher::sizesAndReadout()
{
    DeviceRateMatcher matcher(DeviceRateMatcher::Config{});
    QVERIFY(matcher.valid());
    // Automatic: callback 128 + write 64 frames is 4 ms, so 5 ms.
    QCOMPARE(matcher.delayStepMs(), 5);
    const DeviceRateMatcherStats stats = matcher.stats();
    QCOMPARE(stats.rsizeFrames, 480);       // 2 * 240
    QCOMPARE(stats.capacityFrames, 4096);   // 40 ms rsize 3840 + 68
    QCOMPARE(stats.delayStepMs, 5);
    QVERIFY(!stats.controlActive);
    QCOMPARE(stats.ratio, 1.0);
    QCOMPARE(matcher.ring()->slewFrames, 127u);   // (int)(0.003 * 48000) capped at 256 / 2 - 1
    QCOMPARE(matcher.fillFrames(), 240.0);        // rsize / 2 of silence
    QCOMPARE(matcher.resamplerDelayFrames(), 69);
    QCOMPARE(DeviceRateMatcher::ringBytes(DeviceRateMatcher::Config{}), matcherRingBytes(4096, 2));

    const AudioDelayParts parts = matcher.delayParts(10.0, 3.0);
    QCOMPARE(parts.matcherFillMs, 5.0);
    QCOMPARE(parts.resamplerMs, 69.0 / 48.0);
    QCOMPARE(parts.deviceBufferMs, 10.0);
    QCOMPARE(parts.deviceLatencyMs, 3.0);
    QCOMPARE(parts.totalMs(), parts.matcherFillMs + parts.resamplerMs + parts.deviceBufferMs
                                  + parts.deviceLatencyMs);
    QCOMPARE(AudioDelayParts{}.totalMs(), -1.0);

    // The first read plays the silence.
    MatcherReader reader = matcher.makeReader();
    QVERIFY(reader.valid());
    std::vector<float> out(kReadFrames * 2, 1.0f);
    reader.read(out.data(), kReadFrames);
    QVERIFY(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
    QCOMPARE(matcher.fillFrames(), 112.0);
    QCOMPARE(matcher.stats().dryRuns, std::uint64_t{0});
    QVERIFY(!MatcherReader().valid());
}

void TestDeviceRateMatcher::manualSizeAndRates()
{
    DeviceRateMatcher::Config config;
    config.delayMs = 20;
    DeviceRateMatcher twenty(config);
    QVERIFY(twenty.valid());
    QCOMPARE(twenty.delayStepMs(), 20);
    QCOMPARE(twenty.stats().rsizeFrames, 1920);

    config.delayMs = 7;   // not a step: the next one up
    DeviceRateMatcher seven(config);
    QCOMPARE(seven.delayStepMs(), 10);

    DeviceRateMatcher::Config converting;
    converting.inRate = 48000;
    converting.outRate = 44100;
    QVERIFY(DeviceRateMatcher::ringBytes(converting) > 0);
    DeviceRateMatcher matcher(converting);
#ifdef HAVE_WDSP
    QVERIFY(matcher.valid());
    // varsamp.c: out below in, so min_rate 44100, norm_rate 48000.
    QCOMPARE(matcher.resamplerDelayFrames(), static_cast<int>(140.0 * 48000.0 / 44100.0) / 2 - 1);
#else
    QVERIFY(!matcher.valid());   // copies only: no rate conversion without WDSP
#endif

    DeviceRateMatcher::Config bad;
    bad.outRate = 0;
    QCOMPARE(DeviceRateMatcher::ringBytes(bad), std::size_t{0});
    DeviceRateMatcher invalid(bad);
    QVERIFY(!invalid.valid());
    QVERIFY(invalid.ring() == nullptr);
    QVERIFY(!invalid.makeReader().valid());
    QCOMPARE(invalid.delayParts(1.0, 1.0).totalMs(), -1.0);
}

void TestDeviceRateMatcher::callerMemory()
{
    const DeviceRateMatcher::Config config;
    const std::size_t bytes = DeviceRateMatcher::ringBytes(config);
    auto memory = std::make_unique<std::uint64_t[]>(bytes / sizeof(std::uint64_t) + 1);

    DeviceRateMatcher tooSmall(config, memory.get(), bytes - 8);
    QVERIFY(!tooSmall.valid());

    DeviceRateMatcher matcher(config, memory.get(), bytes);
    QVERIFY(matcher.valid());
    QCOMPARE(static_cast<void*>(matcher.ring()), static_cast<void*>(memory.get()));
    QCOMPARE(attachMatcherRing(memory.get(), bytes), matcher.ring());
}

namespace {

struct DriftResult {
    double ratio = 0.0;
    double maxStep = 0.0;
    DeviceRateMatcherStats settled;
    DeviceRateMatcherStats end;
};

// Ten simulated minutes on one clock pair; the ratio is averaged over the
// last minute.  `reference` runs WDSP's rmatchV on the same clocks instead.
DriftResult runDrift(int ppm, bool reference)
{
    ClockSim::Options options;
    options.ppm = ppm;
    options.reference = reference;
    ClockSim sim(options);
    DriftResult result;
    sim.runUntil(10.0);
    result.settled = sim.matcher().stats();
    sim.runUntil(540.0);
    sim.runUntil(600.0, 540.0);
    result.end = sim.matcher().stats();
    result.ratio = sim.meanRatio();
    result.maxStep = sim.maxStep();
    return result;
}

} // namespace

void TestDeviceRateMatcher::driftMatchesRmatch_data()
{
    QTest::addColumn<int>("ppm");
    for (int ppm : kDriftOffsets) {
        QTest::addRow("%s", qPrintable(driftTag(ppm))) << ppm;
    }
}

void TestDeviceRateMatcher::driftMatchesRmatch()
{
#ifndef HAVE_WDSP
    QSKIP("WDSP's rmatchV is the reference; this build has no WDSP");
#else
    QFETCH(int, ppm);
    std::future<DriftResult> ours = std::async(std::launch::async, runDrift, ppm, false);
    std::future<DriftResult> references = std::async(std::launch::async, runDrift, ppm, true);
    const DriftResult result = ours.get();
    const double reference = references.get().ratio;
    const DeviceRateMatcherStats& end = result.end;
    qInfo("ppm %d: ratio %.9f, rmatchV %.9f, expected %.9f; step %d ms, dry runs %llu, overruns %llu",
          ppm, result.ratio, reference, 1.0 + ppm * 1.0e-6, end.delayStepMs,
          static_cast<unsigned long long>(end.dryRuns),
          static_cast<unsigned long long>(end.overruns));
    QVERIFY(end.controlActive);
    QCOMPARE(end.dryRuns, result.settled.dryRuns);
    QCOMPARE(end.overruns, result.settled.overruns);
    QCOMPARE(end.delayStepMs, result.settled.delayStepMs);
    QVERIFY2(std::abs(result.ratio - reference) <= 10.0e-6,
             qPrintable(QStringLiteral("ppm %1: ratio %2 vs rmatchV %3")
                            .arg(ppm).arg(result.ratio, 0, 'f', 9).arg(reference, 0, 'f', 9)));
    QVERIFY(result.maxStep <= kMaxAllowedStep);
#endif
}

// Equal clocks with a continuous reader: intentional missing writes must
// recover as well as transport silence. Counter checks start at the first
// resumed write; continuity starts after 50 ms of existing padding/fade.
// Ratio agreement uses
// driftMatchesRmatch's final minute of 600 s and its unchanged tolerance.
// Keep the earlier 30-60 s ratio diagnostic: neither arm is necessarily
// settled then. Real varsamp, no forced ratio, repeated 0.5, 5 and 30 s gaps.
void TestDeviceRateMatcher::blockWriterRecoversAfterHiatus_data()
{
    QTest::addColumn<int>("callbackFrames");
    QTest::addColumn<double>("warmupSeconds");
    QTest::addColumn<int>("delayMs");
    for (const HiatusRow& row : hiatusRows()) {
        QTest::addRow("%s", qPrintable(row.tag)) << row.callbackFrames << row.warmupSeconds << row.delayMs;
    }
}

void TestDeviceRateMatcher::blockWriterRecoversAfterHiatus()
{
#ifndef HAVE_WDSP
    QSKIP("Recovery acceptance needs the production variable resampler");
#else
    QFETCH(int, callbackFrames);
    QFETCH(double, warmupSeconds);
    QFETCH(int, delayMs);
    const DeviceRateMatcher::Config config{kRate, kRate, kWriteFrames, callbackFrames, delayMs};
    DeviceRateMatcher gap(config);
    DeviceRateMatcher zeros(config);
    MatcherReader gapReader = gap.makeReader();
    MatcherReader zeroReader = zeros.makeReader();
    QVERIFY(gap.valid() && zeros.valid() && gapReader.valid() && zeroReader.valid());
    std::array<DeviceRateMatcher*, 2> matchers{&gap, &zeros};
    std::array<MatcherReader*, 2> readers{&gapReader, &zeroReader};
    std::vector<float> tone(kWriteFrames * 2);
    const std::vector<float> silence(kWriteFrames * 2, 0.0f);
    std::vector<float> output(static_cast<std::size_t>(callbackFrames) * 2);
    SineSource source;
    std::array<double, 2> last{};
    std::int64_t nextWriter = 0;
    std::int64_t nextReader = 0;
    std::int64_t keyBegin = static_cast<std::int64_t>(warmupSeconds * kRate);

    for (double duration : {0.5, 5.0, 30.0}) {
        const std::int64_t keyEnd = keyBegin + static_cast<std::int64_t>(duration * kRate);
        const std::int64_t recoveryBegin = keyEnd + kRate / 20;
        const std::int64_t sampleBegin = keyEnd + 540 * kRate;
        const std::int64_t end = keyEnd + 600 * kRate;
        std::array<DeviceRateMatcherStats, 2> before{};
        std::array<DeviceRateMatcherStats, 2> resume{};
        std::array<double, 2> ratioSum{};
        std::array<double, 2> earlyRatioSum{};
        std::array<double, 2> maxStep{};
        std::uint64_t ratioSamples = 0;
        std::uint64_t earlyRatioSamples = 0;
        bool keyed = false;
        bool resumed = false;
        while (nextWriter < end || nextReader < end) {
            if (nextWriter <= nextReader) {
                if (!keyed && nextWriter >= keyBegin) {
                    keyed = true;
                    for (std::size_t arm = 0; arm < matchers.size(); ++arm) {
                        before[arm] = matchers[arm]->stats();
                    }
                }
                source.fill(tone.data(), kWriteFrames); // source time also advances in the gap
                const bool inGap = nextWriter >= keyBegin && nextWriter < keyEnd;
                const std::int64_t nowNs = nextWriter * 1'000'000'000LL / kRate;
                if (!inGap) {
                    gap.write(tone.data(), kWriteFrames, nowNs);
                }
                zeros.write(inGap ? silence.data() : tone.data(), kWriteFrames, nowNs);
                if (!resumed && nextWriter >= keyEnd) {
                    resumed = true;
                    for (std::size_t arm = 0; arm < matchers.size(); ++arm) {
                        resume[arm] = matchers[arm]->stats();
                    }
                }
                if (nextWriter >= keyEnd + 30 * kRate && nextWriter < keyEnd + 60 * kRate) {
                    for (std::size_t arm = 0; arm < matchers.size(); ++arm) {
                        earlyRatioSum[arm] += matchers[arm]->ratio();
                    }
                    ++earlyRatioSamples;
                }
                if (nextWriter >= sampleBegin) {
                    for (std::size_t arm = 0; arm < matchers.size(); ++arm) {
                        ratioSum[arm] += matchers[arm]->ratio();
                    }
                    ++ratioSamples;
                }
                nextWriter += kWriteFrames;
            } else {
                for (std::size_t arm = 0; arm < readers.size(); ++arm) {
                    readers[arm]->read(output.data(), callbackFrames);
                    for (int f = 0; f < callbackFrames; ++f) {
                        const double sample = output[static_cast<std::size_t>(2 * f)];
                        if (nextReader + f >= recoveryBegin) {
                            maxStep[arm] = std::max(maxStep[arm], std::abs(sample - last[arm]));
                        }
                        last[arm] = sample;
                    }
                }
                nextReader += callbackFrames;
            }
        }
        QVERIFY(ratioSamples > 0);
        QVERIFY(earlyRatioSamples > 0);
        const double gapMean = ratioSum[0] / ratioSamples;
        const double zeroMean = ratioSum[1] / ratioSamples;
        qInfo("callback %d warm %.1f gap %.1f: ratio %.9f vs zeros %.9f; resumed active %d step %d -> %d",
              callbackFrames, warmupSeconds, duration, gapMean, zeroMean, resume[0].controlActive,
              before[0].delayStepMs, resume[0].delayStepMs);
        qInfo("early 30-60 s ratio %.9f vs zeros %.9f; recovery max steps %.9f and %.9f",
              earlyRatioSum[0] / earlyRatioSamples, earlyRatioSum[1] / earlyRatioSamples,
              maxStep[0], maxStep[1]);
        QVERIFY(resume[0].dryRuns > before[0].dryRuns); // silence during the gap is expected
        QCOMPARE(resume[1].dryRuns, before[1].dryRuns);
        const auto& steps = DeviceRateMatcher::kDelayStepsMs;
        const auto beforeIndex = std::find(steps.begin(), steps.end(), before[0].delayStepMs) - steps.begin();
        const int recoveryStep = delayMs == 0
            ? steps[std::min(beforeIndex + 1, static_cast<decltype(beforeIndex)>(steps.size() - 1))]
            : before[0].delayStepMs;
        QCOMPARE(resume[0].delayStepMs, recoveryStep);
        for (std::size_t arm = 0; arm < matchers.size(); ++arm) {
            const DeviceRateMatcherStats final = matchers[arm]->stats();
            QVERIFY(final.controlActive);
            QCOMPARE(final.dryRuns, resume[arm].dryRuns);
            QCOMPARE(final.overruns, resume[arm].overruns);
            QCOMPARE(final.delayStepMs, resume[arm].delayStepMs);
            QVERIFY(maxStep[arm] <= kMaxAllowedStep);
        }
        QVERIFY2(std::abs(gapMean - zeroMean) <= 10.0e-6,
                 qPrintable(QStringLiteral("resumed ratio %1 vs transport silence %2")
                                .arg(gapMean, 0, 'f', 9).arg(zeroMean, 0, 'f', 9)));
        keyBegin = end;
    }
#endif
}

void TestDeviceRateMatcher::blockWriterReacquiresRealDriftAfterHiatus_data()
{
    QTest::addColumn<int>("ppm");
    for (int ppm : kDriftOffsets) {
        QTest::addRow("%s", qPrintable(driftTag(ppm))) << ppm;
    }
}

void TestDeviceRateMatcher::blockWriterReacquiresRealDriftAfterHiatus()
{
#ifndef HAVE_WDSP
    QSKIP("Drift recovery acceptance needs the production variable resampler");
#else
    QFETCH(int, ppm);
    ClockSim::Options options;
    options.ppm = ppm;
    options.writerHiatusAtSeconds = 15.0;
    ClockSim sim(options);
    sim.runUntil(50.0);
    const DeviceRateMatcherStats settled = sim.matcher().stats();
    sim.runUntil(540.0);
    sim.runUntil(600.0, 540.0);
    const DeviceRateMatcherStats end = sim.matcher().stats();
    qInfo("resumed drift %d ppm: ratio %.9f, dry %llu, over %llu, step %d ms", ppm, sim.meanRatio(),
          static_cast<unsigned long long>(end.dryRuns), static_cast<unsigned long long>(end.overruns),
          end.delayStepMs);
    QVERIFY(end.controlActive);
    QCOMPARE(end.dryRuns, settled.dryRuns);
    QCOMPARE(end.overruns, settled.overruns);
    QCOMPARE(end.delayStepMs, settled.delayStepMs);
    QVERIFY(std::abs(sim.meanRatio() - (1.0 + ppm * 1.0e-6)) <= 10.0e-6);
    QVERIFY(sim.maxStep() <= kMaxAllowedStep);
#endif
}

void TestDeviceRateMatcher::forcedDryRunStepsUpOnce()
{
    ClockSim::Options options;
    options.ppm = 200;
    options.readerSkipAtSeconds = 15.0;
    ClockSim sim(options);
    sim.runUntil(14.9);
    const DeviceRateMatcherStats before = sim.matcher().stats();
    QVERIFY(before.controlActive);
    sim.runUntil(15.2);
    const DeviceRateMatcherStats after = sim.matcher().stats();
    sim.runUntil(25.0);
    const DeviceRateMatcherStats end = sim.matcher().stats();
    qInfo("dry run: step %d -> %d -> %d ms, dry runs %llu -> %llu -> %llu, max step %.5f (limit %.5f)",
          before.delayStepMs, after.delayStepMs, end.delayStepMs,
          static_cast<unsigned long long>(before.dryRuns),
          static_cast<unsigned long long>(after.dryRuns),
          static_cast<unsigned long long>(end.dryRuns), sim.maxStep(), kMaxAllowedStep);
    QVERIFY(after.dryRuns > before.dryRuns);
    const auto& steps = DeviceRateMatcher::kDelayStepsMs;
    const auto beforeIndex = std::find(steps.begin(), steps.end(), before.delayStepMs) - steps.begin();
    const auto endIndex = std::find(steps.begin(), steps.end(), end.delayStepMs) - steps.begin();
    QCOMPARE(endIndex, beforeIndex + 1);
    QCOMPARE(end.delayStepMs, after.delayStepMs);
    QCOMPARE(end.dryRuns, after.dryRuns);
    QVERIFY(sim.maxStep() <= kMaxAllowedStep);
}

void TestDeviceRateMatcher::forcedOverrunStaysSmooth()
{
    ClockSim::Options options;
    options.ppm = -200;
    options.writerBurstAtSeconds = 15.0;
    ClockSim sim(options);
    sim.runUntil(14.9);
    const DeviceRateMatcherStats before = sim.matcher().stats();
    QVERIFY(before.controlActive);
    sim.runUntil(25.0);
    const DeviceRateMatcherStats end = sim.matcher().stats();
    qInfo("overrun: overruns %llu -> %llu, dry runs %llu -> %llu, max step %.5f (limit %.5f)",
          static_cast<unsigned long long>(before.overruns),
          static_cast<unsigned long long>(end.overruns),
          static_cast<unsigned long long>(before.dryRuns),
          static_cast<unsigned long long>(end.dryRuns), sim.maxStep(), kMaxAllowedStep);
    QVERIFY(end.overruns > before.overruns);
    QVERIFY(sim.maxStep() <= kMaxAllowedStep);
}

void TestDeviceRateMatcher::flushRestartAndFade()
{
    ClockSim::Options options;
    ClockSim sim(options);
    sim.runUntil(4.0);
    QVERIFY(sim.matcher().stats().controlActive);

    // A restart turns the control off until 3 s of reads and writes again.
    sim.matcher().requestRestart();
    sim.runUntil(4.5);
    QVERIFY(!sim.matcher().stats().controlActive);
    QCOMPARE(sim.matcher().ratio(), 1.0);
    sim.runUntil(8.0);
    QVERIFY(sim.matcher().stats().controlActive);

    // A flush drops what is queued and starts again from the target.
    sim.matcher().requestFlush();
    sim.runUntil(8.5);
    QVERIFY(sim.maxStep() <= kMaxAllowedStep);

    // The fade: down to silence over ntslew (127) frames, then silence.
    DeviceRateMatcher matcher(DeviceRateMatcher::Config{});
    MatcherReader reader = matcher.makeReader();
    SineSource source;
    std::vector<float> in(kWriteFrames * 2);
    std::vector<float> out(kReadFrames * 2);
    for (int i = 0; i < 20; ++i) {
        source.fill(in.data(), kWriteFrames);
        matcher.write(in.data(), kWriteFrames, i * 1'333'333LL);
        if (i % 2 == 1) {
            reader.read(out.data(), kReadFrames);
        }
    }
    QVERIFY(!reader.fadedOut());
    reader.requestFadeOut();
    reader.read(out.data(), kReadFrames);
    QVERIFY(reader.fadedOut());
    QCOMPARE(out[2 * 127 + 0], 0.0f);
    QCOMPARE(out[2 * 127 + 1], 0.0f);
    source.fill(in.data(), kWriteFrames);
    matcher.write(in.data(), kWriteFrames, 30 * 1'333'333LL);
    reader.read(out.data(), kReadFrames);
    QVERIFY(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
}

// A loud tone, a flush, then silence.  The reader takes the flush's skip on
// its next read and blends from the tone it was playing into the silence
// over ntslew + 1 frames (rmatch's blend(), the declick that keeps the cut
// from clicking).  From the frame after that blend every sample is exactly
// 0.0f: the padded silence is zeros, and with the resampler's history
// flushed the resampled silence is zeros too.  Exact, not a threshold: the
// blend's last weight is cslew[ntslew] = 1.0, so its own last frame is
// already 0.0 * tone + 1.0 * 0.0.
void TestDeviceRateMatcher::flushLeavesNothingOfTheEarlierAudio()
{
    constexpr float kLoud = 0.9f;
    constexpr std::int64_t kWritePeriodNs = 1'333'333;   // 64 frames at 48 kHz
    DeviceRateMatcher matcher(DeviceRateMatcher::Config{});
    MatcherReader reader = matcher.makeReader();
    const int blendFrames = static_cast<int>(matcher.ring()->slewFrames) + 1;
    QVERIFY(blendFrames > 1 && blendFrames <= kReadFrames);

    std::vector<float> tone(static_cast<std::size_t>(kWriteFrames) * 2);
    const std::vector<float> silence(static_cast<std::size_t>(kWriteFrames) * 2, 0.0f);
    std::vector<float> out(static_cast<std::size_t>(kReadFrames) * 2);
    std::vector<float> after;
    std::uint64_t toneFrame = 0;
    std::int64_t nowNs = 0;
    int writes = 0;
    float tonePeak = 0.0f;
    auto step = [&](bool loud, bool collect) {
        if (loud) {
            for (int f = 0; f < kWriteFrames; ++f, ++toneFrame) {
                const double phase = 2.0 * std::numbers::pi * static_cast<double>(toneFrame % 48) / 48.0;
                tone[static_cast<std::size_t>(2 * f + 0)] = kLoud * static_cast<float>(std::sin(phase));
                tone[static_cast<std::size_t>(2 * f + 1)] = -kLoud * static_cast<float>(std::sin(phase));
            }
        }
        matcher.write(loud ? tone.data() : silence.data(), kWriteFrames, nowNs);
        nowNs += kWritePeriodNs;
        if (++writes % 2 == 0) {
            reader.read(out.data(), kReadFrames);
            if (collect) {
                after.insert(after.end(), out.begin(), out.end());
            } else {
                for (float v : out) {
                    tonePeak = std::max(tonePeak, std::abs(v));
                }
            }
        }
    };
    for (int i = 0; i < 375; ++i) {   // 0.5 s of the tone
        step(true, false);
    }
    QVERIFY2(tonePeak > 0.8f, qPrintable(QString::number(tonePeak)));

    matcher.requestFlush();
    for (int i = 0; i < 150; ++i) {   // 0.2 s of silence after the flush
        step(false, true);
    }
    QVERIFY(after.size() >= static_cast<std::size_t>(kReadFrames) * 2 * 50);
    QCOMPARE(matcher.stats().dryRuns, std::uint64_t{0});

    // The blend: the tone's fade, under its raised-cosine envelope.
    for (int f = 0; f < blendFrames; ++f) {
        const double weight = 0.5 * (1.0 + std::cos(std::numbers::pi * f / (blendFrames - 1)));
        for (int c = 0; c < 2; ++c) {
            const float v = after[static_cast<std::size_t>(2 * f + c)];
            QVERIFY2(std::abs(v) <= kLoud * weight + 1e-6,
                     qPrintable(QStringLiteral("blend frame %1: %2").arg(f).arg(v)));
        }
    }
    // Then nothing of the tone at all.
    std::size_t nonZero = 0;
    std::size_t firstNonZero = 0;
    float peakAfter = 0.0f;
    for (std::size_t i = static_cast<std::size_t>(blendFrames) * 2; i < after.size(); ++i) {
        if (after[i] != 0.0f) {
            if (nonZero == 0) {
                firstNonZero = i / 2;
            }
            ++nonZero;
            peakAfter = std::max(peakAfter, std::abs(after[i]));
        }
    }
    QVERIFY2(nonZero == 0,
             qPrintable(QStringLiteral("%1 non-zero samples after the blend, the first at frame %2, peak %3")
                            .arg(nonZero).arg(firstNonZero).arg(peakAfter)));
}

// D7: a restart starts the automatic size again.  A writer that was idle
// for 2 s and then restarts the clock match (a quiet remote VAX channel, a
// remote stream ending) sizes from the gaps after the restart, so it
// reaches the step a fresh matcher reaches on the same steady audio, never
// the 40 ms the idle gap would ask for.  The write times are the
// simulated clock's, so the check does not depend on the host.
void TestDeviceRateMatcher::restartSizesAfresh()
{
    constexpr std::int64_t kWritePeriodNs = 1'333'333;   // 64 frames at 48 kHz
    std::vector<float> in(static_cast<std::size_t>(kWriteFrames) * 2, 0.25f);
    std::vector<float> out(static_cast<std::size_t>(kReadFrames) * 2);
    auto steady = [&](DeviceRateMatcher& matcher, MatcherReader& reader, std::int64_t& nowNs,
                      int writes) {
        for (int i = 0; i < writes; ++i) {
            matcher.write(in.data(), kWriteFrames, nowNs);
            nowNs += kWritePeriodNs;
            if (i % 2 == 1) {
                reader.read(out.data(), kReadFrames);
            }
        }
    };
    constexpr int kSteadyWrites = 2625;   // 3.5 s, past the 3 s start-up

    DeviceRateMatcher fresh(DeviceRateMatcher::Config{});
    MatcherReader freshReader = fresh.makeReader();
    std::int64_t freshNow = 0;
    steady(fresh, freshReader, freshNow, kSteadyWrites);
    QVERIFY(fresh.stats().controlActive);
    const int freshStep = fresh.delayStepMs();

    DeviceRateMatcher matcher(DeviceRateMatcher::Config{});
    MatcherReader reader = matcher.makeReader();
    std::int64_t nowNs = 0;
    steady(matcher, reader, nowNs, 10);
    nowNs += 2'000'000'000LL;   // idle 2 s: no writes, no reads
    matcher.requestRestart();
    steady(matcher, reader, nowNs, kSteadyWrites);
    QVERIFY(matcher.stats().controlActive);
    qInfo("restart after 2 s idle: step %d ms, a fresh matcher %d ms", matcher.delayStepMs(), freshStep);
    QVERIFY(freshStep < DeviceRateMatcher::kDelayStepsMs.back());
    QCOMPARE(matcher.delayStepMs(), freshStep);
}

// The reader counts a dry run (dryRuns) and then raises upslewPending.
// The writer can see the flag while the count it loaded is still the old
// one; it must neither fade in that chunk with no padding before it nor
// leave half a fade for after the padding.  The two steps of the reader's
// dry run are made visible one at a time here, in the ring itself, as the
// writer would see them across that interleaving.
void TestDeviceRateMatcher::dryRunFadeInFollowsItsPadding()
{
    constexpr float kLevel = 0.5f;
    constexpr std::int64_t kWritePeriodNs = 1'333'333;
    DeviceRateMatcher matcher(DeviceRateMatcher::Config{});
    MatcherReader reader = matcher.makeReader();
    MatcherRingHeader* ring = matcher.ring();
    QVERIFY(ring != nullptr);
    const std::uint64_t mask = static_cast<std::uint64_t>(ring->capacityFrames) - 1;
    const int ntslew = static_cast<int>(ring->slewFrames);
    QVERIFY(ntslew > kWriteFrames);
    auto frameAt = [&](std::uint64_t index) { return ring->frames()[(index & mask) * 2]; };

    const std::vector<float> in(static_cast<std::size_t>(kWriteFrames) * 2, kLevel);
    std::vector<float> out(static_cast<std::size_t>(kReadFrames) * 2);
    std::int64_t nowNs = 0;
    for (int i = 0; i < 100; ++i) {   // the filter settles; no dry run
        matcher.write(in.data(), kWriteFrames, nowNs);
        nowNs += kWritePeriodNs;
        if (i % 2 == 1) {
            reader.read(out.data(), kReadFrames);
        }
    }
    QCOMPARE(matcher.stats().dryRuns, std::uint64_t{0});

    // The flag without its count: this chunk is written as any other.
    const std::uint64_t w0 = ring->written.load();
    ring->upslewPending.store(1);
    matcher.write(in.data(), kWriteFrames, nowNs);
    nowNs += kWritePeriodNs;
    const std::uint64_t w1 = ring->written.load();
    QVERIFY(w1 > w0);
    for (std::uint64_t f = w0; f < w1; ++f) {
        QVERIFY2(std::abs(frameAt(f) - kLevel) < 0.01f,
                 qPrintable(QStringLiteral("frame %1 of the chunk: %2").arg(f - w0).arg(frameAt(f))));
    }

    // Then the count: the next chunk follows the padding, faded in from
    // its first frame.  Its last frame, about 64 frames into the 127-frame
    // raised cosine, is near half the level; a fade begun on the chunk
    // before would be over by then.
    ring->dryRuns.fetch_add(1);
    matcher.write(in.data(), kWriteFrames, nowNs);
    const std::uint64_t w2 = ring->written.load();
    QVERIFY(w2 > w1);
    const float last = frameAt(w2 - 1);
    QVERIFY2(last > 0.15f * kLevel && last < 0.7f * kLevel,
             qPrintable(QStringLiteral("last frame of the faded chunk: %1").arg(last)));
    const float first = frameAt(w2 - static_cast<std::uint64_t>(kWriteFrames) + 1);
    QVERIFY2(std::abs(first) < 0.01f,
             qPrintable(QStringLiteral("second frame of the faded chunk: %1").arg(first)));
}

// A writer of whole packets (remote playback: 1920 frames of Opus, 192 of
// lossless) gets a size whose ring holds a packet, the device's callback
// and one write block, from its first write; the automatic size starts at
// the callback plus a packet, and a manual size too small is raised.  The
// block writer's size comes back with setWritePacketFrames(0).
void TestDeviceRateMatcher::packetWriterSizeHoldsAPacket_data()
{
    QTest::addColumn<int>("delayMs");
    QTest::addColumn<int>("packetFrames");
    QTest::addColumn<int>("stepMs");
    QTest::addColumn<int>("blockWriterStepMs");
    QTest::newRow("automatic, Opus") << 0 << 1920 << 40 << 5;
    QTest::newRow("automatic, lossless") << 0 << 192 << 10 << 5;
    QTest::newRow("2 ms, lossless") << 2 << 192 << 5 << 2;
    QTest::newRow("5 ms, lossless") << 5 << 192 << 5 << 5;
    QTest::newRow("20 ms, lossless") << 20 << 192 << 20 << 20;
    QTest::newRow("5 ms, Opus") << 5 << 1920 << 40 << 5;
}

void TestDeviceRateMatcher::packetWriterSizeHoldsAPacket()
{
    QFETCH(int, delayMs);
    QFETCH(int, packetFrames);
    QFETCH(int, stepMs);
    QFETCH(int, blockWriterStepMs);
    DeviceRateMatcher::Config config;
    config.delayMs = delayMs;
    DeviceRateMatcher matcher(config);
    QVERIFY(matcher.valid());
    MatcherReader reader = matcher.makeReader();
    QCOMPARE(matcher.delayStepMs(), blockWriterStepMs);

    // Asked, and not yet applied: the stats still read no packet.
    matcher.setWritePacketFrames(packetFrames);
    QCOMPARE(matcher.stats().packetFrames, 0);

    const std::vector<float> packet(static_cast<std::size_t>(packetFrames) * 2, 0.25f);
    std::vector<float> out(static_cast<std::size_t>(kReadFrames) * 2);
    std::int64_t nowNs = 0;
    matcher.write(packet.data(), packetFrames, nowNs);
    DeviceRateMatcherStats stats = matcher.stats();
    QCOMPARE(stats.delayStepMs, stepMs);
    QCOMPARE(stats.packetFrames, packetFrames);
    QVERIFY(stats.packetOutFrames >= packetFrames);
    QVERIFY(stats.rsizeFrames >= stats.packetOutFrames + config.callbackFrames + kWriteFrames);
    QVERIFY(stats.packetHighWaterFrames <= stats.rsizeFrames);
    // The first packet went in whole, on the silence a restart leaves, and
    // the fill then swings about the target.
    QCOMPARE(stats.overruns, std::uint64_t(0));
    QCOMPARE(stats.queuedFrames, stats.rsizeFrames / 2 + packetFrames / 2);
    QVERIFY(stats.queuedFrames <= stats.packetHighWaterFrames);

    // A restart and a flush keep the size, and the packet after each
    // fits: the device plays until there is room for a packet, as the
    // writer's room rule has it, and the restart or the flush is taken
    // with that packet's write.
    const auto playUntilRoom = [&] {
        for (int i = 0; i < 100; ++i) {
            reader.read(out.data(), kReadFrames);
            const DeviceRateMatcherStats now = matcher.stats();
            if (now.queuedFrames + now.packetOutFrames <= now.packetHighWaterFrames) {
                return true;
            }
        }
        return false;
    };
    QVERIFY(playUntilRoom());
    matcher.requestRestart();
    matcher.write(packet.data(), packetFrames, nowNs);
    QCOMPARE(matcher.stats().overruns, std::uint64_t(0));
    QCOMPARE(matcher.stats().queuedFrames, stats.rsizeFrames / 2 + packetFrames / 2);
    QVERIFY(playUntilRoom());
    matcher.requestFlush();
    matcher.write(packet.data(), packetFrames, nowNs);
    stats = matcher.stats();
    QCOMPARE(stats.delayStepMs, stepMs);
    QCOMPARE(stats.overruns, std::uint64_t(0));
    QCOMPARE(stats.dryRuns, std::uint64_t(0));
    QCOMPARE(stats.queuedFrames, stats.rsizeFrames / 2 + packetFrames / 2);

    // The block writer again: its own first size, and no packet.
    QVERIFY(playUntilRoom());
    matcher.setWritePacketFrames(0);
    matcher.write(packet.data(), kWriteFrames, nowNs);
    stats = matcher.stats();
    QCOMPARE(stats.delayStepMs, blockWriterStepMs);
    QCOMPARE(stats.packetFrames, 0);
    QCOMPARE(stats.packetHighWaterFrames, 0);
    QCOMPARE(stats.overruns, std::uint64_t(0));
    reader.read(out.data(), kReadFrames);
    QCOMPARE(matcher.stats().dryRuns, std::uint64_t(0));
}

// The packet writer with its room rule (a packet is written when
// queuedFrames + packetOutFrames is within packetHighWaterFrames, and says
// how long it was kept back), on two simulated clocks 300 ppm apart:
// packets one at a time, in batches of seven as a link delivers them, and
// after the source stopped for 250 ms and its backlog came at once.  The
// ring never overruns, the device runs dry only while the source is
// stopped, the control still finds the clocks' ratio, and a backlog is
// played out no faster than the matcher's limit.
void TestDeviceRateMatcher::packetWriterTakesBurstsAndMatchesTheClocks_data()
{
    QTest::addColumn<int>("packetFrames");
    QTest::addColumn<int>("ppm");
    QTest::addColumn<int>("batch");
    QTest::addColumn<bool>("stall");
    for (int packetFrames : {1920, 192}) {
        const char* name = packetFrames == 1920 ? "Opus" : "lossless";
        for (int ppm : {300, -300}) {
            QTest::addRow("%s, %d ppm", name, ppm) << packetFrames << ppm << 1 << false;
            QTest::addRow("%s, %d ppm, batches of 7", name, ppm) << packetFrames << ppm << 7 << false;
            QTest::addRow("%s, %d ppm, a 250 ms stall", name, ppm) << packetFrames << ppm << 1 << true;
        }
    }
}

void TestDeviceRateMatcher::packetWriterTakesBurstsAndMatchesTheClocks()
{
    QFETCH(int, packetFrames);
    QFETCH(int, ppm);
    QFETCH(int, batch);
    QFETCH(bool, stall);
    constexpr double kSeconds = 120.0;
    constexpr double kStallAt = 40.0;
    constexpr double kStallSeconds = 0.25;
    DeviceRateMatcher matcher(DeviceRateMatcher::Config{});
    QVERIFY(matcher.valid());
    MatcherReader reader = matcher.makeReader();
    SineSource source;
    std::vector<float> packet(static_cast<std::size_t>(packetFrames) * 2);
    std::vector<float> out(static_cast<std::size_t>(kReadFrames) * 2);

    // The source's clock is the time line; the device's runs ppm fast.
    const double packetSeconds = static_cast<double>(packetFrames) / kRate;
    const double readSeconds = static_cast<double>(kReadFrames) / (kRate * (1.0 + ppm * 1.0e-6));
    std::int64_t sent = 0;        // packets the source has made
    std::int64_t delivered = 0;   // packets the link has handed over
    std::int64_t written = 0;
    std::deque<double> waitingSince;   // when each packet not yet written was handed over
    bool refused = false;
    // The device's dry runs before the first packet are not the stream's.
    std::uint64_t dryAtStart = 0;
    std::uint64_t dryInStall = 0;
    bool stallOver = false;
    std::int64_t keptBackAfterStall = 0;
    double lowestRatioAfterStall = 2.0;
    int lowestFill = std::numeric_limits<int>::max();

    const auto writeWhatFits = [&](double now) {
        // A packet written in the same pass as one that waited stood
        // behind it, and waited with it (the receiver's passIntoMatcher).
        bool behindWaited = false;
        while (written < delivered) {
            const DeviceRateMatcherStats stats = matcher.stats();
            const bool room = stats.packetFrames != packetFrames || stats.queuedFrames <= 0
                || stats.queuedFrames + stats.packetOutFrames <= stats.packetHighWaterFrames;
            if (!room) {
                refused = true;
                return;
            }
            if (now > kSeconds / 2 && !(stall && now < kStallAt + 20.0)) {
                lowestFill = std::min(lowestFill, stats.queuedFrames);
            }
            if (written == 0) {
                dryAtStart = stats.dryRuns;
            }
            source.fill(packet.data(), packetFrames);
            // The link handed it over that long ago; kept back, it waited
            // all that time (at least 1 ns, which says that it waited).
            MatcherPacketTiming timing;
            timing.arrivedAgoNs = static_cast<std::int64_t>((now - waitingSince.front()) * 1.0e9);
            timing.waitedNs
                = refused || behindWaited ? std::max<std::int64_t>(1, timing.arrivedAgoNs) : 0;
            timing.sourceFrame = written * packetFrames;
            matcher.setWritePacketFrames(packetFrames, timing);
            refused = false;
            behindWaited = timing.waitedNs > 0;
            matcher.write(packet.data(), packetFrames, static_cast<std::int64_t>(now * 1.0e9));
            waitingSince.pop_front();
            ++written;
        }
    };

    double nextPacket = 0.0;
    double nextRead = readSeconds / 2;
    while (std::min(nextPacket, nextRead) < kSeconds) {
        if (nextPacket <= nextRead) {
            const double now = nextPacket;
            ++sent;
            nextPacket = static_cast<double>(sent) * packetSeconds;
            // The link hands over whole batches; nothing while it is stalled,
            // and then all of it.
            const bool stalled = stall && now >= kStallAt && now < kStallAt + kStallSeconds;
            if (!stalled) {
                delivered = sent - sent % batch;
                waitingSince.resize(static_cast<std::size_t>(delivered - written), now);
            }
            const bool stallEnds = stall && !stallOver && now >= kStallAt + kStallSeconds;
            if (stallEnds) {
                dryInStall = matcher.stats().dryRuns - dryAtStart;
            }
            writeWhatFits(now);
            if (stallEnds) {
                stallOver = true;
                keptBackAfterStall = delivered - written;
            }
        } else {
            const double now = nextRead;
            nextRead += readSeconds;
            reader.read(out.data(), kReadFrames);
            writeWhatFits(now);
            if (stallOver) {
                lowestRatioAfterStall = std::min(lowestRatioAfterStall, matcher.stats().ratio);
            }
        }
    }

    const DeviceRateMatcherStats stats = matcher.stats();
    const double clocksRatio = 1.0 + ppm * 1.0e-6;
    qInfo("ratio %.6f, step %d ms, lowest fill ahead of a packet %d frames, dry runs %llu, "
          "packets kept back after the stall %lld and at the end %lld, lowest ratio after it "
          "%.0f ppm from the clocks'",
          stats.ratio, stats.delayStepMs, lowestFill,
          static_cast<unsigned long long>(stats.dryRuns - dryAtStart),
          static_cast<long long>(keptBackAfterStall), static_cast<long long>(delivered - written),
          stall ? (lowestRatioAfterStall - clocksRatio) * 1.0e6 : 0.0);
    QCOMPARE(stats.overruns, std::uint64_t(0));
    QVERIFY(stats.controlActive);
    // Audio stayed queued ahead of every packet.
    QVERIFY(lowestFill > kReadFrames);
    if (stall) {
        // The stall outlasted the audio that was queued; nothing ran dry
        // after it.  Its backlog came at once and waited behind the writer
        // (here nothing sheds it, as the receiver's jitter queue would),
        // and the control plays it out: at its limit of 0.5% (the clocks'
        // ratio is found to 3e-4 or better, as below), and at the end
        // nothing stands behind the writer.
        QVERIFY(dryInStall > 0);
        QCOMPARE(stats.dryRuns - dryAtStart, dryInStall);
        QVERIFY(keptBackAfterStall > 1);
        QVERIFY2(delivered - written <= 1,
                 qPrintable(QStringLiteral("%1 of %2").arg(delivered - written)
                                .arg(keptBackAfterStall)));
        QVERIFY2(lowestRatioAfterStall < clocksRatio - 0.005 + 3.0e-4
                     && lowestRatioAfterStall > clocksRatio - 0.005 - 3.0e-4,
                 qPrintable(QStringLiteral("ratio %1").arg(lowestRatioAfterStall, 0, 'f', 6)));
        return;
    }
    QCOMPARE(stats.dryRuns - dryAtStart, std::uint64_t(0));
    // The clocks' ratio, as the block writer finds it (driftMatchesRmatch),
    // and no backlog built up behind the writer.
    QVERIFY2(std::abs(stats.ratio - clocksRatio) < 1.0e-4,
             qPrintable(QStringLiteral("ratio %1").arg(stats.ratio, 0, 'f', 6)));
    QVERIFY(delivered - written <= batch);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestDeviceRateMatcher test;
    QTEST_SET_MAIN_SOURCE_PATH
    // 2026-10-16 CI speed round: 214 s on the Linux runner as one entry.
    // The ten-minute simulations run as ctest entries of their own
    // (tests/CMakeLists.txt): each drift offset (two threads each) as
    // tst_device_rate_matcher_drift1 and _drift2, the hiatus resumes after
    // a real drift as _reacquire, and blockWriterRecoversAfterHiatus's
    // rows, built here from the list its _data function reads, as
    // _hiatus1 to _hiatus3, every third row each; the rest as
    // tst_device_rate_matcher.
    QList<NereusSDR::TestFunctionGroups::Group> groups;
    for (std::size_t i = 0; i < kDriftOffsets.size(); ++i) {
        groups.append({QStringLiteral("drift%1").arg(i + 1),
                       {QStringLiteral("driftMatchesRmatch:") + driftTag(kDriftOffsets[i])}});
    }
    groups.append({QStringLiteral("reacquire"), {QStringLiteral("blockWriterReacquiresRealDriftAfterHiatus")}});
    constexpr int kHiatusEntries = 3;
    const qsizetype firstHiatus = groups.size();
    for (int k = 0; k < kHiatusEntries; ++k) {
        groups.append({QStringLiteral("hiatus%1").arg(k + 1), {}});
    }
    const QList<HiatusRow> rows = hiatusRows();
    for (qsizetype i = 0; i < rows.size(); ++i) {
        groups[firstHiatus + i % kHiatusEntries].entries.append(
            QStringLiteral("blockWriterRecoversAfterHiatus:") + rows.at(i).tag);
    }
    const std::optional<QStringList> arguments = NereusSDR::TestFunctionGroups::arguments(
        test.metaObject(), app.arguments(), "NEREUS_DEVICE_RATE_MATCHER_GROUP", groups);
    return arguments ? QTest::qExec(&test, *arguments) : 1;
}
#include "tst_device_rate_matcher.moc"
