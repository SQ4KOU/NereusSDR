// =================================================================
// tests/tst_vax_tx_mic_source.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test file. Mirrors tst_pc_mic_source.cpp.
//
// Exercises VaxTxMicSource — the TxMicRouter implementation that
// taps AudioEngine::pullVaxTxMic. Verifies the dispatch shim
// behaves correctly under null engine, empty bus, and populated
// bus, and that the stereo→mono downmix in pullVaxTxMic produces
// the expected average of L and R channels.
//
// Strategy: inject a FakeAudioBus into AudioEngine's VAX-TX slot via
// the NEREUS_BUILD_TESTS seam setVaxTxBusForTest, then drive
// VaxTxMicSource::pullSamples through AudioEngine::pullVaxTxMic and
// confirm the mono float results.
//
// The VAX TX shm contract is fixed at 48 kHz stereo float32 (see
// hal-plugin/NereusSDRVAX.cpp makePCMFormat); the tests assume that
// format throughout, matching the producer side.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-05-06 — Original test for NereusSDR by J.J. Boyd (KG4VCF),
//                 VAX TX → mic-source wiring, with AI-assisted
//                 implementation via Anthropic Claude Code
//                 (eager-borg-d64bed).
// =================================================================

// no-port-check: NereusSDR-original test file.

#include <QtTest/QtTest>

#include "core/AudioEngine.h"
#include "core/audio/VaxTxMicSource.h"
#include "core/IAudioBus.h"

#include "fakes/FakeAudioBus.h"

#include <array>
#include <cstring>
#include <memory>

using namespace NereusSDR;

// ---------------------------------------------------------------------------
// Helper: pack interleaved float32 stereo frames into a QByteArray.
// ---------------------------------------------------------------------------
static QByteArray makeFloat32StereoBytes(
    std::initializer_list<std::pair<float, float>> frames)
{
    QByteArray out;
    out.reserve(static_cast<int>(frames.size()) * 2 * static_cast<int>(sizeof(float)));
    for (auto [l, r] : frames) {
        out.append(reinterpret_cast<const char*>(&l), sizeof(float));
        out.append(reinterpret_cast<const char*>(&r), sizeof(float));
    }
    return out;
}

class TstVaxTxMicSource : public QObject {
    Q_OBJECT

private:
    // Build an AudioEngine + FakeAudioBus harness with a 48 kHz
    // stereo float32 VAX-TX bus — the format the HAL plugin
    // negotiates (hal-plugin/NereusSDRVAX.cpp makePCMFormat).
    struct Harness {
        std::unique_ptr<AudioEngine> engine;
        FakeAudioBus* bus;  // non-owning; engine owns via m_vaxTxBus
    };

    static Harness makeHarness() {
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        Harness h;
        h.engine = std::make_unique<AudioEngine>();

        auto fakeBus = std::make_unique<FakeAudioBus>(
            QStringLiteral("FakeVaxTx"));
        fakeBus->open(fmt);
        h.bus = fakeBus.get();
        h.engine->setVaxTxBusForTest(std::move(fakeBus));
        return h;
    }

private slots:

    // ── 1. Null engine: returns 0 ─────────────────────────────────────────

    void nullEngine_returnsZero()
    {
        VaxTxMicSource src(nullptr);
        std::array<float, 4> dst{1.0f, 2.0f, 3.0f, 4.0f};
        QCOMPARE(src.pullSamples(dst.data(), 4), 0);
        QCOMPARE(dst[0], 1.0f);  // buffer untouched
    }

    // ── 2. Valid engine, no data in bus: returns 0 ────────────────────────

    void validEngine_noBusData_returnsZero()
    {
        Harness h = makeHarness();
        VaxTxMicSource src(h.engine.get());

        std::array<float, 4> dst{};
        QCOMPARE(src.pullSamples(dst.data(), 4), 0);
    }

    // ── 3. Valid engine, data in bus: returns sample count ─────────────────
    // 3 stereo frames → 3 mono samples after downmix.

    void validEngine_withBusData_returnsFrameCount()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeFloat32StereoBytes({
            {0.5f,  0.5f},
            {0.25f, 0.75f},
            {-1.0f, 1.0f},
        }));

        VaxTxMicSource src(h.engine.get());
        std::array<float, 8> dst{};
        const int got = src.pullSamples(dst.data(), 8);
        QCOMPARE(got, 3);
    }

    // ── 4. Stereo→mono downmix correctness ────────────────────────────────
    // Each output sample = 0.5 * (L + R). Tests verify exact values.

    void validEngine_downmixIsAverageOfLR()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeFloat32StereoBytes({
            {0.5f,  0.5f},   // both → 0.5
            {0.25f, 0.75f},  // average → 0.5
            {-1.0f, 1.0f},   // average → 0
            {0.8f,  0.2f},   // average → 0.5
        }));

        VaxTxMicSource src(h.engine.get());
        std::array<float, 8> dst{};
        const int got = src.pullSamples(dst.data(), 8);
        QCOMPARE(got, 4);

        QCOMPARE(dst[0], 0.5f);
        QCOMPARE(dst[1], 0.5f);
        QCOMPARE(dst[2], 0.0f);
        QCOMPARE(dst[3], 0.5f);
        // Beyond `got`, samples are untouched.
        QCOMPARE(dst[4], 0.0f);
    }

    // ── 5. n = 0 guard returns 0, buffer untouched ────────────────────────

    void zeroN_returnsZero()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeFloat32StereoBytes({{0.5f, 0.5f}}));

        VaxTxMicSource src(h.engine.get());
        std::array<float, 4> dst{9.9f, 9.9f, 9.9f, 9.9f};
        QCOMPARE(src.pullSamples(dst.data(), 0), 0);
        QCOMPARE(dst[0], 9.9f);
    }

    // ── 6. null dst guard returns 0 ───────────────────────────────────────

    void nullDst_returnsZero()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeFloat32StereoBytes({{0.5f, 0.5f}}));

        VaxTxMicSource src(h.engine.get());
        QCOMPARE(src.pullSamples(nullptr, 4), 0);
    }

    // ── 7. Usable via TxMicRouter base pointer ────────────────────────────

    void viaBasePointer_dispatchWorks()
    {
        Harness h = makeHarness();
        h.bus->setPullData(makeFloat32StereoBytes({{0.4f, 0.6f}}));

        VaxTxMicSource concrete(h.engine.get());
        TxMicRouter* router = &concrete;

        std::array<float, 4> dst{};
        const int got = router->pullSamples(dst.data(), 4);
        QCOMPARE(got, 1);
        QCOMPARE(dst[0], 0.5f);  // average of 0.4 + 0.6
    }
};

QTEST_APPLESS_MAIN(TstVaxTxMicSource)
#include "tst_vax_tx_mic_source.moc"
