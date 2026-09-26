// no-port-check: NereusSDR-original remote meter wiring regressions.
// Modification history (NereusSDR):
//   2026-09-25: iPhone app plan Task 39 (D14, R-IOS-13): a remote window's
//               transmit meters from the Core's `txState`, and the ones it
//               does not send shown disabled with the reason. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
#include <QTest>
#include <QSignalSpy>

#include "core/RadioStatus.h"
#include "core/RxChannel.h"
#include "gui/SMeterWidget.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/MeterItem.h"
#include "core/StepAttenuatorController.h"
#include "core/session/TransmitStateFacade.h"
#include "gui/HGauge.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <memory>

using namespace NereusSDR;

class TestRemoteMeterPoller : public QObject {
    Q_OBJECT
private slots:
    // R-R3-46 fix wave: the Core adds its own calibration (its attenuator,
    // preamp and meter offset) to the S-meter readings and the spectrum
    // frames it sends. A remote window's model adds none on top, so the
    // spectrum's calibration offset and Max Bin show the Core's values;
    // a local model keeps the Thetis chain.
    void remoteModelAddsNoSecondCalibration()
    {
        StepAttenuatorController controller;
        controller.setTickTimerEnabled(false);
        controller.setStepAttEnabled(true);
        controller.setAttenuation(20);

        RadioModel remote(RadioModel::Role::Remote);
        QSignalSpy changed(&remote, &RadioModel::rxMeterOffsetChanged);
        remote.setStepAttController(&controller);
        QCOMPARE(remote.rxMeterOffsetDb(), 0.0);
        controller.setAttenuation(25);
        QCOMPARE(remote.rxMeterOffsetDb(), 0.0);
        // Whatever it tells the spectrum is 0.
        for (const QList<QVariant>& emitted : std::as_const(changed)) {
            QCOMPARE(emitted.at(0).toDouble(), 0.0);
        }
        remote.setStepAttController(nullptr);

        RadioModel local;
        local.setStepAttController(&controller);
        QVERIFY(local.rxMeterOffsetDb() != 0.0);
        local.setStepAttController(nullptr);
    }

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
        MeterWidget bars;
        auto* peakText = new TextItem(&bars);
        auto* averageText = new TextItem(&bars);
        peakText->setBindingId(MeterBinding::SignalPeak);
        averageText->setBindingId(MeterBinding::SignalAvg);
        bars.addItem(peakText);
        bars.addItem(averageText);
        slice->setSignalAverageDbm(-66);
        MeterPoller poller;
        poller.setSMeter(&meter);
        poller.addTarget(&bars);
        bool snapshotReady = true;
        poller.setRemoteRadioModel(model.get(), [&]() { return snapshotReady; });
        auto tick = [&]() {
            QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        };
        tick();
        QCOMPARE(meter.levelDbm(), -52.0f);
        QCOMPARE(peakText->displayText(), QStringLiteral("-52.0 dBm"));
        QCOMPARE(averageText->displayText(), QStringLiteral("-66.0 dBm"));

        model->radioStatus().setTransmitting(true);
        slice->setSignalPeakDbm(-30);
        tick();
        QCOMPARE(meter.levelDbm(), -52.0f);
        model->radioStatus().setTransmitting(false);
        tick();
        QCOMPARE(meter.levelDbm(), -30.0f);

