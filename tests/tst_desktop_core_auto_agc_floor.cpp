// no-port-check: NereusSDR-original. A desktop window acting as Core
// publishes each slice's Auto AGC noise floor for signed-in windows.
#include <QtTest>

#include "core/HpsdrModel.h"
#include "core/NoiseFloorTracker.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {
void warm(NoiseFloorTracker& tracker, float dbm)
{
    for (int i = 0; i < 70; ++i) { tracker.feed(QVector<float>(32, dbm), 33.0f); }
}
}

class TstDesktopCoreAutoAgcFloor final : public QObject {
    Q_OBJECT

private slots:
    // The fields below are what a signed-in window mirrors and shows as
    // "NF -98 dB" or "NF awaiting measurement" (MirrorPolicy, outbound).
    void desktopCorePublishesEachSlicesOwnFloor()
    {
        RadioModel radio;
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        SliceModel* first = radio.sliceById(radio.addSlice(QStringLiteral("pan-a")));
        SliceModel* second = radio.sliceById(radio.addSlice(QStringLiteral("pan-b")));
        QVERIFY(first && second);
        QVERIFY(first->streamIndex() >= 0 && second->streamIndex() >= 0);
        QVERIFY(first->streamIndex() != second->streamIndex());

        // As the desktop window registers its per-stream trackers.
        NoiseFloorTracker firstFloor, secondFloor;
        radio.setStreamNoiseFloorTracker(first->streamIndex(), &firstFloor);
        radio.setStreamNoiseFloorTracker(second->streamIndex(), &secondFloor);

        // No reading yet: not valid.
        radio.runAutoAgcTickForTest();
        QVERIFY(!first->stationAutoAgcNoiseFloorValid());
        QVERIFY(!second->stationAutoAgcNoiseFloorValid());

        // A good local reading: the same number, valid, AUTO on or off.
        warm(firstFloor, -98.0f);
        warm(secondFloor, -71.0f);
        QVERIFY(firstFloor.isGood() && secondFloor.isGood());
        radio.runAutoAgcTickForTest();
        QVERIFY(first->stationAutoAgcNoiseFloorValid());
        QVERIFY(second->stationAutoAgcNoiseFloorValid());
        QCOMPARE(first->stationAutoAgcNoiseFloorDbm(),
                 static_cast<double>(firstFloor.noiseFloor()));
        QCOMPARE(second->stationAutoAgcNoiseFloorDbm(),
                 static_cast<double>(secondFloor.noiseFloor()));
        const quint64 generation = first->stationAutoAgcNoiseFloorGeneration();

        // The local reading is lost (a retune restarts the tracker).
        firstFloor.triggerFastAttack();
        radio.runAutoAgcTickForTest();
        QVERIFY(!first->stationAutoAgcNoiseFloorValid());
        QVERIFY(first->stationAutoAgcNoiseFloorGeneration() > generation);
        QVERIFY(second->stationAutoAgcNoiseFloorValid());

        // Measured again.
        warm(firstFloor, -90.0f);
        radio.runAutoAgcTickForTest();
        QVERIFY(first->stationAutoAgcNoiseFloorValid());
        QCOMPARE(first->stationAutoAgcNoiseFloorDbm(),
                 static_cast<double>(firstFloor.noiseFloor()));

        // Transmit: not valid at once, as on the headless Core, and it
        // stays so for the whole of the over.
        radio.transmitModel().setMox(true);
        QVERIFY(!first->stationAutoAgcNoiseFloorValid());
        QVERIFY(!second->stationAutoAgcNoiseFloorValid());
        QCOMPARE(first->stationAutoAgcNoiseFloorDbm(), -200.0);
        radio.runAutoAgcTickForTest();
        QVERIFY(!first->stationAutoAgcNoiseFloorValid());
        radio.transmitModel().setMox(false);
        radio.runAutoAgcTickForTest();
        QVERIFY(first->stationAutoAgcNoiseFloorValid());
        QVERIFY(second->stationAutoAgcNoiseFloorValid());

        // The radio drops.
        radio.setConnectionStateForTest(ConnectionState::Disconnected);
        radio.runAutoAgcTickForTest();
        QVERIFY(!first->stationAutoAgcNoiseFloorValid());
        QVERIFY(!second->stationAutoAgcNoiseFloorValid());

        radio.clearStreamNoiseFloorTracker(first->streamIndex(), &firstFloor);
        radio.clearStreamNoiseFloorTracker(second->streamIndex(), &secondFloor);
    }

    // A source of its own (the headless Core) keeps the fields to itself.
    void aSourceOfItsOwnKeepsTheFields()
    {
        RadioModel radio;
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.configureStreamPool(5, 5, 192000);
        radio.setConnectionStateForTest(ConnectionState::Connected);
        SliceModel* slice = radio.sliceById(radio.addSlice(QStringLiteral("pan-a")));
        QVERIFY(slice);
        NoiseFloorTracker floor;
        warm(floor, -98.0f);
        radio.setStreamNoiseFloorTracker(slice->streamIndex(), &floor);
        radio.setAutoAgcFloorPublishedBySource(true);
        radio.runAutoAgcTickForTest();
        QVERIFY(!slice->stationAutoAgcNoiseFloorValid());
        radio.clearStreamNoiseFloorTracker(slice->streamIndex(), &floor);
    }
};

QTEST_MAIN(TstDesktopCoreAutoAgcFloor)
#include "tst_desktop_core_auto_agc_floor.moc"
