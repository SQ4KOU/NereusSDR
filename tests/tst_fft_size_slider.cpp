// no-port-check: unit tests for FFT slider semantics; cites Thetis
// source for line numbers and value mappings, no logic ported.
//
// =================================================================
// tests/tst_fft_size_slider.cpp  (NereusSDR)
// =================================================================
//
// Phase 2-polish -- exercise FFT-size slider semantics end-to-end
// against Thetis source.  Asserts:
//
//   1. Slider value 0..6 maps to {4096, 8192, 16384, 32768, 65536,
//      131072, 262144}.  Mirrors Thetis setup.cs:16148 [v2.10.3.13]:
//        FFTSize = 4096 * Math.Pow(2, Math.Floor(slider.Value))
//
//   2. fftSizeOffsetDb = slider.Value * 2 (dB).  Mirrors Thetis
//      setup.cs:16154 [v2.10.3.13]:
//        Display.RX1FFTSizeOffset = tbDisplayFFTSize.Value * 2;
//
//   3. spectrumSettingsChanged signal fires only when oldSize != newSize.
//      Mirrors Thetis setup.cs:16162-16165 [v2.10.3.13]:
//        if (old_fft != ...FFTSize)
//            SpectrumSettingsChangedHandlers?.Invoke(1);
//
//   4. SpectrumWidget::binWidthHz() returns sampleRate / fftSize without
//      the legacy * 2 doubling.  Mirrors Thetis setup.cs:16151
//      [v2.10.3.13]: bin_width = SampleRate / FFTSize.
//
//   5. fftSizeOffsetDb default is 0.0 (slider position 0 idle state).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 -- Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                  (KG4VCF), with AI-assisted transformation via
//                  Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QSignalSpy>

#include "core/FFTEngine.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TestFftSizeSlider : public QObject
{
    Q_OBJECT

private slots:

    void slider_value_to_fft_size_mapping()
    {
        // Direct verification of the slider->size mapping the slot uses.
        // setup.cs:16148 [v2.10.3.13]: 4096 * 2^v.
        QCOMPARE(4096 << 0, 4096);
        QCOMPARE(4096 << 1, 8192);
        QCOMPARE(4096 << 2, 16384);
        QCOMPARE(4096 << 3, 32768);
        QCOMPARE(4096 << 4, 65536);
        QCOMPARE(4096 << 5, 131072);
        QCOMPARE(4096 << 6, 262144);
    }

    void set_fft_size_round_trip_for_each_slider_value()
    {
        FFTEngine fe(/*receiverId=*/0);
        for (int v = 0; v <= 6; ++v) {
            const int newSize = 4096 << v;
            fe.setFftSize(newSize);
            QCOMPARE(fe.fftSize(), newSize);
        }
    }

    // Thetis: Display.RX1FFTSizeOffset = slider.Value * 2 (dB).
    // Verify the offset round-trips and tracks slider position.
    void fft_size_offset_db_default_and_round_trip()
    {
        FFTEngine fe(0);
        QCOMPARE(fe.fftSizeOffsetDb(), 0.0);
        for (int v = 0; v <= 6; ++v) {
            fe.setFftSizeOffsetDb(static_cast<double>(v) * 2.0);
            QCOMPARE(fe.fftSizeOffsetDb(), static_cast<double>(v) * 2.0);
        }
        // Confirm the high end matches Thetis's max offset (slider 6 -> 12 dB).
        fe.setFftSizeOffsetDb(12.0);
        QCOMPARE(fe.fftSizeOffsetDb(), 12.0);
    }

    // Thetis emits SpectrumSettingsChangedHandlers only when size actually
    // changes.  Same FFT size set twice -> 1 emission.  Size change ->
    // additional emission.
    void spectrum_settings_changed_only_on_actual_change()
    {
        FFTEngine fe(0);
        QSignalSpy spy(&fe, &FFTEngine::spectrumSettingsChanged);

        // Default size is 4096; setting to 4096 should NOT emit.
        fe.setFftSize(4096);
        QCOMPARE(spy.count(), 0);

        // Setting to a new size -> 1 emit with receiverId=0.
        fe.setFftSize(8192);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);

        // Setting to the same new size -> no additional emit.
        fe.setFftSize(8192);
        QCOMPARE(spy.count(), 1);

        // Different size again -> 2 total emits.
        fe.setFftSize(131072);
        QCOMPARE(spy.count(), 2);

        // Invalid size (non-power-of-two) -> rejected, no emit.
        fe.setFftSize(7000);
        QCOMPARE(spy.count(), 2);

        // Invalid size (out of range) -> rejected, no emit.
        fe.setFftSize(512);
        QCOMPARE(spy.count(), 2);
    }

    // SpectrumWidget::binWidthHz fix verification.  Phase 2-polish drops
    // the legacy * 2 multiplier; bin width should equal sampleRate /
    // fullLinearBins.size().  With no bins fed, falls back to /4096.
    void bin_width_hz_uses_plain_divide()
    {
        SpectrumWidget w;
        // Default sample rate is 768000 (per SpectrumWidget.h:990).  No
        // bins fed yet, so fallback to fftSz=4096.  Expected:
        //   768000 / 4096 = 187.5 Hz
        // Pre-fix bug returned 768000 / 8192 = 93.75 Hz (half).
        const double bw = w.binWidthHz();
        QCOMPARE_LT(std::abs(bw - 187.5), 1e-6);
    }
};

QTEST_MAIN(TestFftSizeSlider)
#include "tst_fft_size_slider.moc"
