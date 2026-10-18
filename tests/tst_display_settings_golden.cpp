// =================================================================
// tests/tst_display_settings_golden.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure for the
// NereusSDR-original DisplaySettingsModel persistence migration (3D
// Stacked-Trace Spectrum Plan Task 23). See src/models/
// DisplaySettingsModel.h and src/gui/SpectrumWidget.cpp's
// loadSettings()/saveSettings() for the code this file pins.
//
// GOLDEN TEST: this file exists to catch a persistence FORMAT
// regression the moment SpectrumWidget::loadSettings()/saveSettings()
// stop reading and writing the fourteen per-pan display keys directly
// and start routing them through DisplaySettingsModel::load()/save()
// instead (Task 23). It is committed FIRST, alone, green against the
// pre-refactor code (Tasks 18-22's direct-read/direct-write shape), so
// the refactor commit that follows can be judged against it: if this
// file still passes UNCHANGED after the refactor, the on-disk format
// -- key names, per-pan "_<N>" suffixing, "True"/"False" booleans, and
// the exact numeric string every float round-trips to -- has not moved
// by a single byte.
//
// Every expected string below was read off a real saveSettings() run
// (a QCOMPARE mismatch's "Actual"/"Expected" output on the pre-refactor
// code), not computed by reasoning about QString::number()'s formatting
// rules -- see the task report for the read-don't-reason note this
// follows.
//
// Isolation: AppSettings::instance() is a process-wide in-memory
// singleton with no automatic load()/save() in this test process
// (TestSandboxInit.cpp also sandboxes QStandardPaths so nothing here
// can reach the developer's real ~/.config/NereusSDR/NereusSDR.settings).
// init()/cleanup() clear it before and after every slot, matching
// tst_dss_persistence.cpp and tst_display_settings_model.cpp, so no
// test's writes can leak into another's.
//
// Display pill plan Task 1 (2026-10-10): WHERE the strings are stored
// moved; the strings did not. The bare key is the Setup default and a
// pan's own value lives in <base>_<pan>, pan 0 included, written only
// when it differs from what the pan would inherit. Two cases pinned the
// old placement and were rewritten to the new one, keeping every expected
// string: saveDuringMox_preservesSelectedGridsAndSpans (the receive grid
// and both spans are unchanged by the keyed save, so no key is written
// for them and the bare keys still hold them) and
// fourteenKeys_byteForByte_pan0OwnKeysThenPan2InheritsDefaultThenOwnSave
// (was ..._pan0ThenPan2InheritsThenOwnSave: pan 0 now writes "_0", and
// pan 2 inherits the Setup default instead of pan 0).
//
// Display pill plan Task 3 (2026-10-10): the waterfall Black Level and
// Color Gain ship defaults moved from 104 and 45 to 57 and 60 and Black
// Level is 0 to 100. No expected string in the existing cases changed (62
// and 77 are in range and are not defaults under either model); the two
// "default" comments were corrected and
// blackLevelAndColorGain_shipDefaultsAre57And60 is new.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"
#include "gui/SpectrumWidget.h"
#include "models/DisplaySettingsModel.h"

using namespace NereusSDR;

