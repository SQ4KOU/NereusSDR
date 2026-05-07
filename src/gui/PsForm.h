// =================================================================
// src/gui/PsForm.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/PSForm.cs
//   Project Files/Source/Console/PSForm.designer.cs
// original licences from Thetis source are included below.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Phase 3M-4 Task 8: created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic Claude
//                 Code.  Source-first 1:1 port of the Thetis modeless
//                 PureSignal dialog (PSForm.cs + PSForm.designer.cs)
//                 [v2.10.3.13].  Title bar reads "PureSignal 2.0"
//                 verbatim.  ClientSize 560x300 default; Advanced
//                 toggle collapses to 560x60 top-strip mode per
//                 PSForm.cs:889-905 setAdvancedView.  All 23 designer
//                 controls bound to the PureSignal coordinator (Task 7)
//                 via Qt6 signals/slots.  Layout uses QGridLayout
//                 approximating Thetis x/y absolute positions (Thetis-
//                 userland-parity-with-our-spin per
//                 docs/architecture/feedback_thetis_userland_parity.md).
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

#include <QDialog>
#include <QVector>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

namespace NereusSDR {

class AmpViewWindow;
class RadioModel;
class PureSignal;

// PsForm modeless dialog — opens from `Tools > PureSignal...`.  Source-
// first port of Thetis PSForm.cs (1,164 LOC) [v2.10.3.13].
//
// Layout:
//
//   ┌────────────────────────────────────────────────────────────────┐
//   │ Top action row (always visible, even in Advanced collapsed)    │
//   │   [Two-tone] [Single Cal] [AmpView] [Advanced] [Save] [Restore] [OFF]
//   ├────────────────────────────────────────────────────────────────┤
//   │ ▣ FB Feedback Level   ▣ CO Correcting    ☐ Show 2Tone meas.    │
//   ├────────────────────────────────────────────────────────────────┤
//   │ MOX Wait (s) [_2.0_]      ☑ Auto-Attenuate                     │
//   │ CAL Wait (s) [_0.0_]      ☐ Relax Tolerance      ☑ PIN         │
//   │ AMP Delay (ns) [_150_]    ☐ Quick Attenuate Resp ☑ MAP         │
//   │                                                  ☐ STBL         │
//   │                                                  TINT [_0.5_]   │
//   ├────────────────────────────────────────────────────────────────┤
//   │ ☐ Display PS-RX and PS-TX spectra              ☐ Always On Top │
//   ├────────────────────────────────────────────────────────────────┤
//   │ ── Calibration Information (hidden in Advanced collapsed) ─────┤
//   │   bldr.rx  [_0_]    state    [______]                          │
//   │   bldr.cm  [_0_]    feedbk   [______]    GetPk [______]        │
//   │   bldr.cc  [_0_]    sln.chk  [______]    SetPk [______]        │
//   │   bldr.cs  [_0_]    dg.cnt   [______]                          │
//   │                     cor.cnt  [______]                          │
//   │   ☐ Display PS-RX/PS-TX                            [Default]   │
//   └────────────────────────────────────────────────────────────────┘
//
// Wiring: every control bound to PureSignal coordinator via
// signals/slots.  100ms QTimer mirrors PSForm.cs:555 timer1code polling
// (the coordinator class owns the polling loop; PsForm subscribes to its
// Q_PROPERTY signals to update label text + badge colours).
//
// Lifecycle: modeless dialog persists across show/hide cycles.  Owner
// (MainWindow) lazy-constructs on the first Tools > PureSignal... click.
// closeEvent() ignores and hides instead of destroying — matches the
// TxEqDialog pattern.
//
// Test seam: constructor accepts RadioModel + PureSignal pointers
// independently so tests can pass a real PureSignal coordinator with
// nullptr RadioModel (avoids needing a full MainWindow stand-up).
class PsForm : public QDialog {
    Q_OBJECT

public:
    // The radioModel is currently used only by Save/Restore (default
    // folder lookup) and AmpView lifecycle (Task 9).  PureSignal is the
    // coordinator (Task 7); all 23 controls bind to its setters/signals.
    explicit PsForm(RadioModel* radioModel,
                    PureSignal* pureSignal,
                    QWidget* parent = nullptr);
    ~PsForm() override;

