// no-port-check: NereusSDR-original test.
//
// tst_slice_settings_restore.cpp
//
// A slice's settings come back after it is made again.
//
//   * The audio panel, squelch, APF, BIN, FM, DIG offset and RTTY values
//     were never written by SliceModel::saveToSettings, so each came back
//     at its default whenever the slice was made again (settings restore
//     audit 2026-10-11, gap 1). One table drives every one of them, and a
//     second pass saves the restored slice again and compares the whole
//     Slice<N>/ key set, so a key that is written but not read back fails
//     here as soon as the table gives it a value other than its default.
//   * AGC-T AUTO and its offset are kept per receiver, not per band (the
//     maintainer's ruling 2026-10-11; Thetis keeps one switch and offset per
//     receiver too, console.cs:45992-46033 and setup.cs:21995-22026
//     [v2.10.3.15]). A value an older build saved per band is still read,
//     once, when the per-slice key is absent.
//
// The settings store is the test sandbox's (TestSandboxInit), never the
// operator's file.

#include <QtTest/QtTest>

#include <QDir>
#include <QStandardPaths>

#include <cmath>

#include <QMap>

#include "core/AppSettings.h"
#include "core/WdspTypes.h"
#include "models/Band.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

struct Row {
    const char* property;   // SliceModel Q_PROPERTY name
    const char* key;        // key under Slice<N>/
    QVariant value;         // a value other than the default
};

// Every slice value that was never saved before 2026-10-11.
QList<Row> neverSavedRows()
{
    return {
        {"audioPan",        "AudioPan",        -0.35},
        {"binauralEnabled", "BinauralEnabled", true},
        {"ssqlEnabled",     "SsqlEnabled",     true},
        {"ssqlThresh",      "SsqlThresh",      42.0},
        {"amsqEnabled",     "AmsqEnabled",     true},
        {"amsqThresh",      "AmsqThresh",      -95.0},
        {"fmsqEnabled",     "FmsqEnabled",     true},
        {"fmsqThresh",      "FmsqThresh",      -88.0},
        {"apfEnabled",      "ApfEnabled",      true},
        {"apfTuneHz",       "ApfTuneHz",       120},
        {"fmCtcssMode",     "FmCtcssMode",     1},
        {"fmCtcssValueHz",  "FmCtcssValueHz",  123.0},
        {"fmOffsetHz",      "FmOffsetHz",      600000},
        {"fmTxMode",        "FmTxMode",        QVariant::fromValue(FmTxMode::High)},
        {"fmReverse",       "FmReverse",       true},
        {"diglOffsetHz",    "DiglOffsetHz",    1500},
        {"diguOffsetHz",    "DiguOffsetHz",    -1200},
        {"rttyMarkHz",      "RttyMarkHz",      2125},
        {"rttyShiftHz",     "RttyShiftHz",     850},
    };
}

// The keys directly under Slice<index>/ (no band part), with their values.
QMap<QString, QString> sliceLevelKeys(int index)
{
    QMap<QString, QString> out;
    const QString prefix = QStringLiteral("Slice%1/").arg(index);
    auto& s = AppSettings::instance();
    for (const QString& key : s.allKeys()) {
        if (!key.startsWith(prefix)) {
            continue;
        }
        const QString rest = key.mid(prefix.size());
        if (rest.startsWith(QLatin1String("Band"))) {
            continue;
        }
        out.insert(rest, s.value(key).toString());
    }
    return out;
}

bool sameValue(const QVariant& a, const QVariant& b)
{
    if (a.typeId() == QMetaType::Double || b.typeId() == QMetaType::Double) {
        return qFuzzyIsNull(a.toDouble() - b.toDouble());
    }
    return a.toInt() == b.toInt();
}

} // namespace

