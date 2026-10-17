// =================================================================
// tests/tst_display_settings_model.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure for the
// NereusSDR-original DisplaySettingsModel (3D Stacked-Trace Spectrum
// Plan Task 17). See src/models/DisplaySettingsModel.h for the design
// note this file pins.
//
// 2026-10-12 - Align stale ownership assertions with accepted master/equality
// semantics and exercise the full twelve-key lifecycle. J.J. Boyd (KG4VCF),
// AI-assisted via OpenAI Codex.
//
// 2026-10-15 - Carry three checks from Combined K (bf1a3e5ee) onto the
// master/equality rule: a genuinely different own value survives an
// unchanged save, the twelve per-pan key names are pinned by name, and
// each saved value's stored text is pinned. J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.
//
// 2026-10-15 - The FFT fill alpha and spectrum fraction keys are pinned by
// their stored text ("0.23", "0.61"), not compared as numbers, as the
// stored-text check says. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
// Claude Code.
//
// Isolation: AppSettings::instance() is a process-wide in-memory
// singleton with no automatic load()/save() in this test process
// (TestSandboxInit.cpp also sandboxes QStandardPaths so nothing here
// can reach the developer's real ~/.config/NereusSDR/NereusSDR.settings).
// init()/cleanup() clear it before and after every slot, matching the
// established pattern in tst_dss_persistence.cpp, so no test's writes
// can leak into another's fresh-model round trip.
//
// Display pill plan Task 1 (2026-10-10): the bare key is the Setup
// default and a pan's own value lives in <base>_<pan>, pan 0 included,
// written only when it differs from what the pan would inherit. The cases
// that pinned "pan 0 writes the bare key", "every save writes every key"
// and "an untouched pan inherits pan 0" were rewritten to that:
// save_actuallyWritesAppSettings_notJustTheInMemoryCache,
// booleansPersistAsTrueFalseStrings,
// booleansPersistFalseAsTheLiteralFalseString,
// dssFloorDepth_untouchedByLoadAndSave,
// perPanFields_usePanIndexSuffixConvention,
// anUntouchedPan_inheritsTheSetupDefault (was ..._inheritsPanZerosValue)
// and setDssRowDivider_clampsGuardsEmitsAndRoundTrips. Three cases are
// new: save_isSparse_anUnchangedPanWritesNothing,
// save_clearsAnOverrideThatWasSetBackToTheInheritedValue and
// perPanKeyBases_isExactlyWhatSaveWrites.
//
// Display pill plan Task 3 (2026-10-10): waterfall Black Level is 0 to 100
// (was 0 to 125) and the Black Level and Color Gain ship defaults are 57
// and 60 (were 104 and 45). clampingHoldsAtBothEndsOfEveryRangedControl
// pinned the old upper end and was rewritten to 100;
// blackLevelAndColorGain_defaultTo57And60 is new.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"
#include "models/DisplaySettingsModel.h"

using namespace NereusSDR;

