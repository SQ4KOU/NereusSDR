# Phase 3M-1b — Mic + SSB Voice: Implementation Plan

**Status:** ready for execution.
**Date:** 2026-04-27.
**Branch:** `feature/phase3m-1b-mic-ssb-voice`.
**Base:** `origin/feature/phase3m-1a-tune-only-first-rf` @ `708ceb8`.
**Pre-code review:** [`docs/architecture/phase3m-1b-thetis-pre-code-review.md`](phase3m-1b-thetis-pre-code-review.md).
**Master design:** [`docs/architecture/phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.2.
**Risk profile:** MEDIUM — pre-code review §15.

> **For agentic workers:** REQUIRED SUB-SKILL — use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** First voice transmission from NereusSDR. PC mic + radio mic both
selectable. Clean unprocessed SSB through the TXA pipeline; speech-processing
stages built but `Run=false`. MON pulled forward from 3M-3c so users hear
themselves through headphones. HL2 PC-mic-only by capability flag.

**Architecture:** Strategy-pattern `TxMicRouter` selects between
`PcMicSource` (taps `AudioEngine::pullTxMic` from the existing 3O VAX TX-input
bus — no duplicate audio stack) and `RadioMicSource` (subscribes to P2 port
1026 / P1 EP2 mic byte zone via `RadioConnection::micFrameDecoded` signal).
Mic samples flow into `TxChannel::driveOneTxBlock` replacing the
`NullMicSource` stub. Per-mode TXA config (`SetTXAMode` + `SetTXABandpassFreqs`)
runs on `SliceModel::modeChanged`. Mic-jack hardware bits port byte-exact
from Thetis `networkproto1.c` (P1) and deskhpsdr `new_protocol.c` (P2).
MON is a TXA `Stage::Sip1` siphon → `AudioEngine` mix-in folded with the
RX-leak-during-MOX `activeSlice` gate fix.

**Tech Stack:** C++20, Qt6, WDSP TXA pipeline (already constructed in
3M-1a), PortAudio + PipeWire (3O VAX infrastructure).

---

## 0. Brainstorm-locked decisions (already taken)

Per pre-code review §0.3 and §12, all eleven decisions resolved before
plan-writing. Each gets a brief recap here so the plan is self-contained.

| # | Decision | Choice |
|---|---|---|
| 1 | MON ↔ RX-leak fold (locked in kickoff) | Single audio-mixer pass: TXA siphon mix-in + `activeSlice` gate fix |
| 2 | HL2 mic-jack flag (locked in kickoff) | New `BoardCapabilities::hasMicJack` field; HL2 sets `false` |
| 3 | PcMicSource architecture (locked in kickoff) | Tap `AudioEngine::pullTxMic(float*, int)` — no duplicate audio stack |
| 4 | Closed-source mic-jack research | deskhpsdr cross-fork closed both gaps; full P2 byte-50 emission ports cleanly (pre-code §6.3) |
| 5 | Mic-source UX | Radio buttons on Setup → Audio → TX Input + read-only badge in TxApplet |
| 6 | PortAudio backend defaults | macOS = CoreAudio; Linux = PipeWire (`NEREUS_HAVE_PIPEWIRE`) → Pulse fallback; Windows = WASAPI shared |
| 7 | Anti-VOX source default | Local-RX (`antiVoxSourceVax = false`; matches Thetis) |
| 8 | VOX defaults | OFF at startup; threshold/gain/hang-time persist; **enable flag does not persist** |
| 9 | MON defaults | OFF at startup; volume default `0.5` (matches Thetis literal `cmaster.SetAAudioMixVol(...,0.5)` at `audio.cs:417`) |
| 10 | Setup page location | `src/gui/setup/AudioTxInputPage.{h,cpp}` (matches existing flat audio-page convention) |
| 11 | Mic gain default | `−6 dB` first run (NereusSDR-original safety addition vs. ALC overdrive) |
| 12 | SSB-mode-only TX | LSB / USB / DIGL / DIGU TX-enabled. AM / SAM / DSB / FM / DRM rejected by `BandPlanGuard` with tooltip "AM/FM TX coming in Phase 3M-3 (audio modes)". CW rejected until 3M-2 |

---

## 1. Workflow shape

**Single branch, single PR.** All commits on
`feature/phase3m-1b-mic-ssb-voice`.

**Commit shape:**

- **Commit 1:** Pre-code Thetis review (`phase3m-1b-thetis-pre-code-review.md`) — landed @ `2a7a3b1`.
- **Commit 2:** This plan (`phase3m-1b-mic-ssb-voice-plan.md`).
- **Commits 3..N:** TDD task commits (one per task, see §3).
- **Commit N+1:** Verification matrix update (3M-1b rows).
- **Commit N+2:** Post-code review (`phase3m-1b-post-code-review.md`).
- **PR open:** drafted in chat first per `feedback_no_public_posts_without_review`,
  opened only on explicit "post it" sign-off.

**Per-task discipline (rigid TDD):**

1. State the pre-code review section being ported.
2. Write the failing test first (or for source-first ports: state cite,
   write the failing assertion that captures the cited behaviour).
3. Implement to green.
4. Run the relevant `ctest` subset locally before commit.
5. GPG-sign the commit (no `--no-gpg-sign`; per `feedback_gpg_sign_commits`).
6. After each commit, run a two-stage subagent review (spec compliance +
   code quality). Fresh subagent per task — no shared state.
7. Resolve review feedback (if any) via fixup commit on the same task,
   then advance.

**Inline cite stamp:** `[v2.10.3.13]` for Thetis tag-aligned values;
`[@501e3f51]` for Thetis post-tag commits; `[@120188f]` for deskhpsdr
post-tag commits. Verifier `scripts/verify-inline-tag-preservation.py` runs
in pre-commit hook chain — `NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis` must
be set in the shell before each commit.

**Codex-review patterns to watch (from PR #139 + PR #144):**

- **P1: signal multi-emit.** Subscribers attach at the per-sample boundary
  signal (e.g., `MoxController::txStateChanged`), not individual setters.
- **P2: idempotent-guard placement.** Safety effects execute BEFORE the
  `if (m_x == newX) return` idempotent guard. Repeated transitions can't
  skip the safety effect. Order: *(safety effect) → (state transition) →
  (boundary signal emit) → (idempotent guard for next call)*.

**Bench-test gate:** HL2 first (lower stakes), then G2, BEFORE PR opens.
PR review happens on already-validated code.

---

## 2. Files

**New (8):**

- `src/core/audio/PcMicSource.{h,cpp}` — `TxMicRouter` impl tapping `AudioEngine::pullTxMic`.
- `src/core/audio/RadioMicSource.{h,cpp}` — `TxMicRouter` impl subscribing to `RadioConnection::micFrameDecoded`.
- `src/core/TxMicRouter.cpp` — split from header-only stub; full router with selector.
- `src/gui/setup/AudioTxInputPage.{h,cpp}` — Setup → Audio → TX Input page.

**Modified (~15):**

- `src/core/AudioEngine.{h,cpp}` — `pullTxMic`, `setTxMonitorEnabled`, `setTxMonitorVolume`, `txMonitorBlockReady` slot, `rxBlockReady` activeSlice + MOX gate.
- `src/core/TxChannel.{h,cpp}` — `setTxMode`, `setTxBandpass`, `setVoxRun`, `setVoxAttackThreshold`, `setAntiVoxGain`, `setAntiVoxRun`, `sip1OutputReady` signal, panel-gain mute, TxMic + ALC meter readouts, `setStageRunning` expansion (Panel/MicMeter/AlcMeter/AmMod/FmMod).
- `src/core/MoxController.{h,cpp}` — `setVoxEnabled` + mode-gate, `setVoxThreshold` + mic-boost-aware scaling, `setVoxHangTime`, `setAntiVoxGain`, `setAntiVoxSourceVax`, PTT-source dispatch (MIC/CAT/VOX/SPACE/X2 wires), `mic_ptt` extraction from status frames.
- `src/core/RadioConnection.{h,cpp}` — `setMicBoost` / `setLineIn` / `setMicTipRing` / `setMicBias` / `setMicPTT` / `setMicXlr` virtuals, `micFrameDecoded` signal.
- `src/core/P1RadioConnection.{h,cpp}` — mic-jack wire emission (case 10 C2 bits 0-1, case 11 C1 bits 4-6); `micFrameDecoded` from EP2 mic byte zone.
- `src/core/P2RadioConnection.{h,cpp}` — mic-jack wire emission (transmit-specific byte 50 bits 0-5); `micFrameDecoded` from port 1026 subscription.
- `src/core/BoardCapabilities.{h,cpp}` — new `hasMicJack` field; HL2 sets `false`; all others `true`.
- `src/core/TxMicRouter.h` — split header-only into header + cpp; add real implementations + selector.
- `src/models/TransmitModel.{h,cpp}` — mic properties (gain, mute, boost, xlr, lineIn, lineInBoost, tipRing, bias, pttDisabled), VOX properties (enabled, thresholdDb, gainScalar, hangTimeMs), anti-VOX (gainDb, sourceVax), MON (enabled, volume).
- `src/core/RadioModel.{h,cpp}` — own `PcMicSource` + `RadioMicSource` + `TxMicRouter`; wire selector based on `BoardCapabilities::hasMicJack` and `TransmitModel::micSource`.
- `src/core/safety/BandPlanGuard.{h,cpp}` — SSB-mode allow-list for TX; reject AM/SAM/DSB/FM/DRM/CW with tooltip.
- `src/gui/applets/TxApplet.{h,cpp}` — Mic Gain slider, VOX toggle, MON toggle, monitor volume slider, mic-source badge.
- `src/gui/setup/SetupDialog.cpp` — register `AudioTxInputPage` between `AudioDevicesPage` and `AudioVaxPage`.
- `src/gui/setup/SetupHelpers.{h,cpp}` — capability-gating helper (board family detection for mic-jack panel layout).
- `docs/attribution/THETIS-PROVENANCE.md` — register P1 mic-jack ports.
- `docs/attribution/DESKHPSDR-PROVENANCE.md` — new file for deskhpsdr P2 mic-jack ports (or extend existing PROVENANCE schema).

**Tests (new, ~25 files):** listed inline per task.

---

## 3. TDD task list

Tasks numbered as `<phase>.<index>`. Each is one commit. Phase letters
match the master design components, decomposed for TDD.

### Phase A — Pre-code documentation (already in flight)

| # | Task | Status |
|---|---|---|
| A.1 | Pre-code Thetis review document (`phase3m-1b-thetis-pre-code-review.md`) | **landed @ `2a7a3b1`** |
| A.2 | Implementation plan (this file) | **in-progress** |

### Phase B — BoardCapabilities + provenance (2 tasks)

Foundation. `hasMicJack` flag drives Setup → Audio → TX Input visibility. The
deskhpsdr provenance entry must land before any task ports deskhpsdr code.

| # | Task | Tests | Cite |
|---|---|---|---|
| B.1 | Add `bool hasMicJack {true}` to `BoardCapabilities`; HL2 entry sets `false`. All other boards inherit default `true`. Update existing `BoardCapsTable::forBoard` switch accordingly. Cross-check entries in `BoardCapabilities.cpp` for Atlas, Hermes, Hermes-II, Angelia, Orion, Orion-MkII, ANAN-G2, ANAN-G2-1K, ANAN-7000DLE, ANAN-8000DLE, ANAN-Anvelina, HermesLite 2 (the last sets `false`). | `tst_board_capabilities_mic_jack.cpp` (parametrized over all board kinds; HL2 → false; rest → true) | Pre-code review §11 |
| B.2 | Register deskhpsdr as a recognised upstream in `docs/attribution/`. Two options: extend `THETIS-PROVENANCE.md` with multi-upstream rows, OR create new `DESKHPSDR-PROVENANCE.md`. Prefer the latter for clarity. Add `scripts/discover-deskhpsdr-author-tags.py` (mechanical mirror of `discover-thetis-author-tags.py`) and a verifier hook in `verify-inline-tag-preservation.py`. Add deskhpsdr corpus regeneration step to upstream-sync protocol. | None (script-only commit; verifier is exercised by Phase G tasks that port deskhpsdr code) | Pre-code review §6.3 license-attribution note |

### Phase C — TransmitModel mic + VOX + MON properties (5 tasks)

Pure model state. No WDSP, no protocol, no UI. Each property has a setter
that emits a Qt signal; persistence to `AppSettings` happens in Phase L.

| # | Task | Tests | Cite |
|---|---|---|---|
| C.1 | `TransmitModel::micGainDb` (int, default `−6`; range from per-board `mic_gain_min` / `mic_gain_max`) and derived `micPreampLinear` (double, computed via `pow(10, dB/20)`). Setter emits `micGainChanged(int)` and `micPreampChanged(double)`. Property comment includes Thetis cite `console.cs:28805-28817 [v2.10.3.13]` for the dB→linear conversion. | `tst_transmit_model_mic_gain.cpp` (round-trip dB↔linear; default value; range clamping) | Pre-code review §1.4 + §2.2 |
| C.2 | Mic-jack flag properties on `TransmitModel`: `micMute` (default `true` = mic in use; preserves Thetis counter-intuitive naming with comment), `micBoost` (default `true`), `micXlr` (default `true`), `lineIn` (default `false`), `lineInBoost` (default `0.0`), `micTipRing` (default `true` = Tip is mic), `micBias` (default `false`), `micPttDisabled` (default `false`). All setters emit corresponding `*Changed` signals. | `tst_transmit_model_mic_jack_flags.cpp` (defaults; setter signals; round-trip) | Pre-code review §2.3 + §2.7 + Thetis `console.cs:13213-13260 [v2.10.3.13]` |
| C.3 | VOX properties: `voxEnabled` (default `false`), `voxThresholdDb` (int, default `−40`; range from Thetis `ptbVOX.Minimum`/`Maximum`), `voxGainScalar` (float, default `1.0f`; mic-boost-aware threshold scaler), `voxHangTimeMs` (int, default `500`). Setters emit `*Changed`. | `tst_transmit_model_vox_properties.cpp` (defaults; setter signals) | Pre-code review §1.4 + §8.5 |
| C.4 | Anti-VOX properties: `antiVoxGainDb` (int, default `0`), `antiVoxSourceVax` (bool, default `false` = local-RX; matches Thetis `Audio.AntiVOXSourceVAC = false`). Setters emit `*Changed`. | `tst_transmit_model_anti_vox.cpp` (defaults match Thetis; setter signals) | Pre-code review §1.4 + §3.4 |
| C.5 | MON properties: `monEnabled` (bool, default `false`), `monitorVolume` (float, default `0.5f`; matches Thetis literal mix coefficient). Setters emit `monEnabledChanged(bool)` and `monitorVolumeChanged(float)`. | `tst_transmit_model_mon.cpp` (defaults; setter signals) | Pre-code review §4.2 + §12.5 |

### Phase D — TxChannel mic + per-mode + meter + siphon (7 tasks)

WDSP-side wiring. `TxChannel` already has the 31-stage pipeline from 3M-1a;
this phase adds the mic-input integration, per-mode setters, VOX/anti-VOX
WDSP wrappers, mute path, MON siphon signal, and meter readouts.

| # | Task | Tests | Cite |
|---|---|---|---|
| D.1 | `TxChannel::driveOneTxBlock` integration with non-Null `TxMicRouter`. Currently calls `m_micRouter->pullSamples(m_inI.data(), m_inI.size())` — that's already wired in 3M-1a but only for `NullMicSource`. This task verifies the mic samples actually drive `fexchange2` when a real `TxMicRouter` is attached. Includes Q-channel zero-fill for real-mic input (Q = 0; mic is real-valued). | `tst_tx_channel_real_mic_router.cpp` (inject test mic source emitting known samples; verify `m_inI` matches; verify `m_inQ` is zero-filled) | Pre-code review §0.3 (PcMicSource arch) |
| D.2 | `TxChannel::setTxMode(DSPMode)` and `TxChannel::setTxBandpass(int low, int high)` and `TxChannel::setSubAmMode(int sub)` (deferred — defined but throws on use until 3M-3b). Wires `WDSP.SetTXAMode` + `WDSP.SetTXABandpassFreqs`. | `tst_tx_channel_per_mode_config.cpp` (mode set propagates; bandpass set propagates; AM/SAM/DSB sub-mode throws with explanatory error in 3M-1b) | Pre-code review §7 + Thetis `radio.cs:2670-2780 [v2.10.3.13]` |
| D.3 | VOX/anti-VOX WDSP wrappers on `TxChannel`: `setVoxRun(bool)` → `WDSP.SetDEXPRunVox`; `setVoxAttackThreshold(double)` → `WDSP.SetDEXPAttackThreshold`; `setVoxHangTime(double sec)` → `WDSP.SetDEXPHangTime`; `setAntiVoxRun(bool)` → `WDSP.SetAntiVOXRun`; `setAntiVoxGain(double)` → `WDSP.SetAntiVOXGain`. | `tst_tx_channel_vox_anti_vox.cpp` (each wrapper stores last value; idempotent) | Pre-code review §3.5 + Thetis `cmaster.cs:208-221 [v2.10.3.13]` |
| D.4 | `TxChannel::setStageRunning` expansion to support `Panel`, `MicMeter`, `AlcMeter`, `AmMod`, `FmMod`. Currently the method supports Gen0/Gen1/Panel/PhRot/AmSq/Eqp/Compressor/OsCtrl/Cfir/CfComp; add `Panel` (already there but verify), `MicMeter`, `AlcMeter`, `AmMod`, `FmMod`. SSB chain in 3M-1b activates: rsmpin (auto), panel, micmeter, bp0 (auto), alc (auto), uslew (auto), alcmeter, cfir (already on per 3M-1a), rsmpout (auto), outmeter (auto). Plus `Sip1` for MON siphon (already on per WDSP defaults). | `tst_tx_channel_stage_running_expansion.cpp` (each new stage's `Run` flag set/cleared correctly) | Pre-code review §1.2 (all 22 stages built; only 10 SSB-path active) + master design §5.2.1 |
| D.5 | `TxChannel::sip1OutputReady(const float* samples, int frames)` Qt signal. Emitted on the audio thread inside `driveOneTxBlock` after `fexchange2` returns. Carries the post-SSB-modulator audio at TXA dsp-rate (96 kHz P2; 48 kHz P1). Connection wiring (DirectConnection to `AudioEngine::txMonitorBlockReady`) lives in `RadioModel` (Phase L). | `tst_tx_channel_sip1_signal.cpp` (signal-spy on `sip1OutputReady`; matches frame count to `m_outputBufferSize`) | Pre-code review §4.3 |
| D.6 | Mic-mute path: when `TransmitModel::micMute == false`, `TxChannel` calls `WDSP.SetTXAPanelGain1(channelId, 0)`. Achieved by `TxChannel::recomputeTxAPanelGain1()` which reads `m_micPreamp` (set via `setMicPreamp(double)`); when caller passes `0` (because `MicMute` toggled off), gain drops to zero. Recompute slot connects to `TransmitModel::micPreampChanged`. | `tst_tx_channel_mic_mute.cpp` (`micPreampLinear = 0.0` → SetTXAPanelGain1 called with 0) | Pre-code review §2.1 + Thetis `console.cs:28805-28817 [v2.10.3.13]` |
| D.7 | TX meter readouts: `TxChannel::getTxMicMeter()` reads `WDSP.GetTXAMeter(channelId, TXA_MIC_PK)` (peak); `TxChannel::getAlcMeter()` reads `TXA_ALC_PK`. Returns float dB value or sentinel `−999` if WDSP not initialised. EQ/Lev/CFC/Comp meters return zero (deferred to 3M-3a per master design §5.2.1). | `tst_tx_channel_meters.cpp` (TxMic + ALC return non-sentinel during MOX; EQ/Lev/CFC/Comp return zero) | Pre-code review §1.4 (TX meters live row) + master design §5.2.1 |

### Phase E — AudioEngine pullTxMic + MON path + RX-leak gate (4 tasks)

`AudioEngine` already owns `m_txInputBus` (3O VAX TX-input bus). Add the
public pull accessor for `PcMicSource`, the MON path slots, and the
activeSlice-during-MOX gate that fixes the RX-leak cosmetic bug.

| # | Task | Tests | Cite |
|---|---|---|---|
| E.1 | `AudioEngine::pullTxMic(float* dst, int n) → int`. Drains `m_txInputBus->pull(...)` and applies a sample-format conversion (Int16 → float32 mono). Returns sample count actually written. Audio-thread safe (lock-free SPSC ring on the bus). Returns 0 if `m_txInputBus` is null (mic not configured). | `tst_audio_engine_pull_tx_mic.cpp` (inject fake bus; pull samples; verify count + format conversion) | Pre-code review §0.3 (PcMicSource arch) |
| E.2 | `AudioEngine::setTxMonitorEnabled(bool)` and `setTxMonitorVolume(float)`. Backed by `std::atomic<bool>` and `std::atomic<float>`. Setters emit `txMonitorEnabledChanged` and `txMonitorVolumeChanged`. Volume clamped to `[0.0, 1.0]`. | `tst_audio_engine_tx_monitor_state.cpp` (setter atomicity; signal emission; clamping) | Pre-code review §4.4 |
| E.3 | `AudioEngine::txMonitorBlockReady(const float* samples, int frames)` slot. When `m_txMonitorEnabled == true`, mixes `samples` × `m_txMonitorVolume` into `m_masterMix` at `kTxMonitorSlotId`. When false, no-op. Resamples from TXA dsp-rate (96 kHz / 48 kHz) to speakers rate via existing MasterMixer resampling. | `tst_audio_engine_tx_monitor_block.cpp` (enabled + samples → mixer slot fed; disabled → no mixer call; volume applied) | Pre-code review §4.3 + §4.4 |
| E.4 | `AudioEngine::rxBlockReady` activeSlice + MOX gate (the RX-leak fold). Add cross-thread-safe access to `m_radio->moxState()` via std::atomic mirror. When `moxActive && slice->isActiveSlice()`, return early (silences the active-TX RX). Non-active slices unaffected — matches Thetis IVAC mox behavior. | `tst_audio_engine_rx_leak_during_mox.cpp` (MOX off → block flows; MOX on + active slice → block dropped; MOX on + non-active slice → block flows) | Pre-code review §10.3 + §10.4 |

### Phase F — TxMicRouter implementations (4 tasks)

The strategy-pattern realisation. Replaces 3M-1a's `NullMicSource` stub with
real `PcMicSource` + `RadioMicSource`, plus the selector logic with
HL2-forced-PC enforcement.

| # | Task | Tests | Cite |
|---|---|---|---|
| F.1 | `PcMicSource` (`src/core/audio/PcMicSource.{h,cpp}`). Implements `TxMicRouter::pullSamples` by calling `AudioEngine::pullTxMic`. Constructor takes a non-owning `AudioEngine*` pointer. Returns 0 on null `AudioEngine`. Audio-thread-safe (no allocations, no blocking; just dispatches to E.1). | `tst_pc_mic_source.cpp` (inject mock AudioEngine; pull samples; verify dispatch) | Pre-code review §0.3 |
| F.2 | `RadioMicSource` (`src/core/audio/RadioMicSource.{h,cpp}`). Implements `TxMicRouter::pullSamples` by draining a lock-free SPSC ring fed from `RadioConnection::micFrameDecoded(const float* samples, int frames)`. Ring sized for ~50ms at 48kHz (2400 samples). Audio-thread reads; connection-thread writes. Underrun = silence (zero-fill remainder). Constructor takes a non-owning `RadioConnection*`. | `tst_radio_mic_source.cpp` (inject samples via mock connection signal; pull on audio thread; verify FIFO order; underrun produces zero-fill) | Master design §5.2.1 |
| F.3 | `TxMicRouter` selector. Move from header-only to `src/core/TxMicRouter.{h,cpp}`. Add `class CompositeTxMicRouter : public TxMicRouter` that holds `PcMicSource` + `RadioMicSource` and dispatches based on `m_activeSource` enum (`MicSource::Pc` / `MicSource::Radio`). HL2 forced to `Pc` regardless of selection (gate via `BoardCapabilities::hasMicJack`). MOX-locked: source switch ignored while `moxActive`; takes effect on next MOX-off. | `tst_tx_mic_router_selector.cpp` (PC selected → PC pulled; Radio selected → Radio pulled; HL2 caps → forced PC; switch during MOX → ignored, applied after MOX-off) | Pre-code review §0.3 + master design §5.2.1 |
| F.4 | `RadioConnection::micFrameDecoded(const float* samples, int frames)` Qt signal. Add to base class. Subscribers (RadioMicSource) connect with QueuedConnection. P1 / P2 emission lives in Phase G alongside the wire-bit setters. | `tst_radio_connection_mic_frame_signal.cpp` (signal-spy; correct sample format) | Pre-code review §6.4 |

### Phase G — RadioConnection mic-jack wire emission (6 tasks)

P1 + P2 wire-bit setters. Each task lands one logical setter group with both
P1 and P2 implementations and wire-byte snapshot tests cross-checked against
Thetis `networkproto1.c` (P1) and deskhpsdr `new_protocol.c` (P2).

For each task: write the failing wire-byte snapshot test first; implement the
setter on `RadioConnection` (base virtual); implement on `P1RadioConnection`
(C0=0x12 case 10 or C0=0x14 case 11); implement on `P2RadioConnection`
(`transmit_specific_buffer[50]`); verify the snapshot.

| # | Task | Tests | Cite |
|---|---|---|---|
| G.1 | `setMicBoost(bool on)`. P1: case 10 (C0=0x12) C2 bit 0 (`0x01`). P2: byte 50 bit 1 (`0x02`). Polarity: `1 = boost on`. | `tst_p1_mic_boost_wire.cpp` + `tst_p2_mic_boost_wire.cpp` (each: hand-crafted state, verify wire-byte snapshot at the bit level) | Pre-code review §6.1 + §6.3 + Thetis `networkproto1.c:581 [v2.10.3.13]` + deskhpsdr `new_protocol.c:1484-1486 [@120188f]` |
| G.2 | `setLineIn(bool on)`. P1: case 10 (C0=0x12) C2 bit 1 (`0x02`). P2: byte 50 bit 0 (`0x01`). Polarity: `1 = line in`. | `tst_p1_line_in_wire.cpp` + `tst_p2_line_in_wire.cpp` | Pre-code review §6.1 + §6.3 + Thetis `networkproto1.c:581` + deskhpsdr `new_protocol.c:1480-1482` |
| G.3 | `setMicTipRing(bool tipHot)`. P1: case 11 (C0=0x14) C1 bit 4 (`0x10`). P2: byte 50 bit 3 (`0x08`). **Polarity inversion at the wire layer:** in NereusSDR the parameter is `tipHot` (intuitive — `true` means Tip is mic); on the wire, both Thetis `mic_trs` and deskhpsdr `mic_ptt_tip_bias_ring` are `1` when **Tip is BIAS/PTT**. So setter writes `!tipHot` to the wire bit. | `tst_p1_mic_tip_ring_wire.cpp` + `tst_p2_mic_tip_ring_wire.cpp` (parametrized over `tipHot ∈ {true, false}`; verify wire bit is inverted) | Pre-code review §6.1 + §6.2 + §6.3 |
| G.4 | `setMicBias(bool on)`. P1: case 11 (C0=0x14) C1 bit 5 (`0x20`). P2: byte 50 bit 4 (`0x10`). Polarity: `1 = bias on`. | `tst_p1_mic_bias_wire.cpp` + `tst_p2_mic_bias_wire.cpp` | Pre-code review §6.1 + §6.3 |
| G.5 | `setMicPTT(bool enabled)`. P1: case 11 (C0=0x14) C1 bit 6 (`0x40`). P2: byte 50 bit 2 (`0x04`). **Polarity inversion at the wire layer:** parameter is `enabled` (intuitive — `true` means PTT is enabled); on the wire, both ends carry the *disable* flag. So setter writes `!enabled` to the wire bit. | `tst_p1_mic_ptt_wire.cpp` + `tst_p2_mic_ptt_wire.cpp` (parametrized; verify polarity inversion; `enabled=true` → wire bit `0`) | Pre-code review §6.1 + §6.2 + §6.3 + Thetis `console.cs:19764` + deskhpsdr `old_protocol.c:3000-3002` |
| G.6 | `setMicXlr(bool xlrJack)`. **P2-only.** P2: byte 50 bit 5 (`0x20`). Polarity: `1 = XLR jack`. P1 implementation stores the flag with comment `// Saturn G2 P2-only feature; P1 hardware has no XLR jack`. No P1 wire emission. | `tst_p1_mic_xlr_storage.cpp` (P1: stored, no wire change) + `tst_p2_mic_xlr_wire.cpp` (P2: bit 5 toggles correctly) | Pre-code review §6.3 + deskhpsdr `new_protocol.c:1500-1502 [@120188f]` |

### Phase H — MoxController VOX + anti-VOX + PTT-source dispatch (5 tasks)

`MoxController` already has the state machine from 3M-1a. This phase adds
the VOX/anti-VOX integration with mode-gating + mic-boost-aware threshold
scaling, plus PTT-source dispatch for the 5 newly-wireable sources
(MIC/CAT/VOX/SPACE/X2).

| # | Task | Tests | Cite |
|---|---|---|---|
| H.1 | `MoxController::setVoxEnabled(bool)` with mode-gate (active iff mode in voice family: LSB/USB/DSB/AM/SAM/FM/DIGL/DIGU). Drives `TxChannel::setVoxRun`. Mode-change re-evaluates: connect to `SliceModel::modeChanged`. | `tst_mox_controller_vox_enabled.cpp` (CW mode + VOX on → setVoxRun(false); LSB + VOX on → setVoxRun(true); LSB + VOX off → setVoxRun(false)) | Pre-code review §3.2 + Thetis `cmaster.cs:1039-1052 [v2.10.3.13]` |
| H.2 | `MoxController::setVoxThreshold(int dB)` with mic-boost-aware scaling. When `TransmitModel::micBoost == true`, scaled threshold = base × `voxGainScalar`. Calls `TxChannel::setVoxAttackThreshold(scaled)`. Mic-boost-change re-evaluates: connect to `TransmitModel::micBoostChanged`. | `tst_mox_controller_vox_threshold.cpp` (boost off → threshold passes through; boost on → threshold scaled by voxGainScalar; cross-check with Thetis values) | Pre-code review §3.3 + §8.3 + Thetis `cmaster.cs:1054-1059 [v2.10.3.13]` |
| H.3 | `MoxController::setVoxHangTime(int ms)`, `setAntiVoxGain(int dB)`, `setAntiVoxSourceVax(bool useVax)`. The latter implements the path-agnostic version of `CMSetAntiVoxSourceWhat`: when `useVax == false`, all RX slots get `1` (per Thetis `cmaster.cs:937-942`). When `useVax == true`, defer to 3M-3a (NotImplemented placeholder). | `tst_mox_controller_anti_vox.cpp` (default false → all-RX-on; true → throws NotImplemented) | Pre-code review §3.4 + §9 |
| H.4 | PTT-source dispatch in `MoxController`. The 5 wireable sources for 3M-1b are MIC, MANUAL (already wired), CAT (basic — full CAT integration in 3K), VOX, SPACE, X2, NONE. Add slots: `onMicPttFromRadio(bool pressed)`, `onCatPtt(bool)`, `onVoxActive(bool)`, `onSpacePtt(bool)`, `onX2Ptt(bool)`. Each routes through `setMox` with the corresponding `PttMode` set. CW + TCI rejected (assert + log). | `tst_mox_controller_ptt_source_dispatch.cpp` (each source triggers MOX with correct PttMode; CW + TCI rejected) | Pre-code review §0.3 + master design §5.2.1 |
| H.5 | `mic_ptt` extraction from P1/P2 status frames. `RadioConnection::statusFrameDecoded` already exists; add `bool mic_ptt = (dotdashptt & 0x01) != 0;` extraction at the MoxController boundary. Emit `MoxController::onMicPttFromRadio(mic_ptt)`. P1 and P2 both extract from the dot-dash-ptt byte. | `tst_mox_controller_mic_ptt_extraction.cpp` (status frame with bit 0 set → onMicPttFromRadio(true)) | Pre-code review §8.2 + Thetis `console.cs:25426 [v2.10.3.13]` |

### Phase I — Setup → Audio → TX Input page (5 tasks)

New Setup page at `src/gui/setup/AudioTxInputPage.{h,cpp}`. Layout follows
the existing `AudioVaxPage` pattern. Capability-gated per
`BoardCapabilities::hasMicJack` and per-board family.

| # | Task | Tests | Cite |
|---|---|---|---|
| I.1 | `AudioTxInputPage` skeleton: top-level `PC Mic [○]` / `Radio Mic [○]` radio buttons. Radio Mic disabled with tooltip `"Radio mic jack not present on Hermes Lite 2"` when `caps.hasMicJack == false`. Selection persists via `TransmitModel::setMicSource`. Register page in `SetupDialog.cpp` between `AudioDevicesPage` and `AudioVaxPage`. | `tst_audio_tx_input_page_skeleton.cpp` (HL2 → Radio Mic disabled; G2 → Radio Mic enabled; selection round-trips) | Pre-code review §5.4 + master design §5.2.2 |
| I.2 | PC Mic settings group: PortAudio backend selector (per OS defaults from §0 row 6), device picker (reads from `PortAudioBus::inputDevicesFor(hostApiIndex)`), buffer-size slider with ms-latency readout, Test Mic button + live VU bar (10 ms refresh). Mic Gain slider mirrors TxApplet (bidirectional sync). | `tst_audio_tx_input_pc_mic_group.cpp` (backend + device round-trip; buffer-size → latency calc; mic gain mirror) | Pre-code review §5.4 + master design §5.2.2 |
| I.3 | Radio Mic settings group with per-family layout, capability-gated. Visible only when `caps.hasMicJack == true && Radio Mic selected`. Three sub-layouts: Hermes/Atlas (radMicIn/radLineIn + chk20dbMicBoost + LineInBoost), Orion-MkII family (Mic Tip-Ring + Mic Bias + Mic PTT-disable + 20dB Boost), Saturn G2 (Mic Input 3.5mm/XLR + Mic PTT/Bias/Boost). | `tst_audio_tx_input_radio_mic_group.cpp` (per-family visibility; each control wires to its `TransmitModel` setter) | Pre-code review §5.1 + §5.4 |
| I.4 | Mic gain slider on `AudioTxInputPage`. Mirrors TxApplet's slider (bidirectional via `TransmitModel::micGainDbChanged`). Range from per-board `mic_gain_min` / `mic_gain_max`. | `tst_audio_tx_input_mic_gain_mirror.cpp` (slider on Setup page tracks TxApplet slider via signal) | Pre-code review §5.4 |
| I.5 | `chk20dbMicBoost` handler: re-trigger VOX threshold scaling on toggle. Wires `TransmitModel::micBoostChanged` → `MoxController::setVoxThreshold` (already handled in H.2; this task verifies the integration). | `tst_audio_tx_input_mic_boost_vox_thresh.cpp` (toggle boost; VOX threshold recomputes) | Pre-code review §3.3 + §8.3 |

### Phase J — TxApplet additions (3 tasks)

Mic Gain slider, VOX toggle, MON toggle + monitor volume slider. Mic-source
read-only badge.

| # | Task | Tests | Cite |
|---|---|---|---|
| J.1 | TxApplet Mic Gain slider. Add slider + value label between RF Power and Tune Power (or its own row). Range from `TransmitModel::micGainDbMin/Max`. Bidirectional with `TransmitModel::micGainDb`. Show `_mic_muted` state visually (greyed when MicMute toggled off). | `tst_tx_applet_mic_gain.cpp` (default value; setter signals; mute-state visual) | Pre-code review §2.2 + master design §5.2.2 |
| J.2 | TxApplet VOX toggle button. Below Tune Power slider. Checkable. Bidirectional with `TransmitModel::voxEnabled`. Visual: green border when active. Right-click opens "VOX settings" popup with threshold/gain/hang-time sliders (lightweight `DspParamPopup` style — already a NereusSDR pattern). | `tst_tx_applet_vox_toggle.cpp` (toggle + signal + visual; popup opens on right-click) | Pre-code review §8 + master design §5.2.2 |
| J.3 | TxApplet MON toggle + monitor volume slider. Below VOX toggle. Toggle bidirectional with `TransmitModel::monEnabled`. Volume slider with value `[0..100]` mapped to float `[0.0..1.0]` for `monitorVolume`. Default volume `50` (matches `0.5f` from §0 row 9). Plus mic-source read-only badge above the gauges (`"PC mic"` / `"Radio mic"` label, no interaction). | `tst_tx_applet_mon.cpp` (toggle + signal; volume slider mapping; badge updates on micSource change) | Pre-code review §4 + §12.5 + master design §5.2.2 |

### Phase K — BandPlanGuard mode rejection for non-SSB MOX (2 tasks)

Reject MOX in non-SSB modes with tooltip + status-bar toast. SSB family
(LSB/USB/DIGL/DIGU) is the allow-list for 3M-1b.

| # | Task | Tests | Cite |
|---|---|---|---|
| K.1 | `BandPlanGuard::isModeAllowedForTx(DSPMode mode)` returns `true` for LSB / USB / DIGL / DIGU; `false` for AM / SAM / DSB / FM / DRM / CWL / CWU. `BandPlanGuard::checkMoxAllowed(...)` adds mode-check to the existing band-edge check, returns the same `(bool ok, QString reason)` signature. CW gets distinct reason `"CW TX coming in Phase 3M-2"`. AM/SAM/DSB/FM/DRM share `"AM/FM TX coming in Phase 3M-3 (audio modes)"`. | `tst_band_plan_guard_mode_allow_list.cpp` (parametrized over all DSPModes; correct allow/reject; correct reason strings) | Pre-code review §0.4 + master design §5.2 |
| K.2 | Tooltip + status-bar toast on rejected MOX. `MoxController::setMox(true)` consults `BandPlanGuard::checkMoxAllowed`; on reject, emits `moxRejected(QString reason)` and aborts. `MainWindow` slot pushes the reason to a transient status-bar toast (~3s). TxApplet MOX button gets a tooltip override matching the reason for the active mode. | `tst_band_plan_guard_mox_rejection.cpp` (CW + MOX → moxRejected with CW reason; AM + MOX → moxRejected with AM/FM reason; LSB + MOX → MOX engages) | Pre-code review §12.11 |

### Phase L — RadioModel integration + persistence (3 tasks)

Wire the new strategy-pattern owners + signal connections + AppSettings
persistence. Consolidates the cross-component wiring.

| # | Task | Tests | Cite |
|---|---|---|---|
| L.1 | `RadioModel` owns `PcMicSource` + `RadioMicSource` + `CompositeTxMicRouter`. Constructed in `RadioModel::onConnected`; destroyed in `onDisconnected`. The active `TxMicRouter` is wired into `TxChannel::setMicRouter`. Connect: `TxChannel::sip1OutputReady` → `AudioEngine::txMonitorBlockReady` (DirectConnection); `RadioConnection::micFrameDecoded` → `RadioMicSource::pushFrame` (QueuedConnection); `TransmitModel::micPreampChanged` → `TxChannel::setMicPreamp`; `TransmitModel::voxEnabledChanged` → `MoxController::setVoxEnabled`; etc. | `tst_radio_model_3m1b_ownership.cpp` (parent ownership; signal wiring verification; thread-affinity) | Pre-code review §0.3 + master design §5.2.4 |
| L.2 | AppSettings persistence per-MAC for `TransmitModel` mic/VOX/MON properties. Keys: `tx/<mac>/micGainDb`, `tx/<mac>/micBoost`, `tx/<mac>/micXlr`, `tx/<mac>/lineIn`, `tx/<mac>/lineInBoost`, `tx/<mac>/micTipRing`, `tx/<mac>/micBias`, `tx/<mac>/micPttDisabled`, `tx/<mac>/voxThresholdDb`, `tx/<mac>/voxGainScalar`, `tx/<mac>/voxHangTimeMs`, `tx/<mac>/antiVoxGainDb`, `tx/<mac>/antiVoxSourceVax`, `tx/<mac>/monEnabled`, `tx/<mac>/monitorVolume`, `tx/<mac>/micSource`. **Note:** `voxEnabled` and `monEnabled` and `micMute` do NOT persist (per §0 rows 8 and §0 row 9 — safety: VOX always loads OFF; MON always loads OFF; MicMute always loads `true` = mic in use). `micGainDb` defaults to `−6` first run per §0 row 11. | `tst_transmit_model_persistence.cpp` (round-trip each persisted key; verify no-persist for the three flags) | Pre-code review §0.3 + §12.4 + §12.5 + §12.12 |
| L.3 | Mic-source selector wiring + HL2 force. `RadioModel::onConnected` checks `BoardCapabilities::hasMicJack`; if `false`, calls `TransmitModel::setMicSource(MicSource::Pc)` and locks (HL2 force). Otherwise loads from AppSettings (default `MicSource::Pc` for new MACs). | `tst_radio_model_mic_source_hl2_force.cpp` (HL2 caps → micSource locked to Pc; non-HL2 → loaded from AppSettings or default Pc) | Pre-code review §11 |

### Phase M — Verification (7 tasks)

| # | Task | Description |
|---|---|---|
| M.1 | Unit-test sweep + build green. `cmake --build build && ctest --test-dir build --output-on-failure -j$(sysctl -n hw.ncpu)`. Baseline 170 + new ~28 = ~198 tests; all pass. No regressions. Verification step, no commit. |
| M.2 | **HL2 bench test.** Connect HL2 + dummy load. Launch app. Setup → Audio → TX Input → confirm Radio Mic radio button hidden + tooltip. Confirm PC Mic selected by default. Tune to 7.241 MHz LSB. Configure PC mic device. Run Test Mic, see VU bar. Set Mic Gain to −6 dB initial. PTT via TxApplet MOX button → speak → observe SSB carrier on test receiver. Verify TxApplet TxMic + ALC meters paint live values. Confirm clean release on MOX off. Document in verification matrix row `[3M-1b-bench-HL2]`. Commit: `test(verify): 3M-1b HL2 bench pass — PC mic SSB out`. |
| M.3 | **G2 bench test.** Connect G2 + dummy load. Tune to 7.241 MHz LSB. Configure PC mic (default), confirm SSB out. Switch to Radio Mic, configure 3.5mm (default), plug radio mic, speak, confirm SSB out. Toggle MicBoost ↔ note volume change. Toggle MicTipRing, MicBias, MicPTT-disable — verify wireshark captures show byte-50 bits flip correctly per §6.3. Switch to XLR mic (if available), enable XLR, speak, confirm SSB out. Document in verification matrix row `[3M-1b-bench-G2]`. Commit: `test(verify): 3M-1b G2 bench pass — PC + Radio mic + mic-jack bits`. |
| M.4 | **MON verification on both radios.** With MON enabled and headphones in, talk, hear self in headphones (TXA siphon mix). Verify monitor volume slider is independent of RX volume (move RX volume to 0 → no RX audio; MON audio still flows during MOX). Verify RX-leak fix: with MON disabled and MOX engaged, active slice goes silent (was bug in PR #144). Document in matrix. Commit: `test(verify): 3M-1b MON + RX-leak fix bench pass`. |
| M.5 | **P2 byte-50 polarity bench-verify.** 12 wireshark snapshots: 6 mic-jack bits × 2 states each. Compare against deskhpsdr-known polarity. Document any deltas (none expected; surface immediately if found). Commit: `test(verify): 3M-1b P2 byte-50 polarity wireshark cross-check`. |
| M.6 | Verification matrix update at `docs/architecture/phase3m-0-verification/README.md`. Add ~10 new rows (mic source switching, VOX with mic-boost-aware threshold, anti-VOX path-agnostic, MON enable + volume, RX-leak fix, mic-jack bit emission per board, HL2 force-PC, mode-rejection BandPlanGuard tooltips, PTT-source dispatch for 5 sources, P2 byte-50 polarity). Commit: `docs(3m-1b): verification matrix update`. |
| M.7 | Post-code review. Author `phase3m-1b-post-code-review.md` — for each pre-code §1-§14 + §0.3, one paragraph: did the impl match the pre-code transcription? Note any deltas. Codex P1/P2 patterns checked. Bench results summary. Follow-up issues to file. Commit: `docs(3m-1b): post-code Thetis review`. |

---

## 4. Estimated task count and ordering

**Total tasks:** ~50 (A.1, A.2, B.1-B.2, C.1-C.5, D.1-D.7, E.1-E.4, F.1-F.4, G.1-G.6, H.1-H.5, I.1-I.5, J.1-J.3, K.1-K.2, L.1-L.3, M.1-M.7).

**Strict ordering** for dependent layers:

- Phase A (docs) is the entry point; all subsequent phases depend on the
  pre-code review.
- Phase B (BoardCapabilities + provenance) before Phase G (mic-jack wire
  emission needs deskhpsdr provenance entry).
- Phase C (TransmitModel) before Phases D, H, J, L (everything reads model
  state).
- Phase D (TxChannel) before Phase E (siphon signal), before Phase F (mic
  router driving TxChannel input), before Phase L (signal wiring).
- Phase E (AudioEngine) before Phase F.1 (PcMicSource taps E.1's
  `pullTxMic`).
- Phase F (TxMicRouter implementations) before Phase L.1 (wiring).
- Phase G (wire emission) can run in parallel within itself after each
  task's base setter lands; tasks G.2-G.6 don't depend on each other.
- Phase H (MoxController) depends on Phase C + Phase D wrappers.
- Phase I (Setup page) and J (TxApplet) can run in parallel; both depend
  on Phase C.
- Phase K (BandPlanGuard) is independent.
- Phase L (RadioModel integration) after C/D/E/F/H land.
- Phase M (verification) gated on all impl tasks green.

**Per-task review cadence:** subagent-driven-development manages a two-stage
review per task (spec compliance + code quality). Fresh subagent per task;
no shared state. Expect one review checkpoint per task in chat.

---

## 5. Verification matrix delta

Updates to `docs/architecture/phase3m-0-verification/README.md`. Approximately
10 new rows + carry-forward flips:

**New rows:**

- `[3M-1b-bench-HL2]` PC mic SSB out — bench observed.
- `[3M-1b-bench-HL2]` Radio Mic radio button hidden on HL2 — bench observed.
- `[3M-1b-bench-G2]` PC mic + Radio mic switching — bench observed.
- `[3M-1b-bench-G2]` Mic-jack bits (boost/lineIn/tipRing/bias/PTT) — wireshark
  cross-check vs. deskhpsdr expected polarity.
- `[3M-1b-bench-G2]` MicXlr P2 byte-50 bit 5 — wireshark cross-check.
- `[3M-1b-bench]` MON enable + monitor volume independent — bench observed.
- `[3M-1b-bench]` RX-leak fix during MOX (active slice silenced; non-active
  unaffected) — bench observed.
- `[3M-1b]` BandPlanGuard mode rejection — unit test.
- `[3M-1b]` VOX with mic-boost-aware threshold scaling — unit test
  cross-checked against Thetis values.
- `[3M-1b]` Anti-VOX path-agnostic — unit test.
- `[3M-1b-bench]` PTT-source dispatch (MIC/CAT/VOX/SPACE/X2) — bench observed
  per source.

**Carry-forward 3M-1a rows that flip:**

- `[3M-1a]` "RX still plays during MOX (cosmetic)" → flips from "deferred"
  to "fixed in 3M-1b" (the fold-in per Decision 1).
- `[3M-1a]` "PA telemetry meters during TUN" → flips to "PA telemetry
  meters during MOX-voice TX" (now exercised under voice TX, not just TUN).

---

## 6. Rollback strategy

**Pre-merge:** "don't merge yet" — the bench-test gate is the safety belt.
Branch can keep iterating with rebase / fixup commits before PR opens.

**Post-merge (if regression surfaces in main):** revert the merge commit
on main; the 3M-1b branch can be re-rebased + fix + re-merged. 3M-1a's TX
path doesn't depend on any 3M-1b work, so reverting 3M-1b leaves TUN-only
TX functional.

**Risk-bounded follow-ups (no rollback needed):**

- If P2 byte-50 polarity diverges from deskhpsdr expected (per §M.5): patch
  in a fixup task; document the polarity correction with both
  Thetis-vs-deskhpsdr observations.
- If MON+RX-leak fold turns out to need a separate task (per pre-code §14.5):
  spin out into a Phase E.5 task; MON itself can ship with the leak still
  cosmetic.

---

## 7. Open follow-up issues to file at PR-merge time

| Item | Phase | Notes |
|---|---|---|
| Headphone-only MON toggle | 3M-3c | Speakers/headphones bus separation required |
| VAX TX integration with anti-VOX (full source-state machine) | 3M-3a | Full Thetis `console.cs:27602-27721` state-machine port |
| Atlas Penelope mic-source switch (`atlas_mic_source` per deskhpsdr) | TBD | Atlas-only; rare hardware; defer until user-requested |
| Wave-playback gain path in `CMSetTXAPanelGain1` | 3M-6 | Recording phase |
| Full DEXP knob set (attack/release/hysteresis envelopes) | 3M-3a-iii | Today's basic VOX is threshold + gain + hang-time only |

---

End of implementation plan.
