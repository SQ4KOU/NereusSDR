# GPU-Accelerated Waterfall Rendering

**Status:** Phase 2B -- Architecture Design

## Overview

NereusSDR computes ALL spectrum and waterfall data client-side from raw I/Q
samples. Unlike AetherSDR where the radio streams pre-computed FFT bins and
waterfall tiles via VITA-49, NereusSDR must:

1. Extract raw I/Q samples from the OpenHPSDR data stream
2. Apply a window function and compute FFT via FFTW3
3. Convert FFT output to dBm power spectrum bins
4. Feed bins to the GPU for spectrum line + waterfall rendering

This document covers the full pipeline from I/Q to pixels, including the
FFT engine, SpectrumWidget GPU rendering, waterfall ring buffer, overlay
system, and shader pipeline.

---

## 1. Client-Side FFT Pipeline

### Data Flow

```
RadioConnection (UDP)
  |
  | Raw I/Q samples (24-bit big-endian, from OpenHPSDR EP6 frames)
  v
FFTEngine (spectrum thread)
  |
  | 1. Accumulate I/Q into FFT-sized buffer
  | 2. Apply window function (Blackman-Harris, etc.)
  | 3. Execute FFTW3 complex-to-complex FFT
  | 4. Compute magnitude: 10*log10(I^2 + Q^2) -> dBm
  | 5. Apply averaging (if enabled)
  v
emit fftReady(receiverId, QVector<float> binsDbm)
  |
  | (auto-queued signal, spectrum thread -> main thread)
  v
FFTRouter -> PanadapterApplet(s) -> SpectrumWidget::updateSpectrum()
```

### FFTEngine Class

```cpp
#pragma once

#include <QObject>
#include <QVector>
#include <fftw3.h>
#include <complex>
#include <memory>

namespace NereusSDR {

// Window function types for FFT pre-processing.
enum class WindowFunction : int {
    None = 0,
    Hanning,
    Hamming,
    BlackmanHarris4,   // 4-term Blackman-Harris (default)
    BlackmanHarris7,   // 7-term Blackman-Harris
    Kaiser,
    Flat,              // flat-top for calibration
    Count
};

// Averaging mode for FFT output smoothing.
enum class AveragingMode : int {
    None = 0,          // no averaging, raw FFT each frame
    Linear,            // arithmetic mean of last N frames
    Exponential        // IIR: alpha * new + (1-alpha) * old
};

// Per-receiver FFT computation engine.
// Lives on the spectrum worker thread. Receives raw I/Q samples,
// computes FFT, emits dBm bins for display.
//
// One FFTEngine instance per active receiver. Thread-safe parameter
// updates via std::atomic for values set from the main thread.
class FFTEngine : public QObject {
    Q_OBJECT

public:
    explicit FFTEngine(int receiverId, QObject* parent = nullptr);
    ~FFTEngine() override;

    // --- Configuration (thread-safe, main thread sets these) ---

    void setFftSize(int size);          // 1024, 2048, 4096, 8192, 16384
    int  fftSize() const;

    void setWindowFunction(WindowFunction wf);
    WindowFunction windowFunction() const;

    void setSampleRate(double rateHz);  // needed for dBm calibration
    double sampleRate() const;

    void setAveragingMode(AveragingMode mode);
    void setAveragingCount(int frames);     // for Linear mode (2-100)
    void setAveragingAlpha(float alpha);    // for Exponential mode (0.01-1.0)

    void setOutputFps(int fps);         // target frames per second (1-60)

public slots:
    // Feed raw I/Q samples from RadioConnection.
    // Samples are interleaved float pairs: [I0, Q0, I1, Q1, ...].
    // Accumulates until fftSize samples are collected, then runs FFT.
    void feedIQ(const QVector<float>& iqSamples);

signals:
    // Emitted on the spectrum thread when a new FFT frame is ready.
    // binsDbm contains fftSize/2 float values (positive frequencies only),
    // representing power in dBm at each frequency bin.
    void fftReady(int receiverId, const QVector<float>& binsDbm);

private:
    void replanFft();
    void computeWindow();
    void processFrame();
    float binToDbm(float re, float im) const;

    int m_receiverId;

    // FFTW3 state
    int               m_fftSize{4096};
    fftwf_complex*    m_fftIn{nullptr};
    fftwf_complex*    m_fftOut{nullptr};
    fftwf_plan        m_plan{nullptr};

    // Window function coefficients (precomputed for current fftSize)
    QVector<float>    m_window;
    WindowFunction    m_windowFunc{WindowFunction::BlackmanHarris4};

    // I/Q accumulation buffer
    QVector<float>    m_iqBuffer;       // interleaved I/Q pairs
    int               m_iqWritePos{0};  // next write position (in sample pairs)

    // Averaging state
    AveragingMode     m_avgMode{AveragingMode::None};
    int               m_avgCount{4};
    float             m_avgAlpha{0.3f};
    QVector<QVector<float>> m_avgHistory;   // ring buffer for Linear mode
    int               m_avgHistoryIdx{0};
    QVector<float>    m_avgAccum;           // IIR accumulator for Exponential

    // Output rate limiting
    int               m_targetFps{30};
    double            m_sampleRate{48000.0};
    qint64            m_lastFrameMs{0};

    // Calibration constant: accounts for window gain, FFT normalization,
    // and ADC reference level. Computed once per fftSize/window change.
    float             m_dbmOffset{0.0f};
};

} // namespace NereusSDR
```

### FFT Size and Performance

| FFT Size | Bins (display) | Resolution at 192 kHz | CPU Time (est.) |
|----------|---------------|----------------------|-----------------|
| 1024 | 512 | 187.5 Hz | ~0.05 ms |
| 2048 | 1024 | 93.75 Hz | ~0.12 ms |
| 4096 | 2048 | 46.88 Hz | ~0.28 ms |
| 8192 | 4096 | 23.44 Hz | ~0.65 ms |
| 16384 | 8192 | 11.72 Hz | ~1.5 ms |

At 30 FPS with 4096-point FFT, total FFT CPU time is approximately 8.4 ms/s
(less than 1% of a single core). FFTW3 plan creation uses `FFTW_MEASURE` for
optimal performance on the host CPU.

### Window Functions

Window coefficients are precomputed when FFT size or window type changes.
The default is 4-term Blackman-Harris, which provides excellent sidelobe
suppression (-92 dB) suitable for identifying weak signals near strong ones.

