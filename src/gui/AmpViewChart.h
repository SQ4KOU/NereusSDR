// no-port-check: NereusSDR-original chart widget.  Thetis renders
// AmpView through System.Windows.Forms.DataVisualization.Charting
// (a Win32-only managed-chart control); NereusSDR replaces it with a
// custom QPainter widget per phase3m-4-puresignal-design.md §15 #14.
// Series semantics, axis ranges, and toggle behaviours are sourced
// 1:1 from AmpView.cs [v2.10.3.13] — this file is the rendering
// surface those semantics drive.
//
// =================================================================
// src/gui/AmpViewChart.h  (NereusSDR)
// =================================================================
//
// Custom QPainter chart widget for AmpViewWindow.  5 named series:
//
//   Ref     — perfect 1:1 reference (DimGray dashed)
//   MagAmp  — PA magnitude characteristic from raw envelope buffers
//             (DodgerBlue scatter)
//   PhsAmp  — PA phase characteristic computed atan2(yc, ys)
//             (Gold scatter, secondary Y axis)
//   MagCorr — Correction magnitude evaluated from cm[] cubic-Hermite
//             spline coefficients (Crimson polyline)
//   PhsCorr — Correction phase computed atan2(qys, qyc) from cc[]/cs[]
//             spline evaluations (Lime polyline, secondary Y axis)
//
// Axis ranges (toggle-driven):
//
//   ShowGain off → AxisY = [0, 1.0],  title "Magnitude"
//   ShowGain on  → AxisY = [0, 2.0],  title "Gain"
//   PhaseZoom off → AxisY2 = [-180, +180],  unit "deg"
//   PhaseZoom on  → AxisY2 = [-45,  +45 ]
//   LowRes on  → render skip stride = 4 (perf)
//   LowRes off → render skip stride = 1
//
// Source-mapped behavioural specs from AmpView.cs [v2.10.3.13]:
//
//   AmpView.cs:435-455  chkAVShowGain_CheckedChanged      → setShowGain
//   AmpView.cs:457-463  chkAVLowRes_CheckedChanged        → setLowRes
//   AmpView.cs:470-482  chkAVPhaseZoom_CheckedChanged     → setPhaseZoom
//   AmpView.cs:107-120  disp_setup                        → drawAxes
//   AmpView.cs:123-153  init_data                         → setSeriesData
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Phase 3M-4 Task 9 created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic
//                 Claude Code.  NereusSDR-original Qt6 widget.
// =================================================================

#pragma once

#include <QColor>
#include <QStringList>
#include <QWidget>

#include <vector>

class QPaintEvent;

namespace NereusSDR {

class AmpViewChart : public QWidget {
    Q_OBJECT

public:
    explicit AmpViewChart(QWidget* parent = nullptr);
    ~AmpViewChart() override;

    // Update buffers for next paint.  All five series share the same
    // x-axis vector.  Sizes may differ between series (the spline-derived
    // MagCorr / PhsCorr typically use np = 512 points; the raw MagAmp /
    // PhsAmp use ints * spi points).  The function only stores; the next
    // paintEvent() draws.
    void setSeriesData(const std::vector<double>& xs,
                       const std::vector<double>& magAmp,
                       const std::vector<double>& phsAmp,
                       const std::vector<double>& magCorr,
                       const std::vector<double>& phsCorr);

    // Render mode toggles.
    void setShowGain(bool on);
    bool showGain() const noexcept { return m_showGain; }
    void setPhaseZoom(bool on);
    bool phaseZoom() const noexcept { return m_phaseZoom; }
    void setLowRes(bool on);
    bool lowRes() const noexcept { return m_lowRes; }

    // Test seams.
    int seriesCount() const noexcept { return 5; }
    QStringList seriesNames() const {
        return {QStringLiteral("Ref"),
                QStringLiteral("MagAmp"),
                QStringLiteral("PhsAmp"),
                QStringLiteral("MagCorr"),
                QStringLiteral("PhsCorr")};
    }

    // Number of points stored on the x axis (used by setSeriesData test).
    int pointCount() const noexcept { return static_cast<int>(m_x.size()); }

    // Axis ranges (toggle-driven, exposed for testing).
    double magYMin()   const noexcept { return 0.0; }
    double magYMax()   const noexcept { return m_showGain ? 2.0 : 1.0; }
    double phaseYMax() const noexcept { return m_phaseZoom ? 45.0 : 180.0; }
    double phaseYMin() const noexcept { return -phaseYMax(); }

    // Low-res stride.  AmpView.cs:457-463 [v2.10.3.13].
    int lowResStride() const noexcept { return m_lowRes ? 4 : 1; }

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return QSize(564, 377); }

private:
    // Stored series data.
    std::vector<double> m_x;
    std::vector<double> m_magAmp;
    std::vector<double> m_phsAmp;
    std::vector<double> m_magCorr;
    std::vector<double> m_phsCorr;

    // Toggle state.
    bool m_showGain{false};
    bool m_phaseZoom{false};
    bool m_lowRes{true};   // chkAVLowRes default Checked per
                           // AmpView.Designer.cs:182-183 [v2.10.3.13]

    // Series colours (matching AmpView.Designer.cs:79-130 [v2.10.3.13]).
    static QColor colourRef()     { return QColor(0x69, 0x69, 0x69); } // DimGray
    static QColor colourMagAmp()  { return QColor(0x1E, 0x90, 0xFF); } // DodgerBlue
    static QColor colourPhsAmp()  { return QColor(0xFF, 0xD7, 0x00); } // Gold
    static QColor colourMagCorr() { return QColor(0xDC, 0x14, 0x3C); } // Crimson
    static QColor colourPhsCorr() { return QColor(0x00, 0xFF, 0x00); } // Lime
};

} // namespace NereusSDR