        // R-R3-13: with no reading the meter gets the -400 dBm no-reading
        // sentinel in every RX mode and shows "--", not "-140 dBm" / "S0".
        QSignalSpy flags(&poller, &MeterPoller::remoteSliceLevelUpdated);
        model->setStationConnectionState(ConnectionState::Disconnected);
        snapshotReady = false;
        for (const char* mode : {"S-Meter", "Sig Avg", "Signal Peak", "Max Bin"}) {
            meter.setRxMode(QString::fromLatin1(mode));
            tick();
            QCOMPARE(meter.levelDbm(), -400.0f);
            QCOMPARE(meter.sUnitsText(), QStringLiteral("--"));
            QVERIFY(!flags.isEmpty());
            QCOMPARE(flags.constLast().at(1).toDouble(), -400.0);
            // The container meter items show no reading too, not -140.
            QCOMPARE(peakText->value(), -400.0);
            QCOMPARE(averageText->value(), -400.0);
            QCOMPARE(peakText->displayText(), QStringLiteral("-- dBm"));
            QCOMPARE(averageText->displayText(), QStringLiteral("-- dBm"));
        }
        meter.setRxMode("S-Meter");
        // Capabilities can mark the radio connected before the new model
        // snapshot arrives. Retained readings must not become live again.
        model->setStationConnectionState(ConnectionState::Connected);
        tick();
        QCOMPARE(meter.levelDbm(), -400.0f);
        QCOMPARE(flags.constLast().at(1).toDouble(), -400.0);
        QCOMPARE(peakText->displayText(), QStringLiteral("-- dBm"));
        slice->setSignalPeakDbm(-74);
        snapshotReady = true;
        tick();
        QCOMPARE(meter.levelDbm(), -74.0f);
        QCOMPARE(flags.constLast().at(1).toDouble(), -74.0);
        QCOMPARE(peakText->displayText(), QStringLiteral("-74.0 dBm"));
        QCOMPARE(averageText->displayText(), QStringLiteral("-66.0 dBm"));
        model.reset();
        tick();
        QCOMPARE(meter.levelDbm(), -400.0f);
        QCOMPARE(peakText->displayText(), QStringLiteral("-- dBm"));
        QCOMPARE(averageText->displayText(), QStringLiteral("-- dBm"));
    }

    void localPollWithoutRxChannelShowsNoReading()
    {
        // R-R3-13: a local window with no RX channel (none yet, or the
        // QPointer cleared when the channel was destroyed) feeds the
        // no-reading sentinel instead of leaving the last value frozen.
        MeterWidget bars;
        auto* peakText = new TextItem(&bars);
        auto* agcText = new TextItem(&bars);
        auto* bar = new BarItem(&bars);
        peakText->setBindingId(MeterBinding::SignalPeak);
        agcText->setBindingId(MeterBinding::AgcAvg);
        bar->setBindingId(MeterBinding::SignalAvg);
        bar->setShowValue(true);
        bars.addItem(peakText);
        bars.addItem(agcText);
        bars.addItem(bar);
        bars.updateMeterValue(MeterBinding::SignalPeak, -71.0);
        bars.updateMeterValue(MeterBinding::AgcAvg, -90.0);
        bars.updateMeterValue(MeterBinding::SignalAvg, -75.0);
        QCOMPARE(peakText->displayText(), QStringLiteral("-71.0 dBm"));

        SMeterWidget meter;
        meter.setLevel(-71.0f);
        QCOMPARE(meter.levelDbm(), -71.0f);

        MeterPoller poller;
        poller.addTarget(&bars);
        poller.setSMeter(&meter);
        QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        QCOMPARE(peakText->displayText(), QStringLiteral("-- dBm"));
        QCOMPARE(agcText->displayText(), QStringLiteral("-- dBm"));
        QCOMPARE(bar->valueText(), QStringLiteral("--"));
        // Fix wave, Important 2: the analog S-meter header must not freeze
        // on the last reading when the channel is gone either.
        QCOMPARE(meter.levelDbm(), -400.0f);
    }

    void localLinkLostWithLiveChannelShowsNoReading()
    {
        // Fix wave, Important 2: on a local LinkLost the RX channels stay
        // alive but WDSP meters stop updating, and an inactive channel reads
        // -140 dBm, which would show as a number. While the radio link is
        // not up, the poller shows no reading; once it is up again it reads
        // the same channel.
        MeterWidget bars;
        auto* peakText = new TextItem(&bars);
        peakText->setBindingId(MeterBinding::SignalPeak);
        bars.addItem(peakText);
        SMeterWidget meter;
        RxChannel channel(0, 1024, 48000);

        MeterPoller poller;
        poller.addTarget(&bars);
        poller.setSMeter(&meter);
        poller.setRxChannel(&channel);
        auto tick = [&]() {
            QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        };
        tick();
        QCOMPARE(peakText->displayText(), QStringLiteral("-140.0 dBm"));
        QVERIFY(meter.levelDbm() > -400.0f);

        poller.setLocalRxReadingAvailable(false);
        QVERIFY(!poller.localRxReadingAvailable());
        tick();
        QCOMPARE(peakText->displayText(), QStringLiteral("-- dBm"));
        QCOMPARE(meter.levelDbm(), -400.0f);

        poller.setLocalRxReadingAvailable(true);
        tick();
        QCOMPARE(peakText->displayText(), QStringLiteral("-140.0 dBm"));
        QVERIFY(meter.levelDbm() > -400.0f);
    }

    // iPhone app plan Task 39: the remote window shows forward and reflected
    // power, SWR, ALC and MIC from the Core's `txState` by the same meter
    // items the local window uses, and the meters the Core does not send
    // are disabled with the reason, never hidden.
    void remoteTxMetersComeFromTheCoresTransmitState()
    {
        RadioModel model(RadioModel::Role::Remote);
        model.setStationConnectionState(ConnectionState::Connected);
        QVERIFY(model.addSliceWithStationId(0) >= 0);
        model.setActiveSlice(0);
        model.sliceById(0)->setSignalPeakDbm(-70);

        MeterWidget bars;
        bars.resize(200, 200);
        QHash<int, TextItem*> items;
        const QList<int> bindings{MeterBinding::TxPower, MeterBinding::TxReversePower,
                                  MeterBinding::TxSwr,   MeterBinding::TxAlc,
                                  MeterBinding::TxMic,   MeterBinding::TxComp,
                                  MeterBinding::TxEq,    MeterBinding::SignalPeak};
        for (int i = 0; i < bindings.size(); ++i) {
            auto* item = new TextItem(&bars);
            item->setBindingId(bindings.at(i));
            item->setRect(0.0f, 0.1f * i, 1.0f, 0.1f);
            bars.addItem(item);
            items.insert(bindings.at(i), item);
        }
        MeterPoller poller;
        poller.addTarget(&bars);
        poller.setRadioStatus(&model.radioStatus());
        poller.setRemoteRadioModel(&model, []() { return true; });
        TransmitState state;
        QString unavailable;
        poller.setRemoteTransmitState(&state, [&unavailable]() { return unavailable; });
        auto tick = [&]() {
            QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        };

        // The meters txState carries are available; the others are shown
        // disabled with the reason.
        for (int binding : {MeterBinding::TxPower, MeterBinding::TxReversePower,
                            MeterBinding::TxSwr, MeterBinding::TxAlc, MeterBinding::TxMic}) {
            QVERIFY2(bars.bindingUnavailableReason(binding).isEmpty(), qPrintable(QString::number(binding)));
        }
        for (int binding : MeterPoller::remoteTxBindingsNotSent()) {
            QCOMPARE(bars.bindingUnavailableReason(binding), MeterPoller::remoteTxMeterNotSentText());
        }
        QCOMPARE(bars.items().size(), bindings.size());   // none hidden
        QCOMPARE(bars.unavailableReasonAt(QPointF(10, 0.1 * 200 * 5 + 5)),
                 MeterPoller::remoteTxMeterNotSentText());  // the TxComp row
        QVERIFY(bars.unavailableReasonAt(QPointF(10, 5)).isEmpty());  // TxPower

        // The Core keys and reads its meters.
        QVERIFY(state.applyStationValue("keyed", true));
        QVERIFY(state.applyStationValue("forwardPowerWatts", 50.0));
        QVERIFY(state.applyStationValue("reflectedPowerWatts", 2.0));
        QVERIFY(state.applyStationValue("alcDb", -3.0));
        QVERIFY(state.applyStationValue("micLevelDb", -12.0));
        tick();
        QCOMPARE(items.value(MeterBinding::TxPower)->value(), 50.0);
        QCOMPARE(items.value(MeterBinding::TxReversePower)->value(), 2.0);
        // SWR from 50 W forward and 2 W reflected: rho 0.2, (1.2 / 0.8).
        QVERIFY(qAbs(items.value(MeterBinding::TxSwr)->value() - 1.5) < 1e-9);
        QCOMPARE(items.value(MeterBinding::TxAlc)->value(), -3.0);
        QCOMPARE(items.value(MeterBinding::TxMic)->value(), -12.0);
        QCOMPARE(model.radioStatus().forwardPowerWatts(), 50.0);

        // Unkeyed: the power falls to 0 with the Core's reading; the receive
        // meters come back.
        QVERIFY(state.applyStationValue("keyed", false));
        QVERIFY(state.applyStationValue("forwardPowerWatts", 0.0));
        QVERIFY(state.applyStationValue("reflectedPowerWatts", 0.0));
        tick();
        QCOMPARE(items.value(MeterBinding::TxPower)->value(), 0.0);
        QCOMPARE(items.value(MeterBinding::TxSwr)->value(), 1.0);
        QCOMPARE(items.value(MeterBinding::SignalPeak)->value(), -70.0);

        // A Core that does not send transmit meters: every transmit meter is
        // disabled with that reason, and nothing is fed.
        unavailable = QStringLiteral("This Core does not send transmit meters.");
        tick();
        QCOMPARE(bars.bindingUnavailableReason(MeterBinding::TxPower), unavailable);
        QCOMPARE(bars.bindingUnavailableReason(MeterBinding::TxComp), unavailable);
        unavailable.clear();
        tick();
        QVERIFY(bars.bindingUnavailableReason(MeterBinding::TxPower).isEmpty());
    }

    void aGaugeCanBeShownUnavailable()
    {
        HGauge gauge;
        QVERIFY(!gauge.isUnavailable());
        gauge.setUnavailable(true);
        QVERIFY(gauge.isUnavailable());
        gauge.setUnavailable(false);
        QVERIFY(!gauge.isUnavailable());
    }
};
QTEST_MAIN(TestRemoteMeterPoller)
#include "tst_remote_meter_poller.moc"