```cpp
void FFTEngine::computeWindow()
{
    m_window.resize(m_fftSize);
    switch (m_windowFunc) {
    case WindowFunction::BlackmanHarris4: {
        // 4-term Blackman-Harris
        constexpr float a0 = 0.35875f;
        constexpr float a1 = 0.48829f;
        constexpr float a2 = 0.14128f;
        constexpr float a3 = 0.01168f;
        for (int i = 0; i < m_fftSize; ++i) {
            float n = static_cast<float>(i) / static_cast<float>(m_fftSize);
            m_window[i] = a0
                        - a1 * std::cos(2.0f * M_PIf * n)
                        + a2 * std::cos(4.0f * M_PIf * n)
                        - a3 * std::cos(6.0f * M_PIf * n);
        }
        break;
    }
    case WindowFunction::Hanning:
        for (int i = 0; i < m_fftSize; ++i) {
            float n = static_cast<float>(i) / static_cast<float>(m_fftSize);
            m_window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PIf * n));
        }
        break;
    // ... other window types
    }

    // Compute window gain correction factor for dBm calibration
    float sum = 0.0f;
    for (float w : m_window) { sum += w; }
    m_dbmOffset = -20.0f * std::log10(sum);
}
```

### Averaging

Two averaging modes smooth the spectrum display:

**Linear (N-frame):** Maintains a ring buffer of the last N frames. Output
is the arithmetic mean. Good for consistent smoothing. N is configurable
from 2 to 100.

**Exponential (IIR):** `output[i] = alpha * new[i] + (1 - alpha) * output[i]`.
Alpha of 0.3 matches AetherSDR's default smoothing behavior (SMOOTH_ALPHA =
0.35 in SpectrumWidget). Responds faster to signal changes than linear
averaging.

### WDSP Alternative

WDSP includes its own spectrum computation functions (`SetAnalyzer`,
`GetPixels`, `Spectrum`). These can be used as an alternative to FFTW3:

- Advantage: Already integrated per WDSP channel, handles windowing and
  averaging internally, exact Thetis spectrum parity.
- Disadvantage: Less control over FFT size and timing, tied to WDSP channel
  lifecycle, may not support independent pan zoom levels.

Decision: Use FFTW3 for display FFT (independent of WDSP channels). This
allows each panadapter to have independent FFT size and zoom without
affecting DSP processing. WDSP spectrum functions remain available as a
future option for exact Thetis-compatible spectrum display.

---

## 2. SpectrumWidget (QRhiWidget) -- GPU Rendering

### Architecture

SpectrumWidget inherits from QRhiWidget when GPU rendering is enabled
(`NEREUS_GPU_SPECTRUM` define), falling back to QWidget with QPainter
otherwise. This follows AetherSDR's conditional compilation pattern.

```cpp
#ifdef NEREUS_GPU_SPECTRUM
#include <QRhiWidget>
#include <rhi/qrhi.h>
#define SPECTRUM_BASE_CLASS QRhiWidget
#else
#define SPECTRUM_BASE_CLASS QWidget
#endif
```

Qt RHI (Rendering Hardware Interface) provides a unified API across:
- Vulkan (Linux primary)
- Direct3D 12 (Windows primary)
- Metal (macOS primary)
- OpenGL (fallback)

### Widget Layout

```
+-------------------------------------------+
|  dBm scale (36px)  |   Spectrum (~40%)    |
|                     |   (FFT line plot)    |
|                     +-----[divider]--------+
|                     |   Waterfall (~60%)   |
|                     |   (scrolling heat    |
|                     |    map history)      |
+-------------------------------------------+
|        Frequency scale bar (20px)          |
+-------------------------------------------+
```

The spectrum/waterfall split ratio is user-adjustable by dragging the
divider. Default is 40% spectrum, 60% waterfall.

### Class Interface

