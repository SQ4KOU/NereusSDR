// tests/tst_slice_nb_persistence.cpp — Phase 3G RX Epic Sub-epic B
//
// Tests that SliceModel's NbMode + NbTuning round-trip through the
// per-slice-per-band AppSettings namespace established in Phase 3G-10 Stage 2.
//
// Isolation: TestSandboxInit.cpp (auto-linked by nereus_add_test) calls
// QStandardPaths::setTestModeEnabled(true) before main(), redirecting
// AppSettings to the Qt test sandbox rather than the real user config file.
// Keys written during tests are cleaned up in cleanup() to prevent cross-test
// pollution within the shared sandbox.

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/NbFamily.h"
#include "models/Band.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestSliceNbPersistence : public QObject {
    Q_OBJECT

private:
    // Remove all Slice0/Band{20m,40m}/Nb* keys from the singleton so
    // tests start clean and don't step on each other.
    static void resetNbKeys() {
        auto& s = AppSettings::instance();
        for (const QString& band : {QStringLiteral("20m"), QStringLiteral("40m")}) {
            const QString bp = QStringLiteral("Slice0/Band") + band + QStringLiteral("/");
            for (const QString& field : {
                     QStringLiteral("NbMode"),
                     QStringLiteral("NbThreshold"),
                     QStringLiteral("NbTauMs"),
                     QStringLiteral("NbLeadMs"),
                     QStringLiteral("NbLagMs") }) {
                s.remove(bp + field);
            }
        }
    }

private slots:

    void init()    { resetNbKeys(); }
    void cleanup() { resetNbKeys(); }

    // ── NbMode round-trip on 40m ──────────────────────────────────────────────

    void nbMode_round_trips_per_band() {
        // Save NB2 for 40m, restore on a fresh model — must come back as NB2.
        {
            SliceModel a(0);
            a.setNbMode(NbMode::NB2);
            a.saveToSettings(Band::Band40m);
        }
        {
            SliceModel b(0);
            b.restoreFromSettings(Band::Band40m);
            QCOMPARE(b.nbMode(), NbMode::NB2);
        }
    }

    // ── NbTuning round-trip on 20m ────────────────────────────────────────────

    void nb_tuning_round_trips_per_band() {
        NbTuning t;
        t.nbThreshold = 42.5;
        t.nbHangMs    = 17.0;
        t.nbAdvMs     = 13.0;
        t.nbTauMs     = 0.7;

        {
            SliceModel a(0);
            a.setNbTuning(t);
            a.saveToSettings(Band::Band20m);
        }
        {
            SliceModel b(0);
            b.restoreFromSettings(Band::Band20m);
            const NbTuning got = b.nbTuning();
            QCOMPARE(got.nbThreshold, 42.5);
            QCOMPARE(got.nbHangMs,    17.0);
            QCOMPARE(got.nbAdvMs,     13.0);
            QCOMPARE(got.nbTauMs,     0.7);
        }
    }

    // ── NbMode is band-separate: 40m=NB, 20m=NB2 ─────────────────────────────

    void nbMode_separate_across_bands() {
        // Write different modes for two bands in one pass.
        {
            SliceModel a(0);
            a.setNbMode(NbMode::NB);  a.saveToSettings(Band::Band40m);
            a.setNbMode(NbMode::NB2); a.saveToSettings(Band::Band20m);
        }
        // Verify both survive independently on a fresh slice.
        {
            SliceModel b(0);
            b.restoreFromSettings(Band::Band40m); QCOMPARE(b.nbMode(), NbMode::NB);
            b.restoreFromSettings(Band::Band20m); QCOMPARE(b.nbMode(), NbMode::NB2);
        }
    }

    // ── Missing key leaves default unchanged ──────────────────────────────────

    void missing_key_leaves_default_unchanged() {
        // No keys written for 40m — restoreFromSettings must be a no-op.
        SliceModel s(0);
        const NbMode defaultMode = s.nbMode();  // Off by default
        s.restoreFromSettings(Band::Band40m);
        QCOMPARE(s.nbMode(), defaultMode);
    }
};

QTEST_MAIN(TestSliceNbPersistence)
#include "tst_slice_nb_persistence.moc"
