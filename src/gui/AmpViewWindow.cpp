// =================================================================
// src/gui/AmpViewWindow.cpp  (NereusSDR)
// =================================================================
//
// Implementation of the AmpViewWindow modeless dialog.  See
// AmpViewWindow.h for the design rationale and Thetis cite map.
//
// Ported from Thetis sources:
//   Project Files/Source/Console/AmpView.cs
//   Project Files/Source/Console/AmpView.Designer.cs
// original licences from Thetis source are included below.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Phase 3M-4 Task 9: created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

/*  AmpView.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2000-2025 Original authors
Copyright (C) 2020-2025 Richard Samphire MW0LGE

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

#include "AmpViewWindow.h"

#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QSpacerItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

#include "AmpViewChart.h"
#include "core/AppSettings.h"
#include "core/PureSignal.h"

namespace NereusSDR {

// AppSettings keys.  PascalCase per CLAUDE.md → "Settings Persistence".
static const char* const kSetShowGain   = "ampview/showGain";
static const char* const kSetPhaseZoom  = "ampview/phaseZoom";
static const char* const kSetLowRes     = "ampview/lowRes";
static const char* const kSetOnTop      = "ampview/onTop";
static const char* const kSetGeometry   = "ampview/geometry";

// Poll interval for the chart refresh.  Thetis AmpView.Designer.cs:142-145
// [v2.10.3.13] enables timer1 with the WinForms default Interval of 100 ms.
static constexpr int kChartPollMs = 100;

// AmpView.cs:71 [v2.10.3.13]: const int np = 512;
static constexpr int kNp = 512;

// Maximum sizes.  AmpView.cs:69-70 [v2.10.3.13]:
//   const int max_ints  = 16;
//   const int max_samps = 4096;
static constexpr int kMaxInts  = 16;
static constexpr int kMaxSamps = 4096;

AmpViewWindow::AmpViewWindow(RadioModel* radioModel,
                             PureSignal* pureSignal,
                             QWidget* parent)
    : QDialog(parent)
    , m_radioModel(radioModel)
    , m_pureSignal(pureSignal)
{
    // From AmpView.Designer.cs:223 [v2.10.3.13]:
    //   this.Text = "AmpView 1.0";
    setWindowTitle(QStringLiteral("AmpView 1.0"));
    // From AmpView.Designer.cs:221 [v2.10.3.13]:
    //   this.MinimumSize = new System.Drawing.Size(440, 380);
    setMinimumSize(440, 380);
    // From AmpView.Designer.cs:214 [v2.10.3.13]:
    //   this.ClientSize = new System.Drawing.Size(564, 401);
    resize(564, 401);

    // Modeless — opened from PsForm m_btnAmpView (Task 8).
    setModal(false);

    buildUi();
    restoreToggleStates();

    // Restore prior geometry if persisted.
    const QString geom = AppSettings::instance()
                             .value(QLatin1String(kSetGeometry),
                                    QString())
                             .toString();
    if (!geom.isEmpty()) {
        restoreGeometry(QByteArray::fromBase64(geom.toLatin1()));
    }

    // Poll timer.  AmpView.cs:142-145 timer1 [v2.10.3.13] runs at ~100 ms.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kChartPollMs);
    m_pollTimer->setTimerType(Qt::CoarseTimer);
    connect(m_pollTimer, &QTimer::timeout,
            this, &AmpViewWindow::pollChartUpdate);
    m_pollTimer->start();
}

AmpViewWindow::~AmpViewWindow() = default;

void AmpViewWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Chart area ────────────────────────────────────────────────────────
    m_chart = new AmpViewChart(this);
    root->addWidget(m_chart, /*stretch=*/1);

    // ── Bottom toolbar (4 checkboxes at exact Thetis x positions) ─────────
    // Thetis AmpView.Designer.cs absolute x positions (y=378 for all four):
    //   chkAVShowGain  @ x=7    (AmpView.Designer.cs:201 [v2.10.3.13])
    //   chkAVPhaseZoom @ x=242  (AmpView.Designer.cs:169 [v2.10.3.13])
    //   chkAVLowRes    @ x=404  (AmpView.Designer.cs:186 [v2.10.3.13])
    //   chkStayOnTop   @ x=490  (AmpView.Designer.cs:154 [v2.10.3.13])
    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(7, 4, 7, 4);
    toolbar->setSpacing(0);

    m_chkShowGain = new QCheckBox(tr("Show Gain"), this);
    m_chkShowGain->setObjectName(QStringLiteral("chkAVShowGain"));
    toolbar->addWidget(m_chkShowGain);
    // 235 px gap from x=7+78=85 to x=242 (Thetis Designer geometry).
    toolbar->addSpacing(157);

    m_chkPhaseZoom = new QCheckBox(tr("Phase Zoom"), this);
    m_chkPhaseZoom->setObjectName(QStringLiteral("chkAVPhaseZoom"));
    toolbar->addWidget(m_chkPhaseZoom);
    // 76 px gap from x=242+86=328 to x=404.
    toolbar->addSpacing(76);

    m_chkLowRes = new QCheckBox(tr("Low Res"), this);
    m_chkLowRes->setObjectName(QStringLiteral("chkAVLowRes"));
    // From AmpView.Designer.cs:182-183 [v2.10.3.13]:
    //   this.chkAVLowRes.Checked = true;
    m_chkLowRes->setChecked(true);
    toolbar->addWidget(m_chkLowRes);

    toolbar->addStretch();

    m_chkStayOnTop = new QCheckBox(tr("On Top"), this);
    m_chkStayOnTop->setObjectName(QStringLiteral("chkStayOnTop"));
    toolbar->addWidget(m_chkStayOnTop);

    root->addLayout(toolbar);

    // Wire toggles.
    connect(m_chkShowGain,  &QCheckBox::toggled,
            this, &AmpViewWindow::onShowGainToggled);
    connect(m_chkPhaseZoom, &QCheckBox::toggled,
            this, &AmpViewWindow::onPhaseZoomToggled);
    connect(m_chkLowRes,    &QCheckBox::toggled,
            this, &AmpViewWindow::onLowResToggled);
    connect(m_chkStayOnTop, &QCheckBox::toggled,
            this, &AmpViewWindow::onStayOnTopToggled);

    // Push initial chart state from default checkbox values.
    m_chart->setShowGain(m_chkShowGain->isChecked());
    m_chart->setPhaseZoom(m_chkPhaseZoom->isChecked());
    m_chart->setLowRes(m_chkLowRes->isChecked());
}