```cpp
#pragma once

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QImage>
#include <QColor>

#ifdef NEREUS_GPU_SPECTRUM
#include <QRhiWidget>
#include <rhi/qrhi.h>
#define SPECTRUM_BASE_CLASS QRhiWidget
#else
#define SPECTRUM_BASE_CLASS QWidget
#endif

namespace NereusSDR {

class SpectrumOverlayMenu;
class VfoWidget;

// Waterfall color scheme presets.
enum class WfColorScheme : int {
    Default = 0,    // black -> blue -> cyan -> green -> yellow -> red
    Grayscale,      // black -> white
    BlueGreen,      // black -> blue -> teal -> green -> white
    Fire,           // black -> red -> orange -> yellow -> white
    Plasma,         // black -> purple -> magenta -> orange -> yellow
    Count
};

// Gradient stop for waterfall color mapping.
struct WfGradientStop { float pos; int r, g, b; };

// Returns gradient stops for a given color scheme.
const WfGradientStop* wfSchemeStops(WfColorScheme scheme, int& count);

// GPU-accelerated spectrum + waterfall display widget.
//
// Layout (top to bottom):
//   ~40% - spectrum line plot (current FFT frame, smoothed)
//   ~60% - waterfall (scrolling heat-map history)
//   20px - absolute frequency scale bar
//
// Overlays drawn on top of spectrum + waterfall:
//   - VFO markers (vertical lines per slice)
//   - Filter passbands (semi-transparent rectangles)
//   - TNF notch markers
//   - DX spot callsign labels
//   - Band plan colored regions
//   - Grid lines (frequency + dBm)
class SpectrumWidget : public SPECTRUM_BASE_CLASS {
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);
    ~SpectrumWidget() override;

    // ---- Per-pan settings persistence ----

    void setPanIndex(int idx) { m_panIndex = idx; }
    int  panIndex() const { return m_panIndex; }
    QString settingsKey(const QString& base) const;
    void loadSettings();
    void saveSettings();

    QSize sizeHint() const override { return {800, 300}; }

    // ---- Frequency range ----

    void setFrequencyRange(double centerMhz, double bandwidthMhz);
    void clearDisplay();

    // ---- FFT data input ----

    // Feed a new FFT frame. binsDbm are dBm values, one per frequency bin.
    // Called from the main thread after FFTRouter delivers the frame.
    void updateSpectrum(const QVector<float>& binsDbm);

    // ---- Display range ----

    void setDbmRange(float minDbm, float maxDbm);
    float refLevel() const { return m_refLevel; }
    float dynamicRange() const { return m_dynamicRange; }

    // ---- Noise floor auto-adjust ----

    void setNoiseFloorPosition(int pos) { m_noiseFloorPosition = pos; }
    void setNoiseFloorEnable(bool on) { m_noiseFloorEnable = on; }

    // ---- Spectrum/waterfall split ----

    float spectrumFrac() const { return m_spectrumFrac; }
    void setSpectrumFrac(float f);

    // ---- Tuning step ----

    int  stepSize() const { return m_stepHz; }
    void setStepSize(int hz) { m_stepHz = hz; }

    // ---- Filter limits ----

    void setFilterLimits(int minHz, int maxHz);
    void setMode(const QString& mode) { m_mode = mode; }

    // ---- Overlay menu and VFO widgets ----

    SpectrumOverlayMenu* overlayMenu() const { return m_overlayMenu; }
    VfoWidget* vfoWidget(int sliceId) const;
    VfoWidget* addVfoWidget(int sliceId);
    void       removeVfoWidget(int sliceId);
    void       setActiveVfoWidget(int sliceId);

    // ---- FFT display controls ----

    void setFftAverage(int frames);
    void setFftWeightedAvg(bool on);
    void setFftFps(int fps);
    void setFftFillAlpha(float a);
    void setFftFillColor(const QColor& c);
    void setFftHeatMap(bool on);
    int   fftAverage() const { return m_fftAverage; }
    int   fftFps() const { return m_fftFps; }
    bool  fftWeightedAvg() const { return m_fftWeightedAvg; }
    float fftFillAlpha() const { return m_fftFillAlpha; }
    QColor fftFillColor() const { return m_fftFillColor; }
    bool  fftHeatMap() const { return m_fftHeatMap; }

    // ---- Waterfall display controls ----

    void setWfColorGain(int gain);
    void setWfBlackLevel(int level);
    void setWfAutoBlack(bool on);
    void setWfLineDuration(int ms);
    void setWfColorScheme(int scheme);
    int  wfColorGain() const { return m_wfColorGain; }
    int  wfBlackLevel() const { return m_wfBlackLevel; }
    bool wfAutoBlack() const { return m_wfAutoBlack; }
    int  wfLineDuration() const { return m_wfLineDuration; }
    int  wfColorScheme() const { return static_cast<int>(m_wfColorScheme); }

    // ---- Multi-slice overlay API ----

    struct SliceOverlay {
        int    sliceId{0};
        double freqMhz{0};
        int    filterLowHz{0};
        int    filterHighHz{0};
        bool   isTxSlice{false};
        bool   isActive{false};
        QString mode;
        bool   ritOn{false};
        int    ritFreq{0};
        bool   xitOn{false};
        int    xitFreq{0};
    };

    void setSliceOverlay(int sliceId, double freq, int fLow, int fHigh,
                         bool tx, bool active, const QString& mode = {},
                         bool ritOn = false, int ritFreq = 0,
                         bool xitOn = false, int xitFreq = 0);
    void setSliceOverlayFreq(int sliceId, double freqMhz);
    void removeSliceOverlay(int sliceId);

    // ---- TNF overlay ----

    struct TnfMarker {
        int    id;
        double freqMhz;
        int    widthHz;
        int    depthDb;
        bool   permanent;
    };
    void setTnfMarkers(const QVector<TnfMarker>& markers);
    void setTnfGlobalEnabled(bool on);

    // ---- Spot overlay ----

    struct SpotMarker {
        int     index;
        QString callsign;
        double  freqMhz;
        QString color;
        QString mode;
        QColor  dxccColor;
        QString source;
        QString spotterCallsign;
        QString comment;
        qint64  timestampMs{0};
    };
    void setSpotMarkers(const QVector<SpotMarker>& markers);
    void setShowSpots(bool on) { m_showSpots = on; update(); }
    bool showSpots() const { return m_showSpots; }

    // ---- Band plan overlay ----

    void setShowBandPlan(bool on);
    void setBandPlanFontSize(int pt);
    void setBandPlanManager(class BandPlanManager* mgr);

signals:
    void sliceClicked(int sliceId);
    void frequencyClicked(double mhz);
    void spotTriggered(int spotIndex);
    void bandwidthChangeRequested(double newBandwidthMhz);
    void centerChangeRequested(double newCenterMhz);
    void filterChangeRequested(int lowHz, int highHz);
    void dbmRangeChangeRequested(float minDbm, float maxDbm);
    void tnfCreateRequested(double freqMhz);
    void tnfMoveRequested(int id, double newFreqMhz);
    void tnfRemoveRequested(int id);
    void tnfWidthRequested(int id, int widthHz);
    void tnfDepthRequested(int id, int depthDb);
    void tnfPermanentRequested(int id, bool permanent);

protected:
#ifdef NEREUS_GPU_SPECTRUM
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
#else
    void paintEvent(QPaintEvent* event) override;
#endif
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // ---- Drawing helpers (CPU fallback) ----
    void drawGrid(QPainter& p, const QRect& r);
    void drawSpectrum(QPainter& p, const QRect& r);
    void drawWaterfall(QPainter& p, const QRect& r);
    void drawSliceMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect);
    void drawTnfMarkers(QPainter& p, const QRect& specRect, const QRect& wfRect);
    void drawSpotMarkers(QPainter& p, const QRect& specRect);
    void drawBandPlan(QPainter& p, const QRect& specRect);
    void drawFreqScale(QPainter& p, const QRect& r);
    void drawDbmScale(QPainter& p, const QRect& specRect);

    // ---- Coordinate helpers ----
    int    mhzToX(double mhz) const;
    double xToMhz(int x) const;

    // ---- Waterfall helpers ----
    void   pushWaterfallRow(const QVector<float>& bins);
    QRgb   dbmToRgb(float dbm) const;

    // ---- Data ----
    QVector<float> m_bins;           // raw FFT frame (dBm)
    QVector<float> m_smoothed;       // exponential-smoothed for display

    double m_centerMhz{14.225};
    double m_bandwidthMhz{0.200};

    QVector<SliceOverlay> m_sliceOverlays;
    int    m_filterMinHz{-12000};
    int    m_filterMaxHz{12000};
    QString m_mode{"USB"};

    float  m_refLevel{-50.0f};
    float  m_dynamicRange{100.0f};

    bool   m_noiseFloorEnable{false};
    int    m_noiseFloorPosition{75};

    int    m_stepHz{100};
    int    m_panIndex{0};

    // FFT display controls
    int    m_fftAverage{0};
    bool   m_fftWeightedAvg{false};
    int    m_fftFps{30};
    float  m_fftFillAlpha{0.70f};
    QColor m_fftFillColor{0x00, 0xe5, 0xff};   // cyan
    bool   m_fftHeatMap{true};

    // Waterfall display controls
    int    m_wfColorGain{50};
    int    m_wfBlackLevel{15};
    bool   m_wfAutoBlack{true};
    WfColorScheme m_wfColorScheme{WfColorScheme::Default};
    int    m_wfLineDuration{100};
    float  m_wfMinDbm{-130.0f};
    float  m_wfMaxDbm{-50.0f};

    // Waterfall ring buffer image (Format_RGB32)
    QImage m_waterfall;
    int    m_wfWriteRow{0};

    // Smoothing constant (matches AetherSDR)
    static constexpr float kSmoothAlpha = 0.35f;

    // Layout constants
    float  m_spectrumFrac{0.40f};
    static constexpr int kFreqScaleH = 20;
    static constexpr int kDividerH = 4;
    static constexpr int kDbmStripW = 36;

    // Drag state
    bool   m_draggingDivider{false};
    bool   m_draggingBandwidth{false};
    bool   m_draggingPan{false};
    bool   m_draggingDbm{false};
    enum class FilterEdge { None, Low, High };
    FilterEdge m_draggingFilter{FilterEdge::None};

    // TNF and spot markers
    QVector<TnfMarker> m_tnfMarkers;
    bool   m_tnfGlobalEnabled{true};
    QVector<SpotMarker> m_spotMarkers;
    bool   m_showSpots{true};

    // Band plan
    int    m_bandPlanFontSize{6};
    BandPlanManager* m_bandPlanMgr{nullptr};

    // Overlay menu and VFO widgets
    SpectrumOverlayMenu* m_overlayMenu{nullptr};
    QMap<int, VfoWidget*> m_vfoWidgets;

#ifdef NEREUS_GPU_SPECTRUM
    bool m_rhiInitialized{false};

    // ---- Waterfall GPU resources ----
    QRhiGraphicsPipeline*       m_wfPipeline{nullptr};
    QRhiShaderResourceBindings* m_wfSrb{nullptr};
    QRhiBuffer*                 m_wfVbo{nullptr};
    QRhiBuffer*                 m_wfUbo{nullptr};
    QRhiTexture*                m_wfGpuTex{nullptr};
    QRhiSampler*                m_wfSampler{nullptr};
    int  m_wfGpuTexW{0};
    int  m_wfGpuTexH{0};
    bool m_wfTexFullUpload{true};
    int  m_wfLastUploadedRow{-1};

    // ---- Overlay GPU resources ----
    QRhiGraphicsPipeline*       m_ovPipeline{nullptr};
    QRhiShaderResourceBindings* m_ovSrb{nullptr};
    QRhiBuffer*                 m_ovVbo{nullptr};
    QRhiTexture*                m_ovGpuTex{nullptr};
    QRhiSampler*                m_ovSampler{nullptr};
    QImage m_overlayStatic;
    QImage m_overlayDynamic;
    bool   m_overlayStaticDirty{true};
    bool   m_overlayNeedsUpload{true};

    // ---- FFT spectrum GPU resources ----
    QRhiGraphicsPipeline*       m_fftLinePipeline{nullptr};
    QRhiGraphicsPipeline*       m_fftFillPipeline{nullptr};
    QRhiShaderResourceBindings* m_fftSrb{nullptr};
    QRhiBuffer*                 m_fftLineVbo{nullptr};
    QRhiBuffer*                 m_fftFillVbo{nullptr};
    static constexpr int kMaxFftBins = 16384;
    static constexpr int kFftVertStride = 6;   // x, y, r, g, b, a

    void initWaterfallPipeline();
    void initOverlayPipeline();
    void initSpectrumPipeline();
    void renderGpuFrame(QRhiCommandBuffer* cb);
#endif

    void markOverlayDirty() {
#ifdef NEREUS_GPU_SPECTRUM
        m_overlayStaticDirty = true;
#endif
        update();
    }
};

} // namespace NereusSDR
```

