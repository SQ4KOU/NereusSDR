// no-port-check: test fixture; cite comments reference upstream
// setup.Designer.cs / setup.cs / clsHardwareSpecific.cs as documentation of
// the auto-default behaviour under test.
//
// Issue #202 deep-fix regression coverage: defaultModelForBoard() must NOT
// auto-pick HPSDRModel::ORIONMKII for an OrionMKII boardtype.  The bare
// ORIONMKII enum is excluded from Thetis's user-facing comboRadioModel.Items
// list (setup.Designer.cs:8515-8528 [v2.10.3.13]) and from
// StringModelToEnum (clsHardwareSpecific.cs:316-351 [v2.10.3.13]).  It
// shares the Hermes 41.x dB PA-gain row at clsHardwareSpecific.cs:471-486
// [v2.10.3.13]; productized OrionMKII boards (ANAN-7000DLE / 8000DLE /
// AnvelinaPro3 / RedPitaya) all have ~50 dB PA gain.  Auto-defaulting
// to ORIONMKII over-drove those PAs — exactly w3ub's "max regardless of
// slider" report on a 7000DLE Mk II.

#include <QtTest/QtTest>
#include "core/HardwareProfile.h"
#include "core/HpsdrModel.h"

using NereusSDR::HPSDRHW;
using NereusSDR::HPSDRModel;
using NereusSDR::defaultModelForBoard;

class TestDefaultModelForBoard : public QObject {
    Q_OBJECT
private slots:

    // ── Atlas / Hermes / HermesII / Angelia / Orion / Saturn / SaturnMKII ──
    // These boardtypes have either a single or unambiguous default; the
    // auto-pick lands on the lowest-enum-value matching HPSDRModel.
    void atlas_returns_HPSDR() {
        QCOMPARE(defaultModelForBoard(HPSDRHW::Atlas), HPSDRModel::HPSDR);
    }
    void hermes_returns_HERMES() {
        QCOMPARE(defaultModelForBoard(HPSDRHW::Hermes), HPSDRModel::HERMES);
    }
    void hermesII_returns_ANAN10E() {
        // ANAN10E (idx 3) precedes ANAN100B (idx 5) in enum order.
        QCOMPARE(defaultModelForBoard(HPSDRHW::HermesII), HPSDRModel::ANAN10E);
    }
    void angelia_returns_ANAN100D() {
        QCOMPARE(defaultModelForBoard(HPSDRHW::Angelia), HPSDRModel::ANAN100D);
    }
    void orion_returns_ANAN200D() {
        QCOMPARE(defaultModelForBoard(HPSDRHW::Orion), HPSDRModel::ANAN200D);
    }
    void hermesLite_returns_HERMESLITE() {
        QCOMPARE(defaultModelForBoard(HPSDRHW::HermesLite),
                 HPSDRModel::HERMESLITE);
    }
    void saturn_returns_ANAN_G2() {
        QCOMPARE(defaultModelForBoard(HPSDRHW::Saturn), HPSDRModel::ANAN_G2);
    }
    void saturnMKII_returns_ANAN_G2_via_specialcase() {
        // SaturnMKII has no dedicated HPSDRModel — special-cased to ANAN_G2.
        QCOMPARE(defaultModelForBoard(HPSDRHW::SaturnMKII),
                 HPSDRModel::ANAN_G2);
    }

    // ── #202 root cause: OrionMKII must NOT return ORIONMKII ────────────────
    //
    // The bare ORIONMKII enum is intentionally skipped in the auto-pick
    // walk (HardwareProfile.cpp::defaultModelForBoard).  Iteration falls
    // through to ANAN7000D (idx 9, lowest matching enum after ORIONMKII
    // is skipped).  Both ANAN7000D and ANAN8000D map to HPSDRHW::OrionMKII
    // via boardForModel, but ANAN7000D is enum-first.
    //
    // Cite (the exclusion rationale):
    //   - Thetis comboRadioModel.Items at setup.Designer.cs:8515-8528
    //     [v2.10.3.13] omits "ORIONMKII".
    //   - StringModelToEnum at clsHardwareSpecific.cs:316-351 [v2.10.3.13]
    //     has no string entry for "ORIONMKII".
    //   - database.cs:10350 [v2.10.3.13] notes: "not implemented in
    //     comboRadioModel list items".
    //   - DefaultPAGainsForBands(ORIONMKII) at clsHardwareSpecific.cs:
    //     471-486 [v2.10.3.13] shares the Hermes 41.x dB row, which is
    //     ~9 dB low for productized OrionMKII PAs (kAnan7000dRow ~50 dB).
    void orionMKII_returns_ANAN7000D_not_ORIONMKII() {
        const auto picked = defaultModelForBoard(HPSDRHW::OrionMKII);
        QCOMPARE(picked, HPSDRModel::ANAN7000D);
        // Pin: must NOT be ORIONMKII.  Regression guard for #202.
        QVERIFY(picked != HPSDRModel::ORIONMKII);
    }
};

QTEST_MAIN(TestDefaultModelForBoard)
#include "tst_default_model_for_board.moc"