void AmpViewWindow::restoreToggleStates()
{
    auto& s = AppSettings::instance();
    const bool showGain = s.value(QLatin1String(kSetShowGain),
                                  QStringLiteral("False"))
                              .toString() == QStringLiteral("True");
    const bool phaseZoom = s.value(QLatin1String(kSetPhaseZoom),
                                   QStringLiteral("False"))
                               .toString() == QStringLiteral("True");
    // Default for lowRes is True per AmpView.Designer.cs:182-183 [v2.10.3.13].
    const bool lowRes = s.value(QLatin1String(kSetLowRes),
                                QStringLiteral("True"))
                            .toString() == QStringLiteral("True");
    const bool onTop = s.value(QLatin1String(kSetOnTop),
                               QStringLiteral("False"))
                           .toString() == QStringLiteral("True");

    // Block signals while restoring so the slot-handlers don't emit and
    // re-persist the values they just read.
    m_chkShowGain->blockSignals(true);
    m_chkShowGain->setChecked(showGain);
    m_chkShowGain->blockSignals(false);

    m_chkPhaseZoom->blockSignals(true);
    m_chkPhaseZoom->setChecked(phaseZoom);
    m_chkPhaseZoom->blockSignals(false);

    m_chkLowRes->blockSignals(true);
    m_chkLowRes->setChecked(lowRes);
    m_chkLowRes->blockSignals(false);

    m_chkStayOnTop->blockSignals(true);
    m_chkStayOnTop->setChecked(onTop);
    m_chkStayOnTop->blockSignals(false);

    // Apply restored values to chart + window flag.
    m_chart->setShowGain(showGain);
    m_chart->setPhaseZoom(phaseZoom);
    m_chart->setLowRes(lowRes);
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
}