---

## 3. Waterfall Rendering -- Ring Buffer Texture

### Concept

The waterfall is a scrolling heat map where each horizontal row represents
one FFT frame. New rows appear at the top; older rows scroll downward.

Implementation uses a ring buffer in a GPU texture. Instead of copying the
entire texture each frame (expensive), only one row is updated and the
fragment shader uses a `fract()` offset to wrap the read position.

### Ring Buffer Operation

```
GPU Texture (width = numBins, height = waterfallRows)
+--------------------------------------------------+
| Row 0: oldest visible frame                       |
| Row 1: ...                                        |
| ...                                               |
| Row W: newest frame (m_wfWriteRow)        <-- write here
| Row W+1: second-newest                            |
| ...                                               |
| Row H-1: wraps to oldest                          |
+--------------------------------------------------+

Write pointer advances: m_wfWriteRow = (m_wfWriteRow + 1) % height

Fragment shader reads: texCoord.y = fract(screenY / height + offset)
where offset = float(m_wfWriteRow) / float(height)
```

### CPU-Side: Pushing a Waterfall Row

Each FFT frame produces one waterfall row. The dBm values are mapped to
RGBA colors using the active color scheme, then uploaded to the GPU texture
at the current write position.

```cpp
void SpectrumWidget::pushWaterfallRow(const QVector<float>& bins)
{
    int w = m_waterfall.width();
    int destRow = m_wfWriteRow;

    // Map bins to display width (may need resampling if bin count != width)
    QRgb* scanline = reinterpret_cast<QRgb*>(m_waterfall.scanLine(destRow));
    float binScale = static_cast<float>(bins.size()) / static_cast<float>(w);

    for (int x = 0; x < w; ++x) {
        int srcBin = static_cast<int>(static_cast<float>(x) * binScale);
        srcBin = qBound(0, srcBin, bins.size() - 1);
        scanline[x] = dbmToRgb(bins[srcBin]);
    }

    m_wfWriteRow = (m_wfWriteRow + 1) % m_waterfall.height();
}
```

### Color Mapping: dBm to RGBA

```cpp
QRgb SpectrumWidget::dbmToRgb(float dbm) const
{
    // Normalize to 0.0-1.0 range based on display range
    float intensity = (dbm - m_wfMinDbm) / (m_wfMaxDbm - m_wfMinDbm);
    intensity = qBound(0.0f, intensity, 1.0f);

    // Apply color gain and black level adjustments
    float adjusted = (intensity - static_cast<float>(m_wfBlackLevel) / 125.0f)
                   * (static_cast<float>(m_wfColorGain) / 50.0f);
    adjusted = qBound(0.0f, adjusted, 1.0f);

    // Look up in gradient stops for current color scheme
    int stopCount = 0;
    const WfGradientStop* stops = wfSchemeStops(m_wfColorScheme, stopCount);

    // Find the two surrounding stops and interpolate
    for (int i = 0; i < stopCount - 1; ++i) {
        if (adjusted <= stops[i + 1].pos) {
            float t = (adjusted - stops[i].pos)
                    / (stops[i + 1].pos - stops[i].pos);
            int r = static_cast<int>(stops[i].r + t * (stops[i + 1].r - stops[i].r));
            int g = static_cast<int>(stops[i].g + t * (stops[i + 1].g - stops[i].g));
            int b = static_cast<int>(stops[i].b + t * (stops[i + 1].b - stops[i].b));
            return qRgb(r, g, b);
        }
    }
    return qRgb(stops[stopCount - 1].r,
                stops[stopCount - 1].g,
                stops[stopCount - 1].b);
}
```

### Default Color Scheme Stops

The default waterfall palette transitions through the thermal spectrum:

| Position | Color | RGB |
|----------|-------|-----|
| 0.00 | Black | (0, 0, 0) |
| 0.15 | Dark blue | (0, 0, 80) |
| 0.30 | Blue | (0, 0, 255) |
| 0.40 | Cyan | (0, 200, 255) |
| 0.55 | Green | (0, 255, 0) |
| 0.70 | Yellow | (255, 255, 0) |
| 0.85 | Orange | (255, 128, 0) |
| 1.00 | Red | (255, 0, 0) |

