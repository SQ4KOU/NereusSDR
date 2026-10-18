// no-port-check: NereusSDR-original tests for the FFT sizing mode; no Thetis
// logic is ported (Thetis facts are cited in src/core/ControlRanges.h).
//
// =================================================================
// tests/tst_display_fft_sizing.cpp  (NereusSDR)
// =================================================================
//
// The FFT sizing mode (design D38 to D41; R-DSP-62 to R-DSP-65):
//   1. ControlRanges::planDisplayFft and displayFftSizingModeOnRead against
//      tests/data/display_fft_sizing_vectors.json, the table the phone's
//      twin reads too.
//   2. readDisplayFftSizing: the upgrade rule on a store with no mode key,
//      and that reading changes nothing.
//   3. writeDisplayFftSizingMode: the mode key first, the target key as an
//      older client understands it, the remembered target.
//   4. followHzPerBinTargetWrite: the Core's rule for a client that does not
//      know the mode.
//
// Modification history (NereusSDR):
//   2026-10-11  J.J. Boyd (KG4VCF). Created for the FFT sizing mode.
//                AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <optional>

#include "core/AppSettings.h"
#include "core/ControlRanges.h"
#include "core/DisplayFftSizing.h"

using namespace NereusSDR;
using ControlRanges::DisplayFftSizingMode;

