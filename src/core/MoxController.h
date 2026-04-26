// =================================================================
// src/core/MoxController.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. The MOX state machine and its enumerated
// states are designed for NereusSDR's Qt6 architecture; logic and
// timer constants are derived from Thetis:
//   console.cs:29311-29678 [v2.10.3.13] — chkMOX_CheckedChanged2
//
// Upstream file has no per-member inline attribution tags in this
// state-machine region except where noted with inline cites below.
//
// Disambiguation: this class is the *radio-level* MOX state machine.
// PttMode (src/core/PttMode.h) carries the Thetis PTTMode enum.
// PttSource (src/core/PttSource.h) is a NereusSDR-native enum tracking
// the UI surface that triggered the PTT event (Diagnostics page).
// None of these three are supersets of each other; all coexist.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-25 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//                 Task: Phase 3M-1a Task B.2 — MoxController skeleton
//                 (Codex P2 ordering). State-machine transitions
//                 derived from chkMOX_CheckedChanged2
//                 (console.cs:29311-29678 [v2.10.3.13]).
// =================================================================

// no-port-check: NereusSDR-original file; Thetis state-machine
// derived values are cited inline below.

#pragma once

#include <QObject>
#include "core/PttMode.h"

namespace NereusSDR {

// ---------------------------------------------------------------------------
// MoxState — the 7-state TX/RX transition machine.
//
// State machine derived from chkMOX_CheckedChanged2
// (console.cs:29311-29678 [v2.10.3.13]).
//
// The 5 transient states (RxToTxRfDelay, RxToTxMoxDelay,
// TxToRxKeyUpDelay, TxToRxBreakIn, TxToRxFlush) are visited only
// during timer-driven transitions. B.2 (this skeleton) immediately
// steps through them to the terminal state; B.3 wires the QTimer
// chains to make them dwell for the correct interval.
// ---------------------------------------------------------------------------
enum class MoxState {
    Rx,                // idle, receiver active
    RxToTxRfDelay,     // waiting for rf_delay (30 ms default) before TX channel on
    RxToTxMoxDelay,    // waiting for mox_delay settle (non-CW RX→TX)
    Tx,                // transmitting
    TxToRxKeyUpDelay,  // CW key-up delay (key_up_delay, 10 ms) before hardware flip
    TxToRxBreakIn,     // break-in settle (space_mox_delay, 0 ms default)
    TxToRxFlush,       // waiting for ptt_out_delay (20 ms) before RX channels on
};

// ---------------------------------------------------------------------------
// MoxController — drives the MOX/PTT state machine.
//
// Lives on the main thread; will be owned by RadioModel (Task G.1).
//
// Codex P2 (PR #139): safety effects in setMox() execute BEFORE the
// idempotent guard so that a repeated setMox(true) call cannot skip
// them. The body of runMoxSafetyEffects() is empty in B.2; Task F.1
// wires AlexController routing, ATT-on-TX, and the MOX wire bit.
// ---------------------------------------------------------------------------
class MoxController : public QObject {
    Q_OBJECT

public:
    explicit MoxController(QObject* parent = nullptr);
    ~MoxController() override;

    // ── Getters ──────────────────────────────────────────────────────────────
    bool     isMox()    const noexcept { return m_mox; }
    MoxState state()    const noexcept { return m_state; }
    PttMode  pttMode()  const noexcept { return m_pttMode; }

    // ── Setter ───────────────────────────────────────────────────────────────
    // setPttMode: idempotent; emits pttModeChanged on actual transition.
    void setPttMode(PttMode mode);

public slots:
    // setMox: Codex P2-ordered slot.
    //
    // Order (must not be reordered):
    //   1. runMoxSafetyEffects(on)       — safety effects fire FIRST
    //   2. idempotent guard              — skip state advance if no change
    //   3. m_mox = on                   — commit new state
    //   4. advanceState(...)             — internal state machine step
    //   5. emit moxStateChanged(on)      — single boundary signal (Codex P1)
    //
    // Task F.1 fills runMoxSafetyEffects with Alex routing, ATT-on-TX,
    // and the MOX wire bit. DO NOT insert an early-return guard above
    // the runMoxSafetyEffects call — that would regress Codex P2.
    void setMox(bool on);

signals:
    // ── Boundary signals (B.2) ───────────────────────────────────────────────
    // moxStateChanged: emitted exactly once per real transition.
    // Subscribers attach here (Codex P1), not to individual setters.
    void moxStateChanged(bool on);

    // pttModeChanged: emitted when m_pttMode transitions.
    void pttModeChanged(PttMode mode);

    // stateChanged: fires on every m_state transition; useful for tests
    // and debugging. B.4 adds the 6 named phase signals.
    void stateChanged(MoxState newState);

protected:
    // runMoxSafetyEffects is protected virtual so test subclasses can
    // override it to verify the Codex P2 ordering invariant without
    // needing a full RadioModel or RadioConnection.
    //
    // F.1 wires: AlexController::applyAntennaForBand,
    //            StepAttenuatorController TX-path, RadioConnection MOX bit.
    //
    // TODO [3M-1a F.1]: fill this body with actual safety effects.
    virtual void runMoxSafetyEffects(bool newMox);

private:
    // advanceState: sets m_state and emits stateChanged.
    // Called from setMox to transition to the terminal state (Tx or Rx
    // in B.2). B.3 replaces the direct call with timer-driven advances
    // through the transient states.
    void advanceState(MoxState newState);

    // ── Fields ───────────────────────────────────────────────────────────────
    bool     m_mox{false};               // single source of truth for MOX
    MoxState m_state{MoxState::Rx};      // current state-machine position
    PttMode  m_pttMode{PttMode::None};   // current PTT mode
};

} // namespace NereusSDR
