#pragma once

#include <QSet>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace NereusSDR {

class SliceModel;
class StatusBadge;
class BadgePair;

// RxDashboard — dense glance dashboard for RX1 state.
//
// Renders three BadgePair slots + one lone SQL badge:
//   Slot 1: Mode (always)   over Filter (always)
//   Slot 2: AGC  (always)   over NR     (active only)
//   Slot 3: NB   (active)   over APF    (active only)
//   Slot 4: SQL — alone     (active only)
//
// The frequency display lived here in earlier revisions but moved to the
// VFO Flag (2026-04-30) — there's no need to repeat it in the chrome.
//
// Three-stage responsive ladder (reapplyDropPriority):
//   1. Wide:    all pairs horizontal — every visible badge in one row.
//   2. Medium:  pairs stack vertically — half the horizontal footprint.
//   3. Narrow:  stacked + drop in priority order (SQL → APF → NB → NR
//               → AGC; mode + filter never drop).
//
// Signal-name notes (verified against SliceModel.h 2026-04-30):
//   - dspModeChanged(DSPMode)         — typed enum
//   - filterChanged(int low, int high) — two params
//   - agcModeChanged(AGCMode)         — typed enum
//   - activeNrChanged(NrSlot)         — NrSlot enum (Off/NR1/NR2/...MNR)
//   - nbModeChanged(NbMode)           — tri-state Off/NB/NB2
//   - apfEnabledChanged(bool)         — Auto-Notch Filter (closest to ANF)
//   - ssqlEnabled/amsqEnabled/fmsqEnabled — per-mode squelch; ssql as indicator
//
// Phase 3Q Sub-PR-5/6 (E.1 + F.1) + 2026-04-30 dashboard polish.
class RxDashboard : public QWidget {
    Q_OBJECT

public:
    explicit RxDashboard(QWidget* parent = nullptr);

    void bindSlice(SliceModel* slice);
    SliceModel* slice() const noexcept { return m_slice; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onModeChanged(int mode);
    void onFilterChanged(int low, int high);
    void onAgcChanged(int agcMode);
    void onNrChanged(int nrSlot);
    void onNbChanged(int nbMode);
    void onApfChanged(bool active);
    void onSsqlChanged(bool active);

private:
    void buildUi();
    void reapplyDropPriority();

    QSet<StatusBadge*> m_droppedBadges;   // badges hidden by resize, not by feature-off
    bool               m_inReapplyDropPriority{false};   // re-entry guard — setting badge
                                                          // visibility triggers a layout
                                                          // update which can re-fire
                                                          // resizeEvent before this method
                                                          // unwinds, invalidating the QSet
                                                          // iterator. See I.1 crash fix.

    // Hysteresis state — without these, the wide↔medium boundary
    // oscillates: stacking shrinks our sizeHint → parent QHBoxLayout
    // hands us a slightly different width → resize re-fires → unstack
    // tries to fit → fails → stack again → loop. The user sees both
    // BadgePair orientations painted alternately ("flash on top of each
    // other"). The deadband gate makes the function a no-op until the
    // budget moves meaningfully.
    int   m_lastDecisionBudget{-1};
    bool  m_settled{false};

    SliceModel*  m_slice{nullptr};

    StatusBadge* m_modeBadge{nullptr};
    StatusBadge* m_filterBadge{nullptr};
    StatusBadge* m_agcBadge{nullptr};
    StatusBadge* m_nrBadge{nullptr};
    StatusBadge* m_nbBadge{nullptr};
    StatusBadge* m_apfBadge{nullptr};
    StatusBadge* m_sqlBadge{nullptr};

    BadgePair*   m_modeFilterPair{nullptr};   // mode + filter
    BadgePair*   m_agcNrPair{nullptr};        // AGC + NR
    BadgePair*   m_nbApfPair{nullptr};        // NB + APF
};

} // namespace NereusSDR
