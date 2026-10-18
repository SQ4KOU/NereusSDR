// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#include "gui/containers/ContainerSourceAdapter.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/WaveformMeterItem.h"
#include "models/RadioModel.h"
#include <QtTest>
using namespace NereusSDR;
class TestWaveformDispatch : public QObject
{
    Q_OBJECT
  private slots:
    void lastHiddenOrGatedViewStopsAndRevealRestartsWaveformTicks()
    {
        MeterPoller poller;
        MeterWidget widget;
        auto face = std::make_unique<WaveformMeterItem>();
        WaveformMeterItem* const trace = face.get();
        widget.addItem(face.release());
        auto frame = std::make_shared<WaveformSnapshot>();
        frame->samples = {0.3f};
        frame->availability = WaveformAvailability::Available;
        poller.setWaveformSource([&](const QJsonObject&, WaveformKind) { return frame; });
        QSignalSpy ticks(&poller, &MeterPoller::waveformFrameAdvanced);
        poller.addTarget(&widget);
        widget.show();
        QVERIFY(ticks.wait(1000));
        QVERIFY(trace->snapshot());
        widget.hide();
        QVERIFY(ticks.wait(1000));
        QVERIFY(!trace->snapshot());
        QVERIFY(!ticks.wait(100));
        widget.show();
        QVERIFY(ticks.wait(1000));
        QVERIFY(trace->snapshot());
        trace->setOnlyWhenTx(true);
        widget.setDisplayGroup(1); // Existing visibility signal announces changed render eligibility.
        QVERIFY(ticks.wait(1000));
        QVERIFY(!trace->snapshot());
        QVERIFY(!ticks.wait(100));
        widget.setMox(true);
        QVERIFY(ticks.wait(1000));
        QVERIFY(trace->snapshot());
    }
    void sourceAdapterRejectsRemoteForeignAndDisconnected()
    {
        RadioModel remote(RadioModel::Role::Remote);
        auto frame = ContainerSourceAdapter::waveform(&remote, {}, nullptr, WaveformKind::RawMicrophone);
        QCOMPARE(frame->availability, WaveformAvailability::Unsupported);
        QVERIFY(frame->samples.isEmpty());
        RadioModel local;
        frame = ContainerSourceAdapter::waveform(&local, {{"sessionId", "foreign"}}, nullptr,
                                                 WaveformKind::DecodedSpeech, "current");
        QCOMPARE(frame->availability, WaveformAvailability::Unavailable);
        QVERIFY(!ContainerSourceAdapter::waveformRequest(&local, {}, nullptr, WaveformKind::RawMicrophone));
    }
    void duplicatesShareSnapshotAndPreviewAddsNoDemand()
    {
        MeterPoller poller;
        MeterWidget a, b, preview;
        auto firstOwned = std::make_unique<WaveformMeterItem>();
        auto secondOwned = std::make_unique<WaveformMeterItem>();
        auto inertOwned = std::make_unique<WaveformMeterItem>();
        WaveformMeterItem* const first = firstOwned.get();
        WaveformMeterItem* const second = secondOwned.get();
        WaveformMeterItem* const inert = inertOwned.get();
        a.addItem(firstOwned.release());
        b.addItem(secondOwned.release());
        preview.addItem(inertOwned.release());
        a.show();
        b.show();
        poller.addTarget(&a);
        poller.addTarget(&b);
        int calls = 0;
        QList<WaveformContextRequest> requests;
        auto frame = std::make_shared<WaveformSnapshot>();
        frame->sampleRate = 16000;
        frame->samples = {0.2};
        frame->segments = {{0, 1, 0, 100}};
        frame->availability = WaveformAvailability::Available;
        poller.setWaveformSource([&](const QJsonObject&, WaveformKind) {
            ++calls;
            return frame;
        });
        poller.setWaveformDemandSource([&](const QList<WaveformContextRequest>& r) { requests = r; });
        poller.setIntervalMs(1500);
        QMetaObject::invokeMethod(&poller, "pollWaveforms", Qt::DirectConnection);
        QCOMPARE(calls, 1);
        QCOMPARE(requests.size(), 1);
        QCOMPARE(first->snapshot(), second->snapshot());
        QCOMPARE(first->snapshot(), WaveformSnapshotPtr(frame));
        QCOMPARE(poller.intervalMs(), 1500);
        poller.copyCachedWaveforms(&preview, {});
        QCOMPARE(inert->snapshot(), first->snapshot());
        QCOMPARE(calls, 1);
        QCOMPARE(poller.targetCountForTest(), 2);
        a.hide();
        QMetaObject::invokeMethod(&poller, "pollWaveforms", Qt::DirectConnection);
        QCOMPARE(requests.size(), 1);
        b.hide();
        QMetaObject::invokeMethod(&poller, "pollWaveforms", Qt::DirectConnection);
        QVERIFY(requests.isEmpty());
        QVERIFY(!first->snapshot());
        QVERIFY(!second->snapshot());
    }
    void resolvedSourceChangeClearsBeforeNextProvider()
    {
        MeterPoller p;
        MeterWidget widget;
        auto owned = std::make_unique<WaveformMeterItem>();
        WaveformMeterItem* const face = owned.get();
        widget.addItem(owned.release());
        widget.show();
        p.addTarget(&widget);
        QByteArray identity = "a";
        auto s = std::make_shared<WaveformSnapshot>();
        s->samples = {0.9};
        p.setRxSourceIdentitySource([&](const QJsonObject&) { return identity; });
        p.setWaveformSource([&](const QJsonObject&, WaveformKind) {
            return identity == "a" ? WaveformSnapshotPtr(s) : WaveformSnapshotPtr();
        });
        QMetaObject::invokeMethod(&p, "pollWaveforms", Qt::DirectConnection);
        QVERIFY(face->snapshot());
        identity = "b";
        QMetaObject::invokeMethod(&p, "pollWaveforms", Qt::DirectConnection);
        QVERIFY(!face->snapshot());
    }
};
QTEST_MAIN(TestWaveformDispatch)
#include "tst_waveform_dispatch.moc"
