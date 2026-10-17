// =================================================================
// tests/tst_clarity_levels.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test. It pins NereusSDR's waterfall
// level model under Clarity (core/spectrum/ClarityLevels.h, which carries
// the AetherSDR attribution for the two constants it takes) and the
// one-time conversion of stored values; nothing here is ported.
//
// Display pill plan Task 3 (2026-10-10). While Clarity drives the levels,
// Black Level and Color Gain place the colour range against the noise
// floor N: black point = N + (Black - 50) * 0.5 dB (up = blacker), span =
// 80 - 0.7 * Gain dB. With Clarity off they do nothing and Low / High
// apply. Values stored under the model before it are converted once so the
// waterfall does not change on upgrade.
//
// What is pinned here:
//   - the two formulas and the two conversion functions, at the values the
//     plan names;
//   - the sweep: for every old (Black, Gain) pair that does not clamp, the
//     converted black point and top land within 0.5 dB of the old ones;
//   - the renderer: the effective pair under Clarity, with Clarity off and
//     under the transmit overlay, through the same function dbmToRgb uses;
//   - the conversion of a settings file: bare keys, per-pan keys, the
//     marker, a second run, a file with none of the keys, and the cases
//     where a Color Gain key has to be added to keep a span.
//
// Isolation: the conversion cases use their own AppSettings on a temporary
// file. The renderer cases use the sandboxed process store
// (TestSandboxInit), cleared around every slot.
// =================================================================

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include <cmath>
#include <utility>

#include "core/AppSettings.h"
#include "core/spectrum/ClarityLevels.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;
using namespace Qt::StringLiterals;
namespace CL = NereusSDR::ClarityLevels;

namespace {

// The model before this one, written out independently of ClarityLevels.h:
// Clarity's pair was N - 5 and N + 55, the old Black Level raised the low
// by 0.4 dB a step below 125 and the old Color Gain lowered the high by
// 0.3 dB a step.
double oldBlackPoint(double n, int black) { return n - 5.0 + 0.4 * (125 - black); }
double oldTop(double n, int gain)         { return n + 55.0 - 0.3 * gain; }

const QString kMarker = u"DisplayWfClarityLevelsConverted"_s;

} // namespace

