// no-port-check: NereusSDR-original unit-test file.  No Thetis logic
// is ported here; the size constants are dictated by the AudioEngine →
// TxChannel contract (720 in / 256 out).
// =================================================================
// tests/tst_mic_re_blocker.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for MicReBlocker (Phase 3M-1c chunk L.4).
//
// The MicReBlocker bridges the cadence mismatch between
// AudioEngine::micBlockReady (720 mono frames) and
// TxChannel::driveOneTxBlock (m_inputBufferSize=256 mono frames).
//
// Tested behaviours:
//   • Single 256-sample input → emits exactly 1 block of 256.
//   • Single 720-sample input → emits 2 blocks of 256, leaves 208 buffered.
//   • Two consecutive 720-sample inputs → emits 5 blocks of 256, leaves 160.
//     (208 + 720 = 928 = 3*256 + 160.)
//   • reset() drops the partial fill; the next push starts aligned.
//   • Sample fidelity: the bytes emitted match the bytes pushed in order.
//   • Edge cases: zero-frame input is a no-op; null samples + 0 frames OK.
//   • Re-installing the sink callback resets the FIFO.
// =================================================================

#include <QtTest/QtTest>

#include "core/audio/MicReBlocker.h"

#include <vector>

using namespace NereusSDR;

class TstMicReBlocker : public QObject
{
    Q_OBJECT

private:
    /// Helper: collect every emitted block into a flat buffer + a list of
    /// per-emit frame counts.  Lets each test verify both the cadence and
    /// the sample fidelity of the rebuild.
    struct Sink {
        std::vector<float> emitted;
        std::vector<int>   frameCounts;

        MicReBlocker::SinkCallback callback() {
            return [this](const float* samples, int frames) {
                this->emitted.insert(this->emitted.end(),
                                     samples, samples + frames);
                this->frameCounts.push_back(frames);
            };
        }
    };

    /// Helper: build a ramp of `n` floats starting at `start` with `step` step.
    /// Same shape as the FakeAudioBus ramp in tst_audio_engine_mic_block_ready.
    static std::vector<float> ramp(int n, float start = 0.0f, float step = 1.0f)
    {
        std::vector<float> v(n);
        float x = start;
        for (int i = 0; i < n; ++i) {
            v[i] = x;
            x += step;
        }
        return v;
    }

private slots:
    void emit_singleAlignedBlock_emitsExactlyOnce()
    {
        Sink sink;
        MicReBlocker rb;
        rb.setSinkCallback(sink.callback());

        const auto in = ramp(256);
        rb.onMicBlock(in.data(), static_cast<int>(in.size()));

        QCOMPARE(sink.frameCounts.size(), std::size_t{1});
        QCOMPARE(sink.frameCounts[0], 256);
        QCOMPARE(sink.emitted.size(), std::size_t{256});
        QCOMPARE(rb.bufferedSamples(), std::size_t{0});
    }

    void emit_single720_emitsTwoBlocksAndLeaves208Buffered()
    {
        Sink sink;
        MicReBlocker rb;
        rb.setSinkCallback(sink.callback());

        const auto in = ramp(720);
        rb.onMicBlock(in.data(), static_cast<int>(in.size()));

        // 720 = 2 * 256 + 208.
        QCOMPARE(sink.frameCounts.size(), std::size_t{2});
        QCOMPARE(sink.frameCounts[0], 256);
        QCOMPARE(sink.frameCounts[1], 256);
        QCOMPARE(sink.emitted.size(), std::size_t{512});
        QCOMPARE(rb.bufferedSamples(), std::size_t{208});

        // First 512 emitted samples are the first 512 input samples in order.
        for (int i = 0; i < 512; ++i) {
            QCOMPARE(sink.emitted[i], static_cast<float>(i));
        }
    }

    void emit_two720s_emitsFiveBlocksAndLeaves160Buffered()
    {
        Sink sink;
        MicReBlocker rb;
        rb.setSinkCallback(sink.callback());

        const auto in1 = ramp(720, 0.0f, 1.0f);   // 0..719
        const auto in2 = ramp(720, 720.0f, 1.0f); // 720..1439
        rb.onMicBlock(in1.data(), static_cast<int>(in1.size()));
        rb.onMicBlock(in2.data(), static_cast<int>(in2.size()));

        // After call 1: 720 in, emitted 2*256=512, buffered 208.
        // After call 2: 208 + 720 = 928 = 3*256 + 160 → emit 3 more blocks.
        // Total: 5 emits, 5*256=1280 samples, 160 left buffered.
        QCOMPARE(sink.frameCounts.size(), std::size_t{5});
        for (int n : sink.frameCounts) {
            QCOMPARE(n, 256);
        }
        QCOMPARE(sink.emitted.size(), std::size_t{1280});
        QCOMPARE(rb.bufferedSamples(), std::size_t{160});

        // Every emitted sample matches the original sequential ramp in order.
        for (int i = 0; i < 1280; ++i) {
            QCOMPARE(sink.emitted[i], static_cast<float>(i));
        }
    }

    void reset_dropsPartialFill_nextPushStartsAligned()
    {
        Sink sink;
        MicReBlocker rb;
        rb.setSinkCallback(sink.callback());

        // Push 720 — leaves 208 buffered.
        const auto in1 = ramp(720, 0.0f, 1.0f);
        rb.onMicBlock(in1.data(), 720);
        QCOMPARE(rb.bufferedSamples(), std::size_t{208});

        // Reset clears the partial fill.
        rb.reset();
        QCOMPARE(rb.bufferedSamples(), std::size_t{0});

        // Subsequent aligned 256 push emits exactly one block, no leftover.
        sink.emitted.clear();
        sink.frameCounts.clear();
        const auto in2 = ramp(256, 1000.0f, 1.0f);  // distinct sequence
        rb.onMicBlock(in2.data(), 256);

        QCOMPARE(sink.frameCounts.size(), std::size_t{1});
        QCOMPARE(sink.frameCounts[0], 256);
        for (int i = 0; i < 256; ++i) {
            QCOMPARE(sink.emitted[i], 1000.0f + static_cast<float>(i));
        }
    }

    void zeroFrames_isNoOp()
    {
        Sink sink;
        MicReBlocker rb;
        rb.setSinkCallback(sink.callback());

        rb.onMicBlock(nullptr, 0);          // null + zero is OK.
        const auto in = ramp(8);
        rb.onMicBlock(in.data(), 0);        // non-null + zero is OK.

        QCOMPARE(sink.frameCounts.size(), std::size_t{0});
        QCOMPARE(rb.bufferedSamples(), std::size_t{0});
    }

    void noSink_keepsBufferingUntilAttached()
    {
        Sink sink;
        MicReBlocker rb;

        // Push two aligned blocks before any sink is attached — they
        // accumulate in the FIFO instead of being emitted.
        const auto in = ramp(256);
        rb.onMicBlock(in.data(), 256);
        rb.onMicBlock(in.data(), 256);
        // FIFO is full but no emits.
        // (Note: setSinkCallback resets the FIFO, so at *attach* time the
        // accumulated samples are dropped.  This is intentional — the
        // first attached sink starts aligned on a fresh sample stream.)
        rb.setSinkCallback(sink.callback());
        QCOMPARE(sink.frameCounts.size(), std::size_t{0});
        QCOMPARE(rb.bufferedSamples(), std::size_t{0});

        // Subsequent push works normally.
        rb.onMicBlock(in.data(), 256);
        QCOMPARE(sink.frameCounts.size(), std::size_t{1});
    }
};

QTEST_APPLESS_MAIN(TstMicReBlocker)
#include "tst_mic_re_blocker.moc"
