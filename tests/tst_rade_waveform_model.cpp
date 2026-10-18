// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#include "models/RadeWaveformModel.h"
#include <QtTest>
using namespace NereusSDR;
class TestRadeWaveformModel : public QObject
{
    Q_OBJECT
  private slots:
    void decoderReceiptRefreshesBeforeAcceptingAQueuedOldReceiver()
    {
        RadeWaveformModel model;
        WaveformPublisher publisher(WaveformKind::DecodedSpeech, 24000);
        model.setEndpoint(1, WaveformKind::DecodedSpeech, &publisher, 7, true);
        model.setDemand(1, {{1, WaveformKind::DecodedSpeech}});
        bool migrated = true;
        connect(&model, &RadeWaveformModel::localObservationRefreshRequested, &model, [&] {
            if (migrated) {
                migrated = false;
                model.setEndpoint(1, WaveformKind::DecodedSpeech, &publisher, 8, true);
            }
        });
        const float oldPcm = 0.6f;
        publisher.appendFloat(publisher.bindingToken(), &oldPcm, 1, 1, 1000000000, true);
        QCoreApplication::processEvents();
        QVERIFY(model.snapshot(1, WaveformKind::DecodedSpeech, 1000000000)->samples.isEmpty());
        const float newPcm = -0.2f;
        publisher.appendFloat(publisher.bindingToken(), &newPcm, 1, 1, 1100000000, true);
        QCoreApplication::processEvents();
        QCOMPARE(model.snapshot(1, WaveformKind::DecodedSpeech, 1100000000)->samples,
                 QVector<float>({newPcm}));
    }
    void microphoneReceiptRefreshesBeforeAcceptingAQueuedOldSource()
    {
        RadeWaveformModel model;
        WaveformPublisher publisher(WaveformKind::RawMicrophone, 48000);
        model.setEndpoint(1, WaveformKind::RawMicrophone, &publisher, 7, true);
        model.setDemand(1, {{1, WaveformKind::RawMicrophone}});
        bool changed = true;
        connect(&model, &RadeWaveformModel::localObservationRefreshRequested, &model, [&] {
            if (changed) {
                changed = false;
                model.setEndpoint(1, WaveformKind::RawMicrophone, &publisher, 8, true);
            }
        });
        const float oldPcm = 0.4f;
        publisher.appendFloat(publisher.bindingToken(), &oldPcm, 1, 1, 1000000000, true);
        QCoreApplication::processEvents();
        QVERIFY(model.snapshot(1, WaveformKind::RawMicrophone, 1000000000)->samples.isEmpty());
        const float newPcm = -0.3f;
        publisher.appendFloat(publisher.bindingToken(), &newPcm, 1, 1, 1100000000, true);
        QCoreApplication::processEvents();
        QCOMPARE(model.snapshot(1, WaveformKind::RawMicrophone, 1100000000)->samples,
                 QVector<float>({newPcm}));
    }
    void microphoneRefreshCanRetireTheReceiverAndStillReleaseTheEventGate()
    {
        auto model = std::make_unique<RadeWaveformModel>();
        WaveformPublisher publisher(WaveformKind::RawMicrophone, 48000);
        model->setEndpoint(1, WaveformKind::RawMicrophone, &publisher, 7, true);
        model->setDemand(1, {{1, WaveformKind::RawMicrophone}});
        connect(model.get(), &RadeWaveformModel::localObservationRefreshRequested, this,
                [&] { model.reset(); });
        const float pcm = 0.4f;
        publisher.appendFloat(publisher.bindingToken(), &pcm, 1, 1, 1000000000, true);
        QCoreApplication::processEvents();
        QVERIFY(!model);
        publisher.setBindingToken(9);
        QSignalSpy batches(&publisher, &WaveformPublisher::batchReady);
        publisher.appendFloat(9, &pcm, 1, 1, 1100000000, true);
        QCOMPARE(batches.size(), 1);
    }
    void localDemandSchedulesHeadlessSourceRefresh()
    {
        RadeWaveformModel model;
        WaveformPublisher publisher(WaveformKind::RawMicrophone, 48000);
        model.setEndpoint(1, WaveformKind::RawMicrophone, &publisher, 7, true);
        QSignalSpy refresh(&model, &RadeWaveformModel::localObservationRefreshRequested);
        model.setDemand(1, {{1, WaveformKind::RawMicrophone}});
        QVERIFY(refresh.wait(1000));
        model.clearDemand(1);
        QVERIFY(!refresh.wait(50));
    }
    void refreshOnlyDemandNeverCapturesUnavailableEndpoints()
    {
        RadeWaveformModel model;
        WaveformPublisher publisher(WaveformKind::ConditionedMicrophone, 16000);
        model.setEndpoint(1, WaveformKind::ConditionedMicrophone, &publisher, 7, false,
                          QStringLiteral("Input has not started"));
        QSignalSpy refresh(&model, &RadeWaveformModel::localObservationRefreshRequested);
        model.setRefreshDemand(42, true);
        QVERIFY(refresh.wait(1000));
        QCOMPARE(publisher.bindingToken(), quint64(0));
        model.setEndpoint(1, WaveformKind::ConditionedMicrophone, &publisher, 7, true);
        QVERIFY(refresh.wait(1000));
        QCOMPARE(publisher.bindingToken(), quint64(0));
        model.clearRefreshDemand(42);
        QVERIFY(!refresh.wait(50));
    }
    void retiringDuringEmissionStillAcknowledgesThePhysicalPublisher()
    {
        RadeWaveformModel m;
        WaveformPublisher p(WaveformKind::RawMicrophone, 48000);
        bool replace = true;
        connect(&p, &WaveformPublisher::batchReady, this, [&](WaveformBatchPtr) {
            if (replace) {
                replace = false;
                m.retireEndpoint(2, WaveformKind::RawMicrophone);
                m.setEndpoint(2, WaveformKind::RawMicrophone, &p, 41, true);
            }
        });
        m.setEndpoint(2, WaveformKind::RawMicrophone, &p, 40, true);
        m.setDemand(1, {{2, WaveformKind::RawMicrophone}});
        float x = 0.1;
        p.appendFloat(p.bindingToken(), &x, 1, 1, 1000000000, true);
        QCoreApplication::processEvents();
        x = 0.8;
        p.appendFloat(p.bindingToken(), &x, 1, 1, 1100000000, true);
        QCoreApplication::processEvents();
        QCOMPARE(m.snapshot(2, WaveformKind::RawMicrophone, 1100000000)->samples, QVector<float>({0.8f}));
    }
    void physicalKindCannotBeMislabeled()
    {
        RadeWaveformModel m;
        WaveformPublisher wrong(WaveformKind::DecodedSpeech, 24000);
        m.setEndpoint(2, WaveformKind::RawMicrophone, &wrong, 40, true);
        m.setDemand(1, {{2, WaveformKind::RawMicrophone}});
        float x = 0.8;
        wrong.appendFloat(wrong.bindingToken(), &x, 1, 1, 1000000000, true);
        QCoreApplication::processEvents();
        QVERIFY(m.snapshot(2, WaveformKind::RawMicrophone, 1000000000)->samples.isEmpty());
    }
    void duplicateViewsShareCaptureAndLastHideDisables()
    {
        RadeWaveformModel m;
        WaveformPublisher p(WaveformKind::RawMicrophone, 48000);
        m.setEndpoint(2, WaveformKind::RawMicrophone, &p, 40, true);
        QCOMPARE(p.bindingToken(), quint64(0));
        m.setDemand(1, {{2, WaveformKind::RawMicrophone}});
        const auto token = p.bindingToken();
        QVERIFY(token != 0);
        m.setDemand(2, {{2, WaveformKind::RawMicrophone}});
        QCOMPARE(p.bindingToken(), token);
        const float x = 0.5;
        p.appendFloat(token, &x, 1, 1, 1000000000, true);
        QTRY_COMPARE(m.snapshot(2, WaveformKind::RawMicrophone, 1000000000)->samples.size(), 1);
        const auto a = m.snapshot(2, WaveformKind::RawMicrophone, 1000000000);
        const auto b = m.snapshot(2, WaveformKind::RawMicrophone, 1000000000);
        QCOMPARE(a, b);
        m.clearDemand(1);
        QCOMPARE(p.bindingToken(), token);
        m.clearDemand(2);
        QCOMPARE(p.bindingToken(), quint64(0));
        QVERIFY(m.snapshot(2, WaveformKind::RawMicrophone, 1000000000)->samples.isEmpty());
    }
    void queuedOldGenerationCannotEnterReplacement()
    {
        RadeWaveformModel m;
        WaveformPublisher p(WaveformKind::DecodedSpeech, 24000),
            replacement(WaveformKind::DecodedSpeech, 24000);
        m.setEndpoint(1, WaveformKind::DecodedSpeech, &p, 30, true);
        m.setDemand(1, {{1, WaveformKind::DecodedSpeech}});
        float x = 0.7;
        p.appendFloat(p.bindingToken(), &x, 1, 1, 1000000000, true);
        m.setEndpoint(1, WaveformKind::DecodedSpeech, &replacement, 31, true);
        QCoreApplication::processEvents();
        QVERIFY(m.snapshot(1, WaveformKind::DecodedSpeech, 1000000000)->samples.isEmpty());
        QCOMPARE(p.bindingToken(), quint64(0));
        x = -0.8;
        replacement.appendFloat(replacement.bindingToken(), &x, 1, 1, 1000000000, true);
        QTRY_COMPARE(m.snapshot(1, WaveformKind::DecodedSpeech, 1000000000)->samples,
                     QVector<float>({-0.8f}));
        m.retireEndpoint(1, WaveformKind::DecodedSpeech);
        QCOMPARE(replacement.bindingToken(), quint64(0));
        QVERIFY(m.snapshot(1, WaveformKind::DecodedSpeech, 1000000000)->samples.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestRadeWaveformModel)
#include "tst_rade_waveform_model.moc"
