// =================================================================
// tests/tst_p1_mic_extraction.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test file.  Verifies the mic16 byte zone
// extraction inside the static P1RadioConnection::parseEp6Frame
// 4-arg overload (Phase 3M-1c TX pump v3).
//
// Test cases (3):
//   - normalMicSamples_extractedInOrder
//   - hl2StyleZeros_yieldZeroSamples
//   - extractionDoesNotMutateRxSamples — backward-compat with the
//     3-arg overload that ignores micOut
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — New test for Phase 3M-1c TX pump v3 by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file.  No Thetis logic ported.

#include <QtTest/QtTest>
#include <QObject>

#include <array>
#include <cstring>
#include <vector>

#include "core/P1RadioConnection.h"

using namespace NereusSDR;

namespace {

// Build a synthetic 1032-byte EP6 frame for `numRx` receivers with
// 16-bit big-endian mic samples specified in micPattern.  Returns the
// frame as a std::array<quint8, 1032>.
//
// Frame layout (Thetis networkproto1.c:319-415 [v2.10.3.13]):
//   [0..7]   metis 8-byte header (EF FE 01 06 + 4 seq bytes)
//   [8..10]  sync (7F 7F 7F)
//   [11..15] C&C C0..C4
//   [16..519] 504 bytes = samplesPerSubframe * (6*nddc + 2)
//   [520..522] sync
//   [523..527] C&C
//   [528..1031] 504 bytes payload
std::array<quint8, 1032> buildEp6Frame(int numRx, const std::vector<int16_t>& micPattern)
{
    std::array<quint8, 1032> frame{};
    // metis header
    frame[0] = 0xEF; frame[1] = 0xFE; frame[2] = 0x01; frame[3] = 0x06;
    frame[4] = 0; frame[5] = 0; frame[6] = 0; frame[7] = 1;  // seq
    // sync subframe 0
    frame[8] = 0x7F; frame[9] = 0x7F; frame[10] = 0x7F;
    // C&C subframe 0 (zeros)
    // sync subframe 1
    frame[520] = 0x7F; frame[521] = 0x7F; frame[522] = 0x7F;

    const int slotBytes = 6 * numRx + 2;
    const int samplesPerSubframe = 504 / slotBytes;
    const int totalMicSamples = samplesPerSubframe * 2;

    // micPattern.size() may be < totalMicSamples; pad with the last value.
    auto micValueAt = [&](int idx) -> int16_t {
        if (micPattern.empty()) {
            return 0;
        }
        if (idx >= static_cast<int>(micPattern.size())) {
            return micPattern.back();
        }
        return micPattern[static_cast<size_t>(idx)];
    };

    int micCounter = 0;
    auto fillSubframe = [&](int sampleStart) {
        for (int s = 0; s < samplesPerSubframe; ++s) {
            // Leave RX I/Q zeros for simplicity.
            const int micOff = sampleStart + s * slotBytes + numRx * 6;
            const int16_t v = micValueAt(micCounter++);
            frame[static_cast<size_t>(micOff + 0)] =
                static_cast<quint8>((static_cast<uint16_t>(v) >> 8) & 0xFF);
            frame[static_cast<size_t>(micOff + 1)] =
                static_cast<quint8>(static_cast<uint16_t>(v) & 0xFF);
        }
    };
    fillSubframe(16);
    fillSubframe(528);

    Q_UNUSED(totalMicSamples);  // captured for clarity above
    return frame;
}

} // namespace

class TestP1MicExtraction : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Normal mic samples — extracted in network order ─────────────────
    void normalMicSamples_extractedInOrder()
    {
        const int numRx = 1;  // slotBytes=8, samplesPerSubframe=63
        // 126 samples total (2 subframes × 63) — fill with 1, 2, 3, ...
        std::vector<int16_t> pattern;
        pattern.reserve(126);
        for (int i = 0; i < 126; ++i) {
            pattern.push_back(static_cast<int16_t>((i + 1) * 100));  // 100..12600
        }
        auto frame = buildEp6Frame(numRx, pattern);

        std::vector<std::vector<float>> perRx;
        std::vector<float> mic;
        bool ok = P1RadioConnection::parseEp6Frame(frame.data(), numRx, perRx, &mic);
        QVERIFY(ok);
        QCOMPARE(static_cast<int>(mic.size()), 126);

        for (int i = 0; i < 126; ++i) {
            const float expected = static_cast<float>(pattern[static_cast<size_t>(i)]) / 32768.0f;
            QCOMPARE(mic[static_cast<size_t>(i)], expected);
        }
    }

    // ── 2. HL2-style mic16 zeros yield zero-valued samples ─────────────────
    //
    // HL2 has no mic jack hardware so the mic16 byte zone arrives as zeros.
    // The parser should still happily extract them as 0.0f samples — the
    // cadence is preserved even without real audio.
    void hl2StyleZeros_yieldZeroSamples()
    {
        const int numRx = 1;
        std::vector<int16_t> pattern;  // empty → all zero (padded)
        auto frame = buildEp6Frame(numRx, pattern);

        std::vector<std::vector<float>> perRx;
        std::vector<float> mic;
        bool ok = P1RadioConnection::parseEp6Frame(frame.data(), numRx, perRx, &mic);
        QVERIFY(ok);
        QCOMPARE(static_cast<int>(mic.size()), 126);
        for (float s : mic) {
            QCOMPARE(s, 0.0f);
        }
    }

    // ── 3. 3-arg overload (no mic) still works; mic-aware overload doesn't
    //       mutate RX samples ───────────────────────────────────────────────
    void backwardCompatible_threeArgOverloadStillWorks()
    {
        const int numRx = 2;
        std::vector<int16_t> pattern{ 100, 200, 300 };
        auto frame = buildEp6Frame(numRx, pattern);

        std::vector<std::vector<float>> perRx1;
        std::vector<std::vector<float>> perRx2;
        std::vector<float> mic;

        // Old 3-arg call (no mic out)
        bool ok1 = P1RadioConnection::parseEp6Frame(frame.data(), numRx, perRx1);
        // New 4-arg call (with mic out)
        bool ok2 = P1RadioConnection::parseEp6Frame(frame.data(), numRx, perRx2, &mic);

        QVERIFY(ok1);
        QVERIFY(ok2);
        QCOMPARE(static_cast<int>(perRx1.size()), numRx);
        QCOMPARE(static_cast<int>(perRx2.size()), numRx);
        for (int r = 0; r < numRx; ++r) {
            QCOMPARE(perRx1[static_cast<size_t>(r)].size(),
                     perRx2[static_cast<size_t>(r)].size());
            for (size_t i = 0; i < perRx1[static_cast<size_t>(r)].size(); ++i) {
                QCOMPARE(perRx1[static_cast<size_t>(r)][i],
                         perRx2[static_cast<size_t>(r)][i]);
            }
        }
        QVERIFY(!mic.empty());
    }
};

QTEST_MAIN(TestP1MicExtraction)
#include "tst_p1_mic_extraction.moc"
