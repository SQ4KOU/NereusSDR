// =================================================================
// tests/tst_spectrum_widget_cal_offset.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test. Nothing in this file is ported.
//
// Display pill plan Task 8 (2026-10-10). Cal Offset as its own term.
//
// Until this task one member of SpectrumWidget held two things: the
// station's receive offset (RadioModel::rxMeterOffsetDbForStream, pushed by
// MainWindow with every frame) and the operator's Cal Offset from Setup.
// The per-frame push overwrote whatever the operator set, and the save
// wrote the station's value under DisplayCalOffset.
//
// What is pinned here:
//   - the one-time reset (AppSettings::resetPollutedDisplayCalOffset):
//     DisplayCalOffset and every DisplayCalOffset_<N> are removed once, the
//     marker DisplayCalOffsetReset is recorded, a value written afterwards
//     is kept, nothing else is touched, and only the window calls it;
//   - station offset alone shifts a pushed row as it did before;
//   - the user's Cal Offset adds to it and survives the next per-frame
//     station push;
//   - DisplayCalOffset saves the user's value, never the station's, and the
//     station setter neither schedules a save nor announces;
//   - a remote spectrum adds nothing, whatever either term holds;
//   - keyed, DUP on and DUP off, the result is what it was with the user
//     term at 0, and the user term is added once.
//
// Isolation: TestSandboxInit redirects the settings path away from the
// operator's profile, and init()/cleanup() clear the in-process store
// around every slot. The reset cases use their own file in a temporary
// directory.
// =================================================================

#include <QtTest/QtTest>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"

// The receive row's calibration is read back from the 3D row store, and a
// remote spectrum is a private flag (as tst_display_duplex does).
#define private public
#include "gui/SpectrumWidget.h"
#undef private

using namespace NereusSDR;
using namespace Qt::StringLiterals;

namespace {
const QString kMarker = u"DisplayCalOffsetReset"_s;
const QString kKey    = u"DisplayCalOffset"_s;

constexpr float kStationDb = -12.5f;
constexpr float kUserDb    = 3.0f;
constexpr float kRowDbm    = -100.0f;

// One receive row pushed through the waterfall path, read back from the 3D
// row store: the level the pan drew it at.
float pushedRowLevel(SpectrumWidget& w)
{
    w.m_waterfall = QImage(8, 2, QImage::Format_RGB32);
    w.setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
    w.setDssRowDivider(1);
    w.m_bandwidthHz = 24000;
    w.m_lastFullBinsDbm = QVector<float>(4096, kRowDbm);
    const int before = w.dssRowsPushedForTest();
    w.pushWaterfallRow(QVector<float>(8, kRowDbm));
    if (w.dssRowsPushedForTest() != before + 1) {
        return qQNaN();
    }
    const int ring = w.m_dss.headRing();
    return w.m_dss.rowDataRing(ring)[w.m_dss.cols() / 2];
}
} // namespace