These positions are tuned to match typical HF band noise floor and signal
levels. The black-to-blue transition covers the noise floor; cyan through
red covers signals from weak to very strong.

### GPU Texture Upload Strategy

To minimize CPU-to-GPU data transfer:

1. **Initial/resize:** Full texture upload (`m_wfTexFullUpload = true`)
2. **Per-frame:** Single-row partial upload at `m_wfWriteRow`
3. **Texture size:** Created at waterfall dimensions (numBins x waterfallRows)

```cpp
// In renderGpuFrame(), waterfall texture upload:
if (m_wfTexFullUpload) {
    // Full upload: entire QImage to texture
    QRhiTextureUploadDescription upload;
    upload.setEntries({QRhiTextureUploadEntry(
        0, 0, QRhiTextureSubresourceUploadDescription(m_waterfall))});
    resourceUpdates->uploadTexture(m_wfGpuTex, upload);
    m_wfTexFullUpload = false;
    m_wfLastUploadedRow = m_wfWriteRow;
} else if (m_wfLastUploadedRow != m_wfWriteRow) {
    // Partial upload: only the rows written since last upload
    int row = m_wfLastUploadedRow;
    while (row != m_wfWriteRow) {
        QRhiTextureSubresourceUploadDescription sub;
        sub.setSourceSize(QSize(m_waterfall.width(), 1));
        sub.setDestinationTopLeft(QPoint(0, row));
        sub.setImage(m_waterfall);
        sub.setSourceTopLeft(QPoint(0, row));

        QRhiTextureUploadDescription upload;
        upload.setEntries({QRhiTextureUploadEntry(0, 0, sub)});
        resourceUpdates->uploadTexture(m_wfGpuTex, upload);

        row = (row + 1) % m_waterfall.height();
    }
    m_wfLastUploadedRow = m_wfWriteRow;
}
```

---

## 4. Spectrum Trace Rendering

### GPU Path

The spectrum trace is rendered as a vertex buffer with per-vertex color.
Each FFT bin maps to one vertex with position (x, y) and color (r, g, b, a).

Vertex layout: 6 floats per vertex (x, y, r, g, b, a).

```cpp
void SpectrumWidget::updateSpectrumVertices()
{
    int binCount = m_smoothed.size();
    float xStep = 2.0f / static_cast<float>(binCount);  // NDC: -1 to +1

    // Line vertices: one per bin
    for (int i = 0; i < binCount; ++i) {
        float x = -1.0f + static_cast<float>(i) * xStep;
        float dbm = m_smoothed[i];
        float y = dbmToNdc(dbm);  // map dBm to NDC y (-1 bottom, +1 top)

        // Per-vertex color: intensity heat map or solid color
        float r, g, b, a;
        if (m_fftHeatMap) {
            float intensity = (dbm - (m_refLevel - m_dynamicRange))
                            / m_dynamicRange;
            intensity = qBound(0.0f, intensity, 1.0f);
            // Heat map: blue -> cyan -> green -> yellow -> red
            heatMapColor(intensity, r, g, b);
            a = 1.0f;
        } else {
            r = m_fftFillColor.redF();
            g = m_fftFillColor.greenF();
            b = m_fftFillColor.blueF();
            a = 1.0f;
        }

        int offset = i * kFftVertStride;
        m_lineVertData[offset + 0] = x;
        m_lineVertData[offset + 1] = y;
        m_lineVertData[offset + 2] = r;
        m_lineVertData[offset + 3] = g;
        m_lineVertData[offset + 4] = b;
        m_lineVertData[offset + 5] = a;
    }

    // Fill vertices: two per bin (top at signal, bottom at specRect bottom)
    for (int i = 0; i < binCount; ++i) {
        int lineIdx = i * kFftVertStride;
        int fillIdx = i * 2 * kFftVertStride;

        // Top vertex = same as line vertex, with fill alpha
        std::memcpy(&m_fillVertData[fillIdx],
                    &m_lineVertData[lineIdx],
                    kFftVertStride * sizeof(float));
        m_fillVertData[fillIdx + 5] = m_fftFillAlpha;

        // Bottom vertex = same x, bottom of spectrum area
        m_fillVertData[fillIdx + kFftVertStride + 0] = m_lineVertData[lineIdx + 0];
        m_fillVertData[fillIdx + kFftVertStride + 1] = -1.0f;  // bottom
        m_fillVertData[fillIdx + kFftVertStride + 2] = m_lineVertData[lineIdx + 2];
        m_fillVertData[fillIdx + kFftVertStride + 3] = m_lineVertData[lineIdx + 3];
        m_fillVertData[fillIdx + kFftVertStride + 4] = m_lineVertData[lineIdx + 4];
        m_fillVertData[fillIdx + kFftVertStride + 5] = m_fftFillAlpha * 0.3f;
    }
}
```

### Exponential Smoothing

Applied before vertex generation to prevent flickering:

```cpp
void SpectrumWidget::updateSpectrum(const QVector<float>& binsDbm)
{
    m_bins = binsDbm;

    if (m_smoothed.size() != binsDbm.size()) {
        m_smoothed = binsDbm;  // first frame: no smoothing
    } else {
        for (int i = 0; i < binsDbm.size(); ++i) {
            m_smoothed[i] = kSmoothAlpha * binsDbm[i]
                          + (1.0f - kSmoothAlpha) * m_smoothed[i];
        }
    }

    pushWaterfallRow(binsDbm);  // unsmoothed for waterfall (sharper signals)
    update();                   // schedule repaint
}
```

The spectrum trace uses smoothed data (kSmoothAlpha = 0.35) for visual
stability. The waterfall uses unsmoothed FFT data so signal edges remain
crisp.

---

## 5. Overlay System

### Layer Architecture

Overlays are rendered in two layers:

1. **Static overlay** (QImage, repainted on state change): grid lines,
   frequency scale, dBm scale, band plan regions, slice markers, TNF markers,
   spot callsign labels.

2. **Dynamic overlay** (per-frame): spectrum line, cursor tracking.

In GPU mode, the static overlay is painted to a QImage using QPainter,
uploaded to a GPU texture, and rendered as a textured quad. This avoids
QPainter-on-QRhi overhead while allowing rich text and anti-aliased drawing
for labels.

### VFO Frequency Markers

Vertical lines at each slice's tuned frequency:

