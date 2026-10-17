// no-port-check: NereusSDR-original tests. An ordinary control change on a
// running Core, and the timed saves that follow one, must not write the
// settings file on the Core's main thread: that thread also sends the
// receive audio, and the save waits for the disk (140 to 470 ms on a Rock
// 5C's eMMC, bench 2026-10-10). Each test makes one change with the writer
// thread held (tests/fakes/HeldSettingsWriter.h) and fails when the file is
// written before the writer is let go.
//
// No radio, no Core and no sound device: the accessory addresses dial this
// process's own loopback listeners, or nothing.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/DxClusterClient.h"
#include "core/FaultLog.h"
#include "core/HpsdrModel.h"
#include "core/PotaClient.h"
#include "core/PskReporterClient.h"
#include "core/RadioDiscovery.h"
#include "core/SmartSdrApiListener.h"
#include "core/SpotCollectorClient.h"
#include "core/SpotSourceHost.h"
#include "core/StationAccessoryData.h"
#include "core/StationTciController.h"
#include "core/StepAttenuatorController.h"
#include "core/TxAnalyzer.h"
#include "core/TxInterlockPolicy.h"
#include "core/WsjtxClient.h"
#include "core/session/StationVaxFacade.h"
#include "core/station/StationRadios.h"
#include "models/AccessoryDataModel.h"
#include "models/RadioModel.h"
#include "models/SpotModel.h"
#include "models/StationTciModel.h"
#include "models/TransmitModel.h"

#include "fakes/HeldSettingsWriter.h"

using namespace NereusSDR;

namespace {

const QString kMac = QStringLiteral("aa:bb:cc:dd:ee:5a");

// A Core's RadioModel with a radio identity and its station accessories,
// as tst_pgxl_station_control prepares one. Nothing is dialed here.
void prepareCore(RadioModel& model)
{
    model.enableStationAccessoryIdentity();
    model.setReceiveOnlyStationPolicy(true);
    RadioInfo radio;
    radio.macAddress = kMac;
    model.setLastRadioInfoForTest(radio);
    model.setConnectionStateForTest(ConnectionState::Connected);
    model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    model.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
}

QString peripheralKey(const char* leaf)
{
    return QStringLiteral("peripherals/") + QLatin1String(leaf);
}

} // namespace