class TestSpectrumWidgetCalOffset : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // ── The one-time reset (test first) ─────────────────────────────────

    void reset_removes_the_stations_values_once_and_keeps_later_ones()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"NereusSDR.settings"_s);
        AppSettings s(path);
        s.setValue(u"DisplayCalOffset"_s, u"-12.5"_s);
        s.setValue(u"DisplayCalOffset_0"_s, u"-12.5"_s);
        s.setValue(u"DisplayCalOffset_1"_s, u"-12.5"_s);
        s.setValue(u"DisplayCalOffset_12"_s, u"7.25"_s);
        // Not these: other keys that only look alike.
        s.setValue(u"RX1_DisplayCalOffsetDb"_s, u"5.259"_s);
        s.setValue(u"DisplayCalOffset_x"_s, u"1"_s);
        s.setValue(u"DisplayCalOffsetExtra"_s, u"2"_s);
        s.setValue(u"DisplayLineWidth_1"_s, u"2"_s);
        s.setValue(u"SettingsSchemaVersion"_s, u"9"_s);

        AppSettings::resetPollutedDisplayCalOffset(s);

        QVERIFY(!s.contains(u"DisplayCalOffset"_s));
        QVERIFY(!s.contains(u"DisplayCalOffset_0"_s));
        QVERIFY(!s.contains(u"DisplayCalOffset_1"_s));
        QVERIFY(!s.contains(u"DisplayCalOffset_12"_s));
        QCOMPARE(s.value(kMarker).toString(), u"True"_s);
        QCOMPARE(s.value(u"RX1_DisplayCalOffsetDb"_s).toString(), u"5.259"_s);
        QCOMPARE(s.value(u"DisplayCalOffset_x"_s).toString(), u"1"_s);
        QCOMPARE(s.value(u"DisplayCalOffsetExtra"_s).toString(), u"2"_s);
        QCOMPARE(s.value(u"DisplayLineWidth_1"_s).toString(), u"2"_s);
        QCOMPARE(s.value(u"SettingsSchemaVersion"_s).toString(), u"9"_s);
        QCOMPARE(s.allKeys().size(), 6);

        // Written after the marker: the operator's own values. A second
        // run keeps them.
        s.setValue(u"DisplayCalOffset"_s, u"1.5"_s);
        s.setValue(u"DisplayCalOffset_1"_s, u"3"_s);
        AppSettings::resetPollutedDisplayCalOffset(s);
        QCOMPARE(s.value(u"DisplayCalOffset"_s).toString(), u"1.5"_s);
        QCOMPARE(s.value(u"DisplayCalOffset_1"_s).toString(), u"3"_s);

        // The marker reached the file: the next launch keeps them too.
        s.save();
        AppSettings reloaded(path);
        reloaded.load();
        QCOMPARE(reloaded.value(kMarker).toString(), u"True"_s);
        AppSettings::resetPollutedDisplayCalOffset(reloaded);
        QCOMPARE(reloaded.value(u"DisplayCalOffset"_s).toString(), u"1.5"_s);
        QCOMPARE(reloaded.value(u"DisplayCalOffset_1"_s).toString(), u"3"_s);
        QVERIFY(!reloaded.contains(u"DisplayCalOffset_0"_s));
    }

    void reset_reaches_the_file_on_the_first_run()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"NereusSDR.settings"_s);
        {
            AppSettings s(path);
            s.setValue(u"DisplayCalOffset"_s, u"-12.5"_s);
            s.setValue(u"DisplayCalOffset_1"_s, u"-12.5"_s);
            s.save();
        }
        {
            AppSettings s(path);
            s.load();
            QCOMPARE(s.value(u"DisplayCalOffset_1"_s).toString(), u"-12.5"_s);
            AppSettings::resetPollutedDisplayCalOffset(s);
        }
        AppSettings after(path);
        after.load();
        QVERIFY(!after.contains(u"DisplayCalOffset"_s));
        QVERIFY(!after.contains(u"DisplayCalOffset_1"_s));
        QCOMPARE(after.value(kMarker).toString(), u"True"_s);
    }

    void reset_of_a_file_with_no_such_keys_sets_only_the_marker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayLineWidth"_s, u"2"_s);

        AppSettings::resetPollutedDisplayCalOffset(s);

        QStringList keys = s.allKeys();
        keys.sort();
        QCOMPARE(keys, (QStringList{u"DisplayCalOffsetReset"_s, u"DisplayLineWidth"_s}));
    }

    // The Core never resets: these are the window's keys.
    void only_the_window_calls_the_reset()
    {
        const auto read = [](const QString& relative) {
            QFile file(QStringLiteral(NEREUS_SOURCE_DIR "/") + relative);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        const QString call = u"resetPollutedDisplayCalOffset"_s;
        const QString window = read(u"src/main.cpp"_s);
        const QString core = read(u"src/server_main.cpp"_s);
        const QString coreInit = read(u"src/core/CoreInit.cpp"_s);
        QVERIFY(!window.isEmpty() && !core.isEmpty() && !coreInit.isEmpty());
        QVERIFY(!core.contains(call));
        QVERIFY(!coreInit.contains(call));
        // After the settings are loaded, before the windows are built.
        const qsizetype loaded = window.indexOf(u"CoreInit::initialize(activeProfile)"_s);
        const qsizetype reset = window.indexOf(call);
        const qsizetype windows = window.indexOf(u"GuiConnectionController connections"_s);
        QVERIFY(loaded >= 0);
        QVERIFY(reset > loaded);
        QVERIFY(windows > reset);
        // The reset took no schema version of its own: the version is 10,
        // which is two-tone leaving TX profiles (it was 9 before that
        // merged, 2026-10-11).
        QVERIFY(coreInit.contains(u"ensureSettingsAtVersion(10)"_s));
    }

    // ── The two terms ───────────────────────────────────────────────────

    void station_offset_alone_shifts_a_pushed_row_as_before()
    {
        SpectrumWidget w;
        w.setStationCalOffsetDb(kStationDb);
        QCOMPARE(w.stationCalOffsetDb(), kStationDb);
        QCOMPARE(w.dbmCalOffset(), 0.0f);
        QCOMPARE(w.displayCalOffsetDb(), kStationDb);
        QCOMPARE(pushedRowLevel(w), kRowDbm + kStationDb);   // -112.5
    }

    void user_offset_adds_and_survives_the_next_station_push()
    {
        SpectrumWidget w;
        w.setStationCalOffsetDb(kStationDb);
        w.setDbmCalOffset(kUserDb);
        QCOMPARE(w.displayCalOffsetDb(), -9.5f);
        QCOMPARE(pushedRowLevel(w), kRowDbm - 9.5f);         // -109.5

        // MainWindow pushes the station offset with every frame.
        w.setStationCalOffsetDb(kStationDb);
        QCOMPARE(w.dbmCalOffset(), kUserDb);
        QCOMPARE(w.displayCalOffsetDb(), -9.5f);
        // A changed station offset moves the row and leaves the 3 dB.
        w.setStationCalOffsetDb(-20.0f);
        QCOMPARE(w.dbmCalOffset(), kUserDb);
        QCOMPARE(w.displayCalOffsetDb(), -17.0f);

        // The row after such a push, on a pan with no earlier row (the 3D
        // store smooths a row against the one before it).
        SpectrumWidget later;
        later.setDbmCalOffset(kUserDb);
        later.setStationCalOffsetDb(kStationDb);
        later.setStationCalOffsetDb(-20.0f);
        QCOMPARE(pushedRowLevel(later), kRowDbm - 17.0f);    // -117
    }

    // The trace takes the same sum as the row.
    void trace_position_takes_both_terms()
    {
        const QRect rect(0, 0, 400, 300);
        SpectrumWidget plain;
        SpectrumWidget w;
        w.setStationCalOffsetDb(kStationDb);
        w.setDbmCalOffset(kUserDb);
        QCOMPARE(w.dbmToYf(-60.0f, rect), plain.dbmToYf(-69.5f, rect));
    }

    void user_offset_keeps_its_range()
    {
        SpectrumWidget w;
        w.setDbmCalOffset(45.0f);
        QCOMPARE(w.dbmCalOffset(), 30.0f);
        w.setDbmCalOffset(-45.0f);
        QCOMPARE(w.dbmCalOffset(), -30.0f);
    }

    // ── What is saved, and what announces ───────────────────────────────

    void station_setter_neither_saves_nor_announces()
    {
        SpectrumWidget w;
        QSignalSpy spy(&w, &SpectrumWidget::displaySettingChanged);
        QVERIFY(spy.isValid());
        QVERIFY(!w.settingsSavePendingForTest());

        w.setStationCalOffsetDb(kStationDb);
        QVERIFY(!w.settingsSavePendingForTest());
        QCOMPARE(spy.count(), 0);

        w.setDbmCalOffset(kUserDb);
        QVERIFY(w.settingsSavePendingForTest());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), kKey);
    }

    void saves_the_users_value_never_the_stations()
    {
        AppSettings& s = AppSettings::instance();
        const QString panKey = PanDisplayKeys::panKey(kKey, 0);
        {
            SpectrumWidget w;
            w.setStationCalOffsetDb(kStationDb);
            w.saveSettingsForTest();
            // User offset 0 is the default: nothing is written, and above
            // all not the station's -12.5.
            QVERIFY(!s.contains(kKey));
            QVERIFY(!s.contains(panKey));

            w.setDbmCalOffset(kUserDb);
            w.setStationCalOffsetDb(kStationDb);
            w.saveSettingsForTest();
            QVERIFY(!s.contains(kKey));
            QCOMPARE(s.value(panKey).toString().toFloat(), kUserDb);
        }
        // The next pan to load takes the 3 dB as the user's term; the
        // station term starts at 0 until the station gives one.
        SpectrumWidget next;
        next.loadSettingsForTest();
        QCOMPARE(next.dbmCalOffset(), kUserDb);
        QCOMPARE(next.stationCalOffsetDb(), 0.0f);
        QCOMPARE(next.displayCalOffsetDb(), kUserDb);
    }

    // ── Remote and keyed ────────────────────────────────────────────────

    void remote_spectrum_adds_nothing()
    {
        SpectrumWidget remote;
        remote.m_remoteSpectrum = true;
        remote.setStationCalOffsetDb(kStationDb);
        remote.setDbmCalOffset(kUserDb);
        QCOMPARE(remote.displayCalOffsetDb(), 0.0f);
        remote.setTxDisplayCalOffsetDb(3.0f);
        remote.setMoxOverlay(true);
        QCOMPARE(remote.displayCalOffsetDb(), 0.0f);
        remote.setDisplayDuplex(true);
        QCOMPARE(remote.displayCalOffsetDb(), 0.0f);
    }

    void keyed_result_is_unchanged_at_user_zero_and_adds_the_user_term_once()
    {
        SpectrumWidget w;
        w.setStationCalOffsetDb(kStationDb);     // preamp + calibration
        w.setRxPreampOffsetDb(4.5f);             // its preamp half
        w.setTxDisplayCalOffsetDb(3.0f);
        w.setTxAttenuatorOffsetDb(7.0f);
        w.setDisplayDuplex(true);
        w.setMoxOverlay(true);
        // Keyed with DUP on, as before this task: 3 + (-12.5 - 4.5) + 7.
        QCOMPARE(w.displayCalOffsetDb(), -7.0f);
        w.setDbmCalOffset(kUserDb);
        QCOMPARE(w.displayCalOffsetDb(), -4.0f);

        // Keyed with DUP off: the TX Display Cal Offset, and the user term.
        w.setDisplayDuplex(false);
        QCOMPARE(w.displayCalOffsetDb(), 6.0f);
        w.setDbmCalOffset(0.0f);
        QCOMPARE(w.displayCalOffsetDb(), 3.0f);
    }
};

QTEST_MAIN(TestSpectrumWidgetCalOffset)
#include "tst_spectrum_widget_cal_offset.moc"
