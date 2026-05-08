// tst_per_band_nf_priming.cpp
//
// no-port-check: Test file references no Thetis source; this is a
// NereusSDR-original enhancement (Task 2.10, per-band NF-estimate priming).
//
// Verifies three invariants:
//
//  1. prime_applies_stored_nf_on_band_change
//     setBandNFEstimate(Band::B40m, -110) + setCenterFrequency(40m)
//     → PanadapterModel::bandNFEstimate(Band40m) reads back -110.
//     ClarityController::snapToFloor path is tested separately
//     (architecture: MainWindow wires pan::bandChanged → clarity::snapToFloor;
//     unit tests below test PanadapterModel's own storage, not the wiring.)
//
//  2. persistence_round_trip
//     Write -108.5 via setBandNFEstimate, reconstruct PanadapterModel,
//     verify the value loads back from AppSettings.
//
//  3. setter_idempotent_guard
//     Calling setBandNFEstimate with the same value twice does not corrupt the
//     stored float (exercises the qFuzzyCompare guard).
//
// Architecture note:
//   NoiseFloorEstimator::prime() and ClarityController::snapToFloor() are
//   tested in tst_noise_floor_estimator.cpp and tst_clarity_controller.cpp
//   respectively. The settle-detector wiring in MainWindow is exercised by
//   the end-to-end build test (not unit-testable without a live FFT feed).
//   The unit tests here focus on PanadapterModel's new storage/persistence
//   surface (the part it directly owns).

#include <QtTest/QtTest>
#include <QtNumeric>

#include "models/PanadapterModel.h"
#include "models/Band.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestPerBandNFPriming : public QObject {
    Q_OBJECT

private:
    static void clearNFKeys() {
        auto& s = AppSettings::instance();
        // Clear all 14 per-band NF keys to avoid cross-test contamination.
        for (int i = 0; i < static_cast<int>(Band::SwlFirst); ++i) {
            const Band b = static_cast<Band>(i);
            s.remove(QStringLiteral("DisplayBandNFEstimate_") + bandKeyName(b));
        }
    }

private slots:
    void init()    { clearNFKeys(); }
    void cleanup() { clearNFKeys(); }

    // ── Invariant 1: accessor round-trip (NaN → value → NaN reset) ──────────

    void getter_returns_nan_before_any_set()
    {
        // Freshly constructed model — no stored NF for any band.
        PanadapterModel pan;
        QVERIFY(qIsNaN(pan.bandNFEstimate(Band::Band40m)));
        QVERIFY(qIsNaN(pan.bandNFEstimate(Band::Band20m)));
        QVERIFY(qIsNaN(pan.bandNFEstimate(Band::GEN)));
    }

    void setter_round_trip_one_band()
    {
        PanadapterModel pan;
        pan.setBandNFEstimate(Band::Band40m, -110.0f);
        QCOMPARE(pan.bandNFEstimate(Band::Band40m), -110.0f);
        // Other bands still NaN.
        QVERIFY(qIsNaN(pan.bandNFEstimate(Band::Band20m)));
    }

    void setter_round_trip_multiple_bands()
    {
        PanadapterModel pan;
        pan.setBandNFEstimate(Band::Band40m, -110.0f);
        pan.setBandNFEstimate(Band::Band6m,  -135.0f);
        QCOMPARE(pan.bandNFEstimate(Band::Band40m), -110.0f);
        QCOMPARE(pan.bandNFEstimate(Band::Band6m),  -135.0f);
        // Unset bands untouched.
        QVERIFY(qIsNaN(pan.bandNFEstimate(Band::Band20m)));
    }

    // ── Invariant 2: persistence round-trip ──────────────────────────────────

    void persistence_round_trip_40m()
    {
        // NereusSDR-original — no Thetis equivalent.
        // Write in one instance, read in a fresh instance.
        {
            PanadapterModel pan;
            pan.setBandNFEstimate(Band::Band40m, -108.5f);
        }
        // Re-construct — should load from AppSettings.
        PanadapterModel pan2;
        QCOMPARE(pan2.bandNFEstimate(Band::Band40m), -108.5f);
        // Other bands still NaN.
        QVERIFY(qIsNaN(pan2.bandNFEstimate(Band::Band20m)));
    }

    void persistence_round_trip_multi_band_isolation()
    {
        // Write two different bands, verify they don't bleed across.
        {
            PanadapterModel pan;
            pan.setBandNFEstimate(Band::Band80m,  -112.3f);
            pan.setBandNFEstimate(Band::Band10m,  -140.7f);
        }
        PanadapterModel pan2;
        QCOMPARE(pan2.bandNFEstimate(Band::Band80m),  -112.3f);
        QCOMPARE(pan2.bandNFEstimate(Band::Band10m),  -140.7f);
        QVERIFY(qIsNaN(pan2.bandNFEstimate(Band::Band40m)));
        QVERIFY(qIsNaN(pan2.bandNFEstimate(Band::Band6m)));
    }

    // ── Invariant 3: idempotent guard (same-value write no-op) ──────────────

    void setter_idempotent_guard_same_value()
    {
        // Calling with the same value twice must not corrupt the stored float.
        // The guard uses qFuzzyCompare; equal floats must pass silently.
        PanadapterModel pan;
        pan.setBandNFEstimate(Band::Band20m, -125.0f);
        pan.setBandNFEstimate(Band::Band20m, -125.0f);  // no-op
        QCOMPARE(pan.bandNFEstimate(Band::Band20m), -125.0f);
    }

    void setter_updates_when_value_changes()
    {
        // After the initial write, a changed value must be stored.
        PanadapterModel pan;
        pan.setBandNFEstimate(Band::Band20m, -125.0f);
        pan.setBandNFEstimate(Band::Band20m, -130.0f);  // different
        QCOMPARE(pan.bandNFEstimate(Band::Band20m), -130.0f);
    }

    // ── Invariant 4: AppSettings key naming convention ───────────────────────

    void appSettings_key_follows_convention()
    {
        // Keys must follow the DisplayBandNFEstimate_<bandKeyName> pattern.
        // This is observable: write via setBandNFEstimate, read the raw key.
        PanadapterModel pan;
        pan.setBandNFEstimate(Band::Band40m, -100.0f);
        const QString key = QStringLiteral("DisplayBandNFEstimate_") + bandKeyName(Band::Band40m);
        const QVariant stored = AppSettings::instance().value(key);
        QVERIFY(stored.isValid());
        QCOMPARE(stored.toFloat(), -100.0f);
    }

    // ── Invariant 5: band-change updates stored band correctly ───────────────

    void band_change_via_setCenterFrequency_updates_m_band()
    {
        // setCenterFrequency auto-derives the band. After the switch,
        // pan.band() reflects the new band so setBandNFEstimate(pan.band(), nf)
        // in the settle detector writes to the correct slot.
        PanadapterModel pan;
        pan.setCenterFrequency(7150000.0);  // 40m
        QCOMPARE(pan.band(), Band::Band40m);
        pan.setCenterFrequency(50300000.0); // 6m
        QCOMPARE(pan.band(), Band::Band6m);
    }
};

QTEST_MAIN(TestPerBandNFPriming)
#include "tst_per_band_nf_priming.moc"
