// no-port-check: smoke test for Hl2OptionsTab + Hl2OptionsModel UI/state
// wiring.  Phase 3L commit #9.  Cite comments to mi0bot setup.designer.cs
// are documentary only — the ported logic lives in the .cpp where the
// attribution header + PROVENANCE row already cover it.
// 2026-10-09: the I/O Pin State strips lay their LEDs out evenly from the
// strip's left edge after a DisplayBits change. J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.

#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>
#include <QSignalSpy>

#include "GridHeaderAlignment.h"
#include "core/AppSettings.h"
#include "core/Hl2OptionsModel.h"
#include "core/IoBoardHl2.h"
#include "gui/setup/hardware/Hl2OptionsTab.h"
#include "gui/widgets/OcLedStripWidget.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestHl2OptionsTab : public QObject {
    Q_OBJECT

private:
    // The visible LED frames of a strip, left to right.
    static QList<QFrame*> visibleLeds(OcLedStripWidget* strip)
    {
        QList<QFrame*> leds;
        for (QFrame* f : strip->findChildren<QFrame*>(Qt::FindDirectChildrenOnly)) {
            if (f->isVisible()) {
                leds.append(f);
            }
        }
        std::sort(leds.begin(), leds.end(), [](QFrame* a, QFrame* b) {
            return a->x() < b->x();
        });
        return leds;
    }

    // The LEDs start at the strip's left edge and sit one spacing apart.
    static void checkEvenFromLeft(OcLedStripWidget* strip, int expected)
    {
        const QList<QFrame*> leds = visibleLeds(strip);
        QCOMPARE(leds.size(), expected);
        QCOMPARE(leds.first()->x(), 0);
        for (int i = 1; i < leds.size(); ++i) {
            QCOMPARE(leds[i]->x() - leds[i - 1]->x(), leds[0]->width() + 2);
        }
    }

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
        QCOMPARE(m.cl2FreqKHz(), Hl2OptionsModel::kDefaultCl2FreqKHz); // 116 MHz
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

        // CL2 freq above max clamps to 200 MHz.
        m.setCl2FreqKHz(500000);
        QCOMPARE(m.cl2FreqKHz(), Hl2OptionsModel::kCl2FreqMaxKHz);  // 200 MHz
        // Below min clamps to 1 MHz.
        m.setCl2FreqKHz(0);
        QCOMPARE(m.cl2FreqKHz(), Hl2OptionsModel::kCl2FreqMinKHz);  // 1 MHz

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
            writer.setCl2FreqKHz(50000);
        }

        Hl2OptionsModel reader;
        reader.setMacAddress(mac);
        reader.load();
        QVERIFY(reader.swapAudioChannels());
        QCOMPARE(reader.pttHangMs(), 25);
        QCOMPARE(reader.cl2FreqKHz(), 50000);
    }

    // The three clock options persist per MAC, and a stored frequency
    // outside mi0bot's 1..200 MHz is clamped when loaded.
    void clock_options_persist_per_mac_and_clamp_on_load()
    {
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:fd");
        auto& s = AppSettings::instance();
        s.clearHardwareValues(mac);
        {
            Hl2OptionsModel writer;
            writer.setMacAddress(mac);
            writer.load();
            writer.setCl2Enabled(true);
            writer.setCl2FreqKHz(10700);
            writer.setExt10MHz(true);
        }
        QCOMPARE(s.hardwareValue(mac, QStringLiteral("hl2/cl2Enable")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.hardwareValue(mac, QStringLiteral("hl2/ext10MHz")).toString(),
                 QStringLiteral("True"));
        Hl2OptionsModel reader;
        reader.setMacAddress(mac);
        reader.load();
        QVERIFY(reader.cl2Enabled());
        QCOMPARE(reader.cl2FreqKHz(), 10700);
        QVERIFY(reader.ext10MHz());

        // Another radio keeps its own (default) options.
        Hl2OptionsModel other;
        other.setMacAddress(QStringLiteral("aa:bb:cc:dd:ee:fc"));
        other.load();
        QVERIFY(!other.cl2Enabled());
        QCOMPARE(other.cl2FreqKHz(), Hl2OptionsModel::kDefaultCl2FreqKHz);
        QVERIFY(!other.ext10MHz());

        s.setHardwareValue(mac, QStringLiteral("hl2/cl2FreqMHz"), 999);
        reader.load();
        QCOMPARE(reader.cl2FreqKHz(), Hl2OptionsModel::kCl2FreqMaxKHz);
        s.setHardwareValue(mac, QStringLiteral("hl2/cl2FreqMHz"), -4);
        reader.load();
        QCOMPARE(reader.cl2FreqKHz(), Hl2OptionsModel::kCl2FreqMinKHz);

        // A value that is not a number loads the default, not the minimum.
        for (const char* bad : {"abc", "nan", "inf", ""}) {
            s.setHardwareValue(mac, QStringLiteral("hl2/cl2FreqMHz"), QString::fromLatin1(bad));
            reader.load();
            QCOMPARE(reader.cl2FreqKHz(), Hl2OptionsModel::kDefaultCl2FreqKHz);
        }
        s.clearHardwareValues(mac);
    }

    // The frequency is stored as decimal text in MHz, three places, as
    // mi0bot's udCl2Freq holds it (setup.designer.cs:11133-11163
    // [@c26a8a4]); an older whole-number value still loads.
    void cl2_frequency_stored_as_decimal_text()
    {
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:fb");
        auto& s = AppSettings::instance();
        s.clearHardwareValues(mac);
        {
            Hl2OptionsModel writer;
            writer.setMacAddress(mac);
            writer.load();
            writer.setCl2FreqKHz(24576);
        }
        QCOMPARE(s.hardwareValue(mac, QStringLiteral("hl2/cl2FreqMHz")).toString(),
                 QStringLiteral("24.576"));
        Hl2OptionsModel reader;
        reader.setMacAddress(mac);
        reader.load();
        QCOMPARE(reader.cl2FreqKHz(), 24576);

        s.setHardwareValue(mac, QStringLiteral("hl2/cl2FreqMHz"), QStringLiteral("50"));
        reader.load();
        QCOMPARE(reader.cl2FreqKHz(), 50000);

        int kHz = 0;
        QVERIFY(Hl2OptionsModel::parseCl2FreqMHz(QStringLiteral("10.7"), &kHz));
        QCOMPARE(kHz, 10700);
        QVERIFY(!Hl2OptionsModel::parseCl2FreqMHz(QStringLiteral("nan"), &kHz));
        QVERIFY(!Hl2OptionsModel::parseCl2FreqMHz(QStringLiteral("abc"), &kHz));
        QCOMPARE(Hl2OptionsModel::cl2FreqMHzText(116000), QStringLiteral("116"));
        QCOMPARE(Hl2OptionsModel::cl2FreqMHzText(10700), QStringLiteral("10.7"));
        s.clearHardwareValues(mac);
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

    // The four I2C read boxes show their "--" in both looks: today a white
    // field with black text, the workspace look its dark value chip with
    // light text (the look drew the chip with the black text, so the boxes
    // read as black squares).
    void i2c_read_boxes_show_their_text_in_both_looks_data()
    {
        QTest::addColumn<bool>("workspace");
        QTest::newRow("today") << false;
        QTest::newRow("look") << true;
    }
    void i2c_read_boxes_show_their_text_in_both_looks()
    {
        QFETCH(bool, workspace);
        GridHeaderAlignment::AppLook look(workspace);
        RadioModel model;
        Hl2OptionsTab tab(&model);
        look.apply(&tab);
        tab.resize(1200, 800);
        tab.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tab));
        int boxes = 0;
        for (QLabel* label : tab.findChildren<QLabel*>()) {
            if (!label->toolTip().startsWith(QStringLiteral("Data at Reg/Ctrl"))) {
                continue;
            }
            ++boxes;
            QCOMPARE(label->text(), QStringLiteral("--"));
            const QImage image = label->grab().toImage();
            // The field: a pixel inside the border, clear of the centred
            // "--". The text: the pixel that differs most from it.
            const int field = image.pixelColor(3, 3).lightness();
            int contrast = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 2; x < image.width() - 2; ++x) {
                    contrast = std::max(contrast, std::abs(image.pixelColor(x, y).lightness() - field));
                }
            }
            QVERIFY2(contrast > 120,
                     qPrintable(QStringLiteral("%1: field lightness %2, text contrast %3, sheet \"%4\"")
                                    .arg(label->toolTip()).arg(field).arg(contrast)
                                    .arg(label->styleSheet())));
            if (!workspace) {
                QVERIFY2(field > 200, qPrintable(label->toolTip() + QStringLiteral(": today's field is white")));
            }
        }
        QCOMPARE(boxes, 4);
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

    // Swap audio channels reaches the radio (the radio codec lane sends the
    // receive audio in P1's L/R bytes), so it is enabled with mi0bot's
    // tooltip (setup.designer.cs:11119 [@c26a8a4]). All nine options
    // reach the radio and are enabled, the clock options among them,
    // except that CL2 frequency follows Enable CL2 (mi0bot ControlCl2 sets
    // udCl2Freq.Enabled = enable, setup.cs:21694-21729 [@c26a8a4]). The
    // same tab is the remote window's.
    void stored_only_options_show_disabled_with_a_reason()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        auto* swap = tab.findChild<QWidget*>(QStringLiteral("hl2SwapAudioChannels"));
        QVERIFY(swap != nullptr);
        QVERIFY(swap->isEnabled());
        QCOMPARE(swap->toolTip(), QStringLiteral("Swap the audio channels sent to the HL2"));
        const QStringList live{
            QStringLiteral("hl2TxBufferLatency"), QStringLiteral("hl2PttHang"),
            QStringLiteral("hl2DisconnectReset"), QStringLiteral("hl2DisablePsSync"),
            QStringLiteral("hl2BandVolts"), QStringLiteral("hl2Cl2Enable"),
            QStringLiteral("hl2Ext10MHz")};
        for (const QString& name : live) {
            auto* w = tab.findChild<QWidget*>(name);
            QVERIFY2(w != nullptr, qPrintable(name));
            QVERIFY2(w->isEnabled(), qPrintable(name));
        }
    }

    // The clock options carry mi0bot's tooltips (setup.designer.cs:11158,
    // 11174, 11187 [@c26a8a4]), and a tick reaches the model, which the
    // Core sends to the radio. CL2 frequency is enabled only while
    // Enable CL2 is on, whichever side changes it.
    void clock_options_are_enabled_and_frequency_follows_cl2()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        auto* cl2 = tab.findChild<QCheckBox*>(QStringLiteral("hl2Cl2Enable"));
        auto* freq = tab.findChild<QDoubleSpinBox*>(QStringLiteral("hl2Cl2Freq"));
        auto* ext = tab.findChild<QCheckBox*>(QStringLiteral("hl2Ext10MHz"));
        QVERIFY(cl2 != nullptr && freq != nullptr && ext != nullptr);
        QCOMPARE(cl2->toolTip(), QStringLiteral("Enable frequency output on CL2"));
        QCOMPARE(freq->toolTip(), QStringLiteral("Output frequency on CL2 output"));
        QCOMPARE(ext->toolTip(), QStringLiteral("Enable external 10 MHz input on CL1"));
        QCOMPARE(freq->minimum(), 1.0);
        QCOMPARE(freq->maximum(), 200.0);
        QCOMPARE(freq->value(), 116.0);
        // mi0bot udCl2Freq: DecimalPlaces 3, Increment 0.1
        // (setup.designer.cs:11133-11163 [@c26a8a4]).
        QCOMPARE(freq->decimals(), 3);
        QCOMPARE(freq->singleStep(), 0.1);
        // The value is taken when the edit is finished, not per keystroke.
        QVERIFY(!freq->keyboardTracking());

        QVERIFY(!freq->isEnabled());
        cl2->setChecked(true);
        QVERIFY(model.hl2Options().cl2Enabled());
        QVERIFY(freq->isEnabled());
        freq->setValue(24.576);
        QCOMPARE(model.hl2Options().cl2FreqKHz(), 24576);
        model.hl2OptionsMutable().setCl2FreqKHz(10700);
        QCOMPARE(freq->value(), 10.7);
        cl2->setChecked(false);
        QVERIFY(!model.hl2Options().cl2Enabled());
        QVERIFY(!freq->isEnabled());

        // A change that arrives through the model (a remote window's edit
        // reloaded on the Core, or a restore) moves the gate too.
        model.hl2OptionsMutable().setCl2Enabled(true);
        QVERIFY(cl2->isChecked());
        QVERIFY(freq->isEnabled());

        ext->setChecked(true);
        QVERIFY(model.hl2Options().ext10MHz());
    }

    // A remote window of a Core that stores the clock options without
    // sending them shows all three disabled with that Core's reason; the
    // frequency box stays disabled even with CL2 on. Back on a Core that
    // sends them, each shows its own tooltip again.
    void clock_options_follow_the_cores_offer()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        model.hl2OptionsMutable().setCl2Enabled(true);
        const QString reason = QStringLiteral("This Core cannot change its radio's clock settings.");
        tab.setClockControlAvailable(false, reason);
        for (const char* name : {"hl2Cl2Enable", "hl2Cl2Freq", "hl2Ext10MHz"}) {
            auto* w = tab.findChild<QWidget*>(QLatin1String(name));
            QVERIFY2(w != nullptr, name);
            QVERIFY2(!w->isEnabled(), name);
            QCOMPARE(w->toolTip(), reason);
        }
        tab.setClockControlAvailable(true, reason);
        QVERIFY(tab.findChild<QWidget*>(QStringLiteral("hl2Cl2Enable"))->isEnabled());
        QVERIFY(tab.findChild<QWidget*>(QStringLiteral("hl2Cl2Freq"))->isEnabled());
        QVERIFY(tab.findChild<QWidget*>(QStringLiteral("hl2Ext10MHz"))->isEnabled());
        QCOMPARE(tab.findChild<QWidget*>(QStringLiteral("hl2Cl2Freq"))->toolTip(),
                 QStringLiteral("Output frequency on CL2 output"));
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

    // Changing DisplayBits leaves no stale LEDs and no second stretch
    // ahead of the new LEDs: they start at the left edge, evenly spaced,
    // as mi0bot's ucOCLedStrip draws its pins from x = 0 in equal steps
    // (x = nPin * 16, ucOCLedStrip.cs:94-98 [@c26a8a4]).
    void led_strip_redraws_evenly_after_display_bits_change()
    {
        OcLedStripWidget strip;
        strip.resize(300, 12);
        strip.show();
        QCoreApplication::processEvents();
        checkEvenFromLeft(&strip, 8);
        if (QTest::currentTestFailed()) { return; }
        strip.setDisplayBits(6);
        QCoreApplication::processEvents();
        checkEvenFromLeft(&strip, 6);
        if (QTest::currentTestFailed()) { return; }
        strip.setDisplayBits(8);
        QCoreApplication::processEvents();
        checkEvenFromLeft(&strip, 8);
        if (QTest::currentTestFailed()) { return; }
    }

    // The HL2 input strip (6 pins, set after the strip was built with 8)
    // starts at its left edge like the output strip beneath it.
    void input_and_output_strips_start_at_the_same_x()
    {
        RadioModel model;
        Hl2OptionsTab tab(&model);
        tab.resize(900, 650);
        tab.show();
        QCoreApplication::processEvents();
        const QList<OcLedStripWidget*> strips = tab.findChildren<OcLedStripWidget*>();
        QCOMPARE(strips.size(), 2);
        for (OcLedStripWidget* strip : strips) {
            checkEvenFromLeft(strip, strip->displayBits());
            if (QTest::currentTestFailed()) { return; }
        }
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