```cpp
void SpectrumWidget::drawSliceMarkers(QPainter& p,
                                       const QRect& specRect,
                                       const QRect& wfRect)
{
    for (const SliceOverlay& ov : m_sliceOverlays) {
        int vfoX = mhzToX(ov.freqMhz);
        if (vfoX < 0 || vfoX > width()) { continue; }

        // VFO line color: orange for active, gray for inactive
        QColor lineColor = ov.isActive
            ? QColor(255, 165, 0)       // orange
            : QColor(128, 128, 128);    // gray

        // Draw VFO line across spectrum and waterfall
        p.setPen(QPen(lineColor, 1));
        p.drawLine(vfoX, specRect.top(), vfoX, wfRect.bottom());

        // Slice letter label at top
        QString label = QChar('A' + ov.sliceId);
        p.setPen(lineColor);
        p.drawText(vfoX + 3, specRect.top() + 12, label);
    }
}
```

### Filter Passband Shading

Semi-transparent rectangle showing the receiver passband:

```cpp
void drawFilterPassband(QPainter& p, const SliceOverlay& ov,
                        const QRect& specRect, const QRect& wfRect)
{
    double loMhz = ov.freqMhz + ov.filterLowHz / 1.0e6;
    double hiMhz = ov.freqMhz + ov.filterHighHz / 1.0e6;
    int xLo = mhzToX(loMhz);
    int xHi = mhzToX(hiMhz);

    QColor fillColor = ov.isActive
        ? QColor(0, 180, 216, 40)       // cyan tint (NereusSDR accent)
        : QColor(128, 128, 128, 30);    // gray tint

    // Spectrum area passband
    p.fillRect(xLo, specRect.top(), xHi - xLo, specRect.height(), fillColor);

    // Waterfall area passband (slightly more transparent)
    QColor wfFill = fillColor;
    wfFill.setAlpha(fillColor.alpha() / 2);
    p.fillRect(xLo, wfRect.top(), xHi - xLo, wfRect.height(), wfFill);
}
```

### Band Plan Overlay

Colored horizontal regions showing band allocations. Drawn at the bottom
of the spectrum area:

```
| Extra  |  General  |  Advanced  |  Extra  |
|  red   |   green   |   blue     |  red    |
  14.000    14.150      14.175      14.225    14.350
```

### TNF Notch Markers

Vertical hatched regions at notch filter frequencies. Width corresponds to
the notch bandwidth:

```cpp
void drawTnfMarker(QPainter& p, const TnfMarker& tnf,
                   const QRect& specRect, const QRect& wfRect)
{
    double loMhz = tnf.freqMhz - tnf.widthHz / 2.0e6;
    double hiMhz = tnf.freqMhz + tnf.widthHz / 2.0e6;
    int xLo = mhzToX(loMhz);
    int xHi = mhzToX(hiMhz);

    QColor notchColor(255, 80, 80, 60);  // red tint
    p.fillRect(xLo, specRect.top(), xHi - xLo,
               specRect.height() + wfRect.height(), notchColor);

    // Center line
    int cx = (xLo + xHi) / 2;
    p.setPen(QPen(QColor(255, 80, 80), 1, Qt::DashLine));
    p.drawLine(cx, specRect.top(), cx, wfRect.bottom());
}
```

### DX Spot Callsign Labels

Callsigns displayed above the spectrum at their spotted frequency. Labels
stack vertically to avoid overlap (configurable max levels).

### Grid Lines

Frequency grid (vertical) and dBm grid (horizontal) with NereusSDR theme
colors:

```cpp
void drawGrid(QPainter& p, const QRect& specRect)
{
    // dBm grid lines (horizontal)
    p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1));  // STYLEGUIDE groove color
    float dbmStep = computeDbmStep(m_dynamicRange);
    for (float dbm = m_refLevel; dbm > m_refLevel - m_dynamicRange; dbm -= dbmStep) {
        int y = dbmToY(dbm, specRect);
        p.drawLine(specRect.left(), y, specRect.right(), y);
    }

    // Frequency grid lines (vertical)
    double freqStep = computeFreqStep(m_bandwidthMhz);
    double startFreq = std::ceil((m_centerMhz - m_bandwidthMhz / 2.0)
                                 / freqStep) * freqStep;
    for (double f = startFreq; f < m_centerMhz + m_bandwidthMhz / 2.0;
         f += freqStep) {
        int x = mhzToX(f);
        p.drawLine(x, specRect.top(), x, specRect.bottom());
    }
}
```

### Mouse Interaction

| Action | Behavior |
|--------|----------|
| Click in spectrum/waterfall | Tune active slice to clicked frequency (snapped to step size) |
| Click on inactive slice marker | Make that slice active |
| Drag filter edge | Resize passband (`filterChangeRequested` signal) |
| Drag inside passband | Move VFO (tune) |
| Drag frequency scale bar | Change display bandwidth |
| Drag waterfall | Pan center frequency |
| Drag dBm scale | Adjust reference level |
| Scroll wheel | Tune active slice by step size (up = higher freq) |
| Ctrl+scroll | Zoom bandwidth in/out |
| Right-click | Context menu (TNF create, spot add, etc.) |

---

## 6. Shader Pipeline

### Shader Files

All shaders are written in GLSL and compiled to Qt's QSB format using
`qsb` from the Qt ShaderTools module. QSB bundles SPIR-V, HLSL, MSL, and
GLSL variants for cross-platform RHI compatibility.

```
src/shaders/
  waterfall.vert      -- fullscreen quad vertex positions
  waterfall.frag      -- ring-buffer texture sampling with fract() offset
  spectrum.vert       -- per-vertex position passthrough
  spectrum.frag       -- per-vertex color passthrough
  overlay.vert        -- textured quad vertex positions
  overlay.frag        -- RGBA texture sampling (alpha blending)
```

### waterfall.vert

```glsl
#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    v_texcoord = texcoord;
}
```

### waterfall.frag

```glsl
#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D waterfallTex;

layout(std140, binding = 1) uniform WaterfallUBO {
    float scrollOffset;    // m_wfWriteRow / textureHeight
    float texHeight;       // texture height in texels
    float opacity;         // overall opacity (1.0 normally)
    float padding;
};

void main()
{
    // Ring buffer wrap: offset the Y coordinate by the write position
    float y = fract(v_texcoord.y + scrollOffset);
    fragColor = texture(waterfallTex, vec2(v_texcoord.x, y));
    fragColor.a *= opacity;
}
```

### spectrum.vert

```glsl
#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    v_color = color;
}
```

### spectrum.frag

```glsl
#version 440

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = v_color;
}
```

### overlay.vert

```glsl
#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    v_texcoord = texcoord;
}
```

### overlay.frag

```glsl
#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D overlayTex;

void main()
{
    fragColor = texture(overlayTex, v_texcoord);
}
```

### QSB Compilation

Shaders are compiled at build time via CMake:

```cmake
qt_add_shaders(NereusSDR "shaders"
    PREFIX "/shaders"
    FILES
        src/shaders/waterfall.vert
        src/shaders/waterfall.frag
        src/shaders/spectrum.vert
        src/shaders/spectrum.frag
        src/shaders/overlay.vert
        src/shaders/overlay.frag
)
```

