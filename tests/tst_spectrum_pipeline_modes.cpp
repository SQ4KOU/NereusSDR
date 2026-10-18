// =================================================================
// tests/tst_spectrum_pipeline_modes.cpp  (NereusSDR)
// =================================================================
//
// Phase 1B verification — exercise every detector x averaging
// combination through SpectrumWidget::updateSpectrumLinear() and
// assert the pipeline produces a finite, displayWidth-sized output
// for both the spectrum and waterfall planes.
//
// Combinations covered:
//   Spectrum  detector x averaging = 5 x 4 = 20
//   Waterfall detector x averaging = 4 x 4 = 16
//   Total                                  = 36
//
// Mirrors the Thetis Pan / WF combo item lists from
// setup.designer.cs:34843-34847 / :34868-34873 / :34436-34440 /
// :34461-34465 [v2.10.3.13] -- 5 spectrum detectors (Peak / Rosenfell
// / Average / Sample / RMS), 4 waterfall detectors (no RMS), 4
// averaging modes for each plane (None / Recursive / Time Window /
// Log Recursive).  PeakHold (av_mode -1) is NOT user-exposed in
// Thetis; intentionally absent from SpectrumAveraging enum.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 -- Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                  (KG4VCF), with AI-assisted transformation via
//                  Anthropic Claude Code.
// =================================================================

// Tests use QApplication (SpectrumWidget is a QWidget subclass).

#include <QtTest/QtTest>
#include <QApplication>
#include <QtNumeric>

