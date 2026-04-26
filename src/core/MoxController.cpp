// =================================================================
// src/core/MoxController.cpp  (NereusSDR)
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

#include "core/MoxController.h"

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MoxController::MoxController(QObject* parent)
    : QObject(parent)
{
}

MoxController::~MoxController() = default;

// ---------------------------------------------------------------------------
// setMox — Codex P2-ordered MOX toggle.
//
// State machine derived from chkMOX_CheckedChanged2
// (console.cs:29311-29678 [v2.10.3.13]).
//
// ORDERING MUST NOT BE CHANGED (Codex P2, PR #139):
//   Step 1 — safety effects run BEFORE the idempotent guard.
//             A repeated setMox(true) must still drive safety effects
//             (e.g. re-assert Alex routing if the band changed under us)
//             even if m_mox is already true.
//   Step 2 — idempotent guard: bail if no real state change.
//   Step 3 — commit new state.
//   Step 4 — advance the internal state machine.
//   Step 5 — emit boundary signal (Codex P1: single emit per transition).
// ---------------------------------------------------------------------------
void MoxController::setMox(bool on)
{
    // ── Step 1: Safety effects (BEFORE idempotent guard — Codex P2) ─────────
    // F.1 wires: AlexController::applyAntennaForBand(currentBand, isTx)
    //            StepAttenuatorController TX-path activation / RX restore
    //            RadioConnection::setMoxBit(isTx) + setTrxRelayBit(isTx)
    runMoxSafetyEffects(on);

    // ── Step 2: Idempotent guard ──────────────────────────────────────────────
    if (m_mox == on) {
        return;  // no real transition — no state advance, no signal emit
    }

    // ── Step 3: Commit new MOX state ─────────────────────────────────────────
    m_mox = on;

    // ── Step 4: Advance the internal state machine ────────────────────────────
    // B.3 replaces this direct-to-terminal jump with QTimer-driven advances
    // through the transient states (RxToTxRfDelay, RxToTxMoxDelay,
    // TxToRxKeyUpDelay, TxToRxBreakIn, TxToRxFlush).
    advanceState(on ? MoxState::Tx : MoxState::Rx);

    // ── Step 5: Boundary signal (Codex P1: exactly one emit per transition) ──
    // Subscribers attach here, not to individual field setters.
    emit moxStateChanged(on);
}

// ---------------------------------------------------------------------------
// setPttMode — idempotent PTT mode setter.
// ---------------------------------------------------------------------------
void MoxController::setPttMode(PttMode mode)
{
    if (m_pttMode == mode) {
        return;
    }
    m_pttMode = mode;
    emit pttModeChanged(mode);
}

// ---------------------------------------------------------------------------
// advanceState — internal state-machine step.
//
// Sets m_state and emits stateChanged. Called from setMox() for the
// terminal states (Tx / Rx) in B.2. B.3 will also call this from
// QTimer slots to drive through the transient states.
// ---------------------------------------------------------------------------
void MoxController::advanceState(MoxState newState)
{
    if (m_state == newState) {
        return;
    }
    m_state = newState;
    emit stateChanged(newState);
}

// ---------------------------------------------------------------------------
// runMoxSafetyEffects — placeholder for F.1 wiring.
//
// Called on EVERY setMox() invocation, including idempotent ones, so
// that safety effects cannot be skipped by a repeated call.
//
// TODO [3M-1a F.1]: wire body with:
//   - AlexController::applyAntennaForBand(currentBand(), isTx)
//   - StepAttenuatorController TX-path activation / RX restore
//   - RadioConnection::setMoxBit(isTx) + setTrxRelayBit(isTx)
// ---------------------------------------------------------------------------
void MoxController::runMoxSafetyEffects(bool /*newMox*/)
{
    // intentionally empty in B.2 — see TODO above
}

} // namespace NereusSDR
