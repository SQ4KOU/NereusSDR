// no-port-check: NereusSDR-original unit-test file.  The Thetis cite
// comments below identify which upstream lines each assertion verifies;
// no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_two_tone_drive_origin.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel TwoToneDrivePowerOrigin enum property,
// Phase 3M-1c B.3.
//
// Source references:
//   enums.cs:456-461 [v2.10.3.13] — public enum DrivePowerSource
//     { DRIVE_SLIDER = 0, TUNE_SLIDER = 1, FIXED = 2 }
//   console.cs:46553 [v2.10.3.13] — _2ToneDrivePowerSource = DRIVE_SLIDER
//     (default).
//   console.cs:46576-46597 [v2.10.3.13] — TwoToneDrivePowerOrigin property
//     (Thetis console-side; NereusSDR puts it on TransmitModel).
//   setup.cs:11111-11119 [v2.10.3.13] — Fixed-mode behaviour: save
//     console.PWR before MOX, override with new_pwr, restore on stop.
//   setup.cs:22850-22878 [v2.10.3.13] — Setup UI radio buttons:
//     radUseDriveSlider2Tone / radUseTuneSlider2Tone /
//     radUseFixedDrive2Tone (3 values, not 2 — pre-code review §2.3
//     abbreviated as "Fixed / Slider"; option C source-first parity
//     ports the full 3-value enum).
// =================================================================

#include <QtTest/QtTest>
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstTransmitModelTwoToneDriveOrigin : public QObject {
    Q_OBJECT
private slots:

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // DEFAULT VALUE
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void default_twoToneDrivePowerSource_isDriveSlider() {
        // Matches Thetis console.cs:46553 [v2.10.3.13]:
        //   private DrivePowerSource _2ToneDrivePowerSource = DRIVE_SLIDER;
        TransmitModel t;
        QCOMPARE(t.twoToneDrivePowerSource(), DrivePowerSource::DriveSlider);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ENUM VALUE COVERAGE — all 3 values per Thetis enums.cs:456-461
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void roundTrip_DriveSlider() {
        TransmitModel t;
        t.setTwoToneDrivePowerSource(DrivePowerSource::DriveSlider);
        QCOMPARE(t.twoToneDrivePowerSource(), DrivePowerSource::DriveSlider);
    }

    void roundTrip_TuneSlider() {
        TransmitModel t;
        t.setTwoToneDrivePowerSource(DrivePowerSource::TuneSlider);
        QCOMPARE(t.twoToneDrivePowerSource(), DrivePowerSource::TuneSlider);
    }

    void roundTrip_Fixed() {
        TransmitModel t;
        t.setTwoToneDrivePowerSource(DrivePowerSource::Fixed);
        QCOMPARE(t.twoToneDrivePowerSource(), DrivePowerSource::Fixed);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ENUM UNDERLYING VALUES — preserve Thetis numeric ordering
    // (enums.cs:458-460 [v2.10.3.13])
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void enum_DriveSlider_isZero() {
        QCOMPARE(static_cast<int>(DrivePowerSource::DriveSlider), 0);
    }

    void enum_TuneSlider_isOne() {
        QCOMPARE(static_cast<int>(DrivePowerSource::TuneSlider), 1);
    }

    void enum_Fixed_isTwo() {
        QCOMPARE(static_cast<int>(DrivePowerSource::Fixed), 2);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // SIGNAL EMISSION
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setTwoToneDrivePowerSource_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneDrivePowerSourceChanged);
        t.setTwoToneDrivePowerSource(DrivePowerSource::Fixed);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(),
                 static_cast<int>(DrivePowerSource::Fixed));
    }

    void setTwoToneDrivePowerSource_signalCarriesNewValue() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneDrivePowerSourceChanged);
        t.setTwoToneDrivePowerSource(DrivePowerSource::TuneSlider);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(),
                 static_cast<int>(DrivePowerSource::TuneSlider));
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // IDEMPOTENT GUARD
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void idempotent_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneDrivePowerSourceChanged);
        t.setTwoToneDrivePowerSource(DrivePowerSource::DriveSlider);  // default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_afterChange_noSignal() {
        TransmitModel t;
        t.setTwoToneDrivePowerSource(DrivePowerSource::Fixed);
        QSignalSpy spy(&t, &TransmitModel::twoToneDrivePowerSourceChanged);
        t.setTwoToneDrivePowerSource(DrivePowerSource::Fixed);
        QCOMPARE(spy.count(), 0);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // STRING CONVERSION (used by AppSettings persistence)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void drivePowerSourceToString_driveSlider() {
        QCOMPARE(drivePowerSourceToString(DrivePowerSource::DriveSlider),
                 QStringLiteral("DriveSlider"));
    }

    void drivePowerSourceToString_tuneSlider() {
        QCOMPARE(drivePowerSourceToString(DrivePowerSource::TuneSlider),
                 QStringLiteral("TuneSlider"));
    }

    void drivePowerSourceToString_fixed() {
        QCOMPARE(drivePowerSourceToString(DrivePowerSource::Fixed),
                 QStringLiteral("Fixed"));
    }

    void drivePowerSourceFromString_driveSlider() {
        QCOMPARE(drivePowerSourceFromString(QStringLiteral("DriveSlider")),
                 DrivePowerSource::DriveSlider);
    }

    void drivePowerSourceFromString_tuneSlider() {
        QCOMPARE(drivePowerSourceFromString(QStringLiteral("TuneSlider")),
                 DrivePowerSource::TuneSlider);
    }

    void drivePowerSourceFromString_fixed() {
        QCOMPARE(drivePowerSourceFromString(QStringLiteral("Fixed")),
                 DrivePowerSource::Fixed);
    }

    void drivePowerSourceFromString_unknown_fallsBackToDriveSlider() {
        // Unknown string fallback to default (DriveSlider) — matches
        // VaxSlot::fromString fallback pattern.
        QCOMPARE(drivePowerSourceFromString(QStringLiteral("Bogus")),
                 DrivePowerSource::DriveSlider);
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelTwoToneDriveOrigin)
#include "tst_transmit_model_two_tone_drive_origin.moc"