---

## 7. GPU Pipeline Initialization

### Waterfall Pipeline

```cpp
void SpectrumWidget::initWaterfallPipeline()
{
    auto* rhi = this->rhi();

    // Vertex buffer: fullscreen quad (2 triangles, 4 vertices)
    static constexpr float quadVerts[] = {
        // position (x,y), texcoord (u,v)
        -1.0f, -1.0f,  0.0f, 1.0f,   // bottom-left
         1.0f, -1.0f,  1.0f, 1.0f,   // bottom-right
        -1.0f,  1.0f,  0.0f, 0.0f,   // top-left
         1.0f,  1.0f,  1.0f, 0.0f,   // top-right
    };
    m_wfVbo = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                             sizeof(quadVerts));
    m_wfVbo->create();

    // Uniform buffer: scroll offset, texture height, opacity
    m_wfUbo = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                             4 * sizeof(float));
    m_wfUbo->create();

    // Texture: ring buffer waterfall
    int texW = m_waterfall.width();
    int texH = m_waterfall.height();
    m_wfGpuTex = rhi->newTexture(QRhiTexture::RGBA8,
                                  QSize(texW, texH));
    m_wfGpuTex->create();
    m_wfGpuTexW = texW;
    m_wfGpuTexH = texH;

    // Sampler: linear filtering, clamp-to-edge
    m_wfSampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                   QRhiSampler::None,
                                   QRhiSampler::ClampToEdge,
                                   QRhiSampler::ClampToEdge);
    m_wfSampler->create();

    // Shader resource bindings
    m_wfSrb = rhi->newShaderResourceBindings();
    m_wfSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(
            0, QRhiShaderResourceBinding::FragmentStage,
            m_wfGpuTex, m_wfSampler),
        QRhiShaderResourceBinding::uniformBuffer(
            1, QRhiShaderResourceBinding::FragmentStage,
            m_wfUbo),
    });
    m_wfSrb->create();

    // Graphics pipeline
    m_wfPipeline = rhi->newGraphicsPipeline();

    QShader vertShader = loadShader(":/shaders/waterfall.vert.qsb");
    QShader fragShader = loadShader(":/shaders/waterfall.frag.qsb");
    m_wfPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader},
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        {4 * sizeof(float)},   // stride: 2 position + 2 texcoord
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},                    // position
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},   // texcoord
    });
    m_wfPipeline->setVertexInputLayout(inputLayout);
    m_wfPipeline->setShaderResourceBindings(m_wfSrb);
    m_wfPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_wfPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_wfPipeline->create();
}
```

### Spectrum Pipeline

```cpp
void SpectrumWidget::initSpectrumPipeline()
{
    auto* rhi = this->rhi();

    // Line VBO: kMaxFftBins vertices x kFftVertStride floats each
    m_fftLineVbo = rhi->newBuffer(QRhiBuffer::Dynamic,
                                   QRhiBuffer::VertexBuffer,
                                   kMaxFftBins * kFftVertStride * sizeof(float));
    m_fftLineVbo->create();

    // Fill VBO: 2 * kMaxFftBins vertices (top + bottom per bin)
    m_fftFillVbo = rhi->newBuffer(QRhiBuffer::Dynamic,
                                   QRhiBuffer::VertexBuffer,
                                   2 * kMaxFftBins * kFftVertStride * sizeof(float));
    m_fftFillVbo->create();

    // No uniforms needed -- vertices carry position and color
    m_fftSrb = rhi->newShaderResourceBindings();
    m_fftSrb->setBindings({});
    m_fftSrb->create();

    QShader vertShader = loadShader(":/shaders/spectrum.vert.qsb");
    QShader fragShader = loadShader(":/shaders/spectrum.frag.qsb");

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        {kFftVertStride * sizeof(float)},  // stride: x, y, r, g, b, a
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},                    // position
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)},   // color
    });

    // Line pipeline (line strip)
    m_fftLinePipeline = rhi->newGraphicsPipeline();
    m_fftLinePipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader},
    });
    m_fftLinePipeline->setVertexInputLayout(inputLayout);
    m_fftLinePipeline->setShaderResourceBindings(m_fftSrb);
    m_fftLinePipeline->setRenderPassDescriptor(
        renderTarget()->renderPassDescriptor());
    m_fftLinePipeline->setTopology(QRhiGraphicsPipeline::LineStrip);
    m_fftLinePipeline->setLineWidth(1.0f);
    m_fftLinePipeline->create();

    // Fill pipeline (triangle strip, alpha blending)
    m_fftFillPipeline = rhi->newGraphicsPipeline();
    m_fftFillPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader},
    });
    m_fftFillPipeline->setVertexInputLayout(inputLayout);
    m_fftFillPipeline->setShaderResourceBindings(m_fftSrb);
    m_fftFillPipeline->setRenderPassDescriptor(
        renderTarget()->renderPassDescriptor());
    m_fftFillPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_fftFillPipeline->setTargetBlends({blend});
    m_fftFillPipeline->create();
}
```

---

## 8. Render Frame Sequence

### GPU Render Loop

Each frame follows this sequence in `render(QRhiCommandBuffer* cb)`:

```
1. Update waterfall UBO (scroll offset)
2. Upload new waterfall row(s) to GPU texture (partial upload)
3. Upload overlay textures if dirty
4. Update FFT vertex buffer with smoothed spectrum data
5. Begin render pass
6. Draw waterfall quad (waterfall pipeline + texture)
7. Draw spectrum fill (fill pipeline + VBO, alpha blending)
8. Draw spectrum line (line pipeline + VBO)
9. Draw static overlay (overlay pipeline + texture, alpha blending)
10. End render pass
```

### Full renderGpuFrame() Sketch

