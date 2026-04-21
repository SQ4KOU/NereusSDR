// no-port-check: test fixture asserts AlexController per-band antenna routing + Block-TX safety + persistence
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/accessories/AlexController.h"
#include "models/Band.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestAlexController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        AppSettings::instance().clear();
    }

    // Default: every band's TX/RX antenna defaults to port 1
    void default_all_antennas_port1() {
        AlexController a;
        for (int b = int(Band::Band160m); b <= int(Band::XVTR); ++b) {
            QCOMPARE(a.txAnt(Band(b)),     1);
            QCOMPARE(a.rxAnt(Band(b)),     1);
            QCOMPARE(a.rxOnlyAnt(Band(b)), 1);
        }
    }

    // Setting antenna for one band doesn't affect others
    void setTxAnt_isolated_per_band() {
        AlexController a;
        a.setTxAnt(Band::Band20m, 2);
        QCOMPARE(a.txAnt(Band::Band20m), 2);
        QCOMPARE(a.txAnt(Band::Band40m), 1);  // unaffected
    }

    // Block-TX safety: when blockTxAnt2 set, setTxAnt(2) is rejected
    void blockTxAnt2_rejects_setTxAnt() {
        AlexController a;
        a.setBlockTxAnt2(true);
        a.setTxAnt(Band::Band20m, 2);
        QCOMPARE(a.txAnt(Band::Band20m), 1);  // not changed; remained at default
    }

    void blockTxAnt3_rejects_setTxAnt() {
        AlexController a;
        a.setBlockTxAnt3(true);
        a.setTxAnt(Band::Band6m, 3);
        QCOMPARE(a.txAnt(Band::Band6m), 1);
    }

    // Block doesn't affect RX antenna setting
    void blockTxAnt2_does_not_block_setRxAnt() {
        AlexController a;
        a.setBlockTxAnt2(true);
        a.setRxAnt(Band::Band20m, 2);
        QCOMPARE(a.rxAnt(Band::Band20m), 2);
    }

    // Antenna value clamped to 1..3
    void setTxAnt_clamps_to_1_3() {
        AlexController a;
        a.setTxAnt(Band::Band20m, 5);
        QCOMPARE(a.txAnt(Band::Band20m), 3);  // clamped high
        a.setTxAnt(Band::Band20m, 0);
        QCOMPARE(a.txAnt(Band::Band20m), 1);  // clamped low
    }

    // setAntennasTo1 forces all bands to port 1
    void setAntennasTo1_forces_all_to_port1() {
        AlexController a;
        a.setTxAnt(Band::Band20m, 2);
        a.setRxAnt(Band::Band40m, 3);
        a.setAntennasTo1(true);
        for (int b = int(Band::Band160m); b <= int(Band::XVTR); ++b) {
            QCOMPARE(a.txAnt(Band(b)), 1);
            QCOMPARE(a.rxAnt(Band(b)), 1);
        }
    }

    // antennaChanged signal fires on per-band update
    void antennaChanged_signal_fires() {
        AlexController a;
        QSignalSpy spy(&a, &AlexController::antennaChanged);
        a.setTxAnt(Band::Band20m, 2);
        QCOMPARE(spy.count(), 1);
    }

    // Per-MAC persistence round-trip
    void persistence_roundtrip() {
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:ff");
        AlexController a1;
        a1.setMacAddress(mac);
        a1.setTxAnt(Band::Band20m, 2);
        a1.setRxOnlyAnt(Band::Band40m, 3);
        a1.setBlockTxAnt2(true);
        a1.save();

        AlexController a2;
        a2.setMacAddress(mac);
        a2.load();
        QCOMPARE(a2.txAnt(Band::Band20m), 2);
        QCOMPARE(a2.rxOnlyAnt(Band::Band40m), 3);
        QVERIFY(a2.blockTxAnt2());
    }
};

QTEST_APPLESS_MAIN(TestAlexController)
#include "tst_alex_controller.moc"