    // Returns true when the dialog is currently in collapsed (Advanced)
    // mode (560x60 strip).  False when expanded (560x300 full form).
    // Mirrors Thetis _advancedON flag (PSForm.cs:888 [v2.10.3.13]).
    bool isAdvancedCollapsed() const noexcept { return m_advancedCollapsed; }

protected:
    // Hide-on-close per the TxEqDialog pattern.  PSForm.cs:418-422
    // PSForm_Closing [v2.10.3.13] also intercepts FormClosing and calls
    // e.Cancel = true plus Common.SaveForm; we save geometry on close.
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Action buttons (top row + Calibration Information group)
    void onSingleCalibrate();
    void onAdvancedClicked();
    void onSavePressed();
    void onRestorePressed();
    void onTwoToneToggled(bool checked);
    void onResetClicked();
    void onAmpViewClicked();
    void onDefaultPeaksClicked();

    // Toggles
    void onAlwaysOnTopToggled(bool on);
    void onPinToggled(bool on);
    void onMapToggled(bool on);
    void onStblToggled(bool on);
    void onAutoAttenuateToggled(bool on);
    void onRelaxPtolToggled(bool on);
    void onQuickAttenuateToggled(bool on);
    void onLoopbackToggled(bool on);
    void onShow2ToneMeasurementsToggled(bool on);

    // Spinboxes / combo
    void onMoxDelayChanged(double v);
    void onCalDelayChanged(double v);
    void onAmpDelayChanged(int v);
    void onTintIndexChanged(int idx);

    // PureSignal -> UI sync
    void onFeedbackLevelChanged(int level);
    // Codex Fix D: CO badge follows BOTH predicates per Thetis PSForm.cs:
    // 574-593 [v2.10.3.13] — Lime/Yellow/Black 3-state logic depends on
    // correctionsBeingApplied AND isCorrecting.  Slot reads both from the
    // PureSignal coordinator's getters; wired to both signals so a flip in
    // either predicate retriggers the colour computation.
    void refreshCoBadge();
    void onCalibrationCountChanged(int count);
    void onFeedbackColourChanged(const QColor& colour);
    // Codex Fix F: Save+Restore buttons gated by BOTH correctionsBeingApplied
    // (Thetis PSForm.cs:574-590 [v2.10.3.13]) AND saveRestoreEnabled (Thetis
    // PSForm.cs:865/871/877/883 [v2.10.3.13] — TINT combo index gating).
    // Slot reads both predicates from the coordinator and AND-combines them.
    void refreshSaveRestoreButtons();

private:
    void buildUi();
    QGroupBox* buildCalibrationInfoGroup(QWidget* parent);
    void wireToPureSignal();
    void syncFromPureSignal();
    void setAdvancedMode(bool collapsed);
    void persistAdvancedMode() const;
    void restoreAdvancedMode();

    RadioModel* m_radioModel{nullptr}; // non-owning; may be null in tests
    PureSignal* m_pureSignal{nullptr}; // non-owning; may be null in tests

    // Echo guard so syncFromPureSignal() doesn't bounce back through the
    // setters and create a feedback loop.  Mirrors AetherSDR/SliceModel
    // m_updatingFromModel pattern.
    bool m_updatingFromModel{false};

    // Top action row (always visible, even in collapsed/Advanced mode).
    // Object names match Thetis PSForm.designer.cs [v2.10.3.13] verbatim.
    QPushButton* m_btnTwoTone{nullptr};       // PSForm.designer.cs:270-283 btnPSTwoToneGen
    QPushButton* m_btnSingleCal{nullptr};     // PSForm.designer.cs:751-763 btnPSCalibrate
    QPushButton* m_btnAmpView{nullptr};       // PSForm.designer.cs:241-253 btnPSAmpView
    QPushButton* m_btnAdvanced{nullptr};      // PSForm.designer.cs:147-158 btnPSAdvanced
    QPushButton* m_btnSave{nullptr};          // PSForm.designer.cs:133-145 btnPSSave
    QPushButton* m_btnRestore{nullptr};       // PSForm.designer.cs:119-131 btnPSRestore
    QPushButton* m_btnReset{nullptr};         // PSForm.designer.cs:738-749 btnPSReset

    // Status row
    QLabel*    m_lblFb{nullptr};                   // PSForm.designer.cs:297-307 lblPSInfoFB (badge)
    QLabel*    m_lblCo{nullptr};                   // PSForm.designer.cs:309-319 lblPSInfoCO (badge)
    QCheckBox* m_chkShow2ToneMeasurements{nullptr};// PSForm.designer.cs:846-857 chkShow2ToneMeasurements
    QLabel*    m_lblWarningSetPk{nullptr};         // PSForm.designer.cs:835-844 pbWarningSetPk