class TestClarityLevels : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // ── The formulas ─────────────────────────────────────────────────────

    void blackPoint_and_span_at_the_named_values()
    {
        QCOMPARE(CL::blackPointDbm(-120.0f, 50), -120.0f);
        QCOMPARE(CL::blackPointDbm(-120.0f, 57), -116.5f);
        QCOMPARE(CL::colorSpanDb(0), 80.0f);
        QCOMPARE(CL::colorSpanDb(100), 10.0f);
        QCOMPARE(CL::colorSpanDb(60), 38.0f);
    }

    void constants_are_the_plans()
    {
        QCOMPARE(CL::kBlackLevelMin, 0);
        QCOMPARE(CL::kBlackLevelMax, 100);
        QCOMPARE(CL::kBlackLevelAtNoise, 50);
        QCOMPARE(CL::kBlackLevelDefault, 57);
        QCOMPARE(CL::kBlackLevelDbPerStep, 0.5f);
        QCOMPARE(CL::kColorGainDefault, 60);
        QCOMPARE(CL::kColorSpanAtZeroGainDb, 80.0f);
        QCOMPARE(CL::kColorSpanDbPerGainStep, 0.7f);
    }

    // Up is blacker: a higher Black Level puts the black point higher.
    void raising_black_raises_the_black_point()
    {
        for (int black = CL::kBlackLevelMin; black < CL::kBlackLevelMax; ++black) {
            QVERIFY(CL::blackPointDbm(-120.0f, black + 1) > CL::blackPointDbm(-120.0f, black));
        }
    }

    // ── The conversion functions ─────────────────────────────────────────

    void conversion_at_the_named_values()
    {
        // The old ship defaults become the new ones.
        QCOMPARE(CL::convertLegacyBlackLevel(104), 57);
        QCOMPARE(CL::convertLegacyColorGain(45, 104), 60);
        // Old Black 125 was N - 5 dB: ten steps under the noise.
        QCOMPARE(CL::convertLegacyBlackLevel(125), 40);
        // Old Black 0 was N + 45 dB, past the top of the new slider.
        QCOMPARE(CL::convertLegacyBlackLevel(0), 100);
        QCOMPARE(CL::convertLegacyColorGain(100, 125), 71);
        QCOMPARE(CL::convertLegacyBlackLevel(CL::kLegacyBlackLevelDefault),
                 CL::kBlackLevelDefault);
        QCOMPARE(CL::convertLegacyColorGain(CL::kLegacyColorGainDefault,
                                            CL::kLegacyBlackLevelDefault),
                 CL::kColorGainDefault);
    }

    // For every old pair that does not clamp, the picture stays where it
    // was: black point and top within 0.5 dB.
    void sweep_keeps_black_point_and_top_within_half_a_db()
    {
        const double n = -120.0;
        int checked = 0;
        for (int black = 0; black <= 125; black += 5) {
            for (int gain = 0; gain <= 100; gain += 10) {
                // What the slider positions would be with no clamp.
                const double wantBlack = 50.0 + (oldBlackPoint(0.0, black)) / 0.5;
                const double oldSpan = oldTop(n, gain) - oldBlackPoint(n, black);
                const double wantGain = (80.0 - oldSpan) / 0.7;
                if (wantBlack < 0.0 || wantBlack > 100.0
                    || wantGain < 0.0 || wantGain > 100.0) {
                    continue; // clamped at a slider end: cannot be kept
                }
                const int newBlack = CL::convertLegacyBlackLevel(black);
                const int newGain = CL::convertLegacyColorGain(gain, black);
                const double newPoint = CL::blackPointDbm(static_cast<float>(n), newBlack);
                const double newTop = newPoint + CL::colorSpanDb(newGain);
                const QByteArray where = QByteArray("old black ") + QByteArray::number(black)
                    + " gain " + QByteArray::number(gain);
                QVERIFY2(std::abs(newPoint - oldBlackPoint(n, black)) <= 0.5, where.constData());
                QVERIFY2(std::abs(newTop - oldTop(n, gain)) <= 0.5, where.constData());
                ++checked;
            }
        }
        // The sweep must not pass by skipping everything.
        QVERIFY2(checked >= 150, QByteArray::number(checked).constData());
    }

    void conversion_clamps_to_the_slider_ends()
    {
        for (int black = -20; black <= 200; ++black) {
            const int b = CL::convertLegacyBlackLevel(black);
            QVERIFY(b >= CL::kBlackLevelMin && b <= CL::kBlackLevelMax);
            for (int gain = -20; gain <= 150; gain += 5) {
                const int g = CL::convertLegacyColorGain(gain, black);
                QVERIFY(g >= CL::kColorGainMin && g <= CL::kColorGainMax);
            }
        }
    }

    // ── The renderer ─────────────────────────────────────────────────────

    void renderer_under_clarity_colours_against_the_noise_floor()
    {
        SpectrumWidget w;
        w.setWfBlackLevel(57);
        w.setWfColorGain(60);
        // Clarity's own pair must not be what colours the waterfall.
        w.setClarityWaterfallThresholds(-125.0f, -65.0f);
        w.setClarityFloorDbm(-120.0f);
        w.setClarityActive(true);

        QCOMPARE(w.clarityFloorDbm(), -120.0f);
        const std::pair<float, float> levels = w.effectiveWaterfallLevelsForTest();
        QCOMPARE(levels.first, -116.5f);
        QCOMPARE(levels.second, -78.5f);

        // The same pair through the colour function: flat at and below the
        // low, flat at and above the high, and a different colour between.
        const QRgb atLow = w.dbmToRgbForTest(-116.5f);
        const QRgb atHigh = w.dbmToRgbForTest(-78.5f);
        const QRgb mid = w.dbmToRgbForTest(-97.5f);
        QCOMPARE(w.dbmToRgbForTest(-140.0f), atLow);
        QCOMPARE(w.dbmToRgbForTest(-60.0f), atHigh);
        QVERIFY(mid != atLow);
        QVERIFY(mid != atHigh);
        QVERIFY(atLow != atHigh);
        // Just inside each end the colour has already moved, so the ends
        // are where the accessor says and not further out.
        QVERIFY(w.dbmToRgbForTest(-112.0f) != atLow);
        QVERIFY(w.dbmToRgbForTest(-83.0f) != atHigh);
    }

    void renderer_under_clarity_raising_black_raises_the_black_point()
    {
        SpectrumWidget w;
        w.setClarityFloorDbm(-120.0f);
        w.setClarityActive(true);
        w.setWfColorGain(60);

        w.setWfBlackLevel(40);
        const float low40 = w.effectiveWaterfallLevelsForTest().first;
        w.setWfBlackLevel(50);
        const float low50 = w.effectiveWaterfallLevelsForTest().first;
        w.setWfBlackLevel(70);
        const float low70 = w.effectiveWaterfallLevelsForTest().first;

        QCOMPARE(low40, -125.0f);
        QCOMPARE(low50, -120.0f);
        QCOMPARE(low70, -110.0f);
        // And the span follows the black point: Gain alone sets it.
        QCOMPARE(w.effectiveWaterfallLevelsForTest().second, -110.0f + 38.0f);

        // A bin 5 dB over the noise is coloured at Black 50 and black at 70.
        w.setWfBlackLevel(50);
        const QRgb shown = w.dbmToRgbForTest(-115.0f);
        w.setWfBlackLevel(70);
        const QRgb hidden = w.dbmToRgbForTest(-115.0f);
        QCOMPARE(hidden, w.dbmToRgbForTest(-160.0f));
        QVERIFY(shown != hidden);
    }

    void renderer_follows_the_noise_floor()
    {
        SpectrumWidget w;
        w.setWfBlackLevel(50);
        w.setWfColorGain(0);
        w.setClarityActive(true);
        w.setClarityFloorDbm(-100.0f);
        QCOMPARE(w.effectiveWaterfallLevelsForTest().first, -100.0f);
        QCOMPARE(w.effectiveWaterfallLevelsForTest().second, -20.0f);
        w.setClarityFloorDbm(-130.0f);
        QCOMPARE(w.effectiveWaterfallLevelsForTest().first, -130.0f);
        // A floor that is not a number is ignored.
        w.setClarityFloorDbm(qQNaN());
        QCOMPARE(w.clarityFloorDbm(), -130.0f);
    }

    // With Clarity off, Low and High set the levels whatever Black Level
    // and Color Gain hold.
    void renderer_without_clarity_uses_low_and_high_exactly()
    {
        SpectrumWidget w;
        w.setWfAgcEnabled(false);
        w.setWaterfallNFAGCEnabled(false);
        w.setWfLowThreshold(-122.0f);
        w.setWfHighThreshold(-62.0f);
        // A floor left from an earlier Clarity run changes nothing.
        w.setClarityFloorDbm(-120.0f);
        QVERIFY(!w.clarityActive());

        const std::pair<int, int> settings[] = {{57, 60}, {0, 0}, {100, 100}, {50, 0}, {10, 90}};
        for (const auto& [black, gain] : settings) {
            w.setWfBlackLevel(black);
            w.setWfColorGain(gain);
            const std::pair<float, float> levels = w.effectiveWaterfallLevelsForTest();
            QCOMPARE(levels.first, -122.0f);
            QCOMPARE(levels.second, -62.0f);
        }
        QCOMPARE(w.dbmToRgbForTest(-122.0f), w.dbmToRgbForTest(-150.0f));
        QCOMPARE(w.dbmToRgbForTest(-62.0f), w.dbmToRgbForTest(-30.0f));
        QVERIFY(w.dbmToRgbForTest(-118.0f) != w.dbmToRgbForTest(-122.0f));
    }

    // Clarity on with no noise floor yet: its own pair, with no offset.
    void renderer_under_clarity_without_a_floor_uses_claritys_pair()
    {
        SpectrumWidget w;
        w.setClarityActive(true);
        w.setClarityWaterfallThresholds(-125.0f, -65.0f);
        QVERIFY(qIsNaN(w.clarityFloorDbm()));
        QCOMPARE(w.effectiveWaterfallLevelsForTest().first, -125.0f);
        QCOMPARE(w.effectiveWaterfallLevelsForTest().second, -65.0f);
    }

    // Transmit: the transmit pair, whatever Clarity and the sliders hold.
    void renderer_under_the_transmit_overlay_is_unchanged()
    {
        SpectrumWidget w;
        w.setClarityFloorDbm(-120.0f);
        w.setClarityActive(true);
        w.setWfBlackLevel(90);
        w.setWfColorGain(10);
        w.setTxWfLowLevel(-70);
        w.setTxWfHighLevel(-10);
        w.setMoxOverlay(true);
        const std::pair<float, float> levels = w.effectiveWaterfallLevelsForTest();
        QCOMPARE(levels.first, -70.0f);
        QCOMPARE(levels.second, -10.0f);
        w.setMoxOverlay(false);
        QCOMPARE(w.effectiveWaterfallLevelsForTest().first, -100.0f);
    }

    void widget_and_model_defaults_are_the_converted_look()
    {
        SpectrumWidget w;
        QCOMPARE(w.wfBlackLevel(), 57);
        QCOMPARE(w.wfColorGain(), 60);
        w.loadSettingsForTest();
        QCOMPARE(w.wfBlackLevel(), 57);
        QCOMPARE(w.wfColorGain(), 60);
    }

    // ── The settings conversion ──────────────────────────────────────────

    void conversion_converts_bare_and_per_pan_keys_once()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayWfBlackLevel"_s, u"104"_s);
        s.setValue(u"DisplayWfColorGain"_s, u"45"_s);
        s.setValue(u"DisplayWfBlackLevel_2"_s, u"80"_s);
        s.setValue(u"DisplayWfColorGain_2"_s, u"20"_s);
        s.setValue(u"SettingsSchemaVersion"_s, u"9"_s);
        s.setValue(u"DisplayWfColorScheme"_s, u"3"_s);

        AppSettings::convertWaterfallLevelsForClarityModel(s);

        QCOMPARE(s.value(u"DisplayWfBlackLevel"_s).toString(), u"57"_s);
        QCOMPARE(s.value(u"DisplayWfColorGain"_s).toString(), u"60"_s);
        QCOMPARE(s.value(u"DisplayWfBlackLevel_2"_s).toInt(), CL::convertLegacyBlackLevel(80));
        QCOMPARE(s.value(u"DisplayWfColorGain_2"_s).toInt(), CL::convertLegacyColorGain(20, 80));
        // (80, 20) by hand: N + 13 dB is Black 76; span 36 dB is Gain 63.
        QCOMPARE(s.value(u"DisplayWfBlackLevel_2"_s).toString(), u"76"_s);
        QCOMPARE(s.value(u"DisplayWfColorGain_2"_s).toString(), u"63"_s);
        QCOMPARE(s.value(kMarker).toString(), u"True"_s);
        // Nothing else moved, the schema version above all.
        QCOMPARE(s.value(u"SettingsSchemaVersion"_s).toString(), u"9"_s);
        QCOMPARE(s.value(u"DisplayWfColorScheme"_s).toString(), u"3"_s);
        QCOMPARE(s.allKeys().size(), 7);

        // A second run changes nothing: 57 is not converted again.
        const QStringList keysAfterFirst = s.allKeys();
        AppSettings::convertWaterfallLevelsForClarityModel(s);
        QCOMPARE(s.value(u"DisplayWfBlackLevel"_s).toString(), u"57"_s);
        QCOMPARE(s.value(u"DisplayWfColorGain"_s).toString(), u"60"_s);
        QCOMPARE(s.value(u"DisplayWfBlackLevel_2"_s).toString(), u"76"_s);
        QCOMPARE(s.value(u"DisplayWfColorGain_2"_s).toString(), u"63"_s);
        QCOMPARE(s.allKeys(), keysAfterFirst);

        // The marker reached the file: a new run on the saved file, as at
        // the next launch, converts nothing either.
        AppSettings reloaded(dir.filePath(u"NereusSDR.settings"_s));
        reloaded.load();
        QCOMPARE(reloaded.value(kMarker).toString(), u"True"_s);
        AppSettings::convertWaterfallLevelsForClarityModel(reloaded);
        QCOMPARE(reloaded.value(u"DisplayWfBlackLevel"_s).toString(), u"57"_s);
        QCOMPARE(reloaded.value(u"DisplayWfColorGain_2"_s).toString(), u"63"_s);
    }

    void conversion_of_a_file_with_no_such_keys_sets_only_the_marker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayWfColorScheme"_s, u"3"_s);

        AppSettings::convertWaterfallLevelsForClarityModel(s);

        QStringList keys = s.allKeys();
        keys.sort();
        QCOMPARE(keys, (QStringList{u"DisplayWfClarityLevelsConverted"_s,
                                    u"DisplayWfColorScheme"_s}));
        QCOMPARE(s.value(kMarker).toString(), u"True"_s);
    }

    // Pan 0's own keys ("_0") are converted like any other pan's.
    void conversion_includes_pan_zeros_own_keys()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayWfBlackLevel_0"_s, u"125"_s);
        s.setValue(u"DisplayWfColorGain_0"_s, u"100"_s);
        s.setValue(u"DisplayWfBlackLevel_11"_s, u"0"_s);

        AppSettings::convertWaterfallLevelsForClarityModel(s);

        QCOMPARE(s.value(u"DisplayWfBlackLevel_0"_s).toString(), u"40"_s);
        QCOMPARE(s.value(u"DisplayWfColorGain_0"_s).toString(), u"71"_s);
        QCOMPARE(s.value(u"DisplayWfBlackLevel_11"_s).toString(), u"100"_s);
        // No Setup default was stored, so none is written.
        QVERIFY(!s.contains(u"DisplayWfBlackLevel"_s));
        QVERIFY(!s.contains(u"DisplayWfColorGain"_s));
    }

    // A pan's Color Gain converts with the Black Level that pan showed:
    // its own, else the Setup default, else the old ship default.
    void conversion_uses_the_black_level_the_pan_showed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayWfBlackLevel"_s, u"90"_s);
        s.setValue(u"DisplayWfColorGain_1"_s, u"30"_s);   // inherits Black 90
        s.setValue(u"DisplayWfBlackLevel_3"_s, u"110"_s);
        s.setValue(u"DisplayWfColorGain_3"_s, u"30"_s);   // its own Black 110

        AppSettings::convertWaterfallLevelsForClarityModel(s);

        QCOMPARE(s.value(u"DisplayWfColorGain_1"_s).toInt(), CL::convertLegacyColorGain(30, 90));
        QCOMPARE(s.value(u"DisplayWfColorGain_3"_s).toInt(), CL::convertLegacyColorGain(30, 110));
        QVERIFY(CL::convertLegacyColorGain(30, 90) != CL::convertLegacyColorGain(30, 110));
        QVERIFY(!s.contains(u"DisplayWfBlackLevel_1"_s));

        // With no Black Level stored anywhere, the old ship default (104).
        QTemporaryDir dir2;
        QVERIFY(dir2.isValid());
        AppSettings bare(dir2.filePath(u"NereusSDR.settings"_s));
        bare.setValue(u"DisplayWfColorGain"_s, u"80"_s);
        AppSettings::convertWaterfallLevelsForClarityModel(bare);
        QCOMPARE(bare.value(u"DisplayWfColorGain"_s).toInt(), CL::convertLegacyColorGain(80, 104));
        QVERIFY(!bare.contains(u"DisplayWfBlackLevel"_s));
    }

    // The old span depended on the Black Level too. Where only a Black
    // Level was stored, the Color Gain that keeps the span is stored with
    // it, so the top of the range does not move either.
    void conversion_adds_a_gain_key_where_only_black_was_stored()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayWfBlackLevel"_s, u"80"_s);     // Setup default, gain 45 (ship)
        s.setValue(u"DisplayWfBlackLevel_2"_s, u"115"_s);  // pan 2, gain inherited
        s.setValue(u"DisplayWfBlackLevel_5"_s, u"80"_s);   // pan 5: same as the default

        AppSettings::convertWaterfallLevelsForClarityModel(s);

        const double n = -120.0;
        const auto top = [&s, n](const QString& blackKey, const QString& gainKey) {
            const int black = s.value(blackKey).toInt();
            const int gain = s.contains(gainKey) ? s.value(gainKey).toInt()
                                                 : s.value(u"DisplayWfColorGain"_s).toInt();
            return double(CL::blackPointDbm(float(n), black)) + double(CL::colorSpanDb(gain));
        };
        QVERIFY(s.contains(u"DisplayWfColorGain"_s));
        QCOMPARE(s.value(u"DisplayWfColorGain"_s).toInt(), CL::convertLegacyColorGain(45, 80));
        QVERIFY(std::abs(top(u"DisplayWfBlackLevel"_s, u"DisplayWfColorGain"_s) - oldTop(n, 45)) <= 0.5);

        QVERIFY(s.contains(u"DisplayWfColorGain_2"_s));
        QCOMPARE(s.value(u"DisplayWfColorGain_2"_s).toInt(), CL::convertLegacyColorGain(45, 115));
        QVERIFY(std::abs(top(u"DisplayWfBlackLevel_2"_s, u"DisplayWfColorGain_2"_s) - oldTop(n, 45)) <= 0.5);

        // Pan 5 showed what the Setup default shows: it keeps following it.
        QVERIFY(!s.contains(u"DisplayWfColorGain_5"_s));
        QVERIFY(std::abs(top(u"DisplayWfBlackLevel_5"_s, u"DisplayWfColorGain_5"_s) - oldTop(n, 45)) <= 0.5);
    }

    // A stored value that was never a number was never shown: left alone.
    void conversion_leaves_values_that_are_not_numbers()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings s(dir.filePath(u"NereusSDR.settings"_s));
        s.setValue(u"DisplayWfBlackLevel"_s, u""_s);
        s.setValue(u"DisplayWfColorGain_1"_s, u"abc"_s);
        s.setValue(u"DisplayWfBlackLevelExtra"_s, u"104"_s);
        s.setValue(u"DisplayWfBlackLevel_x"_s, u"104"_s);

        AppSettings::convertWaterfallLevelsForClarityModel(s);

        QCOMPARE(s.value(u"DisplayWfBlackLevel"_s).toString(), u""_s);
        QCOMPARE(s.value(u"DisplayWfBlackLevelExtra"_s).toString(), u"104"_s);
        QCOMPARE(s.value(u"DisplayWfBlackLevel_x"_s).toString(), u"104"_s);
        QCOMPARE(s.value(kMarker).toString(), u"True"_s);
    }

    // The Core never converts: these are the window's keys.
    void only_the_window_calls_the_conversion()
    {
        const auto read = [](const QString& relative) {
            QFile file(QStringLiteral(NEREUS_SOURCE_DIR "/") + relative);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        const QString call = u"convertWaterfallLevelsForClarityModel"_s;
        const QString window = read(u"src/main.cpp"_s);
        const QString core = read(u"src/server_main.cpp"_s);
        const QString coreInit = read(u"src/core/CoreInit.cpp"_s);
        QVERIFY(!window.isEmpty() && !core.isEmpty() && !coreInit.isEmpty());
        QVERIFY(!core.contains(call));
        QVERIFY(!coreInit.contains(call));
        // After the settings are loaded, before the windows are built.
        const qsizetype loaded = window.indexOf(u"CoreInit::initialize(activeProfile)"_s);
        const qsizetype converted = window.indexOf(call);
        const qsizetype windows = window.indexOf(u"GuiConnectionController connections"_s);
        QVERIFY(loaded >= 0);
        QVERIFY(converted > loaded);
        QVERIFY(windows > converted);
        // The conversion took no schema version of its own: the version is
        // 10, which is two-tone leaving TX profiles (it was 9 before that
        // merged, 2026-10-11).
        QVERIFY(coreInit.contains(u"ensureSettingsAtVersion(10)"_s));
    }
};

QTEST_MAIN(TestClarityLevels)
#include "tst_clarity_levels.moc"