class TestSliceSettingsRestore : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // ── The values that were never saved ────────────────────────────────

    void everyNeverSavedValueComesBack_data()
    {
        QTest::addColumn<QByteArray>("property");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        for (const Row& row : neverSavedRows()) {
            QTest::newRow(row.property) << QByteArray(row.property)
                                        << QString::fromLatin1(row.key) << row.value;
        }
    }

    void everyNeverSavedValueComesBack()
    {
        QFETCH(QByteArray, property);
        QFETCH(QString, key);
        QFETCH(QVariant, value);

        SliceModel saved;
        saved.setSliceIndex(0);
        const QVariant defaultValue = saved.property(property.constData());
        QVERIFY2(defaultValue.isValid(), property.constData());
        QVERIFY2(saved.setProperty(property.constData(), value), property.constData());
        QVERIFY2(!sameValue(saved.property(property.constData()), defaultValue),
                 "the table's value must differ from the default");
        saved.saveToSettings(Band::Band20m);

        // Per slice, beside Muted and AfGain: not under the band.
        QVERIFY2(AppSettings::instance().contains(QStringLiteral("Slice0/") + key),
                 qPrintable(QStringLiteral("Slice0/%1 was not written").arg(key)));
        QVERIFY(!AppSettings::instance().contains(QStringLiteral("Slice0/Band20m/") + key));

        SliceModel restored;
        restored.setSliceIndex(0);
        restored.restoreFromSettings(Band::Band20m);
        QVERIFY2(sameValue(restored.property(property.constData()),
                           saved.property(property.constData())),
                 qPrintable(QStringLiteral("%1 came back as %2, saved as %3")
                                .arg(QString::fromLatin1(property),
                                     restored.property(property.constData()).toString(),
                                     saved.property(property.constData()).toString())));

        // Another band's restore brings the same value: it is the slice's.
        SliceModel otherBand;
        otherBand.setSliceIndex(0);
        otherBand.restoreFromSettings(Band::Band40m);
        QVERIFY(sameValue(otherBand.property(property.constData()),
                          saved.property(property.constData())));
    }

    // Booleans are the strings "True" / "False" (AppSettings convention).
    void booleansAreWrittenAsTrueOrFalse()
    {
        SliceModel slice;
        slice.setSliceIndex(0);
        slice.setBinauralEnabled(true);
        slice.setSsqlEnabled(true);
        slice.saveToSettings(Band::Band20m);
        auto& s = AppSettings::instance();
        QCOMPARE(s.value(QStringLiteral("Slice0/BinauralEnabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("Slice0/SsqlEnabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("Slice0/FmReverse")).toString(),
                 QStringLiteral("False"));
    }

    // Whatever saveToSettings writes under Slice<N>/, restoreFromSettings
    // reads: a restored slice saved again writes the same values.
    void aRestoredSliceSavesTheSameSliceKeys()
    {
        SliceModel saved;
        saved.setSliceIndex(0);
        for (const Row& row : neverSavedRows()) {
            QVERIFY2(saved.setProperty(row.property, row.value), row.property);
        }
        saved.setAutoAgcEnabled(true);
        saved.setAutoAgcOffset(12.5);
        saved.setMuted(true);
        saved.setAfGain(37);
        saved.saveToSettings(Band::Band20m);
        const QMap<QString, QString> first = sliceLevelKeys(0);
        for (const Row& row : neverSavedRows()) {
            QVERIFY2(first.contains(QString::fromLatin1(row.key)), row.key);
        }

        SliceModel restored;
        restored.setSliceIndex(0);
        restored.restoreFromSettings(Band::Band20m);
        AppSettings::instance().clear();
        restored.saveToSettings(Band::Band20m);
        const QMap<QString, QString> second = sliceLevelKeys(0);

        QCOMPARE(second.keys(), first.keys());
        for (auto it = first.cbegin(); it != first.cend(); ++it) {
            QVERIFY2(second.value(it.key()) == it.value(),
                     qPrintable(QStringLiteral("Slice0/%1 was saved as %2 and came back as %3")
                                    .arg(it.key(), it.value(), second.value(it.key()))));
        }
    }

    void absentKeysLeaveTheDefaults()
    {
        const SliceModel pristine;
        SliceModel restored;
        restored.setSliceIndex(0);
        restored.restoreFromSettings(Band::Band20m);
        for (const Row& row : neverSavedRows()) {
            QVERIFY2(sameValue(restored.property(row.property), pristine.property(row.property)),
                     row.property);
        }
        QCOMPARE(restored.autoAgcEnabled(), pristine.autoAgcEnabled());
        QCOMPARE(restored.autoAgcOffset(), pristine.autoAgcOffset());
    }

    void anFmTxModeOutsideItsRangeLeavesTheDefault()
    {
        AppSettings::instance().setValue(QStringLiteral("Slice0/FmTxMode"), 7);
        SliceModel restored;
        restored.setSliceIndex(0);
        restored.restoreFromSettings(Band::Band20m);
        QCOMPARE(restored.fmTxMode(), FmTxMode::Simplex);
    }

    // ── A stored value is not trusted ───────────────────────────────────

    // The file can hold anything: a hand edit, another build's value, a
    // damaged write. Several of these go straight to WDSP, so a value that
    // is not a finite number leaves the default and a number outside the
    // control's range is held to the nearest end of it.
    void aStoredValueIsHeldToItsControlsRange_data()
    {
        QTest::addColumn<QByteArray>("property");
        QTest::addColumn<QString>("key");
        QTest::addColumn<double>("low");
        QTest::addColumn<double>("high");

        const auto row = [](const char* property, const char* key, double low, double high) {
            QTest::newRow(property) << QByteArray(property) << QString::fromLatin1(key) << low << high;
        };
        row("audioPan",       "AudioPan",       -1.0,     1.0);
        row("ssqlThresh",     "SsqlThresh",     0.0,      100.0);
        row("amsqThresh",     "AmsqThresh",     -160.0,   0.0);
        row("fmsqThresh",     "FmsqThresh",     -160.0,   0.0);
        row("apfTuneHz",      "ApfTuneHz",      -500.0,   500.0);
        row("fmCtcssMode",    "FmCtcssMode",    0.0,      3.0);
        row("fmCtcssValueHz", "FmCtcssValueHz", 67.0,     254.1);
        row("fmOffsetHz",     "FmOffsetHz",     0.0,      10000000.0);
        row("diglOffsetHz",   "DiglOffsetHz",   -10000.0, 10000.0);
        row("diguOffsetHz",   "DiguOffsetHz",   -10000.0, 10000.0);
        row("rttyMarkHz",     "RttyMarkHz",     1000.0,   3500.0);
        row("rttyShiftHz",    "RttyShiftHz",    50.0,     1000.0);
        row("autoAgcOffset",  "AgcAutoOffset",  -60.0,    60.0);
    }

    void aStoredValueIsHeldToItsControlsRange()
    {
        QFETCH(QByteArray, property);
        QFETCH(QString, key);
        QFETCH(double, low);
        QFETCH(double, high);

        const SliceModel pristine;
        const double defaultValue = pristine.property(property.constData()).toDouble();
        const auto restoredWith = [&](const QString& stored) {
            AppSettings::instance().clear();
            AppSettings::instance().setValue(QStringLiteral("Slice0/") + key, stored);
            SliceModel restored;
            restored.setSliceIndex(0);
            restored.restoreFromSettings(Band::Band20m);
            return restored.property(property.constData()).toDouble();
        };

        // Not a finite number: the default stays.
        const QStringList notNumbers{QStringLiteral("nan"), QStringLiteral("inf"),
                                     QStringLiteral("-inf"), QStringLiteral("abc"),
                                     QString()};
        for (const QString& stored : notNumbers) {
            const double got = restoredWith(stored);
            QVERIFY2(std::isfinite(got), qPrintable(stored));
            QVERIFY2(qFuzzyIsNull(got - defaultValue), qPrintable(stored));
        }

        // Out of range: held to the nearest end.
        const double tooHigh = restoredWith(QStringLiteral("999999999"));
        QVERIFY(std::isfinite(tooHigh));
        QVERIFY2(qFuzzyIsNull(tooHigh - high), qPrintable(QString::number(tooHigh)));
        const double tooLow = restoredWith(QStringLiteral("-999999999"));
        QVERIFY(std::isfinite(tooLow));
        QVERIFY2(qFuzzyIsNull(tooLow - low), qPrintable(QString::number(tooLow)));
    }

    // The older per-band AUTO offset is checked the same way as the
    // slice's own key.
    void aPerBandAutoAgcOffsetIsHeldToItsRangeToo()
    {
        SliceModel saved;
        saved.setSliceIndex(0);
        saved.saveToSettings(Band::Band20m);
        QString bandKey;
        for (const QString& k : AppSettings::instance().allKeys()) {
            if (k.startsWith(QStringLiteral("Slice0/Band")) && k.endsWith(QStringLiteral("/AgcThreshold"))) {
                bandKey = k.left(k.lastIndexOf(QLatin1Char('/'))) + QStringLiteral("/AgcAutoOffset");
            }
        }
        QVERIFY(!bandKey.isEmpty());
        AppSettings::instance().remove(QStringLiteral("Slice0/AgcAutoOffset"));

        const SliceModel pristine;
        AppSettings::instance().setValue(bandKey, QStringLiteral("nan"));
        {
            SliceModel restored;
            restored.setSliceIndex(0);
            restored.restoreFromSettings(Band::Band20m);
            QCOMPARE(restored.autoAgcOffset(), pristine.autoAgcOffset());
        }
        AppSettings::instance().setValue(bandKey, QStringLiteral("500"));
        {
            SliceModel restored;
            restored.setSliceIndex(0);
            restored.restoreFromSettings(Band::Band20m);
            QCOMPARE(restored.autoAgcOffset(), 60.0);
        }
    }

    // The settings these tests write are the test sandbox's (Qt's test
    // mode, set up by TestSandboxInit), never the operator's.
    void theSettingsFileIsTheSandboxsNotTheOperators()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        const QString path = AppSettings::instance().filePath();
        const QString home = QDir::homePath();
        QVERIFY2(!path.startsWith(home + QStringLiteral("/.config/NereusSDR")), qPrintable(path));
        QVERIFY2(!path.startsWith(home + QStringLiteral("/Library/Preferences/NereusSDR")),
                 qPrintable(path));
    }

    // ── AGC-T AUTO: per receiver ────────────────────────────────────────

    void autoAgcIsSavedPerSliceNotPerBand()
    {
        SliceModel saved;
        saved.setSliceIndex(1);
        saved.setAutoAgcEnabled(true);
        saved.setAutoAgcOffset(12.5);
        saved.saveToSettings(Band::Band20m);

        auto& s = AppSettings::instance();
        QCOMPARE(s.value(QStringLiteral("Slice1/AgcAutoEnabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("Slice1/AgcAutoOffset")).toDouble(), 12.5);

        // The same receiver on a band it has never been saved on.
        SliceModel restored;
        restored.setSliceIndex(1);
        restored.restoreFromSettings(Band::Band40m);
        QVERIFY(restored.autoAgcEnabled());
        QCOMPARE(restored.autoAgcOffset(), 12.5);

        // Another receiver keeps its own.
        SliceModel other;
        other.setSliceIndex(0);
        other.restoreFromSettings(Band::Band20m);
        QVERIFY(!other.autoAgcEnabled());
        QCOMPARE(other.autoAgcOffset(), 20.0);
    }

    // An older build saved AUTO under the band. That value is still read
    // when the slice has no key of its own, and the next save writes the
    // slice's key.
    void aPerBandAutoAgcValueIsReadWhenTheSliceHasNone()
    {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice0/Band20m/AgcAutoEnabled"), QStringLiteral("True"));
        s.setValue(QStringLiteral("Slice0/Band20m/AgcAutoOffset"), 8.0);

        SliceModel restored;
        restored.setSliceIndex(0);
        restored.restoreFromSettings(Band::Band20m);
        QVERIFY(restored.autoAgcEnabled());
        QCOMPARE(restored.autoAgcOffset(), 8.0);

        restored.saveToSettings(Band::Band20m);
        QCOMPARE(s.value(QStringLiteral("Slice0/AgcAutoEnabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("Slice0/AgcAutoOffset")).toDouble(), 8.0);
    }

    // Once the slice has its own key, a band's old value no longer counts.
    void theSliceKeyWinsOverAPerBandValue()
    {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice0/Band20m/AgcAutoEnabled"), QStringLiteral("True"));
        s.setValue(QStringLiteral("Slice0/Band20m/AgcAutoOffset"), 8.0);
        s.setValue(QStringLiteral("Slice0/AgcAutoEnabled"), QStringLiteral("False"));
        s.setValue(QStringLiteral("Slice0/AgcAutoOffset"), 15.0);

        SliceModel restored;
        restored.setSliceIndex(0);
        restored.setAutoAgcEnabled(true);
        restored.restoreFromSettings(Band::Band20m);
        QVERIFY(!restored.autoAgcEnabled());
        QCOMPARE(restored.autoAgcOffset(), 15.0);
    }
};

QTEST_MAIN(TestSliceSettingsRestore)
#include "tst_slice_settings_restore.moc"