class TestControlSavesInBackground : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        AppSettings::instance().clear();
        QFile::remove(AppSettings::instance().filePath());
    }

    void cleanup() { AppSettings::instance().setBackgroundSaveStartHookForTesting({}); }

    // ── The two timed saves ──────────────────────────────────────────────

    void stepAttenuator_timedSave()
    {
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.loadSettings(kMac);
        ctrl.setDebouncedSaveEnabled(true);
        HeldSettingsWriter held;

        ctrl.setAttenuation(17);
        QVERIFY(ctrl.savePending());

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, QStringLiteral("options/stepAtt/rx1Value")),
            QStringLiteral("17"));
    }

    // The teardown save and the flush still write the file before returning.
    void stepAttenuator_flushAndTeardownSaveStillBlock()
    {
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.loadSettings(kMac);
        ctrl.setDebouncedSaveEnabled(true);
        HeldSettingsWriter held;

        ctrl.setAttenuation(9);
        ctrl.flushPendingSave();
        QCOMPARE(held.onDiskHardware(kMac, QStringLiteral("options/stepAtt/rx1Value")),
                 QStringLiteral("9"));

        ctrl.setAttenuation(12);
        ctrl.saveSettings(kMac);
        QCOMPARE(held.onDiskHardware(kMac, QStringLiteral("options/stepAtt/rx1Value")),
                 QStringLiteral("12"));
    }

    void accessoryData_timedSave()
    {
        AccessoryDataModel data;
        StationAccessoryData core(&data, StationAccessoryData::Sources{});
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(core.setPowerCap(true, 800, &reason), qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("PGXL_PowerCapW")), QStringLiteral("800"));
    }

    // The blocking save logged a failure in these words; the timed save
    // still does, from the writer thread's report.
    void accessoryData_failedTimedSaveIsLogged()
    {
        auto& settings = AppSettings::instance();
        AccessoryDataModel data;
        StationAccessoryData core(&data, StationAccessoryData::Sources{});
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);
        // A directory at the file's path refuses the atomic replacement.
        QVERIFY(QDir().mkpath(settings.filePath()));
        const auto tidy = qScopeGuard([&settings] { QDir().rmdir(settings.filePath()); });
        QTest::ignoreMessage(
            QtWarningMsg, QRegularExpression(QStringLiteral("Could not save the accessory records")));

        QString reason;
        QVERIFY2(core.setPowerCap(true, 900, &reason), qPrintable(reason));

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, HeldSettingsWriter::kWaitMs);
        QCOMPARE(finished.at(0).at(1).toBool(), false);
        // The spy hears the report on the writer thread; the object under
        // test hears it queued, posted before the spy counted it.
        QCoreApplication::processEvents();
    }

    // The destructor's flush writes the file before the object is gone.
    void accessoryData_destructorFlushStillBlocks()
    {
        AccessoryDataModel data;
        auto core = std::make_unique<StationAccessoryData>(&data, StationAccessoryData::Sources{});
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(core->setPowerCap(true, 700, &reason), qPrintable(reason));
        core.reset();

        QCOMPARE(held.onDisk(QStringLiteral("PGXL_PowerCapW")), QStringLiteral("700"));
    }

    // ── Control changes that saved at once ───────────────────────────────

    void txAnalyzer_displaySetting()
    {
        TxAnalyzer analyzer;
        const int averaging = analyzer.panAveraging() == 2 ? 3 : 2;
        HeldSettingsWriter held;

        analyzer.setPanAveraging(averaging);

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("DisplayTxPanAveraging")),
            QString::number(averaging));
    }

    void spotSources_freedvHidden()
    {
        DxClusterClient dx;
        DxClusterClient rbn;
        WsjtxClient wsjtx;
        SpotCollectorClient spotCollector;
        PotaClient pota;
        PskReporterClient psk;
        SpotModel spots;
        SpotSourceHost host{&dx, &rbn, &wsjtx, &spotCollector, &pota, &psk, &spots};
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(host.setFreedvHidden(true, &reason), qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("FreeDvReporter/Hidden")), QStringLiteral("True"));
    }

    void tci_enabledAndPort()
    {
        RadioModel model;
        StationTciModel state;
        StationTciController controller(&model, &state);
        controller.setBindOverride(QStringLiteral("127.0.0.1"));
        HeldSettingsWriter held;

        QString reason;
        // Off, with a new port: saved, and nothing listens.
        QVERIFY2(controller.setEnabled(false, 50123, &reason), qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(StationTciController::portKey()), QStringLiteral("50123"));
    }

    void tci_options()
    {
        RadioModel model;
        StationTciModel state;
        StationTciController controller(&model, &state);
        HeldSettingsWriter held;

        controller.setOptions(true, false, true, false);

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("TciCwluBecomesCw")), QStringLiteral("True"));
    }

    void tci_settings()
    {
        RadioModel model;
        StationTciModel state;
        StationTciController controller(&model, &state);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(controller.setSettings({{QStringLiteral("rateLimitMs"), 250}}, &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("TciRateLimitMs")), QStringLiteral("250"));
    }

    void stationRadios_savedChoice()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("NereusSDR.settings")));
        StationRadios radios(settings);
        HeldSettingsWriter held(settings);

        radios.saveChoice(QStringLiteral("AA:BB:CC:00:00:01"));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QLatin1String(StationRadios::kChoiceKey)), radios.savedChoice());
        QVERIFY(!radios.savedChoice().isEmpty());
    }

    void stationRadios_modelOverride()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        AppSettings settings(path);
        StationRadios radios(settings);
        const QString mac = QStringLiteral("AA:BB:CC:00:00:02");
        RadioInfo info;
        info.macAddress = mac;
        info.boardType = HPSDRHW::Saturn;
        info.name = QStringLiteral("G2");
        radios.setVisible({info});
        HeldSettingsWriter held(settings);

        QString reason;
        QVERIFY2(radios.setModel(mac, static_cast<int>(HPSDRModel::ANAN_G2_1K), &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(held, QFile::exists(path), true);
        AppSettings disk(path);
        disk.load();
        QCOMPARE(disk.modelOverride(mac), settings.modelOverride(mac));
        QCOMPARE(disk.modelOverride(mac), HPSDRModel::ANAN_G2_1K);
    }

    void stationRadios_forget()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        AppSettings settings(path);
        StationRadios radios(settings);
        const QString mac = QStringLiteral("AA:BB:CC:00:00:03");
        RadioInfo info;
        info.macAddress = mac;
        info.boardType = HPSDRHW::Saturn;
        settings.saveRadio(info, false, false);
        QVERIFY(settings.save());
        QVERIFY(settings.savedRadio(mac).has_value());
        HeldSettingsWriter held(settings);

        QString reason;
        QVERIFY2(radios.forget(mac, &reason), qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(held, QFile::exists(path), true);
        AppSettings disk(path);
        disk.load();
        QVERIFY(!disk.savedRadio(mac).has_value());
    }

    void stationVax_receiveLevel()
    {
        RadioModel radio;
        StationVax vax;
        vax.bind(&radio, radio.audioEngine(), &AppSettings::instance());
        HeldSettingsWriter held;

        vax.setRxGain(1, 0.5);

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("audio/Vax1/RxGain")), QStringLiteral("0.500"));
    }

    // ── RadioModel: the station's accessory and audio settings ───────────

    void radio_fourO3AEnabled()
    {
        RadioModel model;
        prepareCore(model);
        model.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("False"));
        HeldSettingsWriter held;

        model.setFourO3AEnabled(true);

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("FourO3A_Enabled")),
            QStringLiteral("True"));
    }

    void radio_stationIdentityForFreedv()
    {
        RadioModel model;
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("User/Callsign"), QStringLiteral("KG4VCF"));
        settings.setValue(QStringLiteral("User/GridSquare"), QStringLiteral("EM73"));
        HeldSettingsWriter held;

        model.applyRemoteFreedvSetting(QStringLiteral("User/Callsign"), settings);

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("FreeDvReporter/Callsign")),
            QStringLiteral("KG4VCF"));
    }

    void radio_tgxlAddress()
    {
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.setTgxlAddressForStation(QStringLiteral("192.0.2.9"), 9010, &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("TGXL_ManualPort")),
            QStringLiteral("9010"));
    }

    void radio_tgxlConfigure()
    {
        QTcpServer listener;   // this process's own loopback, never a device
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.configureTgxlForStation(QStringLiteral("127.0.0.1"),
                                               listener.serverPort(), &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("TGXL_ManualPort")),
            QString::number(listener.serverPort()));
    }

    void radio_pgxlAddress()
    {
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.setPgxlAddressForStation(QStringLiteral("192.0.2.9"), 9008, &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("PGXL_ManualPort")),
            QStringLiteral("9008"));
    }

    void radio_pgxlConfigure()
    {
        QTcpServer listener;   // this process's own loopback, never a device
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.configurePgxlForStation(QStringLiteral("127.0.0.1"),
                                               listener.serverPort(), &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("PGXL_ManualPort")),
            QString::number(listener.serverPort()));
    }

    void radio_pgxlConnectionSettings()
    {
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.setPgxlConnectionSettingsForStation(true, 37, 11, &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("PGXL_KeepaliveSec")), QStringLiteral("37"));
    }

    void radio_rfKitEnabled()
    {
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.setRfKitEnabledForStation(true, &reason), qPrintable(reason));
        QVERIFY(model.rfKitEnabled());

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("RfKit_Enabled")),
            QStringLiteral("True"));
    }

    void radio_rfKitAddress()
    {
        RadioModel model;
        prepareCore(model);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.setRfKitAddressForStation(QStringLiteral("192.0.2.9"), 8080, &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("RfKit_ManualPort")),
            QStringLiteral("8080"));
    }

    void radio_rfKitConfigure()
    {
        QTcpServer listener;   // this process's own loopback, never a device
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepareCore(model);
        model.setRfKitEnabled(true);
        HeldSettingsWriter held;

        QString reason;
        QVERIFY2(model.configureRfKitForStation(QStringLiteral("127.0.0.1"),
                                                listener.serverPort(), &reason),
                 qPrintable(reason));

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDiskHardware(kMac, peripheralKey("RfKit_ManualPort")),
            QString::number(listener.serverPort()));
    }

    void radio_pcMicBuffer()
    {
        RadioModel model;
        const int samples = model.audioEngine()->txInputConfig().bufferSamples == 512 ? 1024 : 512;
        HeldSettingsWriter held;

        model.transmitModel().setPcMicBufferSamples(samples);

        NEREUS_VERIFY_SAVED_IN_BACKGROUND(
            held, held.onDisk(QStringLiteral("audio/TxInput/BufferSamples")),
            QString::number(samples));
    }
};

QTEST_MAIN(TestControlSavesInBackground)
#include "tst_control_saves_in_background.moc"
