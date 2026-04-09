#pragma once

#include <QWidget>
#include <QVector>
#include <QImage>
#include <QColor>
#include <QPoint>

namespace NereusSDR {

class SpectrumOverlayMenu;

// Waterfall color scheme presets.
// Default matches AetherSDR/SmartSDR style.
// From Thetis enums.cs:68-79 (ColorScheme enum)
enum class WfColorScheme : int {
    Default = 0,    // AetherSDR: black → blue → cyan → green → yellow → red
    Enhanced,       // Thetis enhanced (9-band progression)
    Spectran,       // SPECTRAN
    BlackWhite,     // Grayscale
    Count
};

// Gradient stop for waterfall color mapping.
struct WfGradientStop { float pos; int r, g, b; };

// Returns gradient stops for a given color scheme.
const WfGradientStop* wfSchemeStops(WfColorScheme scheme, int& count);

// CPU-rendered spectrum + waterfall display widget.
// Phase A: QPainter fallback (get something visible fast).
// Phase B: Switch to QRhiWidget for GPU rendering.
//
// Layout (top to bottom):
//   36px  - dBm scale (left strip)
//   ~40%  - spectrum trace (FFT line plot)
//   4px   - divider
//   ~60%  - waterfall (scrolling heat-map history)
//   20px  - frequency scale bar
//
// From gpu-waterfall.md lines 274-289
class SpectrumWidget : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);
    ~SpectrumWidget() override;

    QSize sizeHint() const override { return {800, 400}; }

    // ---- Frequency range ----
    void setFrequencyRange(double centerHz, double bandwidthHz);
    double centerFrequency() const { return m_centerHz; }
    double bandwidth() const { return m_bandwidthHz; }

    // ---- Display range ----
    void setDbmRange(float minDbm, float maxDbm);
    float refLevel() const { return m_refLevel; }
    float dynamicRange() const { return m_dynamicRange; }

    // ---- Waterfall settings ----
    void setWfColorScheme(WfColorScheme scheme);
    void setWfColorGain(int gain) { m_wfColorGain = gain; }
    void setWfBlackLevel(int level) { m_wfBlackLevel = level; }

    // ---- Per-pan settings persistence ----
    void setPanIndex(int idx) { m_panIndex = idx; }
    int  panIndex() const { return m_panIndex; }
    void loadSettings();
    void saveSettings();

    // ---- VFO / filter overlay ----
    void setVfoFrequency(double hz) { m_vfoHz = hz; update(); }
    void setFilterOffset(int lowHz, int highHz) { m_filterLowHz = lowHz; m_filterHighHz = highHz; update(); }

    // ---- Tuning step ----
    void setStepSize(int hz) { m_stepHz = hz; }
    int  stepSize() const { return m_stepHz; }

public slots:
    // Feed a new FFT frame. binsDbm are dBm values, one per frequency bin.
    // Called from the main thread after FFTEngine delivers the frame.
    void updateSpectrum(int receiverId, const QVector<float>& binsDbm);

signals:
    // Emitted when user clicks on spectrum/waterfall to tune
    void frequencyClicked(double hz);
    // Emitted when user scrolls to change bandwidth
    void bandwidthChangeRequested(double newBandwidthHz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // ---- Drawing helpers ----
    void drawGrid(QPainter& p, const QRect& specRect);
    void drawSpectrum(QPainter& p, const QRect& specRect);
    void drawWaterfall(QPainter& p, const QRect& wfRect);
    void drawFreqScale(QPainter& p, const QRect& r);
    void drawDbmScale(QPainter& p, const QRect& specRect);
    void drawVfoMarker(QPainter& p, const QRect& specRect, const QRect& wfRect);
    void drawCursorInfo(QPainter& p, const QRect& specRect);

    // ---- Coordinate helpers ----
    int    hzToX(double hz, const QRect& r) const;
    double xToHz(int x, const QRect& r) const;
    int    dbmToY(float dbm, const QRect& r) const;

    // ---- Waterfall helpers ----
    void   pushWaterfallRow(const QVector<float>& bins);
    QRgb   dbmToRgb(float dbm) const;

    // ---- FFT data ----
    QVector<float> m_bins;       // latest raw FFT frame (dBm)
    QVector<float> m_smoothed;   // exponential-smoothed for display

    // ---- Frequency range ----
    double m_centerHz{14225000.0};    // 14.225 MHz default
    double m_bandwidthHz{200000.0};   // 200 kHz default

    // ---- Display range ----
    // From Thetis display.cs:1743-1754
    float  m_refLevel{-40.0f};        // top of display (dBm)
    float  m_dynamicRange{100.0f};    // range in dB (bottom = refLevel - dynamicRange)

    // ---- Waterfall ----
    QImage m_waterfall;               // ring buffer (Format_RGB32)
    int    m_wfWriteRow{0};

    // ---- Waterfall display controls ----
    // From AetherSDR SpectrumWidget defaults + Thetis display.cs:2522-2536
    WfColorScheme m_wfColorScheme{WfColorScheme::Default};
    int    m_wfColorGain{50};         // 0-100
    int    m_wfBlackLevel{15};        // 0-125
    // Waterfall uses its own dBm range (narrower than spectrum for better contrast)
    // From Thetis display.cs:2522 waterfall_high_threshold = -80.0F
    // From Thetis display.cs:2536 waterfall_low_threshold = -130.0F
    float  m_wfHighThreshold{-80.0f};
    float  m_wfLowThreshold{-130.0f};

    // ---- Smoothing constant ----
    // From AetherSDR SpectrumWidget.h:417 — SMOOTH_ALPHA = 0.35f
    static constexpr float kSmoothAlpha = 0.35f;

    // ---- Layout constants ----
    // From gpu-waterfall.md:590-593
    float  m_spectrumFrac{0.40f};     // 40% spectrum, 60% waterfall
    static constexpr int kFreqScaleH = 20;
    static constexpr int kDividerH = 4;
    static constexpr int kDbmStripW = 36;

    // ---- Spectrum fill ----
    // From AetherSDR defaults
    QColor m_fillColor{0x00, 0xe5, 0xff};  // cyan
    float  m_fillAlpha{0.70f};
    bool   m_panFill{true};

    // ---- VFO / filter overlay ----
    double m_vfoHz{0.0};
    int    m_filterLowHz{-2850};    // LSB default — from Thetis
    int    m_filterHighHz{-150};
    int    m_stepHz{100};           // tuning step size

    int    m_panIndex{0};            // for per-pan settings keys

    // ---- Overlay menu ----
    SpectrumOverlayMenu* m_overlayMenu{nullptr};

    // ---- Mouse state ----
    bool   m_draggingDbm{false};
    int    m_dragStartY{0};
    float  m_dragStartRef{0.0f};
    QPoint m_mousePos;              // for cursor frequency display
    bool   m_mouseInWidget{false};
};

} // namespace NereusSDR
