// =================================================================
// src/gui/widgets/ParametricEqWidget.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/ucParametricEq.cs [v2.10.3.13],
//   original licence from Thetis source is included below.
//   Sole author: Richard Samphire (MW0LGE) — GPLv2-or-later with
//   Samphire dual-licensing.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-30 — Reimplemented in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.  Phase 3M-3a-ii follow-up
//                 sub-PR Batch 1: skeleton + EqPoint/EqJsonState classes
//                 + default 18-color palette + ctor with verbatim
//                 ucParametricEq.cs:360-447 defaults.
// =================================================================

/*  ucParametricEq.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2020-2026 Richard Samphire MW0LGE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

mw0lge@grange-lane.co.uk
*/
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

#include "ParametricEqWidget.h"

#include <QFontMetrics>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace NereusSDR {

// From Thetis ucParametricEq.cs:254-274 [v2.10.3.13] -- _default_band_palette.
// 18 RGB triples, verbatim.  Ports byte-for-byte; do not reorder, recolor, or trim.
const QVector<QColor>& ParametricEqWidget::defaultBandPalette() {
    static const QVector<QColor> kPalette = {
        QColor(  0, 190, 255),
        QColor(  0, 220, 130),
        QColor(255, 210,   0),
        QColor(255, 140,   0),
        QColor(255,  80,  80),
        QColor(255,   0, 180),
        QColor(170,  90, 255),
        QColor( 70, 120, 255),
        QColor(  0, 200, 200),
        QColor(180, 255,  90),
        QColor(255, 105, 180),
        QColor(255, 215, 120),
        QColor(120, 255, 255),
        QColor(140, 200, 255),
        QColor(220, 160, 255),
        QColor(255, 120,  40),
        QColor(120, 255, 160),
        QColor(255,  60, 120),
    };
    return kPalette;
}

int ParametricEqWidget::defaultBandPaletteSize() {
    return defaultBandPalette().size();
}

QColor ParametricEqWidget::defaultBandPaletteAt(int index) {
    const auto& p = defaultBandPalette();
    if (index < 0 || index >= p.size()) {
        return QColor();
    }
    return p.at(index);
}

// From Thetis ucParametricEq.cs:360-447 [v2.10.3.13] -- public ucParametricEq() ctor.
ParametricEqWidget::ParametricEqWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Bar chart peak-hold timer -- 33 ms cadence (~30 fps), matches WinForms Timer { Interval = 33 }
    // at ucParametricEq.cs:428-430.  Slot wired in Task 3 (painting batch).
    m_barChartPeakTimer = new QTimer(this);
    m_barChartPeakTimer->setInterval(33);

    resetPointsDefault();
}

ParametricEqWidget::~ParametricEqWidget() = default;

// =================================================================
// Axis math + ordering + clamping helpers (Phase 3M-3a-ii follow-up
// Batch 2).  All ports are line-faithful translations of
// ucParametricEq.cs [v2.10.3.13] -- each function carries a verbatim
// cite immediately above its definition.
// =================================================================

// From Thetis ucParametricEq.cs:2983-2988 [v2.10.3.13].
double ParametricEqWidget::clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// From Thetis ucParametricEq.cs:1013-1023 [v2.10.3.13].
double ParametricEqWidget::chooseDbStep(double span) const {
    double s = span;
    if (s <=  3.0) return  0.5;
    if (s <=  6.0) return  1.0;
    if (s <= 12.0) return  2.0;
    if (s <= 24.0) return  3.0;
    if (s <= 48.0) return  6.0;
    if (s <= 96.0) return 12.0;
    return 24.0;
}

// From Thetis ucParametricEq.cs:1007-1011 [v2.10.3.13].
double ParametricEqWidget::getYAxisStepDb() const {
    if (m_yAxisStepDb > 0.0) return m_yAxisStepDb;
    return chooseDbStep(m_dbMax - m_dbMin);
}

// From Thetis ucParametricEq.cs:2632-2643 [v2.10.3.13].
double ParametricEqWidget::chooseFrequencyStep(double span) const {
    double s = span;
    if (s <=   300.0) return   25.0;
    if (s <=   600.0) return   50.0;
    if (s <=  1200.0) return  100.0;
    if (s <=  2500.0) return  250.0;
    if (s <=  6000.0) return  500.0;
    if (s <= 12000.0) return 1000.0;
    if (s <= 24000.0) return 2000.0;
    return 5000.0;
}

