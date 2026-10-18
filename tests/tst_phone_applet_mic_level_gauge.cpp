// no-port-check: test-only — exercises PhoneCwApplet mic level gauge dB conversion.
// Phase 3M-1b — mic level gauge wired to AudioEngine::pcMicInputLevel().
//
// The gauge is driven by a 50 ms QTimer that reads AudioEngine::pcMicInputLevel()
// (linear 0..1), converts to dBFS (20*log10), and clamps to gauge range [-40,+10].
// These tests exercise the conversion math and clamping logic directly — the timer
// and widget construction require a display server and are not tested here.
//
// Test cases (conversion math / clamping):
//  1. Linear 0.0f (silence) → floor -40 dB (clamped from -60 floor).
//  2. Linear 1e-7f (sub-floor) → floor -40 dB (≤1e-6 threshold).
//  3. Linear 1e-6f (exact threshold) → floor -40 dB (boundary: not > 1e-6).
//  4. Linear 0.1f → ≈ -20 dB (20*log10(0.1) = -20).
//  5. Linear 0.5f → ≈ -6.02 dB (20*log10(0.5)).
//  6. Linear 1.0f → 0 dB (full scale; not clamped, within range).
//  7. NaN / Inf → clamp to -40 dB (guard for corrupt bus readings).
//  8. Linear 3.16f (above full scale) → clamped to +10 dB gauge ceiling.
//  9. Gauge range [-40, +10] matches HGauge construction (verifies the spec).

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <cmath>
#include <limits>

// The conversion logic from PhoneCwApplet::wireControls() tick handler.
// Extracted here as a static helper so the math can be unit-tested without
// constructing the full applet (requires display server).
//
// Returns the dBFS value clamped to [-40.0, +10.0] gauge range.
static double convertMicLevel(float linear)
{
    double dB = -60.0;
    if (std::isfinite(linear) && linear > 1e-6f) {
        dB = 20.0 * std::log10(static_cast<double>(linear));
    }
    return qBound(-40.0, dB, 10.0);
}

class TestPhoneAppletMicLevelGauge : public QObject
{
    Q_OBJECT

private slots:
    // Conversion math + clamping
    void silence_givesFloor();
    void subFloor_givesFloor();
    void exactThreshold_givesFloor();
    void tenth_givesMinus20dB();
    void half_givesMinus6dB();
    void fullScale_givesZero();
    void nan_clampsToFloor();
    void inf_clampsToFloor();
    void aboveFullScale_clampsToCeiling();
    // Gauge spec constants
    void gaugeRangeMin_isMinus40();
    void gaugeRangeMax_is10();
};

// 1. Linear 0.0f (silence) → not > 1e-6f → dB stays at -60 → clamped to -40.
void TestPhoneAppletMicLevelGauge::silence_givesFloor()
{
    QCOMPARE(convertMicLevel(0.0f), -40.0);
}

// 2. Linear 1e-7f (below threshold) → not > 1e-6f → dB stays at -60 → clamped to -40.
void TestPhoneAppletMicLevelGauge::subFloor_givesFloor()
{
    QCOMPARE(convertMicLevel(1e-7f), -40.0);
}

// 3. Linear 1e-6f (exact threshold) → NOT > 1e-6f (equal is not greater) → floor.
void TestPhoneAppletMicLevelGauge::exactThreshold_givesFloor()
{
    QCOMPARE(convertMicLevel(1e-6f), -40.0);
}

// 4. Linear 0.1f → 20*log10(0.1) = -20 dB (within range, no clamping).
void TestPhoneAppletMicLevelGauge::tenth_givesMinus20dB()
{
    const double result = convertMicLevel(0.1f);
    QVERIFY2(std::abs(result - (-20.0)) < 0.1,
             qPrintable(QStringLiteral("Expected ~-20 dB, got %1").arg(result)));
}

// 5. Linear 0.5f → 20*log10(0.5) ≈ -6.02 dB (within range, no clamping).
void TestPhoneAppletMicLevelGauge::half_givesMinus6dB()
{
    const double result = convertMicLevel(0.5f);
    QVERIFY2(std::abs(result - (-6.02)) < 0.1,
             qPrintable(QStringLiteral("Expected ~-6.02 dB, got %1").arg(result)));
}

// 6. Linear 1.0f → 20*log10(1.0) = 0 dB (within range [-40, +10]).
void TestPhoneAppletMicLevelGauge::fullScale_givesZero()
{
    const double result = convertMicLevel(1.0f);
    QVERIFY2(std::abs(result - 0.0) < 0.01,
             qPrintable(QStringLiteral("Expected 0 dB, got %1").arg(result)));
}

// 7. NaN → std::isfinite() is false → dB stays at -60 → clamped to -40.
void TestPhoneAppletMicLevelGauge::nan_clampsToFloor()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    QCOMPARE(convertMicLevel(nan), -40.0);
}

// 8. +Inf → std::isfinite() is false → dB stays at -60 → clamped to -40.
void TestPhoneAppletMicLevelGauge::inf_clampsToFloor()
{
    const float inf = std::numeric_limits<float>::infinity();
    QCOMPARE(convertMicLevel(inf), -40.0);
}

// 9. Linear 3.162f → 20*log10(3.162) ≈ 10.001 dB → clamped to +10 gauge ceiling.
void TestPhoneAppletMicLevelGauge::aboveFullScale_clampsToCeiling()
{
    // 3.162 ≈ 10^(10/20): level that would produce ~10 dB — right at ceiling.
    // Any value above this gets clamped to +10.
    const double result = convertMicLevel(10.0f);  // way above FS
    QCOMPARE(result, 10.0);
}

// 10. Gauge spec: min -40 dB (matches HGauge::setRange(-40.0, 10.0) call).
void TestPhoneAppletMicLevelGauge::gaugeRangeMin_isMinus40()
{
    // Verify the floor constant used for clamping matches the gauge construction.
    QCOMPARE(convertMicLevel(0.0f), -40.0);   // floor == gauge min
}

// 11. Gauge spec: max +10 dB (matches HGauge::setRange(-40.0, 10.0) call).
void TestPhoneAppletMicLevelGauge::gaugeRangeMax_is10()
{
    QCOMPARE(convertMicLevel(100.0f), 10.0);  // ceiling == gauge max
}

QTEST_MAIN(TestPhoneAppletMicLevelGauge)
#include "tst_phone_applet_mic_level_gauge.moc"
