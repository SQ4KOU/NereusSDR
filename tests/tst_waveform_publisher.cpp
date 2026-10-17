// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#include "core/audio/WaveformPublisher.h"
#include <QtTest>
using namespace NereusSDR;
class TestWaveformPublisher : public QObject
{
    Q_OBJECT
  private slots:
    void disabledAndStaleCopyNothing()
    {
        WaveformPublisher p(WaveformKind::RawMicrophone, 48000);
        QSignalSpy spy(&p, &WaveformPublisher::batchReady);
        const float pcm[]{0.25f, -0.75f};
        QVERIFY(!p.appendFloat(0, pcm, 2, 1, 100000000, true));
        p.setBindingToken(7);
        QVERIFY(!p.appendFloat(6, pcm, 2, 1, 100000000, true));
        QCOMPARE(spy.size(), 0);
    }
    void strideAndNormalizationAreExact()
    {
        WaveformPublisher p(WaveformKind::ConditionedMicrophone, 16000);
        WaveformBatchPtr batch;
        connect(&p, &WaveformPublisher::batchReady, this, [&](WaveformBatchPtr b) { batch = b; });
        p.setBindingToken(7);
        const int16_t pcm[]{-32768, 9, 0, 8, 32767, 7};
        QVERIFY(p.appendInt16(7, pcm, 3, 2, 100000000, true));
        QVERIFY(batch);
        QCOMPARE(batch->samples, std::vector<float>({-1.f, 0.f, 32767.f / 32768.f}));
        QCOMPARE(batch->token, quint64(7));
    }
    void stalledConsumerAndRetainedLeaseStayBounded()
    {
        WaveformPublisher p(WaveformKind::RawMicrophone, 48000);
        QVector<WaveformBatchPtr> batches;
        connect(&p, &WaveformPublisher::batchReady, this, [&](WaveformBatchPtr b) { batches.append(b); });
        p.setBindingToken(9);
        QVector<float> pcm(480, 0.25f);
        QVERIFY(p.appendFloat(9, pcm.data(), 480, 1, 100000000, true));
        QCOMPARE(batches.size(), 1);
        for (int i = 0; i < 600; ++i) {
            pcm.fill(float(i));
            p.appendFloat(9, pcm.data(), 480, 1, 200000000LL + i * 10000000LL, true);
        }
        QCOMPARE(batches.size(), 1);
        QCOMPARE(batches[0]->samples[0], 0.25f);
        p.acknowledge(batches[0]->serial);
        pcm.fill(601);
        p.appendFloat(9, pcm.data(), 480, 1, 6300000000LL, true);
        QCOMPARE(batches.size(), 2);
        QCOMPARE(batches[0]->samples[0], 0.25f);
        QVERIFY(batches[1]->samples.size() <= 4800);
        QCOMPARE(batches[1]->samples.back(), 601.f);
        QVERIFY(batches[1]->droppedFrames >= 283680);
        p.acknowledge(batches[1]->serial);
        pcm.fill(602);
        p.appendFloat(9, pcm.data(), 480, 1, 6400000000LL, true);
        QCOMPARE(batches.size(), 3);
        p.acknowledge(batches[2]->serial);
        pcm.fill(603);
        p.appendFloat(9, pcm.data(), 480, 1, 6500000000LL, true);
        QCOMPARE(batches.size(), 3);
    }
    void unknownKindIsRejected()
    {
        WaveformPublisher p(static_cast<WaveformKind>(99), 24000);
        p.setBindingToken(1);
        float x = 0.4;
        QVERIFY(!p.appendFloat(1, &x, 1, 1, 100000000, true));
    }
    void invalidRateAndHugeInputAreRejected()
    {
        const float x = 0.3;
        for (int rate : {0, -1, 2147483647, 16000}) {
            WaveformPublisher p(WaveformKind::RawMicrophone, rate);
            p.setBindingToken(1);
            QSignalSpy spy(&p, &WaveformPublisher::batchReady);
            QVERIFY(!p.appendFloat(1, &x, 1, 1, 100000000, true));
            QCOMPARE(spy.size(), 0);
        }
        WaveformPublisher p(WaveformKind::RawMicrophone, 48000);
        p.setBindingToken(1);
        QVERIFY(!p.appendFloat(1, &x, 2147483647, 1, 100000000, true));
    }
};
QTEST_GUILESS_MAIN(TestWaveformPublisher)
#include "tst_waveform_publisher.moc"
