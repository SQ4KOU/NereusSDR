// =================================================================
// tests/tst_pc_mic_source.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test file. No Thetis port at this layer.
//
// Exercises PcMicSource — the TxMicRouter implementation that taps
// AudioEngine::pullTxMic (Phase 3M-1b Task F.1).
//
// Strategy: inject a FakeAudioBus into AudioEngine's TX-input slot via
// the NEREUS_BUILD_TESTS seam setTxInputBusForTest, then verify that
// PcMicSource::pullSamples correctly dispatches to AudioEngine::pullTxMic
// and returns the same sample count and values. This approach exercises
// the full dispatch chain end-to-end rather than just a mock — matching
// the Option 2 recommendation in the F.1 task brief.
//
// Pre-code review cite: §0.3 + §12.7 (PcMicSource arch lock).
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-27 — Original test for NereusSDR by J.J. Boyd (KG4VCF),
//                 Phase 3M-1b Task F.1, with AI-assisted implementation
//                 via Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file.

#include <QtTest/QtTest>

#include "core/AudioEngine.h"
#include "core/audio/PcMicSource.h"
#include "core/IAudioBus.h"

#include "fakes/FakeAudioBus.h"

#include <array>
#include <cstdint>
#include <memory>

using namespace NereusSDR;

// ---------------------------------------------------------------------------
// Helper: build a QByteArray of Int16 interleaved stereo samples.
// Same helper pattern as tst_audio_engine_pull_tx_mic.cpp.
// ---------------------------------------------------------------------------
static QByteArray makeInt16StereoBytes(
    std::initializer_list<std::pair<int16_t, int16_t>> frames)
{
    QByteArray out;
    out.reserve(static_cast<int>(frames.size()) * 2 * 2);
    for (auto [l, r] : frames) {
        out.append(reinterpret_cast<const char*>(&l), 2);
        out.append(reinterpret_cast<const char*>(&r), 2);
    }
    return out;
}

class TstPcMicSource : public QObject {
    Q_OBJECT

private:
    // Build an AudioEngine + FakeAudioBus harness with an Int16 stereo
    // TX-input bus, suitable for most test cases.
    struct Harness {
        std::unique_ptr<AudioEngine> engine;
        FakeAudioBus* bus;  // non-owning; engine owns via m_txInputBus
    };

    static Harness makeHarness() {
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Int16;

        Harness h;
        h.engine = std::make_unique<AudioEngine>();

        auto fakeBus = std::make_unique<FakeAudioBus>(
            QStringLiteral("FakeTxInput"));
        fakeBus->open(fmt);
        h.bus = fakeBus.get();
        h.engine->setTxInputBusForTest(std::move(fakeBus));
        return h;
    }

private slots:

    // ── 1. Null engine: returns 0 ─────────────────────────────────────────
    // PcMicSource constructed with nullptr returns 0 without crashing.

    void nullEngine_returnsZero()
    {
        PcMicSource src(nullptr);
        std::array<float, 4> dst{1.0f, 2.0f, 3.0f, 4.0f};
        QCOMPARE(src.pullSamples(dst.data(), 4), 0);
        // Destination is untouched when engine is null.
        QCOMPARE(dst[0], 1.0f);
    }

    // ── 2. Valid engine, no data in bus: dispatches, returns 0 ────────────
    // Engine has a TX-input bus open but empty — dispatch happens,
    // AudioEngine::pullTxMic returns 0 (no data ready).

    void validEngine_noBusData_returnsZero()
    {
        Harness h = makeHarness();
        PcMicSource src(h.engine.get());

        std::array<float, 4> dst{};
        QCOMPARE(src.pullSamples(dst.data(), 4), 0);
    }

    // ── 3. Valid engine, data in bus: dispatches, returns correct count ────
    // Bus has 3 Int16 stereo frames → pullTxMic converts to 3 mono floats.
    // PcMicSource must return 3.

    void validEngine_withBusData_returnsSampleCount()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeInt16StereoBytes(
            {{1000, -500}, {2000, -1000}, {4096, 0}}));

        PcMicSource src(h.engine.get());
        std::array<float, 8> dst{};
        const int got = src.pullSamples(dst.data(), 8);
        QCOMPARE(got, 3);
    }

    // ── 4. Valid engine: correct sample values forwarded ──────────────────
    // pullSamples returns the same float values that AudioEngine::pullTxMic
    // produces. Tests the dispatch chain is transparent (no extra scaling).
    // Uses power-of-two values so Int16 / 32768.0f is exact.

    void validEngine_correctSamplesForwarded()
    {
        Harness h = makeHarness();
        // Frame 0: left=16384 → 0.5f; right=0 (discarded).
        // Frame 1: left=-8192 → -0.25f; right=32767 (discarded).
        h.bus->setPullData(makeInt16StereoBytes(
            {{16384, 0}, {-8192, 32767}}));

        PcMicSource src(h.engine.get());
        std::array<float, 4> dst{0.0f, 0.0f, 0.0f, 0.0f};
        const int got = src.pullSamples(dst.data(), 4);

        QCOMPARE(got, 2);
        QCOMPARE(dst[0], 16384.0f / 32768.0f);
        QCOMPARE(dst[1], -8192.0f / 32768.0f);
        // Samples beyond `got` must be untouched.
        QCOMPARE(dst[2], 0.0f);
    }

    // ── 5. n = 0: returns 0 (AudioEngine null-guard) ──────────────────────
    // AudioEngine::pullTxMic guards n <= 0. PcMicSource propagates this.

    void zeroN_returnsZero()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeInt16StereoBytes({{1000, 0}}));

        PcMicSource src(h.engine.get());
        std::array<float, 4> dst{9.9f, 9.9f, 9.9f, 9.9f};
        QCOMPARE(src.pullSamples(dst.data(), 0), 0);
        // Buffer is untouched.
        QCOMPARE(dst[0], 9.9f);
    }

    // ── 6. null dst: returns 0 (AudioEngine null-guard) ───────────────────
    // AudioEngine::pullTxMic guards dst == nullptr. PcMicSource propagates.

    void nullDst_returnsZero()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeInt16StereoBytes({{1000, 0}}));

        PcMicSource src(h.engine.get());
        QCOMPARE(src.pullSamples(nullptr, 4), 0);
    }

    // ── 7. Usable via TxMicRouter base pointer (vtable correct) ───────────
    // Construct via base pointer and verify dispatch still works.

    void viaBasePointer_dispatchWorks()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeInt16StereoBytes({{16384, 0}}));

        PcMicSource concrete(h.engine.get());
        TxMicRouter* router = &concrete;

        std::array<float, 4> dst{};
        const int got = router->pullSamples(dst.data(), 4);
        QCOMPARE(got, 1);
        QCOMPARE(dst[0], 16384.0f / 32768.0f);
    }
};

QTEST_APPLESS_MAIN(TstPcMicSource)
#include "tst_pc_mic_source.moc"