```cpp
void SpectrumWidget::renderGpuFrame(QRhiCommandBuffer* cb)
{
    auto* rhi = this->rhi();
    QRhiResourceUpdateBatch* ru = rhi->nextResourceUpdateBatch();

    // 1. Waterfall scroll offset
    float ubo[4] = {
        static_cast<float>(m_wfWriteRow) / static_cast<float>(m_wfGpuTexH),
        static_cast<float>(m_wfGpuTexH),
        1.0f,   // opacity
        0.0f    // padding
    };
    ru->updateDynamicBuffer(m_wfUbo, 0, sizeof(ubo), ubo);

    // 2. Waterfall texture (partial row upload)
    if (m_wfTexFullUpload) {
        ru->uploadTexture(m_wfGpuTex,
            QRhiTextureUploadDescription(
                QRhiTextureUploadEntry(0, 0,
                    QRhiTextureSubresourceUploadDescription(m_waterfall))));
        m_wfTexFullUpload = false;
        m_wfLastUploadedRow = m_wfWriteRow;
    } else if (m_wfLastUploadedRow != m_wfWriteRow) {
        // Upload changed rows
        // ... (see section 3 for detail)
    }

    // 3. Overlay textures
    if (m_overlayStaticDirty) {
        repaintStaticOverlay();   // QPainter -> m_overlayStatic QImage
        m_overlayStaticDirty = false;
        m_overlayNeedsUpload = true;
    }
    if (m_overlayNeedsUpload) {
        ru->uploadTexture(m_ovGpuTex,
            QRhiTextureUploadDescription(
                QRhiTextureUploadEntry(0, 0,
                    QRhiTextureSubresourceUploadDescription(m_overlayStatic))));
        m_overlayNeedsUpload = false;
    }

    // 4. FFT vertex data
    updateSpectrumVertices();
    ru->updateDynamicBuffer(m_fftLineVbo, 0,
        m_smoothed.size() * kFftVertStride * sizeof(float),
        m_lineVertData.constData());
    ru->updateDynamicBuffer(m_fftFillVbo, 0,
        m_smoothed.size() * 2 * kFftVertStride * sizeof(float),
        m_fillVertData.constData());

    // 5. Begin render pass
    const QColor clearColor(0x0f, 0x0f, 0x1a);  // STYLEGUIDE app background
    cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, ru);

    // 6. Draw waterfall
    cb->setGraphicsPipeline(m_wfPipeline);
    cb->setShaderResources(m_wfSrb);
    const QRhiCommandBuffer::VertexInput wfInput(m_wfVbo, 0);
    cb->setVertexInput(0, 1, &wfInput);
    cb->setViewport(waterfallViewport());
    cb->draw(4);   // triangle strip quad

    // 7. Draw spectrum fill
    cb->setGraphicsPipeline(m_fftFillPipeline);
    cb->setShaderResources(m_fftSrb);
    const QRhiCommandBuffer::VertexInput fillInput(m_fftFillVbo, 0);
    cb->setVertexInput(0, 1, &fillInput);
    cb->setViewport(spectrumViewport());
    cb->draw(m_smoothed.size() * 2);

    // 8. Draw spectrum line
    cb->setGraphicsPipeline(m_fftLinePipeline);
    cb->setShaderResources(m_fftSrb);
    const QRhiCommandBuffer::VertexInput lineInput(m_fftLineVbo, 0);
    cb->setVertexInput(0, 1, &lineInput);
    cb->setViewport(spectrumViewport());
    cb->draw(m_smoothed.size());

    // 9. Draw static overlay
    cb->setGraphicsPipeline(m_ovPipeline);
    cb->setShaderResources(m_ovSrb);
    const QRhiCommandBuffer::VertexInput ovInput(m_ovVbo, 0);
    cb->setVertexInput(0, 1, &ovInput);
    cb->setViewport(fullViewport());
    cb->draw(4);   // textured quad

    // 10. End render pass
    cb->endPass();
}
```

---

## 9. Performance Considerations

### Target Performance

- **60 FPS** waterfall rendering at 4096-point FFT
- **4 simultaneous panadapters** without frame drops
- **16384-point FFT** at 30 FPS for high-resolution display

### CPU-to-GPU Transfer Budget

Per frame per panadapter:

| Data | Size (4096 FFT) | Frequency |
|------|-----------------|-----------|
| Waterfall row upload | 4096 x 4 bytes = 16 KB | 30-60x/sec |
| FFT line vertices | 4096 x 24 bytes = 96 KB | 30-60x/sec |
| FFT fill vertices | 8192 x 24 bytes = 192 KB | 30-60x/sec |
| Overlay texture | 800x300 x 4 bytes = 960 KB | On state change only |
| Waterfall UBO | 16 bytes | 30-60x/sec |

Total per-frame: ~304 KB at 60 FPS = ~18 MB/s per pan. Well within
PCIe/USB bandwidth limits.

### Optimization Strategies

1. **Partial texture upload:** Only upload the single new waterfall row,
   not the entire texture. This reduces waterfall upload from ~4 MB to
   16 KB per frame.

2. **Static overlay caching:** Grid, markers, and labels are painted to a
   QImage only when state changes (frequency, dBm range, slice positions).
   The GPU texture is only re-uploaded on change.

3. **Dynamic vertex buffer:** FFT vertices are updated in-place using
   `QRhiBuffer::Dynamic` mode, avoiding per-frame buffer recreation.

4. **Viewport scissoring:** Each render pass element uses a viewport
   restricted to its screen region (spectrum area, waterfall area, etc.)
   to avoid unnecessary fragment processing.

5. **FFT on dedicated thread:** FFTEngine runs on the spectrum worker
   thread, preventing FFT computation from blocking the GUI thread.

6. **Rate limiting:** FFTEngine limits output to target FPS. Extra I/Q
   samples are accumulated, not discarded, ensuring no data loss while
   preventing GPU overload.

### Multi-Pan Resource Independence

Each SpectrumWidget owns its own GPU resources (textures, buffers,
pipelines, samplers). This ensures:

- No shared state between pans (no mutex needed)
- Each pan can have independent FFT size and display settings
- Removing a pan cleanly releases its GPU resources via `releaseResources()`
- GPU memory scales linearly: ~5 MB per pan at 4096-point FFT

### CPU Fallback Path

When `NEREUS_GPU_SPECTRUM` is not defined, SpectrumWidget falls back to
QPainter rendering in `paintEvent()`. The waterfall uses a QImage ring
buffer drawn directly via `QPainter::drawImage()`. This path is functional
but significantly slower (typically 15-20 FPS at 4096 FFT) and serves as
a compatibility fallback for systems without GPU support.

---

## 10. Key Differences from AetherSDR Summary

| Aspect | AetherSDR | NereusSDR |
|--------|-----------|-----------|
| FFT computation | Radio (VITA-49 PCC 0x8003) | Client (FFTW3 on spectrum thread) |
| Waterfall data | Radio tiles (PCC 0x8004) or FFT-derived | Always FFT-derived |
| FFT data path | PanadapterStream -> SpectrumWidget | FFTEngine -> FFTRouter -> SpectrumWidget |
| FFT size control | Radio-side (`display pan set fps/average`) | Client-side (per-pan FFTEngine config) |
| Display BW change | Radio command | Client-side (re-window FFT output) |
| Max FFT bins | 8192 (`kMaxFftBins`) | 16384 (`kMaxFftBins`) |
| Shader define | `AETHER_GPU_SPECTRUM` | `NEREUS_GPU_SPECTRUM` |
| Sample rate source | Radio VITA-49 context packets | OpenHPSDR C&C configuration |
| Waterfall blanker | Client-side (impulse detection) | Same pattern, adapted |
| Noise floor auto | Radio `display pan set` + client auto-black | Client-only auto-black |
