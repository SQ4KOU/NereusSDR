// =================================================================
// src/gui/AmpViewWindow.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/AmpView.cs
//   Project Files/Source/Console/AmpView.Designer.cs
// original licences from Thetis source are included below.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Phase 3M-4 Task 9: created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic Claude
//                 Code.  Source-first 1:1 port of the Thetis modeless
//                 AmpView dialog (AmpView.cs + AmpView.Designer.cs)
//                 [v2.10.3.13].  Title bar reads "AmpView 1.0"
//                 verbatim.  ClientSize 564x401, MinimumSize 440x380.
//                 5 named chart series (Ref / MagAmp / PhsAmp /
//                 MagCorr / PhsCorr) feed from PureSignal::
//                 getDispBuffers (forwards TxChannel::getPSDisp →
//                 calcc.c:1058 [v2.10.3.13]).  4 toolbar checkboxes
//                 at exact Thetis x positions (chkAVShowGain @ 7,378
//                 / chkAVPhaseZoom @ 242,378 / chkAVLowRes @ 404,378
//                 / chkStayOnTop @ 490,378).  Render uses NereusSDR-
//                 native AmpViewChart custom QPainter widget instead
//                 of the upstream System.Windows.Forms.DataVisuali-
//                 zation chart (no QtCharts dependency added — design
//                 decision per phase3m-4-puresignal-design.md §15 #14).
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

#pragma once

#include <QDialog>

class QCheckBox;
class QCloseEvent;
class QShowEvent;
class QTimer;
class QWidget;

namespace NereusSDR {

class AmpViewChart;
class PureSignal;
class RadioModel;

// AmpViewWindow — modeless Qt6 dialog opened from PsForm m_btnAmpView.
//
// Inline-tag preservation (CLAUDE.md §"Inline comment preservation"):
//   AmpView.cs:89   // [2.10.3.5]MW0LGE  #292   (Common.RestoreForm tweak)
//   AmpView.cs:122  // MW0LGE [2.9.0.8]  re-factored to use fixed set of
//                      chart points, which get adjusted
//   AmpView.cs:259  // MW0LGE [2.9.0.8]  kept for code record  (dead path
//                      retained as upstream reference)
//   AmpView.cs:397  // MW0LGE [2.9.0.8]  changed to an add once, update
//                      points method  (refactor inside timer1_Tick)
// All four refactor MW0LGE tags concern the same chart-points-init pattern;
// NereusSDR follows the post-refactor structure verbatim.
//
// Layout (mirrors Thetis AmpView.Designer.cs:209-229 [v2.10.3.13]):
//
//   ┌──────────────────────────────────────────────────────────────────┐
//   │                                                                  │
//   │             AmpViewChart (QPainter custom widget)                │
//   │           5 series: Ref / MagAmp / PhsAmp / MagCorr / PhsCorr    │
//   │                                                                  │
//   │                                                                  │
//   │                                                                  │
//   │                                                                  │
//   ├──────────────────────────────────────────────────────────────────┤
//   │ ☐ Show Gain      ☐ Phase Zoom      ☑ Low Res      ☐ On Top      │
//   └──────────────────────────────────────────────────────────────────┘
//   ↑ chkAVShowGain   ↑ chkAVPhaseZoom   ↑ chkAVLowRes   ↑ chkStayOnTop
//   x=7,y=378         x=242,y=378        x=404,y=378     x=490,y=378
//
// Lifecycle: lazily constructed by PsForm on first AmpView click; persists
// across show/hide cycles.  PsForm calls setStayOnTopFromParent when its own
// Always-On-Top toggle changes, so this dialog tracks the parent.
class AmpViewWindow : public QDialog {
    Q_OBJECT

public:
    // PureSignal pointer is optional (test seam — pass nullptr to construct
    // without wiring the data feed).  When non-null, the poll timer pulls
    // GetPSDisp buffers via PureSignal and pushes them to the chart.
    explicit AmpViewWindow(RadioModel* radioModel = nullptr,
                           PureSignal* pureSignal = nullptr,
                           QWidget* parent = nullptr);
    ~AmpViewWindow() override;

    // Mirror of PsForm Always-On-Top → AmpView (Thetis FixOnTop pattern,
    // AmpView.cs:501-519 [v2.10.3.13]).  Called by PsForm when its own
    // chkPSOnTop toggles.
    void setStayOnTopFromParent(bool on);

protected:
    // Persist geometry on close; hide instead of destroying so the lazy
    // singleton survives subsequent open clicks (matches the TxEqDialog +
    // PsForm hide-on-close pattern).
    void closeEvent(QCloseEvent* event) override;

    // Restore Always-On-Top window flag on first show.  AmpView.cs:521-525
    // OnShown [v2.10.3.13] calls FixOnTop().
    void showEvent(QShowEvent* event) override;

private slots:
    // From AmpView.cs:435-455 chkAVShowGain_CheckedChanged [v2.10.3.13]:
    // toggles chart magnitude axis between [0,1.0] (Magnitude) and [0,2.0]
    // (Gain).  Persisted to AppSettings under "ampview/showGain".
    void onShowGainToggled(bool on);

    // From AmpView.cs:470-482 chkAVPhaseZoom_CheckedChanged [v2.10.3.13]:
    // toggles secondary phase axis between [-180, +180] and [-45, +45].
    // Persisted under "ampview/phaseZoom".
    void onPhaseZoomToggled(bool on);

    // From AmpView.cs:457-463 chkAVLowRes_CheckedChanged [v2.10.3.13]:
    // toggles render-stride between 1 and 4.  Default Checked.
    // Persisted under "ampview/lowRes".
    void onLowResToggled(bool on);

    // From AmpView.cs:329-333 chkStayOnTop_CheckedChanged [v2.10.3.13]:
    // calls FixOnTop() which sets Qt::WindowStaysOnTopHint.
    // Persisted under "ampview/onTop".
    void onStayOnTopToggled(bool on);

    // Poll PureSignal for the latest GetPSDisp buffers and push to the chart.
    // Matches the AmpView.cs:355-433 timer1_Tick pattern [v2.10.3.13].
    void pollChartUpdate();

private:
    void buildUi();
    void restoreToggleStates();
    void persistGeometry() const;

    // Non-owning model handles (may be null in tests).
    RadioModel* m_radioModel{nullptr};
    PureSignal* m_pureSignal{nullptr};

    // UI.
    AmpViewChart* m_chart{nullptr};
    QCheckBox*    m_chkShowGain{nullptr};
    QCheckBox*    m_chkPhaseZoom{nullptr};
    QCheckBox*    m_chkLowRes{nullptr};
    QCheckBox*    m_chkStayOnTop{nullptr};

    // 100 ms QTimer mirroring AmpView.cs timer1 [v2.10.3.13].
    QTimer* m_pollTimer{nullptr};
};

} // namespace NereusSDR
