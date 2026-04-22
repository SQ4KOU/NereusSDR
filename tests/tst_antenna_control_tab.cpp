// no-port-check: smoke test for Antenna Control sub-sub-tab UI construction + AlexController wiring
#include <QtTest/QtTest>
#include <QApplication>
#include <QSignalSpy>

#include "gui/setup/hardware/AntennaAlexAntennaControlTab.h"
#include "models/RadioModel.h"
#include "core/accessories/AlexController.h"
#include "models/Band.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestAntennaControlTab : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!qApp) {
            static int   argc = 0;
            static char* argv = nullptr;
            new QApplication(argc, &argv);
        }
        AppSettings::instance().clear();
    }

    // Widget constructs without crashing.
    void construct_basic()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);
        QVERIFY(true);  // smoke — construction succeeds
    }

    // The controller() accessor returns the model's AlexController.
    void controller_accessor_returns_model_controller()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);
        // pointer equality: tab's controller IS the model's controller
        QCOMPARE(&tab.controller(), &model.alexControllerMutable());
    }

    // Setting an antenna in the model propagates via the signal (signal-driven sync).
    void model_change_propagates_via_signal()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);

        QSignalSpy spy(&model.alexControllerMutable(), &AlexController::antennaChanged);
        model.alexControllerMutable().setTxAnt(Band::Band20m, 2);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.alexController().txAnt(Band::Band20m), 2);
    }

    // Block-TX safety on UI: when blockTxAnt2 is set, attempting setTxAnt(2)
    // is rejected by the controller; TX port stays at default (1).
    void block_tx_ant2_rejects_tx_assignment()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);

        model.alexControllerMutable().setBlockTxAnt2(true);
        model.alexControllerMutable().setTxAnt(Band::Band20m, 2);
        QCOMPARE(model.alexController().txAnt(Band::Band20m), 1);  // stayed at default
    }

    // Block-TX Ant3 rejection works independently.
    void block_tx_ant3_rejects_tx_assignment()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);

        model.alexControllerMutable().setBlockTxAnt3(true);
        model.alexControllerMutable().setTxAnt(Band::Band6m, 3);
        QCOMPARE(model.alexController().txAnt(Band::Band6m), 1);  // stayed at default
    }

    // Block-TX does not prevent RX assignment.
    void block_tx_does_not_block_rx_assignment()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);

        model.alexControllerMutable().setBlockTxAnt2(true);
        model.alexControllerMutable().setRxAnt(Band::Band20m, 2);
        QCOMPARE(model.alexController().rxAnt(Band::Band20m), 2);
    }

    // blockTxChanged signal fires when block-TX state changes.
    void block_tx_changed_signal_fires()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);

        QSignalSpy spy(&model.alexControllerMutable(), &AlexController::blockTxChanged);
        model.alexControllerMutable().setBlockTxAnt2(true);
        QCOMPARE(spy.count(), 1);
    }

    // RX-only antenna assignment works per-band.
    void rx_only_ant_assignment_per_band()
    {
        RadioModel model;
        AntennaAlexAntennaControlTab tab(&model);

        model.alexControllerMutable().setRxOnlyAnt(Band::Band40m, 3);
        QCOMPARE(model.alexController().rxOnlyAnt(Band::Band40m), 3);
        QCOMPARE(model.alexController().rxOnlyAnt(Band::Band20m), 1);  // unaffected
    }
};

QTEST_MAIN(TestAntennaControlTab)
#include "tst_antenna_control_tab.moc"