// From Thetis ucParametricEq.cs:2995-3000 [v2.10.3.13].
double ParametricEqWidget::getLogFrequencyCentreHz(double minHz, double maxHz) const {
    double span = maxHz - minHz;
    if (span <= 0.0) return minHz;
    return minHz + (span * 0.125);
}

// From Thetis ucParametricEq.cs:3069-3078 [v2.10.3.13].
double ParametricEqWidget::getLogFrequencyShape(double centreRatio) const {
    double r = centreRatio;
    if (r <= 0.0 || r >= 1.0) return 0.0;
    if (qAbs(r - 0.5) < 0.0000001) return 0.0;

    double shape = (1.0 - (2.0 * r)) / (r * r);
    if (shape < 0.0) return 0.0;
    return shape;
}

// From Thetis ucParametricEq.cs:3012-3033 [v2.10.3.13].
double ParametricEqWidget::getNormalizedFrequencyPosition(
    double frequencyHz, double minHz, double maxHz, bool useLog) const {
    double span = maxHz - minHz;
    if (span <= 0.0) return 0.0;

    double f = clamp(frequencyHz, minHz, maxHz);
    double u = (f - minHz) / span;

    if (!useLog) {
        return u;
    }

    double centreRatio = (getLogFrequencyCentreHz(minHz, maxHz) - minHz) / span;
    double shape = getLogFrequencyShape(centreRatio);
    if (shape <= 0.0) {
        return u;
    }

    return std::log(1.0 + (shape * u)) / std::log(1.0 + shape);
}

// From Thetis ucParametricEq.cs:3002-3005 [v2.10.3.13].
double ParametricEqWidget::getNormalizedFrequencyPosition(double frequencyHz) const {
    return getNormalizedFrequencyPosition(frequencyHz, m_frequencyMinHz, m_frequencyMaxHz, m_logScale);
}

// From Thetis ucParametricEq.cs:3045-3067 [v2.10.3.13].
double ParametricEqWidget::frequencyFromNormalizedPosition(
    double t, double minHz, double maxHz, bool useLog) const {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    double span = maxHz - minHz;
    if (span <= 0.0) return minHz;

    if (!useLog) {
        return minHz + (t * span);
    }

    double centreRatio = (getLogFrequencyCentreHz(minHz, maxHz) - minHz) / span;
    double shape = getLogFrequencyShape(centreRatio);
    if (shape <= 0.0) {
        return minHz + (t * span);
    }

    double u = (std::exp(t * std::log(1.0 + shape)) - 1.0) / shape;
    return minHz + (u * span);
}

// From Thetis ucParametricEq.cs:3035-3038 [v2.10.3.13].
double ParametricEqWidget::frequencyFromNormalizedPosition(double t) const {
    return frequencyFromNormalizedPosition(t, m_frequencyMinHz, m_frequencyMaxHz, m_logScale);
}

// From Thetis ucParametricEq.cs:2951-2955 [v2.10.3.13].
float ParametricEqWidget::xFromFreq(const QRect& plot, double frequencyHz) const {
    double t = getNormalizedFrequencyPosition(frequencyHz);
    return float(plot.left()) + float(t * plot.width());
}

// From Thetis ucParametricEq.cs:2957-2963 [v2.10.3.13].
double ParametricEqWidget::freqFromX(const QRect& plot, int x) const {
    double t = (double(x) - double(plot.left())) / double(plot.width());
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return frequencyFromNormalizedPosition(t);
}

// From Thetis ucParametricEq.cs:2965-2971 [v2.10.3.13].
float ParametricEqWidget::yFromDb(const QRect& plot, double db) const {
    double span = m_dbMax - m_dbMin;
    if (span <= 0.0) span = 1.0;
    double t = (db - m_dbMin) / span;
    return float(plot.bottom()) - float(t * plot.height());
}

// From Thetis ucParametricEq.cs:2973-2981 [v2.10.3.13].
double ParametricEqWidget::dbFromY(const QRect& plot, int y) const {
    double span = m_dbMax - m_dbMin;
    if (span <= 0.0) span = 1.0;
    double t = (double(plot.bottom()) - double(y)) / double(plot.height());
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return m_dbMin + t * span;
}

