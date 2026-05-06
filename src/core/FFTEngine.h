#pragma once

// =================================================================
// src/core/FFTEngine.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// display.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley (W5WC)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Waterfall AGC Modifications Copyright (C) 2013 Phil Harman (VK6APH)
// Transitions to directX and continual modifications Copyright (C) 2020-2025 Richard Samphire (MW0LGE)
//=================================================================
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

#include <QObject>
#include <QVector>
#include <QElapsedTimer>

#include <atomic>

#ifdef HAVE_FFTW3
#include <fftw3.h>
#endif

namespace NereusSDR {

// Window function types for FFT pre-processing.
// Integer values match Thetis WDSP analyzer.c:52-173 [v2.10.3.13]
// new_window() switch type codes exactly, so the values are wire-compatible
// with Thetis's combo and any future WDSP API surface.  Enum order also
// matches Thetis comboDispWinType item ordering (setup.designer.cs:34966-
// 34973 [v2.10.3.13]) so combo-index == enum-value with no remap.
enum class WindowFunction : int {
    Rectangular        = 0,  // analyzer.c case 0
    BlackmanHarris4    = 1,  // 4-term Blackman-Harris, analyzer.c case 1
    Hann               = 2,  // analyzer.c case 2
    FlatTop            = 3,  // flat-top for calibration, analyzer.c case 3
    Hamming            = 4,  // analyzer.c case 4
    Kaiser             = 5,  // parameterised by KaiserPi, analyzer.c case 5
    BlackmanHarris7    = 6,  // 7-term Blackman-Harris, analyzer.c case 6
    Count              = 7
};

// Per-receiver FFT computation engine.
// Lives on a worker thread. Receives raw interleaved I/Q samples,
// accumulates to FFT size, applies window, computes FFT via FFTW3,
// converts to dBm bins, and emits for display.
//
// Thread-safe parameter updates via std::atomic for values set from
// the main thread (FFT size changes require replan on next frame).
//
// From Thetis display.cs:215 — BUFFER_SIZE = 16384 (max FFT size)
class FFTEngine : public QObject {
    Q_OBJECT

public:
    explicit FFTEngine(int receiverId, QObject* parent = nullptr);
    ~FFTEngine() override;

    // --- Configuration (thread-safe, main thread sets these) ---

    void setFftSize(int size);
    int  fftSize() const { return m_fftSize.load(); }

    void setWindowFunction(WindowFunction wf);
    WindowFunction windowFunction() const {
        return static_cast<WindowFunction>(m_windowFunc.load());
    }

    // Kaiser window shape parameter (PiAlpha = pi * alpha).  Higher values
    // narrow the main lobe + raise sidelobes; lower values widen.  Only
    // applied when windowFunction() == Kaiser.
    // From Thetis specHPSDR.cs:145 [v2.10.3.13] private double kaiser_pi = 14.0;
    void setKaiserPi(double pi);
    double kaiserPi() const { return m_kaiserPi.load(); }

    void setSampleRate(double rateHz);
    double sampleRate() const { return m_sampleRate.load(); }

    void setOutputFps(int fps);
    int  outputFps() const { return m_targetFps.load(); }

    // setDecimation — apply I/Q decimation before FFT processing.
    // Only every Nth I/Q sample pair is passed to the FFT accumulator.
    // Higher decimation reduces effective bandwidth and FFT resolution,
    // mirroring Thetis setup.designer.cs:33732 udDisplayDecimation [v2.10.3.13].
    void setDecimation(int factor);
    int  decimation() const { return m_decimation.load(); }

    // Equivalent Noise Bandwidth of the current window function, expressed
    // as a multiplier on the natural per-bin bandwidth (sample_rate / N).
    //
    //   ENB_bins = N × Σ(w[i]²) / (Σ w[i])²
    //
    // For typical windows: rectangular = 1.00, Hann = 1.50, Hamming ≈ 1.36,
    // Blackman-Harris 4-term ≈ 2.00, Flat-Top ≈ 3.77.  Used by the Thetis
    // detector function (analyzer.c:283-462 [v2.10.3.13]) to scale Average
    // / Sample / RMS modes via inv_enb so that integrated noise reads
    // independent of window choice.  Computed inside computeWindow().
    double windowEnb() const { return m_windowEnb; }

public slots:
    // Feed raw interleaved I/Q samples from RadioConnection.
    // Format: [I0, Q0, I1, Q1, ...] as float pairs.
    // Accumulates until fftSize samples are collected, then runs FFT.
    void feedIQ(const QVector<float>& interleavedIQ);

signals:
    // Emitted when a new FFT frame is ready.
    // binsDbm contains fftSize float values (full FFT-shifted bins, neg-freq
    // first then pos-freq), representing power in dBm at each frequency bin.
    void fftReady(int receiverId, const QVector<float>& binsDbm);