    // Calibration option checkboxes
    QCheckBox* m_chkPin{nullptr};            // PSForm.designer.cs:207-222 chkPSPin
    QCheckBox* m_chkMap{nullptr};            // PSForm.designer.cs:190-205 chkPSMap
    QCheckBox* m_chkStbl{nullptr};           // PSForm.designer.cs:176-188 chkPSStbl
    QCheckBox* m_chkAutoAttenuate{nullptr};  // PSForm.designer.cs:224-239 chkPSAutoAttenuate
    QCheckBox* m_chkRelaxPtol{nullptr};      // PSForm.designer.cs:255-268 chkPSRelaxPtol
    QCheckBox* m_chkQuickAttenuate{nullptr}; // PSForm.designer.cs:808-820 chkQuickAttenuate

    // Timing / TINT
    QDoubleSpinBox* m_spinMoxDelay{nullptr}; // PSForm.designer.cs:344-373 udPSMoxDelay
    QDoubleSpinBox* m_spinCalDelay{nullptr}; // PSForm.designer.cs:776-806 udPSCalWait
    QSpinBox*       m_spinAmpDelay{nullptr}; // PSForm.designer.cs:386-414 udPSPhnum
    QLabel*         m_lblTint{nullptr};      // PSForm.designer.cs:108-117 lblPSTint
    QComboBox*      m_comboTint{nullptr};    // PSForm.designer.cs:160-174 comboPSTint

    // Bottom row
    QCheckBox* m_chkLoopback{nullptr};   // PSForm.designer.cs:466-479 checkLoopback
    QCheckBox* m_chkOnTop{nullptr};      // PSForm.designer.cs:95-106 chkPSOnTop

    // Calibration Information (advanced section, hidden when collapsed)
    QGroupBox*  m_grpCalInfo{nullptr};           // PSForm.designer.cs:416-448 grpPSInfo
    QPushButton* m_btnDefaultPeaks{nullptr};     // PSForm.designer.cs:450-464 btnDefaultPeaks
    QLabel*    m_lblFb2{nullptr};                // PSForm.designer.cs:594-605 lblPSfb2
    QLabel*    m_lblInfo0{nullptr};              // PSForm.designer.cs:681-692 lblPSInfo0  (bldr.rx)
    QLabel*    m_lblInfo1{nullptr};              // PSForm.designer.cs:668-679 lblPSInfo1  (bldr.cm)
    QLabel*    m_lblInfo2{nullptr};              // PSForm.designer.cs:655-666 lblPSInfo2  (bldr.cc)
    QLabel*    m_lblInfo3{nullptr};              // PSForm.designer.cs:642-653 lblPSInfo3  (bldr.cs)
    QLabel*    m_lblInfo5{nullptr};              // PSForm.designer.cs:481-492 lblPSInfo5  (cor.cnt)
    QLabel*    m_lblInfo6{nullptr};              // PSForm.designer.cs:529-540 lblPSInfo6  (sln.chk)
    QLabel*    m_lblInfo13{nullptr};             // PSForm.designer.cs:505-516 lblPSInfo13 (dg.cnt)
    QLabel*    m_lblInfo15{nullptr};             // PSForm.designer.cs:618-629 lblPSInfo15 (state)
    QLabel*    m_lblGetPSpeak{nullptr};          // PSForm.designer.cs:553-561 GetPSpeak (read-only)
    QLabel*    m_lblTxPSpeak{nullptr};           // PSForm.designer.cs:573-582 txtPSpeak (SetPk read-only mirror)

    // Advanced collapse: vector of every widget that disappears in
    // collapsed mode (everything except the top action row).  Built in
    // buildUi() so setAdvancedMode() can iterate without naming each
    // widget individually.  See Thetis PSForm.cs:894-902 [v2.10.3.13].
    QVector<QWidget*> m_advancedSectionWidgets;
    bool m_advancedCollapsed{false};

    // Lazy-constructed AmpView dialog (Task 9).  PsForm owns the lifetime
    // (parented at construction) so it survives across PsForm close/reopen
    // cycles.  Mirrors Thetis PSForm.cs:454-464 btnPSAmpView_Click +
    // PSForm.cs FixAmpViewOnTop pattern [v2.10.3.13].
    AmpViewWindow* m_ampView{nullptr};
};

} // namespace NereusSDR
