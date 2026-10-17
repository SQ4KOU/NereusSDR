// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#include "core/audio/WaveformHistory.h"
#include <QtTest>
using namespace NereusSDR;
class TestWaveformHistory : public QObject
{
    Q_OBJECT
  private slots:
    void packetSplitsDoNotBecomeObservationGaps()
    {
        WaveformHistory h;
        WaveformBatch first;
        first.token = 4;
        first.sampleRate = 24000;
        first.samples = {.1f, .2f};
        first.segments = {{0, 2, 40, 1000000000}};
        h.append(first);
        WaveformBatch second = first;
        second.samples = {.3f, .4f};
        second.segments = {{0, 2, 42, 1000083333}};
        h.append(second);
        const auto frame = h.snapshot(1100000000);
        QCOMPARE(frame->segments.size(), 1);
        QCOMPARE(frame->segments[0].count, 4);
        QCOMPARE(frame->samples, QVector<float>({.1f, .2f, .3f, .4f}));
    }
    void retainsExactSamplesAndImmutableSnapshots()
    {
        WaveformHistory h;
        WaveformBatch b;
        b.token = 5;
        b.sampleRate = 2;
        b.samples = {0.1f, -0.8f, 0.9f};
        b.segments = {{0, 3, 40, 1000000000LL}};
        h.append(b);
        const auto old = h.snapshot(2000000000LL);
        QCOMPARE(old->samples, QVector<float>({0.1f, -0.8f, 0.9f}));
        b.samples = {0.4f};
        b.segments = {{0, 1, 43, 2500000000LL}};
        h.append(b);
        const auto next = h.snapshot(3000000000LL);
        QCOMPARE(old->samples.size(), 3);
        QCOMPARE(next->samples, QVector<float>({0.1f, -0.8f, 0.9f, 0.4f}));
    }
    void agesSilenceAndPreservesGaps()
    {
        WaveformHistory h;
        WaveformBatch b;
        b.token = 7;
        b.sampleRate = 2;
        b.samples = {0, 0, 1, -1};
        b.segments = {{0, 2, 0, 1000000000LL}, {2, 2, 4, 3000000000LL}};
        h.append(b);
        auto s = h.snapshot(6500000000LL);
        QCOMPARE(s->samples, QVector<float>({0, 1, -1}));
        QCOMPARE(s->segments.size(), 2);
        QCOMPARE(s->segments[0].firstSequence, quint64(1));
        s = h.snapshot(9000000000LL);
        QVERIFY(s->samples.isEmpty());
    }
    void replacementCannotJoinOldHistory()
    {
        WaveformHistory h;
        WaveformBatch b;
        b.token = 7;
        b.sampleRate = 2;
        b.samples = {1};
        b.segments = {{0, 1, 0, 1000000000}};
        h.append(b);
        b.token = 8;
        b.samples = {-1};
        h.append(b);
        QCOMPARE(h.snapshot(1000000000)->samples, QVector<float>({-1}));
    }
};
QTEST_GUILESS_MAIN(TestWaveformHistory)
#include "tst_waveform_history.moc"
