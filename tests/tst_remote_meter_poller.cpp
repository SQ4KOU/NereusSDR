// no-port-check: NereusSDR-original remote meter wiring regressions.
#include <QTest>
#include <QSignalSpy>

#include "core/RadioStatus.h"
#include "gui/SMeterWidget.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/MeterItem.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <memory>

using namespace NereusSDR;

class TestRemoteMeterPoller : public QObject {
    Q_OBJECT
private slots:
    void selectsActualRemoteSourceAndFollowsStableActiveSlice()
    {
        RadioModel model(RadioModel::Role::Remote);
        model.setStationConnectionState(ConnectionState::Connected);
        QVERIFY(model.addSliceWithStationId(7) >= 0);
        QVERIFY(model.addSliceWithStationId(23) >= 0);
        SliceModel* first = model.sliceById(7);
        SliceModel* second = model.sliceById(23);
        QVERIFY(first && second);
        first->setSignalPeakDbm(-62);
        first->setSignalAverageDbm(-81);
        second->setSignalPeakDbm(-42);
        second->setSignalAverageDbm(-59);
        model.setActiveSlice(0);

        SMeterWidget meter;
        MeterWidget bars;
        auto* peak = new TextItem(&bars);
        auto* average = new TextItem(&bars);
        peak->setBindingId(MeterBinding::SignalPeak);
        average->setBindingId(MeterBinding::SignalAvg);
        bars.addItem(peak);
        bars.addItem(average);
        MeterPoller poller;
        poller.setSMeter(&meter);
        poller.addTarget(&bars);
        int localCalibrationCalls = 0;
        poller.setRxOffsetSource([&]() { ++localCalibrationCalls; return 37.0; });
        poller.setRemoteRadioModel(&model, []() { return true; }, [](const SliceModel* slice) {
            return slice->sliceIndex() == 7 ? -67.0 : -47.0;
        });
        QSignalSpy flags(&poller, &MeterPoller::remoteSliceLevelUpdated);
        auto tick = [&]() {
            QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        };

        meter.setRxMode("S-Meter");
        tick();
        QCOMPARE(meter.levelDbm(), -62.0f);
        QCOMPARE(peak->value(), -62.0);
        QCOMPARE(average->value(), -81.0);
        QCOMPARE(flags.count(), 2);
        QCOMPARE(flags.at(0).at(0).toInt(), 7);
        QCOMPARE(flags.at(0).at(1).toDouble(), -62.0);
        QCOMPARE(flags.at(1).at(0).toInt(), 23);
        QCOMPARE(flags.at(1).at(1).toDouble(), -42.0);

        meter.setRxMode("Sig Avg");
        tick();
        QCOMPARE(meter.levelDbm(), -81.0f);
        model.setActiveSlice(1);
        tick();
        QCOMPARE(meter.levelDbm(), -59.0f);
        meter.setRxMode("Signal Peak");
        tick();
        QCOMPARE(meter.levelDbm(), -42.0f);
        meter.setRxMode("Max Bin");
        tick();
        QCOMPARE(meter.levelDbm(), -47.0f);
        QCOMPARE(flags.constLast().at(1).toDouble(), -47.0);
        QCOMPARE(localCalibrationCalls, 0);
    }

    void disconnectDeletionAndTxCannotPollLocalDspOrKeepLiveRxReading()
    {
        auto model = std::make_unique<RadioModel>(RadioModel::Role::Remote);
        model->setStationConnectionState(ConnectionState::Connected);
        QVERIFY(model->addSliceWithStationId(12) >= 0);
        model->setActiveSlice(0);
        SliceModel* slice = model->sliceById(12);
        slice->setSignalPeakDbm(-52);
        SMeterWidget meter;
        meter.setRxMode("S-Meter");
        MeterPoller poller;
        poller.setSMeter(&meter);
        bool snapshotReady = true;
        poller.setRemoteRadioModel(model.get(), [&]() { return snapshotReady; });
        auto tick = [&]() {
            QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        };
        tick();
        QCOMPARE(meter.levelDbm(), -52.0f);

        model->radioStatus().setTransmitting(true);
        slice->setSignalPeakDbm(-30);
        tick();
        QCOMPARE(meter.levelDbm(), -52.0f);
        model->radioStatus().setTransmitting(false);
        tick();
        QCOMPARE(meter.levelDbm(), -30.0f);

        model->setStationConnectionState(ConnectionState::Disconnected);
        snapshotReady = false;
        tick();
        QCOMPARE(meter.levelDbm(), -140.0f);
        // Capabilities can mark the radio connected before the new model
        // snapshot arrives. Retained readings must not become live again.
        model->setStationConnectionState(ConnectionState::Connected);
        tick();
        QCOMPARE(meter.levelDbm(), -140.0f);
        slice->setSignalPeakDbm(-74);
        snapshotReady = true;
        tick();
        QCOMPARE(meter.levelDbm(), -74.0f);
        model.reset();
        tick();
        QCOMPARE(meter.levelDbm(), -140.0f);
    }
};
QTEST_MAIN(TestRemoteMeterPoller)
#include "tst_remote_meter_poller.moc"
