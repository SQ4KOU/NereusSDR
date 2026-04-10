#include "FFTEngine.h"
#include "LogCategories.h"

#include <cmath>
#include <cstring>

namespace NereusSDR {

FFTEngine::FFTEngine(int receiverId, QObject* parent)
    : QObject(parent)
    , m_receiverId(receiverId)
{
}

FFTEngine::~FFTEngine()
{
#ifdef HAVE_FFTW3
    if (m_plan) {
        fftwf_destroy_plan(m_plan);
    }
    if (m_fftIn) {
        fftwf_free(m_fftIn);
    }
    if (m_fftOut) {
        fftwf_free(m_fftOut);
    }
#endif
}

void FFTEngine::setFftSize(int size)
{
    // Validate power of 2, within range
    if (size < 1024 || size > kMaxFftSize) {
        return;
    }
    if ((size & (size - 1)) != 0) {
        return;  // not power of 2
    }
    m_fftSize.store(size);
}

void FFTEngine::setWindowFunction(WindowFunction wf)
{
    m_windowFunc.store(static_cast<int>(wf));
}

void FFTEngine::setSampleRate(double rateHz)
{
    m_sampleRate.store(rateHz);
}

void FFTEngine::setOutputFps(int fps)
{
    m_targetFps.store(qBound(1, fps, 60));
}

void FFTEngine::feedIQ(const QVector<float>& interleavedIQ)
{
#ifdef HAVE_FFTW3
    int desiredSize = m_fftSize.load();

    // Replan if FFT size changed
    if (desiredSize != m_currentFftSize) {
        m_fftSize.store(desiredSize);
        replanFft();
    }

    // Append interleaved I/Q to accumulation buffer
    int numPairs = interleavedIQ.size() / 2;
    for (int i = 0; i < numPairs; ++i) {
        if (m_iqWritePos >= m_currentFftSize) {
            // Buffer full — process and reset
            processFrame();
            m_iqWritePos = 0;
        }
        // Swap I↔Q for spectrum display — matches Thetis analyzer.c:1757-1758
        // Audio path (WDSP fexchange2) uses normal I/Q order; display is inverted.
        m_fftIn[m_iqWritePos][0] = interleavedIQ[i * 2 + 1] * m_window[m_iqWritePos];  // Q→I
        m_fftIn[m_iqWritePos][1] = interleavedIQ[i * 2]     * m_window[m_iqWritePos];  // I→Q
        m_iqWritePos++;
    }
#else
    Q_UNUSED(interleavedIQ);
#endif
}

void FFTEngine::replanFft()
{
#ifdef HAVE_FFTW3
    int size = m_fftSize.load();
    qCInfo(lcDsp) << "FFTEngine: replanning FFT size" << size;

    // Destroy old plan and buffers
    if (m_plan) {
        fftwf_destroy_plan(m_plan);
        m_plan = nullptr;
    }
    if (m_fftIn) {
        fftwf_free(m_fftIn);
        m_fftIn = nullptr;
    }
    if (m_fftOut) {
        fftwf_free(m_fftOut);
        m_fftOut = nullptr;
    }

    // Allocate aligned buffers
    m_fftIn  = fftwf_alloc_complex(size);
    m_fftOut = fftwf_alloc_complex(size);

    // Create plan (FFTW_MEASURE uses wisdom for optimal performance)
    m_plan = fftwf_plan_dft_1d(size, m_fftIn, m_fftOut, FFTW_FORWARD, FFTW_MEASURE);

    m_currentFftSize = size;
    m_iqWritePos = 0;

    // Recompute window coefficients
    computeWindow();

    qCInfo(lcDsp) << "FFTEngine: plan created, window computed";
#endif
}

// Window function coefficients.
// From gpu-waterfall.md lines 184-216, constants match Thetis WDSP SetAnalyzer options.
void FFTEngine::computeWindow()
{
    int size = m_currentFftSize;
    m_window.resize(size);

    WindowFunction wf = static_cast<WindowFunction>(m_windowFunc.load());

    switch (wf) {
    case WindowFunction::BlackmanHarris4: {
        // 4-term Blackman-Harris — from gpu-waterfall.md:190-200
        constexpr float a0 = 0.35875f;
        constexpr float a1 = 0.48829f;
        constexpr float a2 = 0.14128f;
        constexpr float a3 = 0.01168f;
        for (int i = 0; i < size; ++i) {
            float n = static_cast<float>(i) / static_cast<float>(size);
            m_window[i] = a0
                        - a1 * std::cos(2.0f * static_cast<float>(M_PI) * n)
                        + a2 * std::cos(4.0f * static_cast<float>(M_PI) * n)
                        - a3 * std::cos(6.0f * static_cast<float>(M_PI) * n);
        }
        break;
    }
    case WindowFunction::Hanning:
        for (int i = 0; i < size; ++i) {
            float n = static_cast<float>(i) / static_cast<float>(size);
            m_window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * n));
        }
        break;
    case WindowFunction::Hamming:
        for (int i = 0; i < size; ++i) {
            float n = static_cast<float>(i) / static_cast<float>(size);
            m_window[i] = 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * n);
        }
        break;
    case WindowFunction::Flat:
        // Flat-top for calibration — minimal scalloping loss
        for (int i = 0; i < size; ++i) {
            float n = static_cast<float>(i) / static_cast<float>(size);
            float pi = static_cast<float>(M_PI);
            m_window[i] = 1.0f
                        - 1.93f  * std::cos(2.0f * pi * n)
                        + 1.29f  * std::cos(4.0f * pi * n)
                        - 0.388f * std::cos(6.0f * pi * n)
                        + 0.028f * std::cos(8.0f * pi * n);
        }
        break;
    case WindowFunction::None:
        std::fill(m_window.begin(), m_window.end(), 1.0f);
        break;
    default:
        // BlackmanHarris7, Kaiser — TODO: implement when needed
        // For now fall back to BlackmanHarris4
        {
            constexpr float a0 = 0.35875f;
            constexpr float a1 = 0.48829f;
            constexpr float a2 = 0.14128f;
            constexpr float a3 = 0.01168f;
            for (int i = 0; i < size; ++i) {
                float n = static_cast<float>(i) / static_cast<float>(size);
                m_window[i] = a0
                            - a1 * std::cos(2.0f * static_cast<float>(M_PI) * n)
                            + a2 * std::cos(4.0f * static_cast<float>(M_PI) * n)
                            - a3 * std::cos(6.0f * static_cast<float>(M_PI) * n);
            }
        }
        break;
    }

    // Compute window coherent gain for dBm normalization.
    // Power normalization: divide |X[k]|² by (sum of window)² to get
    // correct absolute power. The dBm offset accounts for this.
    float sum = 0.0f;
    for (float w : m_window) {
        sum += w;
    }
    // dbmOffset: 10*log10(1/sum²) = -20*log10(sum)
    // This normalizes the FFT output so a full-scale sine reads 0 dBFS.
    m_dbmOffset = -20.0f * std::log10(sum > 0.0f ? sum : 1.0f);
}