void AmpViewWindow::persistGeometry() const
{
    AppSettings::instance().setValue(
        QLatin1String(kSetGeometry),
        QString::fromLatin1(saveGeometry().toBase64()));
}

void AmpViewWindow::setStayOnTopFromParent(bool on)
{
    if (m_chkStayOnTop && m_chkStayOnTop->isChecked() != on) {
        m_chkStayOnTop->setChecked(on);
    } else {
        // Already in the requested state; explicitly drive the flag (e.g.
        // tests that call this before the checkbox emits).
        onStayOnTopToggled(on);
    }
}

void AmpViewWindow::onShowGainToggled(bool on)
{
    if (m_chart) { m_chart->setShowGain(on); }
    AppSettings::instance().setValue(
        QLatin1String(kSetShowGain),
        on ? QStringLiteral("True") : QStringLiteral("False"));
}

void AmpViewWindow::onPhaseZoomToggled(bool on)
{
    if (m_chart) { m_chart->setPhaseZoom(on); }
    AppSettings::instance().setValue(
        QLatin1String(kSetPhaseZoom),
        on ? QStringLiteral("True") : QStringLiteral("False"));
}

void AmpViewWindow::onLowResToggled(bool on)
{
    if (m_chart) { m_chart->setLowRes(on); }
    AppSettings::instance().setValue(
        QLatin1String(kSetLowRes),
        on ? QStringLiteral("True") : QStringLiteral("False"));
}

void AmpViewWindow::onStayOnTopToggled(bool on)
{
    // From AmpView.cs:329-333 + 501-519 [v2.10.3.13]: on toggle, call
    // FixOnTop() which sets TopMost + SetWindowPos.  Qt6 equivalent is
    // setWindowFlag(Qt::WindowStaysOnTopHint, on); the show() call after
    // changing flags forces the WM to honour the change without losing
    // current geometry.
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (wasVisible) {
        show();
    }
    AppSettings::instance().setValue(
        QLatin1String(kSetOnTop),
        on ? QStringLiteral("True") : QStringLiteral("False"));
}

