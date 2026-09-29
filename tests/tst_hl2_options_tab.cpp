// no-port-check: smoke test for Hl2OptionsTab + Hl2OptionsModel UI/state
// wiring.  Phase 3L commit #9.  Cite comments to mi0bot setup.designer.cs
// are documentary only — the ported logic lives in the .cpp where the
// attribution header + PROVENANCE row already cover it.

#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/Hl2OptionsModel.h"
#include "core/IoBoardHl2.h"
#include "gui/setup/hardware/Hl2OptionsTab.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestHl2OptionsTab : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
    }

    // ── Hl2OptionsModel ─────────────────────────────────────────────────────

    // Defaults match mi0bot setup.designer.cs values verbatim.
    void model_defaults_match_mi0bot_designer_values()
    {
        Hl2OptionsModel m;
        QVERIFY(!m.swapAudioChannels());
        QVERIFY(!m.cl2Enabled());
        QCOMPARE(m.cl2FreqMHz(), Hl2OptionsModel::kDefaultCl2FreqMHz); // 116
        QVERIFY(!m.ext10MHz());
        QVERIFY(!m.disconnectReset());
        QCOMPARE(m.pttHangMs(),  Hl2OptionsModel::kDefaultPttHangMs);   // 12
        QCOMPARE(m.txLatencyMs(), Hl2OptionsModel::kDefaultTxLatencyMs); // 20
        QVERIFY(!m.psSync());
        QVERIFY(!m.bandVolts());
    }

    // Setters fire per-property + changed() signals and clamp to mi0bot ranges.
    void model_setters_clamp_to_mi0bot_ranges()
    {
        Hl2OptionsModel m;

        QSignalSpy changedSpy(&m, &Hl2OptionsModel::changed);

        // CL2 freq above max clamps to 200.
        m.setCl2FreqMHz(500);
        QCOMPARE(m.cl2FreqMHz(), Hl2OptionsModel::kCl2FreqMaxMHz);  // 200
        // Below min clamps to 1.
        m.setCl2FreqMHz(0);
        QCOMPARE(m.cl2FreqMHz(), Hl2OptionsModel::kCl2FreqMinMHz);  // 1

        // PTT hang above max clamps to 30.
        m.setPttHangMs(99);
        QCOMPARE(m.pttHangMs(), Hl2OptionsModel::kPttHangMaxMs);    // 30

        // TX buffer latency above max clamps to 70.
        m.setTxLatencyMs(999);
        QCOMPARE(m.txLatencyMs(), Hl2OptionsModel::kTxLatencyMaxMs); // 70

        // changed() fired at least once per mutation that produced a delta.
        QVERIFY(changedSpy.count() >= 4);
    }

    // No-op setter (same value) does NOT re-emit changed().
    void model_setter_is_idempotent()
    {
        Hl2OptionsModel m;
        m.setPttHangMs(15);
        QSignalSpy spy(&m, &Hl2OptionsModel::changed);
        m.setPttHangMs(15);
        QCOMPARE(spy.count(), 0);
    }

    // Per-MAC AppSettings round-trip.
    void model_per_mac_appsettings_roundtrip()
    {
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:ff");

        // Wipe any pre-existing keys for a clean slate.
        auto& s = AppSettings::instance();
        s.setHardwareValue(mac, QStringLiteral("hl2/swapAudioChannels"), QStringLiteral("False"));
        s.setHardwareValue(mac, QStringLiteral("hl2/pttHangMs"),         12);
        s.setHardwareValue(mac, QStringLiteral("hl2/cl2FreqMHz"),        116);

        {
            Hl2OptionsModel writer;
            writer.setMacAddress(mac);
            writer.setSwapAudioChannels(true);
            writer.setPttHangMs(25);
            writer.setCl2FreqMHz(50);
        }

        Hl2OptionsModel reader;
        reader.setMacAddress(mac);
        reader.load();
        QVERIFY(reader.swapAudioChannels());
        QCOMPARE(reader.pttHangMs(), 25);
        QCOMPARE(reader.cl2FreqMHz(), 50);
    }

    void save_writes_only_changed_keys()
    {
        // R-R3-46. Every setter saves. In a remote window those writes go
        // to the Core, which refuses the transmit timings on a receive-only
        // station, so a receive option change writes only its own key.
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:fe");
        auto& s = AppSettings::instance();
        s.clearHardwareValues(mac);

        Hl2OptionsModel m;
        m.setMacAddress(mac);
        m.load();
        m.setSwapAudioChannels(true);
        QCOMPARE(s.hardwareValue(mac, QStringLiteral("hl2/swapAudioChannels")).toString(),
                 QStringLiteral("True"));
        QVERIFY(!s.contains(QStringLiteral("hardware/%1/hl2/pttHangMs").arg(mac)));
        QVERIFY(!s.contains(QStringLiteral("hardware/%1/hl2/txLatencyMs").arg(mac)));
        QVERIFY(!s.contains(QStringLiteral("hardware/%1/hl2/cl2FreqMHz").arg(mac)));

        m.setPttHangMs(20);
        Hl2OptionsModel reader;
        reader.setMacAddress(mac);
        reader.load();
        QVERIFY(reader.swapAudioChannels());
        QCOMPARE(reader.pttHangMs(), 20);
        QCOMPARE(reader.txLatencyMs(), Hl2OptionsModel::kDefaultTxLatencyMs);
        s.clearHardwareValues(mac);
    }

    // ── Hl2OptionsTab construction ──────────────────────────────────────────

    void tab_construction_does_not_crash()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        QVERIFY(!tab.isVisible());
    }

    // Model → UI sync: changing the model after construction updates the
    // QSpinBox / QCheckBox values on the tab.
    void tab_syncs_from_model()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);

        model.hl2OptionsMutable().setSwapAudioChannels(true);
        model.hl2OptionsMutable().setPttHangMs(7);
        model.hl2OptionsMutable().setTxLatencyMs(33);

        QVERIFY(tab.swapAudioChannelsCheckedForTest());
        QCOMPARE(tab.pttHangMsForTest(),  7);
        QCOMPARE(tab.txLatencyMsForTest(), 33);
    }

    // Four options are stored but never reach the radio: Swap audio
    // channels (NereusSDR sends the radio no audio of its own over P1, so
    // there is nothing to swap), Enable CL2, CL2 frequency and External
    // 10 MHz (the HL2 clock settings are not sent). Each shows disabled
    // with a plain reason, never enabled as if it worked. The five that do
    // reach the radio stay enabled. The same tab is the remote window's.
    void stored_only_options_show_disabled_with_a_reason()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        const QStringList storedOnly{
            QStringLiteral("hl2SwapAudioChannels"), QStringLiteral("hl2Cl2Enable"),
            QStringLiteral("hl2Cl2Freq"), QStringLiteral("hl2Ext10MHz")};
        for (const QString& name : storedOnly) {
            auto* w = tab.findChild<QWidget*>(name);
            QVERIFY2(w != nullptr, qPrintable(name));
            QVERIFY2(!w->isEnabled(), qPrintable(name));
            QVERIFY2(!w->toolTip().isEmpty(), qPrintable(name));
        }
        QCOMPARE(tab.findChild<QWidget*>(QStringLiteral("hl2SwapAudioChannels"))->toolTip(),
                 QStringLiteral("NereusSDR does not send the radio audio of its own, "
                                "so there is nothing to swap."));
        QCOMPARE(tab.findChild<QWidget*>(QStringLiteral("hl2Ext10MHz"))->toolTip(),
                 QStringLiteral("NereusSDR does not change the radio's clock settings."));
        const QStringList live{
            QStringLiteral("hl2TxBufferLatency"), QStringLiteral("hl2PttHang"),
            QStringLiteral("hl2DisconnectReset"), QStringLiteral("hl2DisablePsSync"),
            QStringLiteral("hl2BandVolts")};
        for (const QString& name : live) {
            auto* w = tab.findChild<QWidget*>(name);
            QVERIFY2(w != nullptr, qPrintable(name));
            QVERIFY2(w->isEnabled(), qPrintable(name));
        }
    }

    // The power-supply sync option reads as what a tick does: it turns the
    // radio's power supply clock sync off. mi0bot's check box is
    // "Disable PS Sync" (setup.designer.cs:11298 [@c26a8a4]), tooltip
    // "Disables the FPGA synchronisation of the power supply clock"
    // (:11299); PS there is the power supply, not PureSignal. A tick sets
    // psSync, which sends the bit that disables it (setup.cs:13384-13390).
    // The same tab is the remote window's.
    void power_supply_sync_box_says_what_a_tick_does()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        auto* box = tab.findChild<QCheckBox*>(QStringLiteral("hl2DisablePsSync"));
        QVERIFY(box != nullptr);
        QCOMPARE(box->text(), QStringLiteral("Disable power supply sync"));
        QCOMPARE(box->toolTip(),
                 QStringLiteral("Stops the radio synchronizing its power supply clock."));
        QVERIFY(!box->text().contains(QStringLiteral("PureSignal")));
        QVERIFY(!model.hl2Options().psSync());
        box->setChecked(true);
        QVERIFY(model.hl2Options().psSync());
        box->setChecked(false);
        QVERIFY(!model.hl2Options().psSync());
    }

    // I/O Pin State output strip starts blank and shows the I/O board's
    // output register (169) as read back, as mi0bot's ucOutPinsLedStripHF
    // shows it (remote-window parity Task 14); the band output byte the
    // radio is sent is the HL2 I/O tab's OC strip, not this one.
    void output_strip_reflects_the_output_register()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        QCOMPARE(tab.outputBitsForTest(), quint8(0));

        model.reportBandOutputsForTest(/*ocByte=*/0x42, /*band=*/3, /*keyed=*/false);
        QCOMPARE(tab.outputBitsForTest(), quint8(0));

        model.ioBoardMutable().setRegisterValue(IoBoardHl2::Register::REG_OUT_PINS, 0x21);
        QCOMPARE(tab.outputBitsForTest(), quint8(0x21));
    }

    // I/O Pin State input strip shows the input pins register (6), as
    // mi0bot's UpdateIOLedStrip(MOX, readRegister(REG_INPUT_PINS)) sets
    // ucIOPinsLedStripHF after each pin read (console.cs:25887,
    // setup.cs:22606-22610 [@c26a8a4]). Not the output register.
    void input_strip_shows_the_input_pins_register()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        QCOMPARE(tab.inputBitsForTest(), quint8(0));
        QVERIFY(!tab.inputStripTxForTest());

        model.ioBoardMutable().setRegisterValue(IoBoardHl2::Register::REG_INPUT_PINS, 0x15);
        QCOMPARE(tab.inputBitsForTest(), quint8(0x15));
        model.ioBoardMutable().setRegisterValue(IoBoardHl2::Register::REG_OUT_PINS, 0x3F);
        QCOMPARE(tab.inputBitsForTest(), quint8(0x15));
        model.ioBoardMutable().setRegisterValue(IoBoardHl2::Register::REG_INPUT_PINS, 0x00);
        QCOMPARE(tab.inputBitsForTest(), quint8(0));
    }

    // Write button stays disabled until BOTH chkI2CEnable and write-enable
    // gates are checked.  Default state: both unchecked → button disabled.
    void i2c_write_button_default_disabled()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        QVERIFY(!tab.isI2cWriteEnabledForTest());
    }
};

QTEST_MAIN(TestHl2OptionsTab)
#include "tst_hl2_options_tab.moc"
