// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
// no-port-check: Original remote clock/source/demand model behavior.
#include "models/RadeWaveformModel.h"
#include <QtTest>
using namespace NereusSDR;
class TestWaveformRemoteModel : public QObject
{
    Q_OBJECT
  private slots:
    void physicalAvailabilityAndAcceptedFanout()
    {
        RadeWaveformModel m;
        WaveformPublisher p(WaveformKind::RawMicrophone, 48000);
        QSignalSpy states(&m, &RadeWaveformModel::endpointStateChanged),
            batches(&m, &RadeWaveformModel::acceptedBatch);
        m.setEndpoint(2, WaveformKind::RawMicrophone, &p, 40, true);
        const auto state = m.endpointState(2, WaveformKind::RawMicrophone);
        QVERIFY(state.available);
        QCOMPARE(state.sourceGeneration, quint64(40));
        QCOMPARE(state.sampleRateHz, 48000);
        QCOMPARE(states.size(), 1);
        m.setEndpoint(2, WaveformKind::RawMicrophone, &p, 40, true);
        QCOMPARE(states.size(), 1);
        m.setDemand(1, {{2, WaveformKind::RawMicrophone}});
        float x = .5;
        p.appendFloat(p.bindingToken(), &x, 1, 1, 1000000000, true);
        QCoreApplication::processEvents();
        QCOMPARE(batches.size(), 1);
        QCOMPARE(batches[0][0].toInt(), 2);
        QCOMPARE(batches[0][2].toULongLong(), quint64(40));
        m.retireEndpoint(2, WaveformKind::RawMicrophone);
        QCOMPARE(states.size(), 2);
        QCOMPARE(states.last()[2].toULongLong(), quint64(0));
        QVERIFY(!m.endpointState(2, WaveformKind::RawMicrophone).available);
    }
    void remoteDemandUnionAndClockMapping()
    {
        RadeWaveformModel m;
        m.setRemoteSupported(true);
        QSignalSpy demand(&m, &RadeWaveformModel::remoteDemandChanged);
        m.setDemand(1, {{2, WaveformKind::DecodedSpeech}, {2, WaveformKind::DecodedSpeech}});
        QCOMPARE(demand.size(), 1);
        m.setDemand(2, {{2, WaveformKind::DecodedSpeech}});
        QCOMPARE(demand.size(), 1);
        m.applyRemoteContext(2, WaveformKind::DecodedSpeech, 77, 9, 24000, WaveformAvailability::Available,
                             {});
        auto b = std::make_shared<WaveformBatch>();
        b->kind = WaveformKind::DecodedSpeech;
        b->token = 9;
        b->sampleRate = 24000;
        b->samples = {.2f, -.3f};
        b->segments = {{0, 2, 99, 11000000000LL}};
        m.appendRemoteBatch(2, 77, b);
        auto s = m.snapshot(2, WaveformKind::DecodedSpeech, 1000000000);
        QCOMPARE(s->availability, WaveformAvailability::Unavailable);
        QVERIFY(s->samples.isEmpty());
        m.setRemoteClockMap(10000000000LL, true);
        m.appendRemoteBatch(2, 77, b);
        s = m.snapshot(2, WaveformKind::DecodedSpeech, 1000000000);
        QCOMPARE(s->samples, QVector<float>({.2f, -.3f}));
        QCOMPARE(s->segments[0].startNs, qint64(1000000000));
        QCOMPARE(s->asOfNs, qint64(1000000000));
        const WaveformSnapshotPtr previous = s;
        m.setRemoteClockMap(10000000000LL, false);
        QCOMPARE(m.snapshot(2, WaveformKind::DecodedSpeech, 1000000000)->availability,
                 WaveformAvailability::Unavailable);
        m.setRemoteClockMap(9500000000LL, true);
        s = m.snapshot(2, WaveformKind::DecodedSpeech, 1500000000);
        QCOMPARE(s->samples, QVector<float>({.2f, -.3f}));
        QCOMPARE(s->segments[0].startNs, qint64(1500000000));
        QCOMPARE(s->segments[0].firstSequence, quint64(99));
        QCOMPARE(previous->segments[0].startNs, qint64(1000000000));
        QCOMPARE(previous->asOfNs, qint64(1000000000));
        m.clearDemand(1);
        QCOMPARE(demand.size(), 1);
        m.clearDemand(2);
        QCOMPARE(demand.size(), 2);
        QVERIFY(m.snapshot(2, WaveformKind::DecodedSpeech, 1500000000)->samples.isEmpty());
    }
    void staleDeliveryAndSourceCannotReplay()
    {
        RadeWaveformModel m;
        m.setRemoteSupported(true);
        m.setRemoteClockMap(0, true);
        m.setDemand(1, {{1, WaveformKind::RawMicrophone}});
        m.applyRemoteContext(1, WaveformKind::RawMicrophone, 70, 8, 48000, WaveformAvailability::Available,
                             {});
        auto b = std::make_shared<WaveformBatch>();
        b->kind = WaveformKind::RawMicrophone;
        b->sampleRate = 48000;
        b->token = 8;
        b->samples = {.7};
        b->segments = {{0, 1, 0, 1000000000}};
        m.appendRemoteBatch(1, 70, b);
        QCOMPARE(m.snapshot(1, WaveformKind::RawMicrophone, 1000000000)->samples.size(), 1);
        m.applyRemoteContext(1, WaveformKind::RawMicrophone, 71, 9, 48000, WaveformAvailability::Available,
                             {});
        m.appendRemoteBatch(1, 70, b);
        m.appendRemoteBatch(1, 71, b);
        QVERIFY(m.snapshot(1, WaveformKind::RawMicrophone, 1000000000)->samples.isEmpty());
        b->token = 9;
        m.appendRemoteBatch(1, 71, b);
        QCOMPARE(m.snapshot(1, WaveformKind::RawMicrophone, 1000000000)->samples.size(), 1);
        m.setRemoteSupported(false, "Older Core");
        auto s = m.snapshot(1, WaveformKind::RawMicrophone, 1000000000);
        QCOMPARE(s->availability, WaveformAvailability::Unsupported);
        QVERIFY(s->samples.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestWaveformRemoteModel)
#include "tst_waveform_remote_model.moc"