void FFTEngine::processFrame()
{
#ifdef HAVE_FFTW3
    if (!m_plan || m_iqWritePos < m_currentFftSize) {
        return;
    }

    // Rate limiting — skip if we're ahead of target FPS
    int targetFps = m_targetFps.load();
    if (targetFps > 0 && m_frameTimerStarted) {
        qint64 minIntervalMs = 1000 / targetFps;
        qint64 elapsed = m_frameTimer.elapsed();
        if (elapsed < minIntervalMs) {
            return;
        }
    }

    // Execute FFT (window already applied during accumulation in feedIQ)
    fftwf_execute(m_plan);

    // Convert to dBm: 10 * log10(I² + Q²) + normalization
    // Complex I/Q FFT: output all N bins with FFT-shift.
    // Raw FFT order: [DC..+fs/2, -fs/2..DC)
    // Shifted order:  [-fs/2..DC..+fs/2) — matches spectrum display left-to-right
    int N = m_currentFftSize;
    int half = N / 2;
    QVector<float> binsDbm(N);

    // Normalization: the FFT output magnitude needs to be divided by the
    // window's coherent gain (sum of window coefficients) to get correct
    // power. We apply this as a dB offset: m_dbmOffset = -20*log10(sum).
    for (int i = 0; i < N; ++i) {
        // FFT-shift: swap halves so negative freqs are on left.
        // I/Q swap in feedIQ handles spectrum orientation (no mirror needed).
        int srcIdx = (i + half) % N;
        float re = m_fftOut[srcIdx][0];
        float im = m_fftOut[srcIdx][1];
        float powerSq = re * re + im * im;

        // Avoid log(0) — floor at -200 dBm
        // From Thetis display.cs:2842 — initializes display data to -200
        if (powerSq < 1e-20f) {
            binsDbm[i] = -200.0f;
        } else {
            binsDbm[i] = 10.0f * std::log10(powerSq) + m_dbmOffset;
        }
    }

    // Restart frame timer
    m_frameTimer.restart();
    m_frameTimerStarted = true;

    emit fftReady(m_receiverId, binsDbm);
#endif
}

} // namespace NereusSDR