class TestDisplaySettingsGolden : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void saveDuringMox_preservesSelectedGridsAndSpans_data()
    {
        QTest::addColumn<bool>("duplex");
        QTest::addColumn<int>("pan");
        QTest::newRow("pan0-tx-axis") << false << 0;
        QTest::newRow("pan0-rx-axis") << true << 0;
        QTest::newRow("pan2-inherited-tx-axis") << false << 2;
        QTest::newRow("pan2-inherited-rx-axis") << true << 2;
    }
    void saveDuringMox_preservesSelectedGridsAndSpans()
    {
        QFETCH(bool, duplex);
        QFETCH(int, pan);
        auto& s = AppSettings::instance();
        s.setValue("DisplayBandwidth", "192000");
        s.setValue("DisplayTxGridRefLevel", "123.5");
        s.setValue("DisplayTxGridDynamicRange", "91");
        s.setValue("DisplayTxViewBandwidth", "12500");
        SpectrumWidget w;
        w.setPanIndex(pan);
        w.loadSettings();
        // Display pill plan Task 9: the receive range is the band store's
        // (PanadapterModel through PanModels), set here the way that store
        // sets it. No pan key holds it, so no save, keyed or not, can
        // write the transmit grid into it.
        w.setReceiveDbmRange(-116.5f, -43.5f);
        const auto key = [pan](const QString& base) {
            return PanDisplayKeys::panKey(base, pan);
        };
        const auto stored = [pan](const QString& base) {
            return PanDisplayKeys::read(base, pan, QString()).toString();
        };
        QVERIFY(!s.contains(key("DisplayGridMax")));
        w.setDisplayDuplex(duplex);
        w.setMoxOverlay(true);
        QCOMPARE(w.refLevel(), 123.5f);
        w.setDbmRange(34.5f, 125.5f);
        w.displaySettings()->setDssGain(31);
        w.saveSettings();
        QCOMPARE(w.displaySettings()->refLevel(), 125.5f);
        QVERIFY(!s.contains("DisplayGridMax"));
        QVERIFY(!s.contains("DisplayGridMin"));
        QCOMPARE(stored("DisplayBandwidth"), QStringLiteral("192000"));
        QCOMPARE(stored("DisplayTxGridRefLevel"), QStringLiteral("125.5"));
        QCOMPARE(stored("DisplayTxGridDynamicRange"), QStringLiteral("91"));
        QCOMPARE(stored("DisplayTxViewBandwidth"), QStringLiteral("12500"));
        // The transmit scale is one computer-wide pair. A keyed save must
        // neither recreate historical numbered TX-scale keys nor leak it
        // into the receive scale.
        QCOMPARE(s.value("DisplayTxGridRefLevel").toString(), QStringLiteral("125.5"));
        QCOMPARE(s.value("DisplayTxGridDynamicRange").toString(), QStringLiteral("91"));
        QVERIFY(!s.contains(key("DisplayTxGridRefLevel")));
        QVERIFY(!s.contains(key("DisplayGridMax")));
        QVERIFY(!s.contains(key("DisplayGridMin")));
        QVERIFY(!s.contains(key("DisplayBandwidth")));
        QVERIFY(!s.contains(key("DisplayTxGridDynamicRange")));
        QVERIFY(!s.contains(key("DisplayTxViewBandwidth")));
        // A keyed DUP toggle changes span ownership without changing grid ownership.
        w.setDisplayDuplex(!duplex);
        w.saveSettings();
        QCOMPARE(stored("DisplayBandwidth"), QStringLiteral("192000"));
        QCOMPARE(stored("DisplayTxViewBandwidth"), QStringLiteral("12500"));
        QVERIFY(!s.contains(key("DisplayBandwidth")));
        QVERIFY(!s.contains(key("DisplayTxViewBandwidth")));
        QCOMPARE(s.value("DisplayTxGridRefLevel").toString(), QStringLiteral("125.5"));
        QVERIFY(!s.contains("DisplayGridMax"));
        QVERIFY(!s.contains("DisplayGridMin"));
        QVERIFY(!s.contains(key("DisplayGridMax")));
        QVERIFY(!s.contains(key("DisplayGridMin")));
        // Another pan sees the shared TX scale and keeps an independent
        // receive range when it leaves the transmit overlay.
        {
            SpectrumWidget other;
            other.setPanIndex(pan == 0 ? 2 : 0);
            other.loadSettings();
            other.setReceiveDbmRange(-118.5f, -41.5f);
            other.setMoxOverlay(true);
            QCOMPARE(other.refLevel(), 125.5f);
            QCOMPARE(other.dynamicRange(), 91.0f);
            other.saveSettings();
            other.setMoxOverlay(false);
            QCOMPARE(other.refLevel(), -41.5f);
            QCOMPARE(other.dynamicRange(), 77.0f);
        }
        SpectrumWidget restarted;
        restarted.setPanIndex(pan);
        restarted.loadSettings();
        // The widget's own default until the band store gives it a range.
        QCOMPARE(restarted.refLevel(), -48.0f);
        QCOMPARE(restarted.dynamicRange(), 68.0f);
        QCOMPARE(restarted.bandwidth(), 192000.0);
        restarted.setDisplayDuplex(false);
        restarted.setMoxOverlay(true);
        QCOMPARE(restarted.refLevel(), 125.5f);
        QCOMPARE(restarted.dynamicRange(), 91.0f);
        QCOMPARE(restarted.bandwidth(), 12500.0);
        restarted.setMoxOverlay(false);
        QCOMPARE(restarted.refLevel(), -48.0f);
        QCOMPARE(restarted.bandwidth(), 192000.0);
    }

    // ============================================================
    // Task 23 Acceptance, verbatim: "It sets all fourteen values to
    // non-defaults through the widget's setters, calls saveSettings(),
    // and compares the stored string of every key (the fourteen exact
    // key names from the scout table, pan 0) with literal expected
    // strings; then it sets panIndex 2 with no own keys and proves
    // loadSettings() inherits pan 0's values; then it saves on pan 2
    // and proves the _2 suffixed keys hold pan 2's strings while pan
    // 0's are untouched."
    //
    // Display pill plan Task 1 moved the placement, not the strings: pan
    // 0's fourteen now land in "_0" keys and the bare keys stay unwritten;
    // pan 2 inherits the Setup default (the bare keys, written here the
    // way Setup will, with the same fourteen strings) rather than pan 0;
    // and pan 2's own save writes "_2" keys and leaves both the Setup
    // default and pan 0's keys alone.
    //
    // Catches: any key renamed, dropped, or moved off the per-pan
    // PanDisplayKeys convention; a float formatted
    // differently (e.g. "-30" silently becoming "-30.0" or "-30.000000")
    // by whichever object ends up writing it; Dyn Range's derived-key
    // shape (DisplayGridMin = refLevel - dynamicRange, no key of its
    // own) breaking; a boolean written as "1"/"0" instead of the
    // project's "True"/"False"; pan-0-fallback inheritance dropped; or
    // pan 2's save clobbering pan 0's keys instead of writing its own
    // "_2" suffixed ones.
    // ============================================================
    // Display pill plan Task 9 (2026-10-10): twelve, not fourteen. Ref
    // Level and Dyn Range (DisplayGridMax / DisplayGridMin) left the per-pan
    // keys: the receive range is stored per band (PanadapterModel), so the
    // widget and its model neither read nor write those two, bare or with
    // a pan suffix. The test sets both on each pan and proves no such key
    // appears and an untouched pan does not inherit them.
    // ============================================================
    // Display pill plan Task 3 (2026-10-10): the waterfall's Black Level
    // and Color Gain ship defaults are 57 and 60 (they were 104 and 45
    // under the level model before Clarity's). A pan that still shows them
    // writes no key, and one moved back to the OLD defaults does, because
    // 104 clamps to 100 and 45 is no longer the default.
    //
    // Catches: either default left at its old value in the widget or the
    // model (the two must agree or an untouched pan writes a key), and the
    // Black Level range left at 0 to 125.
    void blackLevelAndColorGain_shipDefaultsAre57And60()
    {
        auto& s = AppSettings::instance();
        SpectrumWidget w;
        w.setPanIndex(0);
        QCOMPARE(w.wfBlackLevel(), 57);
        QCOMPARE(w.wfColorGain(), 60);
        w.loadSettings();
        QCOMPARE(w.wfBlackLevel(), 57);
        QCOMPARE(w.wfColorGain(), 60);
        QCOMPARE(w.displaySettings()->wfBlackLevel(), 57);
        QCOMPARE(w.displaySettings()->wfColorGain(), 60);
        w.saveSettings();
        QVERIFY(!s.contains(QStringLiteral("DisplayWfBlackLevel")));
        QVERIFY(!s.contains(QStringLiteral("DisplayWfBlackLevel_0")));
        QVERIFY(!s.contains(QStringLiteral("DisplayWfColorGain")));
        QVERIFY(!s.contains(QStringLiteral("DisplayWfColorGain_0")));

        w.setWfBlackLevel(104);
        w.setWfColorGain(45);
        w.saveSettings();
        QCOMPARE(s.value(QStringLiteral("DisplayWfBlackLevel_0")).toString(),
                 QStringLiteral("100"));
        QCOMPARE(s.value(QStringLiteral("DisplayWfColorGain_0")).toString(),
                 QStringLiteral("45"));
    }

    void twelveKeys_byteForByte_masterClearsPan0ThenPan2InheritsAndSavesOwn()
    {
        // ---- Phase 1: pan 0, all twelve to non-defaults, byte for byte ----
        SpectrumWidget w0;
        w0.setPanIndex(0);

        w0.setWfColorScheme(WfColorScheme::Spectran);    // 2, default Default(0)
        w0.setWfColorGain(62);                           // default 60 (was 45)
        w0.setWfBlackLevel(77);                          // default 57 (was 104)
        w0.setRefLevel(-30.0f);                          // live only (Task 9)
        w0.setDynamicRange(90.0f);                       // live only (Task 9)
        w0.setFillAlpha(0.35f);                          // default 0.70
        w0.setPanFillEnabled(false);                     // default true
        w0.setSpectrumFrac(0.62f);                       // default 0.40
        w0.setSpectrumRenderMode(                        // default Mode2D(0)
            static_cast<int>(SpectrumRenderMode::Mode3D));
        w0.setDssGain(55);                               // default 70
        w0.setDssRowSpan(65);                            // default 100
        w0.setDssAngle(82);                              // default 50
        w0.setDssRowDivider(7);                          // default 0 (Match)
        w0.setThreeDSliceDepth(true);                    // default false

        w0.saveSettings();

        auto& s = AppSettings::instance();
        // The twelve and the exact string each stores.
        struct Golden { QString base; QString text; };
        const QList<Golden> pan0Golden = {
            { QStringLiteral("DisplayWfColorScheme"), QStringLiteral("2") },
            { QStringLiteral("DisplayWfColorGain"), QStringLiteral("62") },
            { QStringLiteral("DisplayWfBlackLevel"), QStringLiteral("77") },
            { QStringLiteral("DisplayFftFillAlpha"), QStringLiteral("0.35") },
            { QStringLiteral("DisplayPanFill"), QStringLiteral("False") },
            { QStringLiteral("DisplaySpectrumFrac"), QStringLiteral("0.62") },
            { QStringLiteral("DisplaySpectrumRenderMode"), QStringLiteral("1") },
            { QStringLiteral("Display3DGain"), QStringLiteral("55") },
            { QStringLiteral("Display3DSpan"), QStringLiteral("65") },
            { QStringLiteral("Display3DAngle"), QStringLiteral("82") },
            { QStringLiteral("Display3DSpeed"), QStringLiteral("7") },
            { QStringLiteral("Display3DSliceShadow"), QStringLiteral("True") },
        };
        for (const Golden& g : pan0Golden) {
            QCOMPARE(s.value(g.base + QStringLiteral("_0")).toString(), g.text);
            // Pan 0 is a pan: it never writes the Setup default.
            QVERIFY2(!s.contains(g.base), qPrintable(g.base));
        }
        // Task 9: no range key from pan 0's save.
        for (const char* gone : { "DisplayGridMax", "DisplayGridMin",
                                  "DisplayGridMax_0", "DisplayGridMin_0" }) {
            QVERIFY2(!s.contains(QLatin1String(gone)), gone);
        }

        // The Setup default, holding the same twelve strings.
        for (const Golden& g : pan0Golden) {
            PanDisplayKeys::writeDefault(g.base, g.text);
            QVERIFY2(!s.contains(g.base + QStringLiteral("_0")), qPrintable(g.base));
        }

        SpectrumWidget inheritedPan0;
        inheritedPan0.setPanIndex(0);
        inheritedPan0.loadSettings();
        QCOMPARE(static_cast<int>(inheritedPan0.wfColorScheme()), 2);
        QCOMPARE(inheritedPan0.wfColorGain(), 62);
        QCOMPARE(inheritedPan0.wfBlackLevel(), 77);
        QVERIFY(qFuzzyCompare(inheritedPan0.fillAlpha() + 1.0f, 1.35f));
        QCOMPARE(inheritedPan0.panFillEnabled(), false);
        QVERIFY(qFuzzyCompare(inheritedPan0.spectrumFrac() + 1.0f, 1.62f));
        QCOMPARE(inheritedPan0.spectrumRenderMode(), static_cast<int>(SpectrumRenderMode::Mode3D));
        QCOMPARE(inheritedPan0.dssGain(), 55);
        QCOMPARE(inheritedPan0.dssRowSpan(), 65);
        QCOMPARE(inheritedPan0.dssAngle(), 82);
        QCOMPARE(inheritedPan0.dssRowDivider(), 7);
        QCOMPARE(inheritedPan0.threeDSliceDepth(), true);

        // ---- Phase 2: an untouched pan 2 inherits the Setup default ----
        SpectrumWidget w2;
        w2.setPanIndex(2);
        w2.loadSettings();

        QCOMPARE(static_cast<int>(w2.wfColorScheme()), 2);
        QCOMPARE(w2.wfColorGain(), 62);
        QCOMPARE(w2.wfBlackLevel(), 77);
        // Task 9: the range is not inherited; the widget's own default.
        QVERIFY(qFuzzyCompare(w2.refLevel() + 1.0f, -48.0f + 1.0f));
        QVERIFY(qFuzzyCompare(w2.dynamicRange() + 1.0f, 68.0f + 1.0f));
        QVERIFY(qFuzzyCompare(w2.fillAlpha() + 1.0f, 0.35f + 1.0f));
        QCOMPARE(w2.panFillEnabled(), false);
        QVERIFY(qFuzzyCompare(w2.spectrumFrac() + 1.0f, 0.62f + 1.0f));
        QCOMPARE(w2.spectrumRenderMode(), static_cast<int>(SpectrumRenderMode::Mode3D));
        QCOMPARE(w2.dssGain(), 55);
        QCOMPARE(w2.dssRowSpan(), 65);
        QCOMPARE(w2.dssAngle(), 82);
        QCOMPARE(w2.dssRowDivider(), 7);
        QCOMPARE(w2.threeDSliceDepth(), true);

        // ---- Phase 3: pan 2 saves its OWN, different, non-default values ----
        w2.setWfColorScheme(WfColorScheme::LinRad);      // 5
        w2.setWfColorGain(20);
        w2.setWfBlackLevel(15);
        w2.setRefLevel(-60.0f);
        w2.setDynamicRange(40.0f);                        // live only (Task 9)
        w2.setFillAlpha(0.85f);
        w2.setPanFillEnabled(true);
        w2.setSpectrumFrac(0.18f);
        w2.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode2D));
        w2.setDssGain(88);
        w2.setDssRowSpan(30);
        w2.setDssAngle(19);
        w2.setDssRowDivider(3);
        w2.setThreeDSliceDepth(false);

        w2.saveSettings();

        QCOMPARE(s.value(QStringLiteral("DisplayWfColorScheme_2")).toString(),
                 QStringLiteral("5"));
        QCOMPARE(s.value(QStringLiteral("DisplayWfColorGain_2")).toString(),
                 QStringLiteral("20"));
        QCOMPARE(s.value(QStringLiteral("DisplayWfBlackLevel_2")).toString(),
                 QStringLiteral("15"));
        QVERIFY(!s.contains(QStringLiteral("DisplayGridMax_2")));
        QVERIFY(!s.contains(QStringLiteral("DisplayGridMin_2")));
        QVERIFY(!s.contains(QStringLiteral("DisplayGridMax")));
        QVERIFY(!s.contains(QStringLiteral("DisplayGridMin")));
        QCOMPARE(s.value(QStringLiteral("DisplayFftFillAlpha_2")).toString(),
                 QStringLiteral("0.85"));
        QCOMPARE(s.value(QStringLiteral("DisplayPanFill_2")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("DisplaySpectrumFrac_2")).toString(),
                 QStringLiteral("0.18"));
        QCOMPARE(s.value(QStringLiteral("DisplaySpectrumRenderMode_2")).toString(),
                 QStringLiteral("0"));
        QCOMPARE(s.value(QStringLiteral("Display3DGain_2")).toString(),
                 QStringLiteral("88"));
        QCOMPARE(s.value(QStringLiteral("Display3DSpan_2")).toString(),
                 QStringLiteral("30"));
        QCOMPARE(s.value(QStringLiteral("Display3DAngle_2")).toString(),
                 QStringLiteral("19"));
        QCOMPARE(s.value(QStringLiteral("Display3DSpeed_2")).toString(),
                 QStringLiteral("3"));
        QCOMPARE(s.value(QStringLiteral("Display3DSliceShadow_2")).toString(),
                 QStringLiteral("False"));

        // Pan 2's save leaves the master intact and does not recreate the
        // pan 0 overrides that the explicit master write retired.
        for (const Golden& g : pan0Golden) {
            QCOMPARE(s.value(g.base).toString(), g.text);
            QVERIFY2(!s.contains(g.base + QStringLiteral("_0")), qPrintable(g.base));
        }
    }

    // ============================================================
    // Task 23 Acceptance, verbatim: "loadSettings() schedules no save
    // and performs no apply (displaySettingsApplyCountForTest() stays
    // 0), and the model holds the loaded values."
    //
    // Seeded with genuinely NON-default values -- unlike
    // tst_display_settings_binding.cpp's noApplyDuringLoad, which
    // starts from an empty AppSettings and so never actually exercises
    // a value CHANGE during load. With real seeded values,
    // DisplaySettingsModel::load()'s internal setters genuinely differ
    // from the model's just-constructed ship defaults; if this widget's
    // model were not signal-blocked while loading, those changes would,
    // through bindDisplaySettings()'s model-to-widget connections, fire
    // straight back into this widget's own appliers (setRefLevel(),
    // setWfColorGain(), ...), each of which calls update(),
    // scheduleSettingsSave() and increments m_displaySettingsApplyCount.
    //
    // Catches: loadSettings()'s model-load path routed through the
    // widget's OWN setters (directly, or indirectly via an unblocked
    // model-to-widget signal) instead of a direct member copy.
    // ============================================================
    void loadSettings_seedsTheModelWithoutApplyingOrScheduling()
    {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("DisplayWfColorScheme"), QStringLiteral("2"));
        s.setValue(QStringLiteral("DisplayWfColorGain"), QStringLiteral("62"));
        s.setValue(QStringLiteral("DisplayWfBlackLevel"), QStringLiteral("77"));
        s.setValue(QStringLiteral("DisplayGridMax"), QStringLiteral("-30"));
        s.setValue(QStringLiteral("DisplayGridMin"), QStringLiteral("-120"));
        s.setValue(QStringLiteral("DisplayFftFillAlpha"), QStringLiteral("0.35"));
        s.setValue(QStringLiteral("DisplayPanFill"), QStringLiteral("False"));
        s.setValue(QStringLiteral("DisplaySpectrumFrac"), QStringLiteral("0.62"));
        s.setValue(QStringLiteral("DisplaySpectrumRenderMode"), QStringLiteral("1"));
        s.setValue(QStringLiteral("Display3DGain"), QStringLiteral("55"));
        s.setValue(QStringLiteral("Display3DSpan"), QStringLiteral("65"));
        s.setValue(QStringLiteral("Display3DAngle"), QStringLiteral("82"));
        s.setValue(QStringLiteral("Display3DSpeed"), QStringLiteral("7"));
        s.setValue(QStringLiteral("Display3DSliceShadow"), QStringLiteral("True"));

        SpectrumWidget w;
        w.setPanIndex(0);
        w.loadSettings();

        QCOMPARE(w.displaySettingsApplyCountForTest(), 0);

        DisplaySettingsModel* m = w.displaySettings();
        QCOMPARE(m->wfColorScheme(), 2);
        QCOMPARE(m->wfColorGain(), 62);
        QCOMPARE(m->wfBlackLevel(), 77);
        // Task 9: the two range keys seeded above are an old install's and
        // are not read.
        QVERIFY(qFuzzyCompare(m->refLevel() + 1.0f, -48.0f + 1.0f));
        QVERIFY(qFuzzyCompare(m->dynamicRange() + 1.0f, 68.0f + 1.0f));
        QVERIFY(qFuzzyCompare(m->fillAlpha() + 1.0f, 0.35f + 1.0f));
        QCOMPARE(m->panFill(), false);
        QVERIFY(qFuzzyCompare(m->spectrumFrac() + 1.0f, 0.62f + 1.0f));
        QCOMPARE(m->spectrumRenderMode(), 1);
        QCOMPARE(m->dssGain(), 55);
        QCOMPARE(m->dssRowSpan(), 65);
        QCOMPARE(m->dssAngle(), 82);
        QCOMPARE(m->dssRowDivider(), 7);
        QCOMPARE(m->threeDSliceDepth(), true);
    }
};

QTEST_MAIN(TestDisplaySettingsGolden)
#include "tst_display_settings_golden.moc"