// From Thetis ucParametricEq.cs:2042-2059 [v2.10.3.13].
QRect ParametricEqWidget::computePlotRect() const {
    QRect r = rect();
    int left   = computedPlotMarginLeft();
    int right  = computedPlotMarginRight();
    int bottom = computedPlotMarginBottom();
    int x = r.x() + left;
    int y = r.y() + m_plotMarginTop;
    int w = r.width()  - left - right;
    int h = r.height() - m_plotMarginTop - bottom;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return QRect(x, y, w, h);
}

// From Thetis ucParametricEq.cs:2002-2013 [v2.10.3.13].
// formatDbTick (cs:2601-2614) is the C# label-builder we'll port in Batch 3.
// For axisLabelMaxWidth we only need a *width upper bound*, so use a
// conservative one-decimal "%.1f" string of m_dbMin/m_dbMax -- which is
// >= every real "0", "+24", "-12.5" rendering, and also avoids pulling
// formatDbTick forward into this batch.
int ParametricEqWidget::axisLabelMaxWidth() const {
    QFontMetrics fm(font());
    QString s1 = QStringLiteral("%1").arg(m_dbMin, 0, 'f', 1);
    QString s2 = QStringLiteral("%1").arg(m_dbMax, 0, 'f', 1);
    return std::max(fm.horizontalAdvance(s1), fm.horizontalAdvance(s2));
}

// From Thetis ucParametricEq.cs:2015-2028 [v2.10.3.13].
int ParametricEqWidget::computedPlotMarginLeft() const {
    int m = m_plotMarginLeft;
    if (m_showAxisScales) {
        int need = m_axisTickLength + 4 + axisLabelMaxWidth() + 8;
        if (need > m) m = need;
    }
    if (m < 10) m = 10;
    return m;
}

// From Thetis ucParametricEq.cs:2030-2040 [v2.10.3.13].
int ParametricEqWidget::computedPlotMarginRight() const {
    int m = m_plotMarginRight;
    int gainNeed = m_globalHandleXOffset + (m_globalHandleSize * 2) + m_globalHitExtra + 6;
    if (gainNeed > m) m = gainNeed;
    if (m < 10) m = 10;
    return m;
}

// From Thetis ucParametricEq.cs:3365-3382 [v2.10.3.13].
int ParametricEqWidget::computedPlotMarginBottom() const {
    if (m_showReadout) return m_plotMarginBottom;
    int m = 8;
    if (m_showAxisScales) {
        QFontMetrics fm(font());
        m += m_axisTickLength + 2 + fm.height() + 4;
    } else {
        m += 8;
    }
    if (m < 10) m = 10;
    return m;
}

// From Thetis ucParametricEq.cs:2910-2934 [v2.10.3.13].
int ParametricEqWidget::hitTestPoint(const QRect& plot, QPoint pt) const {
    int best = -1;
    double bestD2 = std::numeric_limits<double>::max();

    for (int i = 0; i < m_points.size(); ++i) {
        const auto& p = m_points.at(i);
        float x = xFromFreq(plot, p.frequencyHz);
        float y = yFromDb(plot, p.gainDb);
        double dx = double(pt.x()) - double(x);
        double dy = double(pt.y()) - double(y);
        double d2 = dx * dx + dy * dy;
        double r = double(m_hitRadius);
        if (d2 <= r * r && d2 < bestD2) {
            bestD2 = d2;
            best = i;
        }
    }
    return best;
}

// From Thetis ucParametricEq.cs:2936-2949 [v2.10.3.13].
bool ParametricEqWidget::hitTestGlobalGainHandle(const QRect& plot, QPoint pt) const {
    float y = yFromDb(plot, m_globalGainDb);
    int hx = plot.right() + m_globalHandleXOffset;
    int s  = m_globalHandleSize;
    QRect r(hx - m_globalHitExtra,
            int(std::round(y)) - (s + m_globalHitExtra),
            (s + m_globalHitExtra) * 2,
            (s + m_globalHitExtra) * 2);
    return r.contains(pt);
}

// From Thetis ucParametricEq.cs:3384-3387 [v2.10.3.13].
bool ParametricEqWidget::isFrequencyLockedIndex(int index) const {
    return !m_points.isEmpty()
        && (index == 0 || index == m_points.size() - 1);
}

// From Thetis ucParametricEq.cs:3389-3394 [v2.10.3.13].
double ParametricEqWidget::getLockedFrequencyForIndex(int index) const {
    if (index <= 0) return m_frequencyMinHz;
    if (index >= m_points.size() - 1) return m_frequencyMaxHz;
    return m_points.at(index).frequencyHz;
}