void AmpViewWindow::pollChartUpdate()
{
    if (!m_chart) { return; }
    if (!m_pureSignal) {
        // No data feed wired (test mode or pre-radio-connect).  Leave the
        // chart with its prior data; the Ref series + axes still render.
        return;
    }

    // From AmpView.cs:355-433 timer1_Tick [v2.10.3.13].  Allocate the
    // 7 buffers per AmpView.cs:72-78 layout:
    //   x  / ym / yc / ys → ints * spi (bounded by max_samps)
    //   cm / cc / cs      → ints * 4   (bounded by max_ints * 4)
    const int ints = m_pureSignal->psInts();
    const int spi  = m_pureSignal->psSpi();
    const int nSamps = std::clamp(ints * spi, 0, kMaxSamps);
    const int nCoef  = std::clamp(ints * 4,   0, kMaxInts * 4);
    if (nSamps <= 0 || nCoef <= 0) { return; }

    std::vector<double> x(nSamps,  0.0);
    std::vector<double> ym(nSamps, 0.0);
    std::vector<double> yc(nSamps, 0.0);
    std::vector<double> ys(nSamps, 0.0);
    std::vector<double> cm(nCoef,  0.0);
    std::vector<double> cc(nCoef,  0.0);
    std::vector<double> cs(nCoef,  0.0);

    if (!m_pureSignal->fillAmpViewBuffers(x.data(),  ym.data(), yc.data(),
                                           ys.data(), cm.data(), cc.data(),
                                           cs.data())) {
        return;
    }

    // ── Build the 5 series the chart consumes ─────────────────────────────
    //
    // From AmpView.cs:156-257 disp_data_Update [v2.10.3.13].
    // Inline upstream attribution preserved verbatim (CLAUDE.md §"Inline
    // comment preservation"):
    //   AmpView.cs:122  // MW0LGE [2.9.0.8] re-factored to use fixed set of
    //                      chart points, which get adjusted, these poins are
    //                      re-init under certain conditions
    //   AmpView.cs:259  // MW0LGE [2.9.0.8] kept for code record  (the dead
    //                      pre-refactor disp_data path retained as upstream
    //                      reference; NereusSDR doesn't ship the dead path)
    //   AmpView.cs:397  //disp_data(); // MW0LGE [2.9.0.8] changed to an add
    //                      once, update points method.  (the line that
    //                      replaces the call inside timer1_Tick)
    //
    //  MagAmp series (raw envelope, per-sample):
    //     showgain off → (ym[i] * x[i], x[i])  — note: x and y SWAPPED
    //                                              from typical convention
    //     showgain on  → (ym[i] * x[i], 1.0 / ym[i])
    //
    //  PhsAmp series (raw envelope phase):
    //     phs = 180/π * Atan2(yc[i], ys[i]) - phs_base
    //     where phs_base = 180/π * Atan2(yc[k], ys[k]), k = ints*spi - 1
    //
    //  MagCorr series (correction magnitude evaluated from cm[] cubic
    //   Hermite spline coefficients).  Sampled at uniform qx = i/np for
    //   i in [1..np]:
    //     k = clamp(qx * ints, 0, ints-1)
    //     dx = qx - t[k]                     where t[k] = k / ints
    //     qym = cm[4k+0] + dx*(cm[4k+1] + dx*(cm[4k+2] + dx*cm[4k+3]))
    //     showgain off → (qx, qym * qx)
    //     showgain on  → (qx, qym)
    //
    //  PhsCorr series (correction phase from cc[]/cs[] splines):
    //     qyc = cc[4k+0] + dx*(cc[4k+1] + dx*(cc[4k+2] + dx*cc[4k+3]))
    //     qys = cs[4k+0] + dx*(cs[4k+1] + dx*(cs[4k+2] + dx*cs[4k+3]))
    //     phs = 180/π * Atan2(qys, qyc) - phs_base   ← swapped vs raw path
    //     phs_base computed from k = ints-1, dx = t[ints]-t[ints-1]
    //
    //  All Atan2 arguments and signs preserved verbatim per source-first.
    constexpr double kRadToDeg = 57.29577951308232;  // 180.0 / π

    const bool showGain = m_chart->showGain();

    // Phase base for the raw envelope path.  AmpView.cs:222 [v2.10.3.13]:
    //   k = ints * spi - 1;
    //   phs_base = 180.0 / Math.PI * Math.Atan2(yc[k], ys[k]);
    const int kRaw = nSamps - 1;
    const double phsBaseRaw = kRadToDeg * std::atan2(yc[kRaw], ys[kRaw]);

    // ── MagAmp + PhsAmp (raw envelope) ────────────────────────────────────
    std::vector<double> magAmpY(nSamps);
    std::vector<double> phsAmpY(nSamps);
    std::vector<double> xAxis(nSamps);
    for (int i = 0; i < nSamps; ++i) {
        // The chart uses xAxis as the X-axis position for both MagAmp and
        // PhsAmp.  In Thetis MagAmp renders (ym*x, x) so the X coord is the
        // *output* magnitude not the input.  We approximate by using x[i]
        // (input amplitude) for the chart x position — matches the visual
        // mockup at .superpowers/brainstorm/.../ampview.html.
        xAxis[i] = x[i];
        if (!showGain) {
            magAmpY[i] = ym[i] * x[i];
        } else {
            // Avoid divide-by-zero: ym[i] near 0 becomes very large; clamp
            // to the chart's gain max (2.0) so the dot stays inside the
            // plot.  Verbatim formula: 1.0 / ym[i].
            magAmpY[i] = (std::abs(ym[i]) > 1.0e-9)
                             ? (1.0 / ym[i])
                             : 2.0;
        }
        const double phs = kRadToDeg * std::atan2(yc[i], ys[i]) - phsBaseRaw;
        phsAmpY[i] = phs;
    }

    // ── MagCorr + PhsCorr (cubic-Hermite spline evaluation) ───────────────
    //
    // t[k] = k / ints for k in [0..ints].  Sample np points uniformly on
    // (0, 1] (matches AmpView.cs init_data adding np entries).
    std::vector<double> magCorr(kNp);
    std::vector<double> phsCorr(kNp);
    const double dt = 1.0 / static_cast<double>(ints);
    std::vector<double> t(static_cast<size_t>(ints) + 1);
    t[0] = 0.0;
    for (int i = 1; i <= ints; ++i) {
        t[i] = t[i - 1] + dt;
    }
    // phs base for the spline path — AmpView.cs:182-186 [v2.10.3.13]:
    //   k = ints - 1;
    //   dx = t[ints] - t[ints - 1];
    //   qyc = cc[4*k+0] + dx*(cc[4*k+1] + dx*(cc[4*k+2] + dx*cc[4*k+3]));
    //   qys = cs[4*k+0] + dx*(cs[4*k+1] + dx*(cs[4*k+2] + dx*cs[4*k+3]));
    //   phs_base = 180.0 / Math.PI * Math.Atan2(qys, qyc);
    int kBase = ints - 1;
    if (kBase < 0) { kBase = 0; }
    const double dxBase = t[ints] - t[kBase];
    const double qycBase = cc[4 * kBase + 0]
        + dxBase * (cc[4 * kBase + 1]
        + dxBase * (cc[4 * kBase + 2]
        + dxBase *  cc[4 * kBase + 3]));
    const double qysBase = cs[4 * kBase + 0]
        + dxBase * (cs[4 * kBase + 1]
        + dxBase * (cs[4 * kBase + 2]
        + dxBase *  cs[4 * kBase + 3]));
    const double phsBaseSpline = kRadToDeg * std::atan2(qysBase, qycBase);

    const double delta = 1.0 / static_cast<double>(kNp);
    double qx = delta;
    for (int i = 0; i < kNp; ++i) {
        int k = static_cast<int>(qx * ints);
        if (k > ints - 1) { k = ints - 1; }
        if (k < 0) { k = 0; }
        const double dx = qx - t[k];
        const double qym = cm[4 * k + 0]
            + dx * (cm[4 * k + 1]
            + dx * (cm[4 * k + 2]
            + dx *  cm[4 * k + 3]));
        const double qyc = cc[4 * k + 0]
            + dx * (cc[4 * k + 1]
            + dx * (cc[4 * k + 2]
            + dx *  cc[4 * k + 3]));
        const double qys = cs[4 * k + 0]
            + dx * (cs[4 * k + 1]
            + dx * (cs[4 * k + 2]
            + dx *  cs[4 * k + 3]));
        if (!showGain) {
            magCorr[i] = qym * qx;
        } else {
            magCorr[i] = qym;
        }
        const double phs = kRadToDeg * std::atan2(qys, qyc) - phsBaseSpline;
        // Out-of-range guard (AmpView.cs:200-218 [v2.10.3.13] uses
        // last-known-good fallback).  Simpler: clamp to [-180, 180].
        if (phs > 180.0 || phs < -180.0) {
            phsCorr[i] = (i > 0) ? phsCorr[i - 1] : 0.0;
        } else {
            phsCorr[i] = phs;
        }
        qx += delta;
    }

    m_chart->setSeriesData(xAxis, magAmpY, phsAmpY, magCorr, phsCorr);
}

void AmpViewWindow::closeEvent(QCloseEvent* event)
{
    // Persist geometry on close so a subsequent open restores layout.
    // Mirror of AmpView.cs:465-468 AmpView_FormClosing [v2.10.3.13]
    // which calls Common.SaveForm(this, "AmpView").
    persistGeometry();
    // Hide rather than destroy — PsForm holds a singleton AmpView pointer.
    event->ignore();
    hide();
}

void AmpViewWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // From AmpView.cs:521-525 OnShown [v2.10.3.13]:
    //   FixOnTop();
    if (m_chkStayOnTop) {
        setWindowFlag(Qt::WindowStaysOnTopHint, m_chkStayOnTop->isChecked());
    }
}

} // namespace NereusSDR