namespace {

std::optional<DisplayFftSizingMode> modeFor(const QString& token)
{
    if (token == QLatin1String("Auto")) { return DisplayFftSizingMode::Auto; }
    if (token == QLatin1String("HzPerBin")) { return DisplayFftSizingMode::HzPerBin; }
    if (token == QLatin1String("Fixed")) { return DisplayFftSizingMode::Fixed; }
    return std::nullopt;
}

QJsonObject vectors()
{
    QFile file(QStringLiteral(NEREUS_TEST_DATA_DIR "/display_fft_sizing_vectors.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString text(AppSettings& settings, const char* key)
{
    return settings.value(QLatin1String(key)).toString();
}

// The planner is usable in a constant expression (the pill and Setup size
// arrays from it nowhere today, but the phone's twin is checked against
// these same values).
static_assert(ControlRanges::planDisplayFft(DisplayFftSizingMode::Auto, 16384, 1536000.0,
                                            35000.0, 0.0).size == 262144);
static_assert(ControlRanges::planDisplayFft(DisplayFftSizingMode::Fixed, 16384, 1536000.0,
                                            35000.0, 0.0).size == 16384);
static_assert(ControlRanges::displayFftSizingModeOnRead("", 12.5)
              == DisplayFftSizingMode::HzPerBin);

}  // namespace

class TestDisplayFftSizing : public QObject
{
    Q_OBJECT

private slots:

    void tokens_round_trip()
    {
        for (DisplayFftSizingMode mode : {DisplayFftSizingMode::Auto,
                                          DisplayFftSizingMode::HzPerBin,
                                          DisplayFftSizingMode::Fixed}) {
            const auto back = modeFor(QLatin1String(ControlRanges::displayFftSizingModeToken(mode)));
            QVERIFY(back.has_value());
            QCOMPARE(int(*back), int(mode));
        }
        QCOMPARE(QLatin1String(ControlRanges::kDisplayFftSizingModeKey),
                 QLatin1String("DisplayFftSizingMode"));
        QCOMPARE(QLatin1String(ControlRanges::kDisplayHzPerBinLastTargetKey),
                 QLatin1String("DisplayHzPerBinLastTarget"));
        QCOMPARE(int(ControlRanges::kDisplayFftSizingModeDefault), int(DisplayFftSizingMode::Auto));
    }

    void planner_vectors_data()
    {
        QTest::addColumn<QJsonObject>("row");
        const QJsonArray rows = vectors().value(QStringLiteral("plan")).toArray();
        for (const QJsonValue& value : rows) {
            const QJsonObject row = value.toObject();
            QTest::newRow(qPrintable(row.value(QStringLiteral("name")).toString())) << row;
        }
    }

    void planner_vectors()
    {
        QFETCH(QJsonObject, row);
        const auto mode = modeFor(row.value(QStringLiteral("mode")).toString());
        QVERIFY(mode.has_value());
        const ControlRanges::AutoZoomPlan plan = ControlRanges::planDisplayFft(
            *mode, row.value(QStringLiteral("storedSize")).toInt(),
            row.value(QStringLiteral("sampleRateHz")).toDouble(),
            row.value(QStringLiteral("spanHz")).toDouble(),
            row.value(QStringLiteral("hzPerBinTarget")).toDouble());
        QCOMPARE(plan.size, row.value(QStringLiteral("size")).toInt());
        QCOMPARE(plan.hzPerBinInCharge, row.value(QStringLiteral("hzPerBinInCharge")).toBool());
    }

    // The table holds the rows the design names; a table that lost them
    // would pass the loop above.
    void planner_vectors_hold_the_named_cases()
    {
        const QJsonArray rows = vectors().value(QStringLiteral("plan")).toArray();
        QVERIFY(rows.size() >= 30);
        int fixedSizes = 0;
        bool autoAtMaxRate = false;
        for (const QJsonValue& value : rows) {
            const QJsonObject row = value.toObject();
            const QString mode = row.value(QStringLiteral("mode")).toString();
            const bool wide = row.value(QStringLiteral("sampleRateHz")).toDouble() == 1536000.0
                && row.value(QStringLiteral("spanHz")).toDouble() == 35000.0;
            if (mode == QLatin1String("Fixed") && wide) { ++fixedSizes; }
            if (mode == QLatin1String("Auto") && wide
                && row.value(QStringLiteral("storedSize")).toInt() == 16384) {
                autoAtMaxRate = row.value(QStringLiteral("size")).toInt() == 262144;
            }
        }
        QCOMPARE(fixedSizes, ControlRanges::kDisplayFftSizePositionMax + 1);
        QVERIFY(autoAtMaxRate);
    }

    // Fixed is each of the seven sizes at every rate and span, with no cap.
    void fixed_is_exactly_each_size()
    {
        for (int position = 0; position <= ControlRanges::kDisplayFftSizePositionMax; ++position) {
            const int size = ControlRanges::displayFftSizeAt(position);
            for (double rate : {48000.0, 192000.0, 1536000.0}) {
                for (double span : {500.0, 12000.0, 35000.0, rate}) {
                    QCOMPARE(ControlRanges::planDisplayFft(DisplayFftSizingMode::Fixed, size,
                                                           rate, span, 0.0).size, size);
                }
            }
            QCOMPARE(ControlRanges::displayFftSizingBaseline(DisplayFftSizingMode::Fixed, size),
                     size);
            QCOMPARE(ControlRanges::displayFftSizingBaseline(DisplayFftSizingMode::Auto, size),
                     ControlRanges::kDisplayFftSizeDefault);
            QCOMPARE(ControlRanges::displayFftSizingBaseline(DisplayFftSizingMode::HzPerBin, size),
                     ControlRanges::kDisplayFftSizeDefault);
        }
    }

    // Auto and Hz/bin never read the stored size.
    void auto_and_hz_per_bin_ignore_the_stored_size()
    {
        for (double rate : {48000.0, 192000.0, 1536000.0}) {
            for (double span : {1000.0, 12000.0, 35000.0, rate}) {
                const int autoSize = ControlRanges::planDisplayFft(
                    DisplayFftSizingMode::Auto, ControlRanges::kDisplayFftSizeDefault, rate,
                    span, 0.0).size;
                const int targetSize = ControlRanges::planDisplayFft(
                    DisplayFftSizingMode::HzPerBin, ControlRanges::kDisplayFftSizeDefault, rate,
                    span, 12.5).size;
                for (int position = 0; position <= ControlRanges::kDisplayFftSizePositionMax;
                     ++position) {
                    const int stored = ControlRanges::displayFftSizeAt(position);
                    QCOMPARE(ControlRanges::planDisplayFft(DisplayFftSizingMode::Auto, stored,
                                                           rate, span, 0.0).size, autoSize);
                    QCOMPARE(ControlRanges::planDisplayFft(DisplayFftSizingMode::HzPerBin, stored,
                                                           rate, span, 12.5).size, targetSize);
                }
            }
        }
    }

    void on_read_vectors_data()
    {
        QTest::addColumn<QJsonObject>("row");
        const QJsonArray rows = vectors().value(QStringLiteral("onRead")).toArray();
        QVERIFY(rows.size() >= 11);
        for (const QJsonValue& value : rows) {
            const QJsonObject row = value.toObject();
            QTest::newRow(qPrintable(row.value(QStringLiteral("name")).toString())) << row;
        }
    }

    void on_read_vectors()
    {
        QFETCH(QJsonObject, row);
        const QByteArray stored = row.value(QStringLiteral("storedMode")).toString().toLatin1();
        const auto expected = modeFor(row.value(QStringLiteral("mode")).toString());
        QVERIFY(expected.has_value());
        QCOMPARE(int(ControlRanges::displayFftSizingModeOnRead(
                     std::string_view(stored.constData(), std::size_t(stored.size())),
                     row.value(QStringLiteral("hzPerBinTarget")).toDouble())),
                 int(*expected));
    }

    // ── The upgrade rule on a real store (R-DSP-63) ──────────────────────

    void upgrade_no_target_reads_auto_and_keeps_the_size()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        settings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("16384"));
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("0"));

        const DisplayFftSizing sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::Auto));
        QCOMPARE(sizing.storedSize, 16384);
        QCOMPARE(sizing.hzPerBinTarget, 0.0);
        QCOMPARE(sizing.lastHzPerBinTarget, 0.0);
        QCOMPARE(sizing.baseline(), 4096);
        // The stored size plays no part in what Auto plans.
        QCOMPARE(sizing.plan(1536000.0, 35000.0).size, 262144);
        QCOMPARE(sizing.plan(192000.0, 192000.0).size, 4096);

        // Reading is all it did: the size stays and no key appears.
        QCOMPARE(text(settings, "DisplayFftSize"), QStringLiteral("16384"));
        QVERIFY(!settings.contains(QStringLiteral("DisplayFftSizingMode")));
        QVERIFY(!settings.contains(QStringLiteral("DisplayHzPerBinLastTarget")));
        QCOMPARE(int(readDisplayFftSizingMode(settings)), int(DisplayFftSizingMode::Auto));
    }

    void upgrade_with_a_target_reads_hz_per_bin()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        settings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("16384"));
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("12.5"));

        const DisplayFftSizing sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::HzPerBin));
        QCOMPARE(sizing.hzPerBinTarget, 12.5);
        // With none remembered, the target in force is the one to restore.
        QCOMPARE(sizing.lastHzPerBinTarget, 12.5);
        QCOMPARE(sizing.storedSize, 16384);
        QCOMPARE(sizing.baseline(), 4096);
        QCOMPARE(sizing.plan(1536000.0, 35000.0).size, 131072);
        QVERIFY(!settings.contains(QStringLiteral("DisplayFftSizingMode")));
        QCOMPARE(text(settings, "DisplayFftSize"), QStringLiteral("16384"));
    }

    void an_empty_store_reads_the_defaults()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        const DisplayFftSizing sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::Auto));
        QCOMPARE(sizing.storedSize, 4096);
        QCOMPARE(sizing.hzPerBinTarget, 0.0);
    }

    void a_stored_size_that_is_not_an_fft_size_reads_as_the_default()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        settings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("5000"));
        settings.setValue(QStringLiteral("DisplayFftSizingMode"), QStringLiteral("Fixed"));
        const DisplayFftSizing sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::Fixed));
        QCOMPARE(sizing.storedSize, 4096);
        QCOMPARE(sizing.plan(192000.0, 35000.0).size, 4096);
        // Not repaired by a read.
        QCOMPARE(text(settings, "DisplayFftSize"), QStringLiteral("5000"));
    }

    // ── Writing a mode (R-DSP-64) ────────────────────────────────────────

    void writing_fixed_keeps_the_size_and_clears_the_target()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        settings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("32768"));
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("12.5"));

        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::Fixed);
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("Fixed"));
        QCOMPARE(text(settings, "DisplayHzPerBinTarget").toDouble(), 0.0);
        QCOMPARE(text(settings, "DisplayFftSize"), QStringLiteral("32768"));
        const DisplayFftSizing sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::Fixed));
        QCOMPARE(sizing.baseline(), 32768);
        QCOMPARE(sizing.plan(1536000.0, 35000.0).size, 32768);
        QCOMPARE(sizing.plan(1536000.0, 1000.0).size, 32768);
    }

    void hz_per_bin_remembers_its_target_across_modes()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::HzPerBin, 7.5);
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("HzPerBin"));
        QCOMPARE(text(settings, "DisplayHzPerBinTarget").toDouble(), 7.5);
        QCOMPARE(text(settings, "DisplayHzPerBinLastTarget").toDouble(), 7.5);

        // Leaving Hz/bin: the target key says "off", as an older client
        // reads it, and the choice is remembered.
        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::Auto);
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("Auto"));
        QCOMPARE(text(settings, "DisplayHzPerBinTarget").toDouble(), 0.0);
        DisplayFftSizing sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::Auto));
        QCOMPARE(sizing.hzPerBinTarget, 0.0);
        QCOMPARE(sizing.lastHzPerBinTarget, 7.5);

        // Re-entering with no target given restores it.
        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::HzPerBin);
        sizing = readDisplayFftSizing(settings);
        QCOMPARE(int(sizing.mode), int(DisplayFftSizingMode::HzPerBin));
        QCOMPARE(sizing.hzPerBinTarget, 7.5);
        QCOMPARE(text(settings, "DisplayHzPerBinTarget").toDouble(), 7.5);
    }

    void hz_per_bin_target_is_clamped_and_never_zero()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        // None given and none remembered: the coarsest target.
        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::HzPerBin);
        QCOMPARE(readDisplayFftSizing(settings).hzPerBinTarget,
                 ControlRanges::kDisplayHzPerBinTargetMax);
        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::HzPerBin, 5000.0);
        QCOMPARE(readDisplayFftSizing(settings).hzPerBinTarget,
                 ControlRanges::kDisplayHzPerBinTargetMax);
        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::HzPerBin, 2.5);
        QCOMPARE(readDisplayFftSizing(settings).hzPerBinTarget, 2.5);
        QCOMPARE(int(readDisplayFftSizingMode(settings)), int(DisplayFftSizingMode::HzPerBin));
    }

    // ── The Core's rule for a client that does not know the mode ─────────

    void an_old_clients_target_moves_the_mode()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        // Above 0 sets HzPerBin, from no key and from Auto.
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("12.5"));
        QVERIFY(followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("HzPerBin"));
        QCOMPARE(text(settings, "DisplayHzPerBinLastTarget").toDouble(), 12.5);
        // Another target in the same mode: nothing to move.
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("3"));
        QVERIFY(!followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("HzPerBin"));
        QCOMPARE(text(settings, "DisplayHzPerBinLastTarget").toDouble(), 3.0);

        // 0 sets Auto when the mode was HzPerBin.
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("0"));
        QVERIFY(followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("Auto"));
        QCOMPARE(readDisplayFftSizing(settings).lastHzPerBinTarget, 3.0);
        // 0 again in Auto: nothing.
        QVERIFY(!followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("Auto"));

        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("6"));
        QVERIFY(followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("HzPerBin"));
    }

    void an_old_clients_zero_leaves_fixed_alone()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        settings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("65536"));
        writeDisplayFftSizingMode(settings, DisplayFftSizingMode::Fixed);

        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("0"));
        QVERIFY(!followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("Fixed"));
        QCOMPARE(readDisplayFftSizing(settings).plan(192000.0, 1000.0).size, 65536);

        // A target above 0 takes over even from Fixed; the size is kept.
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("12.5"));
        QVERIFY(followHzPerBinTargetWrite(settings));
        QCOMPARE(text(settings, "DisplayFftSizingMode"), QStringLiteral("HzPerBin"));
        QCOMPARE(text(settings, "DisplayFftSize"), QStringLiteral("65536"));
    }

    void an_old_clients_zero_with_no_mode_key_writes_nothing()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        settings.setValue(QStringLiteral("DisplayHzPerBinTarget"), QStringLiteral("0"));
        QVERIFY(!followHzPerBinTargetWrite(settings));
        QVERIFY(!settings.contains(QStringLiteral("DisplayFftSizingMode")));
    }

    // A client that knows the mode writes the mode first; the rule then
    // leaves each of its writes as written.
    void an_aware_clients_writes_are_left_as_written()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings settings(tmp.filePath(QStringLiteral("NereusSDR.settings")));
        for (DisplayFftSizingMode mode : {DisplayFftSizingMode::HzPerBin,
                                          DisplayFftSizingMode::Fixed,
                                          DisplayFftSizingMode::HzPerBin,
                                          DisplayFftSizingMode::Auto,
                                          DisplayFftSizingMode::Fixed,
                                          DisplayFftSizingMode::Auto}) {
            writeDisplayFftSizingMode(settings, mode, 4.0);
            QVERIFY(!followHzPerBinTargetWrite(settings));
            QCOMPARE(int(readDisplayFftSizingMode(settings)), int(mode));
        }
    }
};

QTEST_MAIN(TestDisplayFftSizing)
#include "tst_display_fft_sizing.moc"