// From Thetis ucParametricEq.cs:1152-1160 [v2.10.3.13] -- findPointByBandId.
ParametricEqWidget::EqPoint* ParametricEqWidget::findPointByBandId(int bandId) {
    for (auto& p : m_points) {
        if (p.bandId == bandId) return &p;
    }
    return nullptr;
}

// From Thetis ucParametricEq.cs:1142-1150 [v2.10.3.13] -- GetIndexFromBandId.
int ParametricEqWidget::indexFromBandId(int bandId) const {
    for (int i = 0; i < m_points.size(); ++i) {
        if (m_points.at(i).bandId == bandId) return i;
    }
    return -1;
}

// From Thetis ucParametricEq.cs:3314-3323 [v2.10.3.13].
void ParametricEqWidget::clampAllGains() {
    for (auto& p : m_points) {
        p.gainDb = clamp(p.gainDb, m_dbMin, m_dbMax);
    }
    m_globalGainDb = clamp(m_globalGainDb, m_dbMin, m_dbMax);
}

// From Thetis ucParametricEq.cs:3325-3332 [v2.10.3.13].
void ParametricEqWidget::clampAllQ() {
    for (auto& p : m_points) {
        p.q = clamp(p.q, m_qMin, m_qMax);
    }
}

// From Thetis ucParametricEq.cs:3223-3312 [v2.10.3.13].
// Subtle bit: after the sort, the previously-selected and previously-dragged
// points may have shifted index.  We capture each point's bandId BEFORE the
// sort, then re-resolve by bandId AFTER, so the user's selection stays
// pinned to the same band rather than the old slot.  C# does this by
// holding object references (List<T>.IndexOf); we use bandId because
// EqPoint is a value type in C++.
void ParametricEqWidget::enforceOrdering(bool enforceSpacingAll) {
    if (m_points.isEmpty()) return;

    int oldSelected = m_selectedIndex;
    int oldDrag     = m_dragIndex;
    int selectedBandId = (oldSelected >= 0 && oldSelected < m_points.size())
                        ? m_points.at(oldSelected).bandId : -1;
    int dragBandId     = (oldDrag >= 0 && oldDrag < m_points.size())
                        ? m_points.at(oldDrag).bandId : -1;

    if (m_allowPointReorder && m_points.size() > 1) {
        std::stable_sort(m_points.begin(), m_points.end(),
            [](const EqPoint& a, const EqPoint& b) {
                if (a.frequencyHz != b.frequencyHz) return a.frequencyHz < b.frequencyHz;
                return a.bandId < b.bandId;
            });
    }

    // Re-resolve indices by bandId after sort.
    m_selectedIndex = (selectedBandId >= 0) ? indexFromBandId(selectedBandId) : -1;
    m_dragIndex     = (dragBandId >= 0)     ? indexFromBandId(dragBandId)     : -1;

    for (auto& p : m_points) {
        p.frequencyHz = clamp(p.frequencyHz, m_frequencyMinHz, m_frequencyMaxHz);
    }
    if (m_points.size() > 0) m_points.front().frequencyHz = m_frequencyMinHz;
    if (m_points.size() > 1) m_points.back().frequencyHz  = m_frequencyMaxHz;

    if (!enforceSpacingAll) return;
    if (m_points.size() < 3) return;

    double spacing    = m_minPointSpacingHz;
    double maxSpacing = (m_frequencyMaxHz - m_frequencyMinHz) / double(m_points.size() - 1);
    if (spacing > maxSpacing) spacing = maxSpacing;
    if (spacing < 0.0)        spacing = 0.0;

    for (int i = 1; i < m_points.size() - 1; ++i) {
        double minF = m_frequencyMinHz + (spacing * i);
        double maxF = m_frequencyMaxHz - (spacing * (m_points.size() - 1 - i));
        if (maxF < minF) maxF = minF;
        m_points[i].frequencyHz = clamp(m_points[i].frequencyHz, minF, maxF);
    }
    for (int i = 1; i < m_points.size() - 1; ++i) {
        double wantMin = m_points[i - 1].frequencyHz + spacing;
        if (m_points[i].frequencyHz < wantMin) m_points[i].frequencyHz = wantMin;
    }
    for (int i = m_points.size() - 2; i >= 1; --i) {
        double wantMax = m_points[i + 1].frequencyHz - spacing;
        if (m_points[i].frequencyHz > wantMax) m_points[i].frequencyHz = wantMax;
    }
    m_points.front().frequencyHz = m_frequencyMinHz;
    m_points.back().frequencyHz  = m_frequencyMaxHz;
}