class TestDisplaySettingsModel : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // Display pill plan Task 3: the waterfall's Black Level and Color Gain
    // start at the positions that reproduce the look before the Clarity
    // level model (they were 104 and 45), before a load and after one with
    // nothing stored, and a pan left there writes no key.
    void blackLevelAndColorGain_defaultTo57And60()
    {
        DisplaySettingsModel m;
        QCOMPARE(m.wfBlackLevel(), 57);
        QCOMPARE(m.wfColorGain(), 60);
        m.setWfBlackLevel(10);
        m.setWfColorGain(10);
        m.load();
        QCOMPARE(m.wfBlackLevel(), 57);
        QCOMPARE(m.wfColorGain(), 60);
        m.save();
        QVERIFY2(AppSettings::instance().allKeys().isEmpty(),
                 qPrintable(AppSettings::instance().allKeys().join(QLatin1Char(' '))));
    }

    // ============================================================
    // Round trip: fresh instance, so a load() that silently does
    // nothing (leaving fresh-object ship defaults) cannot pass.
    // ============================================================

    // Catches: any of the thirteen per-pan fields missing from load()
    // or save(); a wrong key name; a swapped default. Two DISTINCT
    // instances (w writes, r reads) so the read can only succeed if
    // load() genuinely pulled the value back out of AppSettings -- a
    // fresh r starts from ship defaults, not from w's in-memory state.
    void allThirteenPerPanFields_roundTripThroughSaveAndLoad()
    {
        DisplaySettingsModel w;
        w.setPanIndex(0);
        w.setWfColorScheme(6);       // LinRad
        w.setWfColorGain(81);
        w.setWfBlackLevel(37);
        w.setRefLevel(-12.5f);
        w.setDynamicRange(96.0f);
        w.setFillAlpha(0.23f);
        w.setPanFill(false);
        w.setSpectrumFrac(0.61f);
        w.setSpectrumRenderMode(1);  // Mode3D
        w.setDssGain(19);
        w.setDssRowSpan(64);
        w.setDssAngle(88);
        w.setThreeDSliceDepth(true);
        w.save();

        DisplaySettingsModel r;
        r.setPanIndex(0);
        r.load();
        QCOMPARE(r.wfColorScheme(), 6);
        QCOMPARE(r.wfColorGain(), 81);
        QCOMPARE(r.wfBlackLevel(), 37);
        // Display pill plan Task 9 (S9): ref level and dynamic range are
        // live values, not persisted. The receive range is the band
        // store's (PanadapterModel), so a fresh model has its own defaults
        // and no DisplayGridMax / DisplayGridMin key was written.
        QVERIFY(qFuzzyCompare(r.refLevel() + 1.0f, -48.0f + 1.0f));
        QVERIFY(qFuzzyCompare(r.dynamicRange(), 68.0f));
        for (const char* key : { "DisplayGridMax", "DisplayGridMin",
                                 "DisplayGridMax_0", "DisplayGridMin_0" }) {
            QVERIFY2(!AppSettings::instance().contains(QLatin1String(key)), key);
        }
        QVERIFY(qFuzzyCompare(r.fillAlpha() + 1.0f, 0.23f + 1.0f));
        QCOMPARE(r.panFill(), false);
        QVERIFY(qFuzzyCompare(r.spectrumFrac(), 0.61f));
        QCOMPARE(r.spectrumRenderMode(), 1);
        QCOMPARE(r.dssGain(), 19);
        QCOMPARE(r.dssRowSpan(), 64);
        QCOMPARE(r.dssAngle(), 88);
        QCOMPARE(r.threeDSliceDepth(), true);
    }

    // Catches: save() never actually reaching AppSettings (in-memory
    // only) -- distinct from the test above because it inspects the
    // raw store directly rather than round-tripping through a second
    // object's getters, so a setter/getter pair that agreed with each
    // other but never touched AppSettings would still be caught.
    void save_actuallyWritesAppSettings_notJustTheInMemoryCache()
    {
        DisplaySettingsModel w;
        w.setPanIndex(0);
        w.setDssGain(55);
        QVERIFY(!AppSettings::instance().contains(QStringLiteral("Display3DGain_0")));
        w.save();
        QVERIFY(AppSettings::instance().contains(QStringLiteral("Display3DGain_0")));
        QCOMPARE(AppSettings::instance().value(QStringLiteral("Display3DGain_0")).toInt(), 55);
        // Pan 0 is a pan: its value goes to its own key, never the bare
        // key, which is the Setup default.
        QVERIFY(!AppSettings::instance().contains(QStringLiteral("Display3DGain")));
    }

    // Display pill plan Task 1. Catches: a save that writes every key
    // whether or not the pan differs from what it inherits, which would
    // freeze a copy of the Setup default into every pan at its first save
    // and stop the pan following a later change to the default.
    void save_isSparse_anUnchangedPanWritesNothing()
    {
        auto& s = AppSettings::instance();
        // Nothing stored at all: the model holds the ship defaults.
        DisplaySettingsModel fresh;
        fresh.setPanIndex(3);
        fresh.load();
        fresh.save();
        QVERIFY2(s.allKeys().isEmpty(), qPrintable(s.allKeys().join(QLatin1Char(' '))));

        // A Setup default the pan follows, spelled the way another writer
        // would ("0.7"-style short float text): still nothing to write.
        PanDisplayKeys::writeDefault(QStringLiteral("DisplayWfColorGain"), QStringLiteral("81"));
        PanDisplayKeys::writeDefault(QStringLiteral("DisplayFftFillAlpha"), QStringLiteral("0.23"));
        DisplaySettingsModel follower;
        follower.setPanIndex(3);
        follower.load();
        QCOMPARE(follower.wfColorGain(), 81);
        follower.save();
        QCOMPARE(s.allKeys().size(), 2);

        // One real change writes that one key and no other.
        follower.setWfColorGain(12);
        follower.save();
        QCOMPARE(s.value(QStringLiteral("DisplayWfColorGain_3")).toString(), QStringLiteral("12"));
        QCOMPARE(s.value(QStringLiteral("DisplayWfColorGain")).toString(), QStringLiteral("81"));
        QCOMPARE(s.allKeys().size(), 3);
    }

    // Approved master/equality rule (cb2f35e4a): saving the inherited
    // value removes an own key, so the pan follows later master changes.
    void save_clearsAnOverrideThatWasSetBackToTheInheritedValue()
    {
        auto& s = AppSettings::instance();
        const QString base = QStringLiteral("DisplayWfBlackLevel");
        const QString own = PanDisplayKeys::panKey(base, 2);
        PanDisplayKeys::writeDefault(base, QStringLiteral("60"));
        s.setValue(own, QStringLiteral("20"));

        DisplaySettingsModel pan2;
        pan2.setPanIndex(2);
        pan2.load();
        QCOMPARE(pan2.wfBlackLevel(), 20);
        // A genuinely different own value stays through an unchanged save.
        pan2.save();
        QCOMPARE(s.value(own).toString(), QStringLiteral("20"));
        pan2.setWfBlackLevel(60);
        pan2.save();
        QVERIFY(!s.contains(own));
        QCOMPARE(s.value(base).toString(), QStringLiteral("60"));

        PanDisplayKeys::writeDefault(base, QStringLiteral("73"));
        pan2.load();
        QCOMPARE(pan2.wfBlackLevel(), 73);
        pan2.save();
        QVERIFY(!s.contains(own));

        // A fresh object reads the resumed inheritance, and a later real
        // divergence creates an own key until an explicit master write.
        DisplaySettingsModel reopened;
        reopened.setPanIndex(2);
        reopened.load();
        QCOMPARE(reopened.wfBlackLevel(), 73);
        reopened.setWfBlackLevel(31);
        reopened.save();
        QCOMPARE(s.value(own).toString(), QStringLiteral("31"));
        PanDisplayKeys::writeDefault(base, QStringLiteral("82"));
        QVERIFY(!s.contains(own));
        reopened.load();
        QCOMPARE(reopened.wfBlackLevel(), 82);
        QCOMPARE(s.allKeys(), QStringList{base});
    }

    // Display pill plan Task 1. Catches: perPanKeyBases() naming a key
    // save() does not write, or missing one it does. A reset clears the
    // listed bases, so a key missing from the list would survive a reset
    // and one that is not written would never be found.
    void perPanKeyBases_isExactlyWhatSaveWrites()
    {
        auto& s = AppSettings::instance();
        DisplaySettingsModel m;
        m.setPanIndex(4);
        const QStringList bases = m.perPanKeyBases();
        QCOMPARE(bases.size(), 12);
        QCOMPARE(QSet<QString>(bases.cbegin(), bases.cend()).size(), 12);
        // Task 9: the receive range is per band, not this model's to store.
        QVERIFY(!bases.contains(QStringLiteral("DisplayGridMax")));
        QVERIFY(!bases.contains(QStringLiteral("DisplayGridMin")));
        QVERIFY(!bases.contains(QStringLiteral("Display3DFloorDepth")));
        // The inventory by name, so a renamed key cannot pass on count.
        const QSet<QString> expectedBases{
            QStringLiteral("DisplayWfColorScheme"), QStringLiteral("DisplayWfColorGain"),
            QStringLiteral("DisplayWfBlackLevel"), QStringLiteral("DisplayFftFillAlpha"),
            QStringLiteral("DisplayPanFill"), QStringLiteral("DisplaySpectrumFrac"),
            QStringLiteral("DisplaySpectrumRenderMode"), QStringLiteral("Display3DGain"),
            QStringLiteral("Display3DSpan"), QStringLiteral("Display3DAngle"),
            QStringLiteral("Display3DSpeed"), QStringLiteral("Display3DSliceShadow")};
        QCOMPARE(QSet<QString>(bases.cbegin(), bases.cend()), expectedBases);

        // Existing invalid own keys disappear when the saved value is
        // the ship default; equality must resume inheritance for all 12.
        for (const QString& base : bases) {
            s.setValue(PanDisplayKeys::panKey(base, 4), QStringLiteral("not-a-value"));
        }
        m.save();
        for (const QString& base : bases) {
            QVERIFY2(!s.contains(PanDisplayKeys::panKey(base, 4)), qPrintable(base));
        }
        QCOMPARE(s.allKeys().size(), 0);

        // Exercise every inventoried key with an actual differing value.
        m.setWfColorScheme(6);
        m.setWfColorGain(81);
        m.setWfBlackLevel(37);
        m.setFillAlpha(0.23f);
        m.setPanFill(false);
        m.setSpectrumFrac(0.61f);
        m.setSpectrumRenderMode(1);
        m.setDssGain(19);
        m.setDssRowSpan(64);
        m.setDssAngle(88);
        m.setDssRowDivider(3);
        m.setThreeDSliceDepth(true);
        m.save();
        QCOMPARE(s.allKeys().size(), 12);
        // The stored text itself, not only the model's own spelling of it:
        // booleans are "True"/"False" and numbers are plain decimal text.
        const QHash<QString, QString> expectedText{
            {QStringLiteral("DisplayWfColorScheme_4"), QStringLiteral("6")},
            {QStringLiteral("DisplayWfColorGain_4"), QStringLiteral("81")},
            {QStringLiteral("DisplayWfBlackLevel_4"), QStringLiteral("37")},
            {QStringLiteral("DisplayPanFill_4"), QStringLiteral("False")},
            {QStringLiteral("DisplaySpectrumRenderMode_4"), QStringLiteral("1")},
            {QStringLiteral("Display3DGain_4"), QStringLiteral("19")},
            {QStringLiteral("Display3DSpan_4"), QStringLiteral("64")},
            {QStringLiteral("Display3DAngle_4"), QStringLiteral("88")},
            {QStringLiteral("Display3DSpeed_4"), QStringLiteral("3")},
            {QStringLiteral("Display3DSliceShadow_4"), QStringLiteral("True")}};
        for (auto it = expectedText.cbegin(); it != expectedText.cend(); ++it) {
            QCOMPARE(s.value(it.key()).toString(), it.value());
        }
        QCOMPARE(s.value(QStringLiteral("DisplayFftFillAlpha_4")).toString(), QStringLiteral("0.23"));
        QCOMPARE(s.value(QStringLiteral("DisplaySpectrumFrac_4")).toString(), QStringLiteral("0.61"));
        DisplaySettingsModel reopened;
        reopened.setPanIndex(4);
        reopened.load();
        for (const QString& base : bases) {
            const QString own = PanDisplayKeys::panKey(base, 4);
            QVERIFY2(s.contains(own), qPrintable(base));
            QCOMPARE(s.value(own).toString(), m.perPanValue(base));
            QCOMPARE(reopened.perPanValue(base), m.perPanValue(base));
            QVERIFY2(!s.contains(base), qPrintable(base));
        }

        // Reset clears exactly that inventory; saving a fresh ship-default
        // model also clears each recreated override by equality.
        PanDisplayKeys::clearPan(4, bases);
        QCOMPARE(s.allKeys().size(), 0);
        m.save();
        QCOMPARE(s.allKeys().size(), 12);
        DisplaySettingsModel inherited;
        inherited.setPanIndex(4);
        inherited.save();
        QCOMPARE(s.allKeys().size(), 0);
    }

    // Catches: a static/shared field instead of a genuine per-instance
    // member -- two different model instances set to two different
    // values must read back independently. Task 18 flattened this field
    // from Band-keyed AppSettings storage onto a plain in-memory member,
    // the same per-instance shape the other thirteen fields already have
    // (see the class-header comment).
    void dssFloorDepth_isPerInstanceNotShared()
    {
        DisplaySettingsModel a;
        DisplaySettingsModel b;
        a.setDssFloorDepth(4);
        b.setDssFloorDepth(18);
        QCOMPARE(a.dssFloorDepth(), 4);
        QCOMPARE(b.dssFloorDepth(), 18);
    }

    // Catches: a fresh instance returning something other than the
    // documented ship default (6 -- matches SpectrumWidget's
    // m_dssFloorDepth in-class initializer and PanadapterModel's
    // BandGridSettings::dss3DFloorDepth default).
    void dssFloorDepth_defaultsToShipDefaultOfSix()
    {
        DisplaySettingsModel m;
        QCOMPARE(m.dssFloorDepth(), 6);
    }

    // ============================================================
    // Booleans persist as the literal strings "True"/"False".
    // Reads the RAW stored string directly -- round-tripping through
    // the bool getter alone cannot distinguish "wrote the wrong
    // string, read back false" from "wrote nothing, defaulted false".
    // ============================================================

    // Catches: booleans written via QString::number(bool) ("1"/"0") or
    // native lowercase ("true"/"false") instead of the project's
    // mandated "True"/"False" strings (CLAUDE.md persistence rule).
    void booleansPersistAsTrueFalseStrings()
    {
        // Fill is on by ship default, and a pan that matches what it
        // inherits writes nothing, so the Setup default is turned off
        // first to make "on" a value this pan has to store. The default
        // itself goes in as a bool and must come out "False".
        PanDisplayKeys::writeDefault(QStringLiteral("DisplayPanFill"), false);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayPanFill")).toString(),
                 QStringLiteral("False"));

        DisplaySettingsModel w;
        w.setPanIndex(0);
        w.setPanFill(true);
        w.setThreeDSliceDepth(true);
        w.save();
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayPanFill_0")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(AppSettings::instance().value(QStringLiteral("Display3DSliceShadow_0")).toString(),
                 QStringLiteral("True"));
    }

    // Catches: an implementation that only ever writes "True" regardless
    // of the actual value (the previous test alone cannot tell "always
    // writes True" apart from "writes correctly", since both pass when
    // the field happens to be true).
    void booleansPersistFalseAsTheLiteralFalseString()
    {
        // Slice shadow is off by ship default; the Setup default is
        // turned on so "off" is a value this pan has to store.
        PanDisplayKeys::writeDefault(QStringLiteral("Display3DSliceShadow"), true);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("Display3DSliceShadow")).toString(),
                 QStringLiteral("True"));

        DisplaySettingsModel w;
        w.setPanIndex(0);
        w.setPanFill(false);         // ship default is true -- a real change
        w.setThreeDSliceDepth(false); // differs from the Setup default above
        w.save();
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayPanFill_0")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(AppSettings::instance().value(QStringLiteral("Display3DSliceShadow_0")).toString(),
                 QStringLiteral("False"));
    }

    // ============================================================
    // Signal-once: exactly one emission per real change, never zero,
    // never a cascade. Mirrors tst_dss_overlay_menu.cpp's
    // movingASlider_emitsItsSignal shape (one QSignalSpy per signal,
    // one edit each, assert count 1).
    // ============================================================

    // Catches: a setter that forgot to emit (spy count 0) or that
    // somehow emits twice for one change (e.g. a copy-paste bug that
    // calls the signal both directly and via a second code path).
    void everyChangedSignal_firesExactlyOnce()
    {
        DisplaySettingsModel m;

        QSignalSpy schemeSpy(&m, &DisplaySettingsModel::wfColorSchemeChanged);
        QSignalSpy gainSpy(&m, &DisplaySettingsModel::wfColorGainChanged);
        QSignalSpy blackSpy(&m, &DisplaySettingsModel::wfBlackLevelChanged);
        QSignalSpy refSpy(&m, &DisplaySettingsModel::refLevelChanged);
        QSignalSpy dynSpy(&m, &DisplaySettingsModel::dynamicRangeChanged);
        QSignalSpy alphaSpy(&m, &DisplaySettingsModel::fillAlphaChanged);
        QSignalSpy fillSpy(&m, &DisplaySettingsModel::panFillChanged);
        QSignalSpy fracSpy(&m, &DisplaySettingsModel::spectrumFracChanged);
        QSignalSpy modeSpy(&m, &DisplaySettingsModel::spectrumRenderModeChanged);
        QSignalSpy dssGainSpy(&m, &DisplaySettingsModel::dssGainChanged);
        QSignalSpy spanSpy(&m, &DisplaySettingsModel::dssRowSpanChanged);
        QSignalSpy angleSpy(&m, &DisplaySettingsModel::dssAngleChanged);
        QSignalSpy shadowSpy(&m, &DisplaySettingsModel::threeDSliceDepthChanged);
        QSignalSpy floorSpy(&m, &DisplaySettingsModel::dssFloorDepthChanged);

        m.setWfColorScheme(2);
        m.setWfColorGain(80);
        m.setWfBlackLevel(20);
        m.setRefLevel(-30.0f);
        m.setDynamicRange(90.0f);
        m.setFillAlpha(0.5f);
        m.setPanFill(false);
        m.setSpectrumFrac(0.5f);
        m.setSpectrumRenderMode(1);
        m.setDssGain(10);
        m.setDssRowSpan(10);
        m.setDssAngle(10);
        m.setThreeDSliceDepth(true);
        m.setDssFloorDepth(12);

        QCOMPARE(schemeSpy.count(), 1);
        QCOMPARE(gainSpy.count(), 1);
        QCOMPARE(blackSpy.count(), 1);
        QCOMPARE(refSpy.count(), 1);
        QCOMPARE(dynSpy.count(), 1);
        QCOMPARE(alphaSpy.count(), 1);
        QCOMPARE(fillSpy.count(), 1);
        QCOMPARE(fracSpy.count(), 1);
        QCOMPARE(modeSpy.count(), 1);
        QCOMPARE(dssGainSpy.count(), 1);
        QCOMPARE(spanSpy.count(), 1);
        QCOMPARE(angleSpy.count(), 1);
        QCOMPARE(shadowSpy.count(), 1);
        QCOMPARE(floorSpy.count(), 1);

        // Payload sanity, not just the count -- catches a signal that
        // fires the right number of times but with a stale/wrong value.
        QCOMPARE(gainSpy.at(0).at(0).toInt(), 80);
        QCOMPARE(floorSpy.at(0).at(0).toInt(), 12);
    }

    // ============================================================
    // The echo guard: setting a value to what it already holds emits
    // NOTHING. This is what stops two surfaces bound to the same model
    // field from bouncing an edit back and forth.
    // ============================================================

    // Catches: a missing (or backwards) equality guard -- a setter that
    // unconditionally assigns-and-emits even when nothing changed.
    void settingTheCurrentValueAgain_emitsNothing()
    {
        DisplaySettingsModel m;
        m.setWfColorGain(80);
        m.setRefLevel(-30.0f);
        m.setPanFill(false);
        m.setDssAngle(77);
        m.setDssFloorDepth(9);

        QSignalSpy gainSpy(&m, &DisplaySettingsModel::wfColorGainChanged);
        QSignalSpy refSpy(&m, &DisplaySettingsModel::refLevelChanged);
        QSignalSpy fillSpy(&m, &DisplaySettingsModel::panFillChanged);
        QSignalSpy angleSpy(&m, &DisplaySettingsModel::dssAngleChanged);
        QSignalSpy floorSpy(&m, &DisplaySettingsModel::dssFloorDepthChanged);

        m.setWfColorGain(80);          // same int
        m.setRefLevel(-30.0f);         // same float
        m.setPanFill(false);           // same bool
        m.setDssAngle(77);             // same int
        m.setDssFloorDepth(9);         // same int

        QCOMPARE(gainSpy.count(), 0);
        QCOMPARE(refSpy.count(), 0);
        QCOMPARE(fillSpy.count(), 0);
        QCOMPARE(angleSpy.count(), 0);
        QCOMPARE(floorSpy.count(), 0);

        // And the getters genuinely hold the value, not some earlier
        // default the no-op path silently fell back to.
        QCOMPARE(m.wfColorGain(), 80);
        QVERIFY(qFuzzyCompare(m.refLevel() + 1.0f, -30.0f + 1.0f));
        QCOMPARE(m.panFill(), false);
        QCOMPARE(m.dssAngle(), 77);
        QCOMPARE(m.dssFloorDepth(), 9);
    }

    // A value that CLAMPS to the current stored value must also emit
    // nothing -- e.g. already at the ceiling and asked to go higher
    // still. Catches a guard that compares the RAW incoming argument
    // instead of the clamped result.
    void aClampedNoOpEdit_emitsNothing()
    {
        DisplaySettingsModel m;
        m.setDssGain(100); // ceiling
        QSignalSpy spy(&m, &DisplaySettingsModel::dssGainChanged);
        m.setDssGain(500); // clamps to 100, same as current
        QCOMPARE(spy.count(), 0);
        QCOMPARE(m.dssGain(), 100);
    }

    // ============================================================
    // Clamping holds at both ends of every ranged control.
    // ============================================================

    // Catches: a missing clamp, a swapped min/max, or an off-by-one
    // boundary on any of the eleven ranged fields.
    void clampingHoldsAtBothEndsOfEveryRangedControl()
    {
        DisplaySettingsModel m;

        m.setWfColorScheme(-5);
        QCOMPARE(m.wfColorScheme(), 0);
        m.setWfColorScheme(999);
        QCOMPARE(m.wfColorScheme(), 7);

        m.setWfColorGain(-5);
        QCOMPARE(m.wfColorGain(), 0);
        m.setWfColorGain(999);
        QCOMPARE(m.wfColorGain(), 100);

        m.setWfBlackLevel(-5);
        QCOMPARE(m.wfBlackLevel(), 0);
        // Display pill plan Task 3: 0 to 100 (was 0 to 125).
        m.setWfBlackLevel(999);
        QCOMPARE(m.wfBlackLevel(), 100);
        m.setWfBlackLevel(101);
        QCOMPARE(m.wfBlackLevel(), 100);
        m.setWfBlackLevel(100);
        QCOMPARE(m.wfBlackLevel(), 100);

        m.setRefLevel(-9999.0f);
        QVERIFY(qFuzzyCompare(m.refLevel() + 1000.0f, -200.0f + 1000.0f));
        m.setRefLevel(9999.0f);
        QVERIFY(qFuzzyCompare(m.refLevel() + 1000.0f, 200.0f + 1000.0f));

        m.setDynamicRange(-9999.0f);
        QVERIFY(qFuzzyCompare(m.dynamicRange(), 10.0f));
        m.setDynamicRange(9999.0f);
        QVERIFY(qFuzzyCompare(m.dynamicRange(), 200.0f));

        m.setFillAlpha(-9.0f);
        QVERIFY(qFuzzyCompare(m.fillAlpha() + 1.0f, 0.0f + 1.0f));
        m.setFillAlpha(9.0f);
        QVERIFY(qFuzzyCompare(m.fillAlpha() + 1.0f, 1.0f + 1.0f));

        m.setSpectrumFrac(-9.0f);
        QVERIFY(qFuzzyCompare(m.spectrumFrac(), 0.10f));
        m.setSpectrumFrac(9.0f);
        QVERIFY(qFuzzyCompare(m.spectrumFrac(), 0.90f));

        m.setSpectrumRenderMode(-5);
        QCOMPARE(m.spectrumRenderMode(), 0);
        m.setSpectrumRenderMode(999);
        QCOMPARE(m.spectrumRenderMode(), 1);

        m.setDssFloorDepth(-5);
        QCOMPARE(m.dssFloorDepth(), 0);
        m.setDssFloorDepth(999);
        QCOMPARE(m.dssFloorDepth(), 24);

        m.setDssGain(-5);
        QCOMPARE(m.dssGain(), 0);
        m.setDssGain(999);
        QCOMPARE(m.dssGain(), 100);

        m.setDssRowSpan(-5);
        QCOMPARE(m.dssRowSpan(), 0);
        m.setDssRowSpan(999);
        QCOMPARE(m.dssRowSpan(), 100);

        m.setDssAngle(-5);
        QCOMPARE(m.dssAngle(), 0);
        m.setDssAngle(999);
        QCOMPARE(m.dssAngle(), 100);
    }

    // ============================================================
    // 3D Floor is untouched by load()/save(); the other thirteen
    // fields key per pan and round-trip through both.
    // ============================================================

    // Catches: load()/save() reaching into dssFloorDepth despite the
    // file header's explicit "load() and save() do not touch 3D Floor" --
    // either save() writing SOME AppSettings key for it under any name
    // (there must be none: as of Task 18 this field's persistence lives
    // entirely on PanadapterModel, not here), or load() overwriting an
    // in-memory value a caller already set.
    void dssFloorDepth_untouchedByLoadAndSave()
    {
        DisplaySettingsModel w;
        w.setPanIndex(0);
        w.setDssFloorDepth(19);
        w.setDssGain(80); // proves save() writes keys at all
        w.save();
        QVERIFY(AppSettings::instance().contains(QStringLiteral("Display3DGain_0")));
        const QStringList keys = AppSettings::instance().allKeys();
        for (const QString& key : keys) {
            QVERIFY(!key.startsWith(QStringLiteral("Display3DFloorDepth")));
        }

        DisplaySettingsModel r;
        r.setPanIndex(0);
        r.setDssFloorDepth(3);
        r.load();
        QCOMPARE(r.dssFloorDepth(), 3); // load() must not have touched it
    }

    // Catches: the per-pan key suffix convention drifting from
    // PanDisplayKeys::panKey(base, panIndex), which SpectrumWidget uses
    // too -- pan 0 gets "_0", pan 1 gets "_1", and neither writes the bare
    // key (display pill plan Task 1; before it pan 0 wrote the bare key).
    void perPanFields_usePanIndexSuffixConvention()
    {
        DisplaySettingsModel pan0;
        pan0.setPanIndex(0);
        pan0.setWfColorGain(11);
        pan0.save();

        DisplaySettingsModel pan1;
        pan1.setPanIndex(1);
        pan1.setWfColorGain(22);
        pan1.save();

        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayWfColorGain_0")).toInt(), 11);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayWfColorGain_1")).toInt(), 22);
        QVERIFY(!AppSettings::instance().contains(QStringLiteral("DisplayWfColorGain")));
    }

    // Catches: the inheritance rule (2026-07-30 bench fix, shared with
    // SpectrumWidget::loadSettings() through PanDisplayKeys) being
    // dropped -- an untouched pan must read the Setup default, not the
    // hardcoded ship default, until it is given a value of its own.
    // What it inherits is the bare key: the value an install from before
    // display pill plan Task 1 left there as pan 0's, and the Setup
    // default since. A value pan 0 gives itself now is pan 0's alone.
    void anUntouchedPan_inheritsTheSetupDefault()
    {
        PanDisplayKeys::writeDefault(QStringLiteral("DisplayWfBlackLevel"), QStringLiteral("77"));

        DisplaySettingsModel pan1;
        pan1.setPanIndex(1);
        pan1.load();
        QCOMPARE(pan1.wfBlackLevel(), 77);

        DisplaySettingsModel pan0;
        pan0.setPanIndex(0);
        pan0.load();
        QCOMPARE(pan0.wfBlackLevel(), 77);
        pan0.setWfBlackLevel(31);
        pan0.save();

        DisplaySettingsModel pan1Again;
        pan1Again.setPanIndex(1);
        pan1Again.load();
        QCOMPARE(pan1Again.wfBlackLevel(), 77);
    }

    // ============================================================
    // 3D Stacked-Trace Spectrum Plan Task 24: the fifteenth field,
    // dssRowDivider (0..10, ship default 0 == Match). Same shape as the
    // other fourteen (clamp, equality guard, one emission per real
    // change, per-pan round trip through Display3DSpeed, pan-0-fallback
    // inheritance) -- one self-contained case covering all of it, rather
    // than duplicating the fourteen-field patterns above field by field.
    // ============================================================
    void setDssRowDivider_clampsGuardsEmitsAndRoundTrips()
    {
        DisplaySettingsModel m;
        QCOMPARE(m.dssRowDivider(), 0); // ship default: Match

        m.setDssRowDivider(-5);
        QCOMPARE(m.dssRowDivider(), 0);
        m.setDssRowDivider(999);
        QCOMPARE(m.dssRowDivider(), 10);

        m.setDssRowDivider(5);
        QSignalSpy spy(&m, &DisplaySettingsModel::dssRowDividerChanged);
        m.setDssRowDivider(5); // same value: equality-guarded, no emit
        QCOMPARE(spy.count(), 0);
        m.setDssRowDivider(7);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 7);

        DisplaySettingsModel w;
        w.setPanIndex(0);
        w.setDssRowDivider(6);
        w.save();
        QCOMPARE(AppSettings::instance().value(QStringLiteral("Display3DSpeed_0")).toInt(), 6);

        DisplaySettingsModel r;
        r.setPanIndex(0);
        r.load();
        QCOMPARE(r.dssRowDivider(), 6);

        // Inheritance: an untouched pan reads the Setup default, which pan
        // 0's own value above is not.
        DisplaySettingsModel pan2;
        pan2.setPanIndex(2);
        pan2.load();
        QCOMPARE(pan2.dssRowDivider(), 0);
        PanDisplayKeys::writeDefault(QStringLiteral("Display3DSpeed"), QStringLiteral("4"));
        pan2.load();
        QCOMPARE(pan2.dssRowDivider(), 4);
    }
};

QTEST_MAIN(TestDisplaySettingsModel)
#include "tst_display_settings_model.moc"
