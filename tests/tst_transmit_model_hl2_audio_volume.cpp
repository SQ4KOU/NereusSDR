// no-port-check: NereusSDR-original unit-test file.  The mi0bot-Thetis
// references below are cite comments documenting which upstream lines each
// assertion verifies; no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_hl2_audio_volume.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel::computeAudioVolume — HL2 audio-volume formula.
//
// Spec §5.4.2; Plan Task 5; Issue #175.
//
// Reference: from mi0bot-Thetis console.cs:47775-47778 [v2.10.3.13-beta2]:
//
//     Audio.RadioVolume = (double)Math.Min((hl2Power * (gbb / 100)) / 93.75, 1.0);
//
// Branch order matters: HL2 path runs BEFORE the existing gbb >= 99.5
// sentinel-fallback short-circuit, so HL2 HF bands (where gbb=100.0f per
// kHermesliteRow) actually get mi0bot's formula instead of falling into
// the legacy linear path.
//
// Cases:
//   §1 HL2 HF band (40m, gbb=100) at full slider — clamps to 1.0.
//   §2 HL2 HF band (40m, gbb=100) at zero slider — returns 0.0.
//   §3 HL2 HF band (40m, gbb=100) at mid slider — exact formula value.
//   §4 HL2 6m (gbb=38.8) at full slider — formula divided by 93.75.
//   §5 Non-HL2 model on 40m — does NOT use mi0bot formula (legacy path).
// =================================================================

#include <QtTest/QtTest>

#include <cmath>

#include "core/HpsdrModel.h"
#include "core/PaProfile.h"
#include "models/Band.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TestTransmitModelHl2AudioVolume : public QObject {
    Q_OBJECT

private slots:

    // §1 HL2 HF band at full slider.
    // HF bands have gbb=100.0f (sentinel) on HL2 per kHermesliteRow.
    // mi0bot: (99 * 100/100) / 93.75 = 99/93.75 = 1.056 -> clamped to 1.0.
    void hl2_hf_band_at_full() {
        TransmitModel m;
        const PaProfile p(QStringLiteral("Default - HERMESLITE"),
                          HPSDRModel::HERMESLITE, true);
        const double v = m.computeAudioVolume(p,
                                              Band::Band40m,
                                              99,
                                              HPSDRModel::HERMESLITE);
        QVERIFY(qFuzzyCompare(v, 1.0));
    }

    // §2 HL2 HF band at zero slider.
    // mi0bot: sliderWatts<=0 short-circuit returns 0.0 (kept in front of
    // the HL2 branch — see implementation in TransmitModel::computeAudioVolume).
    void hl2_hf_band_at_zero() {
        TransmitModel m;
        const PaProfile p(QStringLiteral("Default - HERMESLITE"),
                          HPSDRModel::HERMESLITE, true);
        const double v = m.computeAudioVolume(p,
                                              Band::Band40m,
                                              0,
                                              HPSDRModel::HERMESLITE);
        // qFuzzyCompare-around-zero idiom (see Qt docs).
        QVERIFY(qFuzzyCompare(v + 1.0, 0.0 + 1.0));
    }

    // §3 HL2 HF band at mid slider.
    // mi0bot: (50 * 100/100) / 93.75 = 50/93.75 ~ 0.53333.
    void hl2_hf_band_at_mid() {
        TransmitModel m;
        const PaProfile p(QStringLiteral("Default - HERMESLITE"),
                          HPSDRModel::HERMESLITE, true);
        const double v = m.computeAudioVolume(p,
                                              Band::Band40m,
                                              50,
                                              HPSDRModel::HERMESLITE);
        QVERIFY(std::abs(v - (50.0 / 93.75)) < 1e-9);
    }

    // §4 HL2 6m at full slider.
    // 6m has a real gain entry (gbb=38.8) on HL2.
    // mi0bot: (99 * 38.8/100) / 93.75 ~ 0.4097.
    void hl2_6m_at_full() {
        TransmitModel m;
        const PaProfile p(QStringLiteral("Default - HERMESLITE"),
                          HPSDRModel::HERMESLITE, true);
        const double v = m.computeAudioVolume(p,
                                              Band::Band6m,
                                              99,
                                              HPSDRModel::HERMESLITE);
        QVERIFY(std::abs(v - ((99.0 * 0.388) / 93.75)) < 1e-6);
    }

    // §5 Non-HL2 model — must NOT use the HL2 formula.  ANAN-100 (or any
    // non-HERMESLITE model) runs through the canonical Thetis dBm-target
    // kernel (console.cs:46720-46758 [v2.10.3.13]).  Concretely: assert
    // the result differs from what the HL2 formula would have returned.
    //
    // Issue #202 deep-fix: a previous NereusSDR-original short-circuit
    //   if (gbb >= 99.5f) return clamp(sliderWatts/100.0, 0, 1);
    // was removed because it inverted the Thetis semantic "100 = no output
    // power" (clsHardwareSpecific.cs:463-466 [v2.10.3.13]) into "100 =
    // full output (linear identity)".  With the short-circuit gone, gbb=100
    // now produces audio_volume ≈ 0.000625 at slider 50 (target_dbm = 47 -
    // 100 = -53 dBm; volts ≈ 0.0005; audio = 0.000625) — matching Thetis,
    // which interprets a residual 100 as "no output power".
    void nonHl2_unchanged_path() {
        TransmitModel m;
        // Bypass-style construction with explicit gbb=100 on 40m to force
        // the "no PA gain row" semantic.
        PaProfile p(QStringLiteral("Bypass"), HPSDRModel::FIRST, true);
        p.setGainForBand(Band::Band40m, 100.0f);
        const double vHl2Formula = (50.0 * 100.0 / 100.0) / 93.75;
        const double vReal = m.computeAudioVolume(p,
                                                  Band::Band40m,
                                                  50,
                                                  HPSDRModel::ANAN100);
        // Different from the HL2 formula value.
        QVERIFY(std::abs(vReal - vHl2Formula) > 1e-6);
        // gbb=100 with 50 W slider → target_dbm = 10*log10(50000) - 100 ≈ -53.
        // target_volts = sqrt(10^-5.3 * 0.05) ≈ 0.0005.  audio = 0.000625.
        // Per Thetis "100 = no output power" semantic — essentially silent.
        QVERIFY(vReal < 1.0e-3);
    }
};

QTEST_MAIN(TestTransmitModelHl2AudioVolume)
#include "tst_transmit_model_hl2_audio_volume.moc"
