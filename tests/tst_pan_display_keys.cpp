// =================================================================
// tests/tst_pan_display_keys.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Pins core/settings/PanDisplayKeys,
// the one place that decides where a per-pan display setting is stored:
// the bare key is the Setup default, "<base>_<N>" is pan N's own override
// (pan 0 included), and an override is written only when it differs from
// what the pan would inherit. Display pill plan Task 1.
//
// Isolation: TestSandboxInit keeps AppSettings away from the operator's
// profile; init()/cleanup() clear the in-process store around every slot.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"

using namespace NereusSDR;
using namespace Qt::StringLiterals;

class TestPanDisplayKeys : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void approvedMasterRulesClearEqualityAndReachClosedPans()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(u"DisplayWfOpacity"_s, u"80"_s);
        settings.setValue(u"DisplayWfOpacity_2"_s, u"60"_s);
        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 2, u"80.0"_s, u"100"_s);
        QVERIFY(!PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 2));
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 2, u"100"_s).toString(), u"80"_s);

        settings.setValue(u"DisplayWfOpacity_0"_s, u"70"_s);
        settings.setValue(u"DisplayWfOpacity_4"_s, u"50"_s);
        settings.setValue(u"DisplayWfOpacity_other"_s, u"keep"_s);
        settings.setValue(u"DisplayLineWidth_4"_s, u"2"_s);
        PanDisplayKeys::writeDefault(u"DisplayWfOpacity", u"90"_s);
        QVERIFY(!PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 0));
        QVERIFY(!PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 4));
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 4, u"100"_s).toString(), u"90"_s);
        QCOMPARE(settings.value(u"DisplayWfOpacity_other"_s).toString(), u"keep"_s);
        QCOMPARE(settings.value(u"DisplayLineWidth_4"_s).toString(), u"2"_s);
    }

    // Catches: the Setup default moving off the bare key. Every existing
    // install's pan 0 values are in bare keys; they become the Setup
    // defaults only because this returns the base unchanged.
    void theDefaultKeyIsTheBareKey()
    {
        QCOMPARE(PanDisplayKeys::defaultKey(u"DisplayWfOpacity"), u"DisplayWfOpacity"_s);
    }

    // Catches: pan 0 being special-cased back onto the bare key, which
    // would make pan 0 a writer of the Setup default again.
    void everyPanIncludingPanZeroHasASuffixedKey()
    {
        QCOMPARE(PanDisplayKeys::panKey(u"DisplayWfOpacity", 0), u"DisplayWfOpacity_0"_s);
        QCOMPARE(PanDisplayKeys::panKey(u"DisplayWfOpacity", 1), u"DisplayWfOpacity_1"_s);
        QCOMPARE(PanDisplayKeys::panKey(u"DisplayWfOpacity", 12), u"DisplayWfOpacity_12"_s);
    }

    // Catches: a read order other than own key, Setup default, fallback.
    void readTakesThePanKeyThenTheDefaultThenTheFallback()
    {
        auto& s = AppSettings::instance();
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 2, u"100"_s).toString(), u"100"_s);

        s.setValue(u"DisplayWfOpacity"_s, u"80"_s);
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 0, u"100"_s).toString(), u"80"_s);
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 2, u"100"_s).toString(), u"80"_s);

        s.setValue(u"DisplayWfOpacity_2"_s, u"60"_s);
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 2, u"100"_s).toString(), u"60"_s);
        // Another pan's override is not this pan's.
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 0, u"100"_s).toString(), u"80"_s);
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 3, u"100"_s).toString(), u"80"_s);
    }

    // Catches: an empty value in a pan's own key being read as "no key".
    // A pan that saved an empty transmit gradient beside a pan 0 that has
    // a custom one must keep its empty one.
    void anEmptyPanKeyIsAValue_anEmptyDefaultKeyIsNot()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayTxWfGradient"_s, u"custom"_s);
        s.setValue(u"DisplayTxWfGradient_1"_s, QString());
        QVERIFY(PanDisplayKeys::hasOverride(u"DisplayTxWfGradient", 1));
        QCOMPARE(PanDisplayKeys::read(u"DisplayTxWfGradient", 1, u"fallback"_s).toString(),
                 QString());

        // An empty bare key is how the rest of the code spells "never
        // set", so it does not hide the fallback.
        s.setValue(u"DisplayWfOpacity"_s, QString());
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 1, u"100"_s).toString(), u"100"_s);
    }

    void hasOverrideIsAboutThePanKeyOnly()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayWfOpacity"_s, u"80"_s);
        QVERIFY(!PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 0));
        QVERIFY(!PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 2));
        s.setValue(u"DisplayWfOpacity_2"_s, u"60"_s);
        QVERIFY(PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 2));
        QVERIFY(!PanDisplayKeys::hasOverride(u"DisplayWfOpacity", 0));
    }

    // Catches: a pan freezing a copy of what it inherits. A value equal to
    // the Setup default, or to the fallback when there is no default, is
    // not written, for any pan.
    void writeSparseWritesNothingForAnInheritedValue()
    {
        auto& s = AppSettings::instance();
        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 2, u"100"_s, u"100"_s);
        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 0, u"100"_s, u"100"_s);
        QVERIFY(s.allKeys().isEmpty());

        s.setValue(u"DisplayWfOpacity"_s, u"80"_s);
        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 2, u"80"_s, u"100"_s);
        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 0, u"80"_s, u"100"_s);
        QCOMPARE(s.allKeys(), QStringList{u"DisplayWfOpacity"_s});
    }

    // Catches: a real change not being stored, being stored in the bare
    // key, or pan 0 writing the bare key.
    void writeSparseWritesThePanKeyForADifferentValue()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayWfOpacity"_s, u"80"_s);

        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 2, u"60"_s, u"100"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity_2"_s).toString(), u"60"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity"_s).toString(), u"80"_s);

        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 0, u"40"_s, u"100"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity_0"_s).toString(), u"40"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity"_s).toString(), u"80"_s);

        // The ship default is a difference too, when the Setup default is
        // something else.
        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 3, u"100"_s, u"100"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity_3"_s).toString(), u"100"_s);
    }

    // Returning to the master clears the override and follows later changes.
    void writeSparseClearsAnOverrideWhenItReturnsToTheMaster()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayWfOpacity"_s, u"80"_s);
        s.setValue(u"DisplayWfOpacity_2"_s, u"60"_s);

        PanDisplayKeys::writeSparse(u"DisplayWfOpacity", 2, u"80"_s, u"100"_s);
        QVERIFY(!s.contains(u"DisplayWfOpacity_2"_s));

        // The pan now follows the master.
        PanDisplayKeys::writeDefault(u"DisplayWfOpacity", u"30"_s);
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 2, u"100"_s).toString(), u"30"_s);
    }

    // Catches: a spurious override written because two writers spell one
    // value differently. The widget stores a float 0.7 as
    // "0.699999988079071"; Setup may store "0.7"; a colour may be upper or
    // lower case. None of those is a change.
    void writeSparseComparesByMeaningNotSpelling_data()
    {
        QTest::addColumn<QString>("inherited");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("written");
        QTest::newRow("float, short against full") << u"0.7"_s << u"0.699999988079071"_s << false;
        QTest::newRow("integer against real") << u"-48"_s << u"-48.0"_s << false;
        QTest::newRow("zero against zero") << u"0"_s << u"0.0"_s << false;
        QTest::newRow("colour case") << u"#FF000000"_s << u"#ff000000"_s << false;
        QTest::newRow("identical text") << u"True"_s << u"True"_s << false;
        QTest::newRow("one apart, large integer") << u"1200000"_s << u"1200001"_s << true;
        QTest::newRow("a real float change") << u"0.7"_s << u"0.71"_s << true;
        QTest::newRow("boolean flip") << u"True"_s << u"False"_s << true;
        QTest::newRow("different colour") << u"#FF000000"_s << u"#FF000001"_s << true;
        QTest::newRow("text against number") << u"12"_s << u"custom"_s << true;
    }
    void writeSparseComparesByMeaningNotSpelling()
    {
        QFETCH(QString, inherited);
        QFETCH(QString, value);
        QFETCH(bool, written);
        auto& s = AppSettings::instance();

        // Against the Setup default.
        s.setValue(u"Probe"_s, inherited);
        PanDisplayKeys::writeSparse(u"Probe", 1, value, u"unused"_s);
        QCOMPARE(s.contains(u"Probe_1"_s), written);

        // And against the fallback, with no Setup default.
        s.clear();
        PanDisplayKeys::writeSparse(u"Probe", 1, value, inherited);
        QCOMPARE(s.contains(u"Probe_1"_s), written);
        if (written) {
            QCOMPARE(s.value(u"Probe_1"_s).toString(), value);
        }
    }

    // Catches: a bool reaching the store as "true" / "1" instead of the
    // project's "True" / "False".
    void booleansAreStoredAsTrueAndFalse()
    {
        auto& s = AppSettings::instance();
        PanDisplayKeys::writeDefault(u"DisplayGridEnabled", false);
        QCOMPARE(s.value(u"DisplayGridEnabled"_s).toString(), u"False"_s);
        PanDisplayKeys::writeSparse(u"DisplayGridEnabled", 1, true, true);
        QCOMPARE(s.value(u"DisplayGridEnabled_1"_s).toString(), u"True"_s);
        // A bool equal to the stored "False" is not a change.
        PanDisplayKeys::writeSparse(u"DisplayGridEnabled", 2, false, true);
        QVERIFY(!s.contains(u"DisplayGridEnabled_2"_s));
    }

    // Every pan takes a master write; its old override cannot shadow it.
    void writeDefaultClearsPanKeysAndWritesTheMaster()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayWfOpacity_1"_s, u"60"_s);
        PanDisplayKeys::writeDefault(u"DisplayWfOpacity", u"45"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity"_s).toString(), u"45"_s);
        QVERIFY(!s.contains(u"DisplayWfOpacity_1"_s));
        QCOMPARE(s.allKeys().size(), 1);
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 0, u"100"_s).toString(), u"45"_s);
    }

    // Catches: a reset reaching past its own pan or its own bases: the
    // Setup defaults, another pan's overrides, the station's per-band grid
    // keys (DisplayGridMax_20m shares a prefix with the per-pan
    // DisplayGridMax_<N>) and keys not named must all survive.
    void clearPanRemovesOnlyThatPansKeysForTheNamedBases()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayWfOpacity"_s, u"80"_s);
        s.setValue(u"DisplayWfOpacity_1"_s, u"60"_s);
        s.setValue(u"DisplayWfOpacity_2"_s, u"50"_s);
        s.setValue(u"DisplayWfOpacity_11"_s, u"40"_s);
        s.setValue(u"DisplayGridMax"_s, u"-40"_s);
        s.setValue(u"DisplayGridMax_1"_s, u"-30"_s);
        s.setValue(u"DisplayGridMax_20m"_s, u"-20"_s);
        s.setValue(u"DisplayLineWidth_1"_s, u"3"_s);

        PanDisplayKeys::clearPan(1, {u"DisplayWfOpacity"_s, u"DisplayGridMax"_s,
                                     u"DisplayNeverStored"_s});

        QVERIFY(!s.contains(u"DisplayWfOpacity_1"_s));
        QVERIFY(!s.contains(u"DisplayGridMax_1"_s));
        QCOMPARE(s.value(u"DisplayWfOpacity"_s).toString(), u"80"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity_2"_s).toString(), u"50"_s);
        QCOMPARE(s.value(u"DisplayWfOpacity_11"_s).toString(), u"40"_s);
        QCOMPARE(s.value(u"DisplayGridMax"_s).toString(), u"-40"_s);
        QCOMPARE(s.value(u"DisplayGridMax_20m"_s).toString(), u"-20"_s);
        QCOMPARE(s.value(u"DisplayLineWidth_1"_s).toString(), u"3"_s);
        QCOMPARE(s.allKeys().size(), 6);

        // The pan follows the default again.
        QCOMPARE(PanDisplayKeys::read(u"DisplayWfOpacity", 1, u"100"_s).toString(), u"80"_s);
    }
};

QTEST_MAIN(TestPanDisplayKeys)
#include "tst_pan_display_keys.moc"