#include <cmath>

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TestSpectrumPipelineModes : public QObject
{
    Q_OBJECT

private:
    // Synthetic linear-power FFT bins -- fftSize=4096, with a single
    // tone in the middle (bin 2048) at unit power and shaped noise floor
    // elsewhere.  Linear domain so the detector + avenger see realistic
    // dynamic range without log-scale flattening.
    QVector<float> makeSyntheticBins() const
    {
        constexpr int fftSize = 4096;
        QVector<float> bins(fftSize, 1e-12f);  // -120 dBm noise floor
        bins[2048] = 1e-3f;                     // -30 dBm tone at center
        // Add some shaped variance so detectors with min/max behaviour
        // (Rosenfell) have something interesting to differentiate.
        for (int i = 0; i < fftSize; ++i) {
            const float phase = static_cast<float>(i) * 0.01f;
            bins[i] += static_cast<float>(std::abs(std::sin(phase))) * 1e-11f;
        }
        return bins;
    }

    void feedFrames(SpectrumWidget& w,
                    const QVector<float>& bins,
                    int frameCount) const
    {
        // windowEnb = 2.0 ~ Blackman-Harris-4 ENB; dbmOffset = -10 dB
        // (arbitrary; any finite value exercises the avenger scale path).
        const double windowEnb = 2.0;
        const double dbmOffset = -10.0;
        for (int f = 0; f < frameCount; ++f) {
            w.updateSpectrumLinear(0, bins, windowEnb, dbmOffset);
        }
    }

    void assertFiniteAndSized(const QVector<float>& out,
                              int expectedSize,
                              const char* label) const
    {
        QVERIFY2(out.size() == expectedSize,
                 qPrintable(QStringLiteral("%1: size %2 != expected %3")
                                .arg(QString::fromUtf8(label))
                                .arg(out.size())
                                .arg(expectedSize)));
        for (int i = 0; i < out.size(); ++i) {
            QVERIFY2(std::isfinite(out[i]),
                     qPrintable(QStringLiteral("%1: pixel[%2] not finite (%3)")
                                    .arg(QString::fromUtf8(label))
                                    .arg(i)
                                    .arg(static_cast<double>(out[i]))));
        }
    }

private slots:

    // Drive the pipeline through every spectrum-side combination.
    // For each detector x averaging pair, feed a few frames so window-
    // averaging modes have time to populate their ring buffer, then
    // assert the output is sized to displayWidth and all finite.
    void spectrum_all_detector_x_averaging_combinations()
    {
        const SpectrumDetector  detectors[] = {
            SpectrumDetector::Peak,
            SpectrumDetector::Rosenfell,
            SpectrumDetector::Average,
            SpectrumDetector::Sample,
            SpectrumDetector::RMS
        };
        const SpectrumAveraging averagings[] = {
            SpectrumAveraging::None,
            SpectrumAveraging::Recursive,
            SpectrumAveraging::TimeWindow,
            SpectrumAveraging::LogRecursive
        };

        const QVector<float> bins = makeSyntheticBins();

        for (const auto det : detectors) {
            for (const auto avg : averagings) {
                SpectrumWidget w;
                w.setSpectrumDetector(det);
                w.setSpectrumAveraging(avg);
                // Feed enough frames so window-averaging mode populates.
                feedFrames(w, bins, 4);

                const int expected = qMax(w.width() - 0, 800);  // matches updateSpectrumLinear's qMax(width()-strip, 800)
                Q_UNUSED(expected);
                // Headless width() is 0 so displayWidth falls back to 800.
                assertFiniteAndSized(w.renderedPixels(), 800,
                                     "spectrum");
            }
        }
    }

    // Same exhaustive sweep for the waterfall plane.  Waterfall detector
    // has 4 items (no RMS per Thetis setup.designer.cs:34461-34465).
    void waterfall_all_detector_x_averaging_combinations()
    {
        const SpectrumDetector  detectors[] = {
            SpectrumDetector::Peak,
            SpectrumDetector::Rosenfell,
            SpectrumDetector::Average,
            SpectrumDetector::Sample
        };
        const SpectrumAveraging averagings[] = {
            SpectrumAveraging::None,
            SpectrumAveraging::Recursive,
            SpectrumAveraging::TimeWindow,
            SpectrumAveraging::LogRecursive
        };

        const QVector<float> bins = makeSyntheticBins();

        for (const auto det : detectors) {
            for (const auto avg : averagings) {
                SpectrumWidget w;
                w.setWaterfallDetector(det);
                w.setWaterfallAveraging(avg);
                feedFrames(w, bins, 4);

                assertFiniteAndSized(w.wfRenderedPixels(), 800,
                                     "waterfall");
            }
        }
    }

    // Mode-switch path: cycle through detector values mid-stream and
    // assert no stale-state crash.  Mirrors a user changing the combo
    // while the pipeline is running.
    void spectrum_mode_switch_midstream_does_not_crash()
    {
        const QVector<float> bins = makeSyntheticBins();
        SpectrumWidget w;
        w.setSpectrumAveraging(SpectrumAveraging::Recursive);

        // Prime with Peak detector, switch through every other detector
        // with intermediate frames so the avenger sees the transition.
        w.setSpectrumDetector(SpectrumDetector::Peak);
        feedFrames(w, bins, 2);
        w.setSpectrumDetector(SpectrumDetector::Rosenfell);
        feedFrames(w, bins, 2);
        w.setSpectrumDetector(SpectrumDetector::Average);
        feedFrames(w, bins, 2);
        w.setSpectrumDetector(SpectrumDetector::Sample);
        feedFrames(w, bins, 2);
        w.setSpectrumDetector(SpectrumDetector::RMS);
        feedFrames(w, bins, 2);

        assertFiniteAndSized(w.renderedPixels(), 800, "post-switch");
    }

    // Averaging mode switch should clear the avenger (per setSpectrumAveraging
    // setter at SpectrumWidget.cpp:1020 which calls m_spectrumAvenger.clear()).
    // Verify that switching averaging mid-stream yields finite output without
    // residual NaN from a half-populated ring buffer.
    void spectrum_averaging_switch_resets_cleanly()
    {
        const QVector<float> bins = makeSyntheticBins();
        SpectrumWidget w;
        w.setSpectrumDetector(SpectrumDetector::Peak);

        w.setSpectrumAveraging(SpectrumAveraging::None);
        feedFrames(w, bins, 2);
        w.setSpectrumAveraging(SpectrumAveraging::Recursive);
        feedFrames(w, bins, 2);
        w.setSpectrumAveraging(SpectrumAveraging::TimeWindow);
        feedFrames(w, bins, 2);
        w.setSpectrumAveraging(SpectrumAveraging::LogRecursive);
        feedFrames(w, bins, 2);

        assertFiniteAndSized(w.renderedPixels(), 800, "post-avg-switch");
    }

    // Empty input must not crash and must leave outputs untouched (no
    // resize, no NaN).
    void empty_bins_input_is_noop()
    {
        SpectrumWidget w;
        w.setSpectrumDetector(SpectrumDetector::Peak);
        w.setSpectrumAveraging(SpectrumAveraging::Recursive);
        const QVector<float> emptyBins;
        w.updateSpectrumLinear(0, emptyBins, 2.0, 0.0);
        QVERIFY(w.renderedPixels().isEmpty());
        QVERIFY(w.wfRenderedPixels().isEmpty());
    }
};

QTEST_MAIN(TestSpectrumPipelineModes)
#include "tst_spectrum_pipeline_modes.moc"
