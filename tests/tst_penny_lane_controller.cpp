// no-port-check: test fixture asserts PennyLaneController ext-ctrl master toggle + persistence
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/accessories/PennyLaneController.h"
#include "core/AppSettings.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestPennyLaneController : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        AppSettings::instance().clear();
    }

    // Default: extCtrlEnabled = true (matches Thetis penny_ext_ctrl_enabled = true)
    void default_extctrl_enabled() {
        PennyLaneController p;
        QVERIFY(p.extCtrlEnabled());
    }

    // setExtCtrlEnabled: toggle off, signal fires
    void setExtCtrlEnabled_off_signal_fires() {
        PennyLaneController p;
        QSignalSpy spy(&p, &PennyLaneController::extCtrlEnabledChanged);
        p.setExtCtrlEnabled(false);
        QVERIFY(!p.extCtrlEnabled());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toBool(), false);
    }

    // Idempotent: setting the same value doesn't fire the signal
    void setExtCtrlEnabled_idempotent_no_signal() {
        PennyLaneController p;
        QSignalSpy spy(&p, &PennyLaneController::extCtrlEnabledChanged);
        p.setExtCtrlEnabled(true);  // already true
        QCOMPARE(spy.count(), 0);
    }

    // setExtCtrlEnabled: toggle on then off
    void setExtCtrlEnabled_toggle_on_off() {
        PennyLaneController p;
        p.setExtCtrlEnabled(false);
        QVERIFY(!p.extCtrlEnabled());
        p.setExtCtrlEnabled(true);
        QVERIFY(p.extCtrlEnabled());
    }

    // Per-MAC persistence round-trip (extCtrlEnabled = false)
    void persistence_roundtrip_false() {
        const QString mac = QStringLiteral("de:ad:be:ef:00:01");
        PennyLaneController p1;
        p1.setMacAddress(mac);
        p1.setExtCtrlEnabled(false);
        p1.save();

        PennyLaneController p2;
        p2.setMacAddress(mac);
        p2.load();
        QVERIFY(!p2.extCtrlEnabled());
    }

    // Per-MAC persistence round-trip (extCtrlEnabled = true)
    void persistence_roundtrip_true() {
        const QString mac = QStringLiteral("de:ad:be:ef:00:02");
        PennyLaneController p1;
        p1.setMacAddress(mac);
        p1.setExtCtrlEnabled(true);
        p1.save();

        PennyLaneController p2;
        p2.setMacAddress(mac);
        p2.load();
        QVERIFY(p2.extCtrlEnabled());
    }

    // R-R3-21: the Setup checkbox used to save to the global
    // hardware/oc/pennyExtCtrl key, which nothing read. At startup every
    // saved radio with no value of its own takes that value under its own
    // key, and the global key goes; a radio added later starts at Thetis's
    // default (True), not the old global value.
    void legacy_global_key_migrates_to_saved_radios_then_goes() {
        auto& s = AppSettings::instance();
        const QString legacy = QStringLiteral("hardware/oc/pennyExtCtrl");
        const QString macA = QStringLiteral("de:ad:be:ef:00:0a");
        const QString macB = QStringLiteral("de:ad:be:ef:00:0b");
        const QString macNew = QStringLiteral("de:ad:be:ef:00:0c");
        const auto perMac = [](const QString& mac) {
            return QStringLiteral("hardware/%1/penny/extCtrlEnabled").arg(mac);
        };
        const auto saveTestRadio = [&s](const QString& mac) {
            RadioInfo info;
            info.macAddress = mac;
            info.address    = QHostAddress(QStringLiteral("192.168.1.10"));
            info.port       = 1024;
            info.boardType  = HPSDRHW::Hermes;
            info.protocol   = ProtocolVersion::Protocol1;
            info.name       = QStringLiteral("Test Radio");
            s.saveRadio(info, /*pinToMac=*/false, /*autoConnect=*/false);
        };
        saveTestRadio(macA);
        saveTestRadio(macB);
        // The old checkbox stored a QVariant(bool), saved as "false".
        s.setValue(legacy, false);

        AppSettings::migrateLegacyPennyExtCtrl(s);

        QCOMPARE(s.value(perMac(macA)).toString(), QStringLiteral("False"));
        QCOMPARE(s.value(perMac(macB)).toString(), QStringLiteral("False"));
        QVERIFY2(!s.contains(legacy), "the old global key must be gone");

        for (const QString& mac : {macA, macB}) {
            PennyLaneController p;
            p.setMacAddress(mac);
            p.load();
            QVERIFY(!p.extCtrlEnabled());
        }
        // A radio added later: Thetis's default, not the old value.
        PennyLaneController fresh;
        fresh.setMacAddress(macNew);
        fresh.setExtCtrlEnabled(false);
        fresh.load();
        QVERIFY2(fresh.extCtrlEnabled(), "a new radio must start at the default True");

        // Idempotent: a second run changes nothing.
        s.setValue(perMac(macA), QStringLiteral("True"));
        AppSettings::migrateLegacyPennyExtCtrl(s);
        QCOMPARE(s.value(perMac(macA)).toString(), QStringLiteral("True"));
        s.forgetRadio(macA);
        s.forgetRadio(macB);
        s.remove(perMac(macA));
        s.remove(perMac(macB));
    }

    // A saved radio that already has its own value keeps it.
    void legacy_migration_keeps_a_radio_value() {
        auto& s = AppSettings::instance();
        const QString legacy = QStringLiteral("hardware/oc/pennyExtCtrl");
        const QString mac = QStringLiteral("de:ad:be:ef:00:0d");
        const QString perMac = QStringLiteral("hardware/%1/penny/extCtrlEnabled").arg(mac);
        RadioInfo info;
        info.macAddress = mac;
        info.address    = QHostAddress(QStringLiteral("192.168.1.11"));
        info.port       = 1024;
        info.boardType  = HPSDRHW::Hermes;
        info.protocol   = ProtocolVersion::Protocol1;
        info.name       = QStringLiteral("Test Radio");
        s.saveRadio(info, false, false);
        s.setValue(perMac, QStringLiteral("True"));
        s.setValue(legacy, false);

        AppSettings::migrateLegacyPennyExtCtrl(s);

        QCOMPARE(s.value(perMac).toString(), QStringLiteral("True"));
        QVERIFY(!s.contains(legacy));
        s.forgetRadio(mac);
        s.remove(perMac);
    }

    // load() without MAC is a no-op; default remains true
    void load_without_mac_is_noop() {
        PennyLaneController p;
        p.setExtCtrlEnabled(false);
        p.load();  // no MAC — should not change state or crash
        QVERIFY(!p.extCtrlEnabled());  // state from before, not reset by failed load
    }
};

QTEST_APPLESS_MAIN(TestPennyLaneController)
#include "tst_penny_lane_controller.moc"