// From Thetis ucParametricEq.cs:3163-3197 [v2.10.3.13].
// Note: C# calls getBandBaseColor(i) (cs:2864-2871) which is just
// _default_band_palette[i % palette.Length]; we inline that here since
// Batch 1 already exposed the palette via defaultBandPalette().
void ParametricEqWidget::resetPointsDefault() {
    m_points.clear();
    int count = m_bandCount;
    if (count < 2) count = 2;

    m_selectedIndex      = -1;
    m_dragIndex          = -1;
    m_draggingPoint      = false;
    m_draggingGlobalGain = false;
    m_dragPointRef           = nullptr;
    m_dragDirtyPoint         = false;
    m_dragDirtyGlobalGain    = false;
    m_dragDirtySelectedIndex = false;

    double span = m_frequencyMaxHz - m_frequencyMinHz;
    if (span <= 0.0) span = 1.0;

    const auto& palette = defaultBandPalette();
    for (int i = 0; i < count; ++i) {
        double t = double(i) / double(count - 1);
        double f = m_frequencyMinHz + t * span;
        QColor col = palette.at(i % palette.size());
        m_points.append(EqPoint(i + 1, col, f, 0.0, 4.0));
    }

    enforceOrdering(true);
    clampAllGains();
    clampAllQ();
}

// From Thetis ucParametricEq.cs:3199-3221 [v2.10.3.13].
void ParametricEqWidget::rescaleFrequencies(
    double oldMin, double oldMax, double newMin, double newMax) {
    double oldSpan = oldMax - oldMin;
    double newSpan = newMax - newMin;
    if (oldSpan <= 0.0) oldSpan = 1.0;
    if (newSpan <= 0.0) newSpan = 1.0;

    for (auto& p : m_points) {
        double t = (p.frequencyHz - oldMin) / oldSpan;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        p.frequencyHz = newMin + t * newSpan;
    }
    if (m_points.size() > 0) m_points.front().frequencyHz = m_frequencyMinHz;
    if (m_points.size() > 1) m_points.back().frequencyHz  = m_frequencyMaxHz;
}

// From Thetis ucParametricEq.cs:3080-3161 [v2.10.3.13].
QVector<double> ParametricEqWidget::getLogFrequencyTicks(const QRect& plot) const {
    auto add = [this](QVector<double>& ticks, double f) {
        if (std::isnan(f) || std::isinf(f)) return;
        if (f < m_frequencyMinHz || f > m_frequencyMaxHz) return;
        ticks.append(f);
    };

    QVector<double> candidates;
    add(candidates, m_frequencyMinHz);
    add(candidates, getLogFrequencyCentreHz(m_frequencyMinHz, m_frequencyMaxHz));
    add(candidates, m_frequencyMaxHz);

    if (m_frequencyMinHz <= 0.0 && m_frequencyMaxHz >= 0.0) {
        add(candidates, 0.0);
    }

    double positiveMin = m_frequencyMinHz > 0.0 ? m_frequencyMinHz : 1.0;
    double positiveMax = m_frequencyMaxHz;
    if (positiveMax > 0.0) {
        int expMin = int(std::floor(std::log10(positiveMin)));
        int expMax = int(std::ceil (std::log10(positiveMax)));
        const double mults[] = {1.0, 2.0, 5.0};
        for (int e = expMin; e <= expMax; ++e) {
            double decade = std::pow(10.0, e);
            for (double m : mults) {
                add(candidates, m * decade);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());

    QVector<double> uniq;
    for (double f : candidates) {
        if (!uniq.isEmpty() && qAbs(uniq.last() - f) < 0.000001) continue;
        uniq.append(f);
    }
    if (uniq.size() <= 2) return uniq;

    QVector<double> filtered;
    constexpr double kMinSpacingPx = 28.0;
    for (int i = 0; i < uniq.size(); ++i) {
        double f = uniq.at(i);
        bool keep = (i == 0) || (i == uniq.size() - 1);
        if (!keep) {
            float x = xFromFreq(plot, f);
            bool farEnough = true;
            for (double k : filtered) {
                float kx = xFromFreq(plot, k);
                if (qAbs(x - kx) < kMinSpacingPx) {
                    farEnough = false;
                    break;
                }
            }
            keep = farEnough;
        }
        if (keep) filtered.append(f);
    }
    return filtered;
}

} // namespace NereusSDR
