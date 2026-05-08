// =================================================================
// src/gui/applets/PureSignalApplet.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/PSForm.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-18 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//                 Layout ports Thetis PSForm.cs (PureSignal feedback/correction controls). All controls NYI — wired in later phase (3M-4).
//   2026-05-06 — Phase 3M-4 Task 13: replaced NyiOverlay::markNyi calls with
//                 live wiring to the PureSignal coordinator (Task 7).  Added
//                 right-click pattern (every control routes to
//                 openPureSignalDialogRequested signal; MainWindow connects
//                 it to openPureSignalDialog) per design doc §8.4.2.  Save /
//                 Restore use QFileDialog with default folder
//                 ~/.config/NereusSDR/PureSignal/.  J.J. Boyd (KG4VCF), with
//                 AI-assisted source-first protocol via Anthropic Claude Code.
// =================================================================

/*  PSForm.cs

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
#include "AppletWidget.h"
#include <QPointer>

class QPushButton;
class QLabel;

namespace NereusSDR {

class HGauge;

// PureSignal / PS-A feedback predistortion controls.
//
// Phase 3M-4 Task 13: live wiring to the PureSignal coordinator
// (src/core/PureSignal.h, Task 7).
//
// Controls:
//   1. Calibrate button      — QPushButton (non-toggle) → singleCalibrate
//   2. Auto-cal toggle       — QPushButton green "Auto" ↔ setAutoCalEnabled
//   3. Feedback level gauge  — HGauge (0-100, yellow@70, red@90, title "FB Level")
//                              ← feedbackLevelChanged 0..255 → 0..100
//   4. Correction mag gauge  — HGauge (0-100, yellow@80, red@95, title "Correction")
//                              ← correctionPeakChanged 0..1 → 0..100
//   5. Save coefficients     — QPushButton "Save"  → saveCorrections (file dialog)
//                              gated on correctionsBeingAppliedChanged
//   6. Restore coefficients  — QPushButton "Restore" → restoreCorrections (file dialog)
//   7. Two-tone test         — QPushButton green toggle "2-Tone" ↔ setTwoToneOn
//   8. Status LEDs           — 3x QLabel (24x14 rounded: "Cal", "Run", "Fbk")
//      Cal LED active during LSETUP/LCOLLECT/LCALC (driven by calStateChanged)
//      Run LED active during LSTAYON
//      Fbk LED active when feedback samples flow (feedbackActiveChanged)
//
// Plus: info readout labels (Iterations, Feedback dB, Correction dB) — driven
// by calibrationCountChanged + feedbackLevelChanged + correctionPeakChanged.
//
// Right-click on every control emits openPureSignalDialogRequested, which
// MainWindow routes to openPureSignalDialog (Tools → PureSignal…) per the
// Thetis right-click-to-associated-window pattern (cf. chkFWCATUBypass_MouseDown
// at console.cs:46149-46152 [v2.10.3.13]).
class PureSignal;

class PureSignalApplet : public AppletWidget {
    Q_OBJECT
public:
    explicit PureSignalApplet(RadioModel* model, QWidget* parent = nullptr);

    QString appletId()    const override { return QStringLiteral("pure_signal"); }
    QString appletTitle() const override { return QStringLiteral("PureSignal"); }
    void    syncFromModel() override;

public slots:
    // ── Phase 3M-4 Task 13: late-bound coordinator wiring ──────────────────
    //
    // PureSignal is constructed by RadioModel inside the WDSP-init lambda
    // AFTER MainWindow / applet construction.  The applet listens to
    // RadioModel::pureSignalCoordinatorReady to (re)wire its controls.
    // Tests call this slot directly with their own coordinator instance.
    //
    // Calling with nullptr disconnects the prior coordinator's bindings
    // (subscribers must safely tolerate later signal firings).
    void setPureSignal(PureSignal* coordinator);

signals:
    // ── Phase 3M-4 Task 13: right-click → open PsForm ──────────────────────
    // Mirrors the Thetis right-click-to-associated-window pattern (e.g.
    // chkFWCATUBypass_MouseDown at console.cs:46149-46152 [v2.10.3.13]).
    // MainWindow connects this signal to openPureSignalDialog (Tools →
    // PureSignal…); the same singleton dialog instance services every right-
    // click on every PureSignalApplet control.
    void openPureSignalDialogRequested();

private:
    void buildUI();
    void wireRightClicks();
    void wireCoordinator(PureSignal* ps);
    void setLedActive(QLabel* led, bool active);
    void setupRightClick(QWidget* widget);

    // Non-owning pointer to the live PureSignal coordinator.  Set by
    // setPureSignal() (test seam) or RadioModel::pureSignalCoordinatorReady.
    // Null until the WDSP-init lambda fires (production) or until the
    // test injects its own.
    //
    // PR #212 follow-up bench fix (J.J. KG4VCF, 2026-05-07): converted to
    // QPointer for stale-pointer safety on radio-disconnect teardown
    // (RadioModel destroys PureSignal then emits coordinatorReady(nullptr);
    // setPureSignal's `disconnect(m_ps, ...)` previously dereferenced freed
    // memory).  Same fix applied to TxApplet::m_ps.
    QPointer<PureSignal> m_ps;

    // Control 1 — calibrate button (non-toggle)
    QPushButton* m_calibrateBtn  = nullptr;
    // Control 2 — auto-cal toggle (green)
    QPushButton* m_autoCalBtn    = nullptr;

    // Control 3 — feedback level gauge (0-100, yellow@70, red@90)
    HGauge* m_feedbackGauge      = nullptr;
    // Control 4 — correction magnitude gauge (0-100, yellow@80, red@95)
    HGauge* m_correctionGauge    = nullptr;

    // Control 5 — save coefficients
    QPushButton* m_saveBtn       = nullptr;
    // Control 6 — restore coefficients
    QPushButton* m_restoreBtn    = nullptr;

    // Control 7 — two-tone test (green toggle)
    QPushButton* m_twoToneBtn    = nullptr;

    // Control 8 — status LEDs: "Cal", "Run", "Fbk"
    QLabel* m_led[3]             = {};

    // Info readout labels
    QLabel* m_iterations         = nullptr;
    QLabel* m_feedbackDb         = nullptr;
    QLabel* m_correctionDb       = nullptr;
};

} // namespace NereusSDR