    // Linear-power side-channel for the Thetis-faithful display pipeline.
    //
    // binsLinear contains the same length and ordering as binsDbm but values
    // are |X[k]|² (linear power, no log conversion, no dBm normalization).
    // Required by the Thetis WDSP analyzer detector + averaging pipeline
    // which operates on linear-domain bins and applies the 10×log₁₀
    // conversion only at final pixel output (analyzer.c:464-554
    // [v2.10.3.13] avenger() does the log step per av_mode).
    //
    // windowEnb is the Equivalent Noise Bandwidth of the window in bins.
    // The downstream detector applies inv_enb = 1/windowEnb to scale
    // Average/Sample/RMS modes (analyzer.c:368-441 [v2.10.3.13]).
    //
    // dbmOffset is the window coherent-gain compensation -20·log10(Σw[i])
    // that fftReady's binsDbm[i] adds after 10·log10(|X[k]|²).  The avenger
    // applies it via its `scale` parameter as scale = 10^(dbmOffset/10) so
    // the post-avenger pixels read the same dBm as fftReady.
    //
    // Both metadata values are emitted in-band so the slot always has the
    // values matching the bins it just received — no separate setter
    // coordination needed.
    //
    // Emitted in lock-step with fftReady — every frame, both signals fire.
    // Cost: one extra QVector<float> heap allocation + 16 bytes per frame.
    void fftReadyLinear(int receiverId, const QVector<float>& binsLinear,
                        double windowEnb, double dbmOffset);

private:
    void replanFft();
    void computeWindow();
    void processFrame();

    int m_receiverId;

    // FFT configuration (atomics for cross-thread access)
    std::atomic<int>    m_fftSize{4096};
    std::atomic<int>    m_pendingFftSize{0};  // 0 = no change pending
    std::atomic<int>    m_windowFunc{static_cast<int>(WindowFunction::BlackmanHarris4)};
    // Kaiser window shape parameter.  Default 14.0 from Thetis specHPSDR.cs:
    // 145 [v2.10.3.13] private double kaiser_pi = 14.0.
    std::atomic<double> m_kaiserPi{14.0};
    std::atomic<double> m_sampleRate{48000.0};
    std::atomic<int>    m_targetFps{30};
    // From Thetis setup.designer.cs:33732 udDisplayDecimation [v2.10.3.13].
    // Range 1..32; 1 = no decimation (pass every sample).
    std::atomic<int>    m_decimation{1};

    // Internal state (only accessed on worker thread)
    int m_currentFftSize{0};  // last planned size (triggers replan on mismatch)
    int m_decimationCounter{0};  // counts input sample pairs; reset each decimation pass

#ifdef HAVE_FFTW3
    fftwf_complex* m_fftIn{nullptr};
    fftwf_complex* m_fftOut{nullptr};
    fftwf_plan     m_plan{nullptr};
#endif

    // Window function coefficients (precomputed for current fftSize)
    QVector<float> m_window;

    // I/Q accumulation buffer (interleaved pairs)
    QVector<float> m_iqBuffer;
    int m_iqWritePos{0};  // write position in sample pairs

    // Output rate limiting
    QElapsedTimer m_frameTimer;
    bool m_frameTimerStarted{false};

    // dBm calibration offset (accounts for window gain + FFT normalization)
    float m_dbmOffset{0.0f};

    // Equivalent Noise Bandwidth of the current window in bins:
    //   m_windowEnb = N × Σ(w[i]²) / (Σ w[i])²
    // Recomputed every time the window changes (computeWindow()).  Used by
    // downstream consumers (e.g. SpectrumDetector) for inv_enb scaling
    // matching analyzer.c:368-441 [v2.10.3.13].
    double m_windowEnb{1.0};

    // From Thetis display.cs:215
    static constexpr int kMaxFftSize = 65536;
};

} // namespace NereusSDR
