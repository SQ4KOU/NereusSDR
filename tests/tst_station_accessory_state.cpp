// no-port-check: NereusSDR-original. R-R3-22 Core accessory ownership tests.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via OpenAI Codex.
#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "core/AppSettings.h"
#include "core/SmartSdrApiListener.h"
#include "core/PgxlConnection.h"
#include "core/TxSliceArbiter.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class StationAccessoryStateTest : public QObject {
    Q_OBJECT
    static void prepare(RadioModel& model)
    {
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        RadioInfo info;
        info.macAddress = QStringLiteral("aa:bb:cc:dd:ee:61");
        model.setLastRadioInfoForTest(info);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    }
private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void masterRequiresStationAndLiveMac()
    {
        RadioModel model;
        QString reason;
        QVERIFY(!model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(!reason.isEmpty());
        model.enableStationAccessoryIdentity();
        QVERIFY(!model.setFourO3AEnabledForStation(true, &reason));
        model.setFourO3AEnabled(true);
        QVERIFY(!model.smartSdrListener()->isListening());
        QVERIFY(!model.fourO3AEnabled());

        RadioModel remote(RadioModel::Role::Remote);
        QVERIFY(!remote.setFourO3AEnabledForStation(true, &reason));
        remote.setFourO3AEnabled(true);
        QVERIFY(!remote.smartSdrListener()->isListening());
    }

    void bindFailureIsObservedAndRepeatedEnableRetries()
    {
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
        const auto port = blocker.serverPort();
        RadioModel model;
        prepare(model);
        model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, port);
        QSignalSpy status(&model, &RadioModel::fourO3AStatusChanged);
        QString reason;
        // Acceptance is persisted intent; actual bind failure is a separate
        // state. A checkbox must never masquerade as a listening socket.
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(reason.isEmpty());
        QVERIFY(model.fourO3AEnabled());
        QVERIFY(!model.fourO3AListening());
        QVERIFY(!model.fourO3AListenerError().isEmpty());
        QVERIFY(!status.isEmpty());
        AppSettings disk(AppSettings::instance().filePath());
        disk.load();
        QCOMPARE(disk.hardwareValue(model.currentRadioMac(),
            QStringLiteral("peripherals/FourO3A_Enabled")).toString(), QStringLiteral("True"));
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
        QVERIFY(model.fourO3AListenerError().isEmpty());
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(!model.fourO3AListenerError().isEmpty());
        blocker.close();
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(model.fourO3AListening());
        QVERIFY(model.fourO3AListenerError().isEmpty());
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
        QVERIFY(!model.fourO3AListening());
        QVERIFY(!model.fourO3AEnabled());
        QVERIFY(model.fourO3AListenerError().isEmpty());
    }

    void remoteObservationCannotStartListenerOrWriteSettings()
    {
        RadioModel remote(RadioModel::Role::Remote);
        QSignalSpy enabled(&remote, &RadioModel::fourO3AEnabledChanged);
        QVERIFY(remote.applyMirroredValue("fourO3AEnabled", true).isEmpty());
        QVERIFY(remote.applyMirroredValue("fourO3AListening", true).isEmpty());
        QVERIFY(remote.fourO3AEnabled());
        QVERIFY(remote.fourO3AListening());
        QVERIFY(!remote.smartSdrListener()->isListening());
        QVERIFY(!remote.applyMirroredValue("fourO3AEnabled", QStringLiteral("True")).isEmpty());
        QVERIFY(remote.applyMirroredValue("fourO3AListenerError", QStringLiteral("bind failed")).isEmpty());
        QCOMPARE(remote.fourO3AListenerError(), QStringLiteral("bind failed"));
        remote.clearRemoteFourO3AState();
        QVERIFY(!remote.fourO3AEnabled());
        QVERIFY(!remote.fourO3AListening());
        QVERIFY(remote.fourO3AListenerError().isEmpty());
        QCOMPARE(enabled.count(), 2);
        QVERIFY(AppSettings::instance().hardwareValue(QStringLiteral("aa:bb:cc:dd:ee:61"),
            QStringLiteral("peripherals/FourO3A_Enabled")).toString().isEmpty());
    }

    void debouncedPgxlBandUsesCurrentBinding()
    {
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        auto* sliceA = model.sliceById(a);
        auto* sliceB = model.sliceById(b);
        QVERIFY(sliceA && sliceB);
        sliceA->setFrequency(14200000);
        sliceB->setFrequency(3700000);
        QString reason;
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        pgxl->connectToPgxl(QStringLiteral("127.0.0.1"), amp.serverPort());
        QTRY_VERIFY(amp.hasPendingConnections());
        auto* peer = amp.nextPendingConnection();
        peer->write("V3.8.9\n"); peer->flush();
        QTRY_VERIFY(pgxl->isConnected());
        // Existing parser's acknowledged pairing gate, driven only against
        // this loopback fixture. No real amp or tuner is contacted.
        QSignalSpy paired(pgxl, &PgxlConnection::pairingResult);
        const auto seq = pgxl->flexradioPair(QLatin1Char('A'), QStringLiteral("TEST"),
                                           QStringLiteral("ANT1"), false, true);
        peer->write(QStringLiteral("R%1|0|\n").arg(seq).toUtf8()); peer->flush();
        QTRY_COMPARE(paired.count(), 1);
        QVERIFY(paired.first().first().toBool());
        const auto hasInitialBand = [&] {
            for (const auto& frame : frames) {
                if (frame.first().toString().contains(QStringLiteral("band=14200000"))) { return true; }
            }
            return false;
        };
        QTRY_VERIFY(hasInitialBand());
        frames.clear();
        sliceA->setFrequency(14210000); // schedules old binding's debounce
        QVERIFY(model.txSliceArbiter()->requestHandoff(b));
        sliceB->setFrequency(3750000);
        model.setActiveSlice(a);
        sliceA->setFrequency(14220000); // viewed, unrelated receiver
        const auto bands = [&] {
            QStringList result;
            for (const auto& row : frames) {
                const QString frame = row.first().toString();
                if (frame.contains(QStringLiteral(" band="))) { result.append(frame); }
            }
            return result;
        };
        QTRY_VERIFY_WITH_TIMEOUT(bands().join(QLatin1Char(' ')).contains(QStringLiteral("band=3750000")), 800);
        QTest::qWait(250);
        QVERIFY(!bands().join(QLatin1Char(' ')).contains(QStringLiteral("band=142")));
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
    }

    void headlessListenerFollowsBoundSliceAcrossRetuneModeAndRemoval()
    {
        RadioModel model;
        prepare(model);
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        QVERIFY(a >= 0 && b >= 0);
        auto* sliceA = model.sliceById(a);
        auto* sliceB = model.sliceById(b);
        sliceA->setFrequency(14200000);
        sliceA->setDspMode(DSPMode::USB);
        sliceB->setFrequency(3700000);
        sliceB->setDspMode(DSPMode::LSB);
        model.setActiveSlice(b); // Looking at B must not change TX-bound A.
        QString reason;
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QTcpSocket subscriber;
        QByteArray received;
        connect(&subscriber, &QTcpSocket::readyRead, &subscriber,
                [&] { received += subscriber.readAll(); });
        subscriber.connectToHost(QHostAddress::LocalHost, model.smartSdrListener()->serverPort());
        QTRY_COMPARE(subscriber.state(), QAbstractSocket::ConnectedState);
        subscriber.write("C1|sub slice all\n");
        subscriber.flush();
        QTRY_VERIFY(received.contains("RF_frequency=14.200000"));
        QVERIFY(received.contains("mode=USB"));
        received.clear();
        sliceB->setFrequency(3750000);
        QTest::qWait(60);
        QVERIFY(!received.contains("RF_frequency=3.750000"));
        sliceA->setFrequency(14250000);
        QTRY_VERIFY(received.contains("RF_frequency=14.250000"));
        received.clear();
        sliceA->setDspMode(DSPMode::AM);
        QTRY_VERIFY(received.contains("mode=AM"));
        received.clear();
        QVERIFY(model.txSliceArbiter()->requestHandoff(b));
        QTRY_VERIFY(received.contains("RF_frequency=3.750000"));
        QTRY_VERIFY(received.contains("mode=LSB"));
        received.clear();
        sliceA->setFrequency(14300000);
        QTest::qWait(60);
        QVERIFY(!received.contains("RF_frequency=14.300000"));
        model.removeSlice(b);
        QTRY_VERIFY(received.contains("RF_frequency=14.300000"));
        QTRY_VERIFY(received.contains("mode=AM"));
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
        QTRY_COMPARE(subscriber.state(), QAbstractSocket::UnconnectedState);
    }
};
QTEST_GUILESS_MAIN(StationAccessoryStateTest)
#include "tst_station_accessory_state.moc"
