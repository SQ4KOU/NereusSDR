# Phase 3M-1c — Polish & Persistence: Implementation Plan

**Status:** ready for execution.
**Date:** 2026-04-28.
**Branch:** `feature/phase3m-1c-polish-persistence`.
**Base:** `origin/main` @ `bfce1cf` (post-3M-1b merge).
**Pre-code review:** [`phase3m-1c-thetis-pre-code-review.md`](phase3m-1c-thetis-pre-code-review.md).
**Design spec:** [`phase3m-1c-polish-persistence-design.md`](phase3m-1c-polish-persistence-design.md).
**HL2 desk-review:** [`phase3m-1c-hl2-tx-path-review.md`](phase3m-1c-hl2-tx-path-review.md).
**Master design:** [`phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.3.
**Risk profile:** Low–Medium per design spec §9.

> **For agentic workers:** REQUIRED SUB-SKILL — use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wrap Phase 3M-1 with the polish layer per design spec.
Implements 9 chunks (chunk 0 desk-review + 2 fixup commits already
landed). After 3M-1c ships, the radio TX path is production-quality
SSB voice (LSB / USB / DIGL / DIGU) on Saturn G2 / Anan-G2 with HL2
path verified against mi0bot-Thetis.

**Tech Stack:** C++20, Qt6, WDSP (TXA pipeline already constructed in
3M-1a/1b), AppSettings (XML persistence).

---

## 0. Brainstorm-locked decisions (recap)

Per design spec §2 + pre-code review §0.5. All decisions resolved
before plan-writing.

| # | Decision | Choice |
|---|---|---|
| Q1 | Scope | 10 chunks (chunk 0 = HL2 desk-review precursor; chunks 1-9 = main implementation) |
| Q2 | Mic profile schema | Live-fields-only with Thetis column names; 1 default factory profile; SSB-only mode-family pointer |
| Q3 | AppSettings rename strategy | Hard cutover (no migration code) |
| Q4 | Two-tone test scope | Full Thetis port — continuous + pulsed, all 6+1 user-tunable params, Setup → Test → Two-Tone page |
| Q5 | TX recording scope | Defer to 3M-6 |
| Q6 | HL2 desk-review | Chunk 0 — completed; 2 ship-blocking fixes already absorbed at `cf93ab6` and `69c4054` |

**Open items locked during pre-code review (§0.5):**

1. VFO TX badge: single colour swap (NO per-mode palette — Thetis has none)
2. Profile combo: NO Rename button (Thetis pattern: Save-new + Delete-old)
3. MoxChanged `int rx` arg = receiver index that owns TX path (2 iff rx2_enabled && VFOBTX, else 1)
4. TX accumulator: 720 samples (matches Thetis exactly per `cmaster.cs:495 [v2.10.3.13]`)
5. MoxController observer count: framework signal wired for future subscribers (3M-7, 3J, 3M-4)

---

## 1. Workflow shape

**Single branch, single PR.** All commits on
`feature/phase3m-1c-polish-persistence`.

**Commit shape:**

- ✅ Commits 1-5 already landed:
  - `81ff57e` design spec
  - `a8fb2d3` chunk 0 design addition
  - `a79a158` chunk 0 HL2 desk-review deliverable
  - `cf93ab6` chunk 0 fixup: HL2 PA scaling absorption (Bug 2)
  - `69c4054` chunk 0 fixup: HL2 TX step att inversion (Bug 1)
  - `45ae619` pre-code Thetis review for chunks 1-9
- ✅ Commit 6: this plan.
- Commits 7..N: TDD task commits (one per task, see §3).
- Commit N+1: Verification matrix update (3M-1c rows).
- Commit N+2: Post-code review (`phase3m-1c-post-code-review.md`).
- PR open: drafted in chat first per `feedback_no_public_posts_without_review`,
  opened only on explicit "post it" sign-off.

**Per-task discipline (rigid TDD, mirrors 3M-1b):**

1. State the pre-code review section being implemented.
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
post-tag; `[v2.10.3.13-beta2]` or `[@c26a8a4]` for mi0bot-Thetis. Verifier
`scripts/verify-inline-tag-preservation.py` runs in pre-commit hook chain
— `NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis` must be set in shell before
each commit.

**Bench-test gate:** Manual matrix rows are NOT blocking PR open (3M-1c
is mostly UI / persistence). Bench rows complete after merge as JJ has
hardware time. The auto-tested code paths gate PR review.

---

## 2. Files

**New (5):**

- `src/core/MicProfileManager.{h,cpp}` — profile bank load/save/delete/
  set-active (chunk 3)
- `src/gui/setup/TestTwoTonePage.{h,cpp}` — Setup → Test → Two-Tone page
  (chunk 2)
- `src/gui/setup/SetupCategoryTest.{h,cpp}` — Setup → Test category
  parent (chunk 2)
- `tests/tst_mic_profile_manager_*.cpp` — ~6 test files for chunk 3
- `tests/tst_two_tone_*.cpp` — ~8 test files for chunk 2

**Modified (~12):**

- `src/core/MoxController.{h,cpp}` — chunk 6: rename `moxChanged(bool)` →
  `moxStateChanged(bool)`; add `moxChanging(int, bool, bool)` and new
  3-arg `moxChanged(int, bool, bool)` signals; migrate ~30 callsites
- `src/models/TransmitModel.{h,cpp}` — chunks 2 + 3 + 5: add 7 two-tone
  properties; rename 16 keys to Thetis column names (hard cutover); wire
  `MicProfileManager` association
- `src/core/TxChannel.{h,cpp}` — chunk 7: replace QTimer-driven
  `driveOneTxBlock` with push-driven slot; add `setTxPostGen*` setters
  for 2-tone (~12 setters)
- `src/core/AudioEngine.{h,cpp}` — chunk 7: add `micBlockReady(const
  float*, int)` signal emitted on 720-sample accumulation
- `src/core/RadioModel.{h,cpp}` — chunk 8 + cross-cutting: initial-state
  sync audit + push current values after each connect()
- `src/gui/applets/TxApplet.{h,cpp}` — chunk 4: profile combo + 2-TONE
  button + right-click jump to Setup
- `src/gui/setup/TransmitSetupPages.cpp` — chunk 4: TX Profile section
  with Save / Delete buttons
- `src/gui/meters/VfoDisplayItem.{h,cpp}` — chunk 1: wire
  `setTransmitting(bool)` to `MoxController::moxStateChanged`; route
  badge to active VFO based on `int rx`
- `src/gui/setup/SetupDialog.cpp` — chunk 2: register Setup → Test
  category + Two-Tone page

**Tests (new, ~46):** itemised inline per task.

---

## 3. TDD task list

Tasks numbered as `<phase>.<index>`. Each is one commit. Phase letters
match 3M-1b pattern, decomposed for TDD.

### Phase A — Pre-code documentation (already landed)

| # | Task | Status |
|---|---|---|
| A.1 | Design spec (`phase3m-1c-polish-persistence-design.md`) | **landed @ `81ff57e` + `a8fb2d3`** |
| A.2 | Chunk 0 deliverable (`phase3m-1c-hl2-tx-path-review.md`) | **landed @ `a79a158`** |
| A.3 | Chunk 0 fixup: HL2 PA scaling (Bug 2) | **landed @ `cf93ab6`** |
| A.4 | Chunk 0 fixup: HL2 TX step att inversion (Bug 1) | **landed @ `69c4054`** |
| A.5 | Pre-code Thetis review (`phase3m-1c-thetis-pre-code-review.md`) | **landed @ `45ae619`** |
| A.6 | This plan (`phase3m-1c-polish-persistence-plan.md`) | **in-progress** |

### Phase B — TransmitModel rename + 2-tone properties (chunks 2 + 5)

Pure model state. No WDSP, no UI. Each property has a setter that emits
a Qt signal; persistence to `AppSettings` happens here in B.4.

| # | Task | Tests | Cite |
|---|---|---|---|
| B.1 | Rename 16 3M-1b keys to Thetis column names per pre-code §5.2. Hard cutover (no migration code). Update setters and AppSettings load/save paths to use new names. | `tst_transmit_model_thetis_key_rename.cpp` (16 round-trip cases; old names absent post-write) | Pre-code §5.2 |
| B.2 | Add 7 two-tone properties to `TransmitModel`: `TwoToneFreq1` (700 Hz default), `TwoToneFreq2` (1900 Hz), `TwoToneLevel` (-6 dBm), `TwoTonePower` (50%), `TwoToneFreq2Delay` (0 ms), `TwoToneInvert` (false), `TwoTonePulsed` (false). All with setter signals + persistence. | `tst_transmit_model_two_tone_properties.cpp` (defaults, setter signals, range clamping) | Pre-code §2.3 + Thetis `setup.cs:11019-11200ish [v2.10.3.13]` |
| B.3 | Add `TwoToneDrivePowerOrigin` enum (`Fixed` / `Slider`) property to `TransmitModel`. | `tst_transmit_model_two_tone_drive_origin.cpp` (enum round-trip; signals fire) | Pre-code §2.3 + Thetis `setup.cs:11111-11119` |

### Phase C — MoxController generalisation (chunk 6)

Generalise existing `moxChanged(bool)` to Thetis-style multicast signals.

| # | Task | Tests | Cite |
|---|---|---|---|
| C.1 | Rename existing `moxChanged(bool)` → `moxStateChanged(bool)` in `MoxController`. Migrate ~30 call-sites — compile-time error-loud. | `tst_mox_controller_state_changed_rename.cpp` (existing call-sites still receive on/off bool via renamed signal) | Pre-code §6.1 |
| C.2 | Add new `moxChanging(int rx, bool oldMox, bool newMox)` signal — fires BEFORE state change. Subscribers can defensively freeze state. | `tst_mox_controller_mox_changing_pre_emit.cpp` (signal-spy: pre-fire happens before any setMox side effects) | Pre-code §6.3 + Thetis `console.cs:29324 [v2.10.3.13]` |
| C.3 | Add new `moxChanged(int rx, bool oldMox, bool newMox)` signal — fires AFTER full state change. Replaces the old single-arg signal at the boundary. | `tst_mox_controller_mox_changed_post_emit.cpp` (signal-spy: post-fire happens after audio path flip) | Pre-code §6.4 + Thetis `console.cs:29677 [v2.10.3.13]` |
| C.4 | Implement `int rx` argument: `rx2_enabled && VFOBTX ? 2 : 1`. The receiver index that owns the TX path. | `tst_mox_controller_rx_argument_semantics.cpp` (parametrised over rx2_enabled × VFOBTX combinations) | Pre-code §6.5 + Thetis `console.cs:29324, 29677` |

### Phase D — AudioEngine micBlockReady signal (chunk 7 part 1)

Add the push-driven mic block accumulator + signal.

| # | Task | Tests | Cite |
|---|---|---|---|
| D.1 | Add `AudioEngine::micBlockReady(const float* samples, int frames)` Qt signal. Backed by a 720-sample accumulator buffer (matches Thetis `cmaster.cs:495 [v2.10.3.13]`). Fires on every full 720-sample block. | `tst_audio_engine_mic_block_ready.cpp` (signal fires on accumulation; not on partial; 720-sample size matches Thetis exactly) | Pre-code §7.2 + Thetis `cmaster.cs:493-497` |
| D.2 | `AudioEngine::clearMicBuffer()` slot to drop accumulated samples on MOX-off (avoid stale audio in the next TX cycle). | `tst_audio_engine_clear_mic_buffer.cpp` (post-clear, accumulator returns 0 frames) | NereusSDR-original |

### Phase E — TxChannel push-driven refactor + 2-tone setters (chunks 7 part 2, 2 part)

Replace QTimer with push-driven slot; add WDSP setters for 2-tone.

| # | Task | Tests | Cite |
|---|---|---|---|
| E.1 | Replace `TxChannel::driveOneTxBlock` QTimer with slot connected to `AudioEngine::micBlockReady` via `Qt::DirectConnection`. Drop the `1b353f4` zero-fill workaround. | `tst_tx_channel_push_driven.cpp` (driveOneTxBlock only fires via signal, no QTimer); `tst_tx_channel_no_zero_fill.cpp` (zero-fill code path removed) | Pre-code §7.5 |
| E.2 | Add `TxChannel::setTxPostGenMode(int)` setter (0=off, 1=continuous, 7=pulsed) wiring `WDSP.SetTXAPostGenMode`. | `tst_tx_channel_tx_post_gen_mode.cpp` | Pre-code §2.4 + Thetis `setup.cs:11084, 11096` |
| E.3 | Add 4 continuous-mode setters: `setTxPostGenTTFreq1`, `setTxPostGenTTFreq2`, `setTxPostGenTTMag1`, `setTxPostGenTTMag2`. | `tst_tx_channel_two_tone_continuous_setters.cpp` (each setter wires its WDSP target) | Pre-code §2.4 + Thetis `setup.cs:11097-11105` |
| E.4 | Add 4 pulsed-mode setters: `setTxPostGenTTPulseToneFreq1`, `setTxPostGenTTPulseToneFreq2`, `setTxPostGenTTPulseMag1`, `setTxPostGenTTPulseMag2`. | `tst_tx_channel_two_tone_pulsed_setters.cpp` | Pre-code §2.4 + Thetis `setup.cs:11085-11092` |
| E.5 | Add 3 pulse-profile setters: `setTxPostGenTTPulsePeriod`, `setTxPostGenTTPulseDuration`, `setTxPostGenTTPulseTransition` (called by `setupTwoTonePulse()` equivalent). | `tst_tx_channel_two_tone_pulse_profile.cpp` | Pre-code §2.7 + Thetis `setup.cs:setupTwoTonePulse()` (need to find exact line) |
| E.6 | Add `TxChannel::setTxPostGenRun(bool)` setter wiring `WDSP.SetTXAPostGenRun(channel, on ? 1 : 0)`. | `tst_tx_channel_tx_post_gen_run.cpp` | Pre-code §2.4 + Thetis `setup.cs:11107` |

### Phase F — MicProfileManager (chunk 3)

Profile bank: load / save / delete / set-active.

| # | Task | Tests | Cite |
|---|---|---|---|
| F.1 | Create `src/core/MicProfileManager.{h,cpp}`. Load / save / delete / setActive operations on per-MAC AppSettings. Hold `QVector<MicProfile>` and emit `profileListChanged` / `activeProfileChanged` signals. | `tst_mic_profile_manager_load_save.cpp` (round-trip a profile; AppSettings keys match expected) | Pre-code §3.1 |
| F.2 | Implement save flow with overwrite confirmation + comma-strip. Mirror Thetis `btnTXProfileSave_Click` (`setup.cs:9545-9612 [v2.10.3.13]`) including `name = name.replace(",", "_")` for TCI safety. | `tst_mic_profile_manager_save_overwrite.cpp` (existing name → confirmation dialog) | Pre-code §4.3 + Thetis `setup.cs:9545-9612` |
| F.3 | Implement delete flow with last-profile guard. Mirror Thetis `btnTXProfileDelete_Click` (`setup.cs:9615-9656 [v2.10.3.13]`). String "It is not possible to delete the last remaining TX profile" preserved verbatim. | `tst_mic_profile_manager_delete.cpp` (cannot delete last; confirmation dialog) | Pre-code §4.4 + Thetis `setup.cs:9615-9656` |
| F.4 | Set-active flow: writes `hardware/<mac>/tx/profile/active = <name>` and applies all live fields to TransmitModel. | `tst_mic_profile_manager_active_pointer.cpp` (active key round-trip; TransmitModel state updated) | Pre-code §3.4 |
| F.5 | First-launch creates "Default" profile with documented default values per the live-field subset (chunk 3 §3.2). | `tst_mic_profile_manager_default_seed.cpp` (first launch creates Default; subsequent launches don't overwrite) | Pre-code §3.4 |
| F.6 | Per-MAC isolation. Two MACs have independent profile lists; no cross-talk. | `tst_mic_profile_manager_per_mac_isolation.cpp` | Pre-code §3.4 |

### Phase G — VfoDisplayItem TX badge wire (chunk 1)

Wire the existing 3G-8 widget machinery to MoxController. **AetherSDR
pattern** (VFO Flag widget) + **NereusSDR-native** (`setTransmitting`
API + colour-swap behaviour). No Thetis port — per memory
`feedback_source_first_ui_vs_dsp`, source-first governs DSP/radio only;
Qt widgets are NereusSDR-native.

| # | Task | Tests | Source |
|---|---|---|---|
| G.1 | Wire `VfoDisplayItem::setTransmitting(bool)` to `MoxController::moxStateChanged(bool)`. The colour-swap behaviour is already implemented in 3G-8 (`VfoDisplayItem.h:83/96/122/131`); chunk 1 just wires the slot. | `tst_vfo_display_item_tx_badge.cpp` (`setTransmitting(true)` → render uses `m_txColour`) | NereusSDR-native (3G-8); AetherSDR `VfoWidget.{h,cpp}` pattern reference |
| G.2 | Route badge to active VFO based on `int rx` arg from chunk 6 `moxChanged(int, bool, bool)`. `rx == 1` → VFO-A's `VfoDisplayItem`; `rx == 2` → VFO-B's. | `tst_vfo_display_item_active_vfo_routing.cpp` | Pre-code §1.3 decision 2; `int rx` semantic from chunk 6 (Thetis `console.cs:29324, 29677` as the *origin* of the 3-arg signature, not for the VFO Flag widget itself) |

### Phase H — Setup → Test → Two-Tone page (chunk 2 part)

New Setup category + Two-Tone page.

| # | Task | Tests | Cite |
|---|---|---|---|
| H.1 | Create `SetupCategoryTest.{h,cpp}` — new Setup → Test category parent. First test page; future siblings: PA scaling, IMD analyzer, calibration probes. | `tst_setup_category_test_registers.cpp` (category appears in Setup tree) | NereusSDR-original |
| H.2 | Create `TestTwoTonePage.{h,cpp}` — 6 user-tunable params + Pulsed checkbox + Defaults / Stealth preset buttons + DrivePowerSource radio buttons. Bidirectional sync with `TransmitModel::TwoTone*` properties. | `tst_test_two_tone_page_layout.cpp` (all controls present); `tst_test_two_tone_page_sync.cpp` (model ↔ UI bidirectional) | Pre-code §2.7 |
| H.3 | "Defaults" button restores: Freq1=700, Freq2=1900, Level=-6, Power=50, Freq2Delay=0, Invert=false, Pulsed=false. "Stealth" button restores compliance-friendly defaults (look up Thetis `btnTwoToneF_stealth` for exact values). | `tst_test_two_tone_page_presets.cpp` | Pre-code §2.7 + Thetis `btnTwoToneF_defaults`, `btnTwoToneF_stealth` |

### Phase I — Two-tone test handler (chunk 2 part)

Wire the on/off button to the WDSP setter chain.

| # | Task | Tests | Cite |
|---|---|---|---|
| I.1 | Implement `TransmitModel::setTwoTone(bool)` handler. Mirrors Thetis `chkTestIMD_CheckedChanged` (`setup.cs:11019-11200ish [v2.10.3.13]`): power-check, MOX-off-first, mode-aware invert, pulsed/continuous branch, magnitude scaling `0.49999 * pow(10, level/20)`, set-power-using-targetdBm, MOX engage. | `tst_two_tone_continuous.cpp` + `tst_two_tone_pulsed.cpp` + `tst_two_tone_invert_modes.cpp` + `tst_two_tone_magnitude_scaling.cpp` (4 tests covering all branches) | Pre-code §2.2-§2.6 |
| I.2 | Freq2 delay implementation: when `TwoToneFreq2Delay > 0`, magnitude2 set to 0.0 initially, then to `ttmag2` after `Task.Delay(N)` ms. Use `QTimer::singleShot` for the delay. | `tst_two_tone_freq2_delay.cpp` (Freq2 magnitude is 0 initially when delay > 0; set after delay) | Pre-code §2.5 + Thetis `setup.cs:11102-11105` |
| I.3 | TUN auto-stop: pressing 2-TONE while TUN is on stops TUN first with 300 ms delay. Mirror Thetis `chk2TONE_CheckedChanged` (`console.cs:44728-44760 [v2.10.3.13]`). | `tst_two_tone_tun_auto_stop.cpp` | Pre-code §2.2 |
| I.4 | DrivePowerSource enum: FIXED uses `TwoTonePower`; SLIDER uses console PWR slider. | `tst_two_tone_drive_power_origin.cpp` | Pre-code §2.4 + Thetis `setup.cs:11111-11119` |
| I.5 | BandPlanGuard interaction: 2-TONE rejected in CW modes (CWL/CWU) per 3M-1b K.1 SSB-mode allow-list. | `tst_two_tone_band_plan_guard.cpp` | 3M-1b post-code §K.1 |

### Phase J — TxApplet additions (chunk 4)

Profile combo + 2-TONE button on TxApplet, plus Setup → TX Profile
buttons.

| # | Task | Tests | Cite |
|---|---|---|---|
| J.1 | Profile combo (read-only selector) on TxApplet. Wired to `MicProfileManager`. Selecting a profile triggers `setActive`. Right-click opens Setup → TX Profile page. | `tst_tx_applet_profile_combo_select.cpp` + `tst_tx_applet_combo_right_click.cpp` | Pre-code §4.7 + Thetis `console.cs:30594-30605, 44519-44522` |
| J.2 | 2-TONE button on TxApplet. Mutually exclusive with TUN. Calls `TransmitModel::setTwoTone(true)`. Visual feedback (button-state + tooltip). | `tst_tx_applet_two_tone_button.cpp` | Pre-code §4.6 |
| J.3 | Setup → TX Profile page: editing combo (`comboTXProfileName`) + Save + Delete buttons. Wired to `MicProfileManager`. | `tst_setup_tx_profile_save.cpp` + `tst_setup_tx_profile_delete.cpp` (covers full save/delete flows) | Pre-code §4.3-§4.4 |
| J.4 | Focus-gated unsaved-changes prompt on Setup combo selection change. Yes/No/Cancel handling. | `tst_setup_tx_profile_unsaved_prompt.cpp` | Pre-code §4.5 + Thetis `setup.cs:9505-9543` |

### Phase K — Initial-state-sync audit (chunk 8)

Audit pass over `RadioModel::onConnected()` connect calls; push current
values where the slot didn't fire on attach.

| # | Task | Tests | Cite |
|---|---|---|---|
| K.1 | Audit + fix `TransmitModel::txMonitorEnabledChanged` → `AudioEngine::setTxMonitorEnabled`. Push current value after `connect()`. | `tst_initial_state_sync_tx_monitor_enabled.cpp` | Pre-code §8.3 + 3M-1b `1841462` |
| K.2 | Audit + fix `TransmitModel::txMonitorVolumeChanged` → `AudioEngine::setTxMonitorVolume`. | `tst_initial_state_sync_tx_monitor_volume.cpp` | Pre-code §8.3 |
| K.3 | Audit + fix `TransmitModel::antiVoxGainChanged` → `MoxController::setAntiVoxGain`. | `tst_initial_state_sync_anti_vox_gain.cpp` | Pre-code §8.3 |
| K.4 | Audit + fix `TransmitModel::voxThresholdChanged` → `MoxController::setVoxThreshold`. | `tst_initial_state_sync_vox_threshold.cpp` | Pre-code §8.3 |
| K.5 | Audit + fix `TransmitModel::voxGainScalarChanged` → `MoxController::setVoxGainScalar`. | `tst_initial_state_sync_vox_gain_scalar.cpp` | Pre-code §8.3 |
| K.6 | Audit + fix `TransmitModel::voxHangTimeChanged` → `MoxController::setVoxHangTime`. | `tst_initial_state_sync_vox_hang_time.cpp` | Pre-code §8.3 |
| K.7 | Audit + fix `TransmitModel::voxEnabledChanged` → `MoxController::setVoxEnabled`. | `tst_initial_state_sync_vox_enabled.cpp` | Pre-code §8.3 |
| K.8 | Audit + fix `TransmitModel::antiVoxSourceVaxChanged` → `MoxController::setAntiVoxSourceVax`. | `tst_initial_state_sync_anti_vox_source.cpp` | Pre-code §8.3 |
| K.9 | Audit + fix `TransmitModel::micMuteChanged` → `TxChannel::setMicMute`. | `tst_initial_state_sync_mic_mute.cpp` | Pre-code §8.3 |
| K.10 | Audit + fix all mic-jack flag attaches (`micBoost`, `micXlr`, `lineIn`, `lineInBoost`, `micTipRing`, `micBias`, `micPttDisabled`). | `tst_initial_state_sync_mic_jack_flags.cpp` (single test covering all 7 flags) | Pre-code §8.3 |

### Phase L — RadioModel cross-cutting wiring

Wire the new `MicProfileManager` ownership + the new chunk-1c-specific
attach paths.

| # | Task | Tests | Cite |
|---|---|---|---|
| L.1 | `RadioModel` owns `MicProfileManager`. Constructed in `RadioModel::onConnected`; destroyed in `onDisconnected`. Connect: `MicProfileManager::activeProfileChanged` → `TransmitModel::loadProfile`; `TransmitModel::*Changed` (16 properties) → `MicProfileManager::saveActive` (auto-save) — gated by chkAutoSaveTXProfile (defer; see pre-code §4.7 decision 7). | `tst_radio_model_3m1c_ownership.cpp` (lifecycle + ownership) | Pre-code §3.4 |
| L.2 | Wire chunk-1c-specific 2-tone attaches: `TransmitModel::twoToneFreq1Changed` → `TxChannel::setTxPostGenTTFreq1`, etc. (~12 connect calls, all initial-state-synced). | `tst_radio_model_two_tone_wiring.cpp` (all 2-tone attaches push current values) | Pre-code §2.4 |
| L.3 | Wire VFO badge: `MoxController::moxChanged(int, bool, bool)` → `VfoDisplayItem::setTransmittingForRx(int rx, bool active)`. | (Covered by G.1 / G.2 tests) | Pre-code §1.3 |

### Phase M — Verification

| # | Task | Description |
|---|---|---|
| M.1 | Unit-test sweep + build green. `cmake --build build && ctest --test-dir build --output-on-failure -j$(sysctl -n hw.ncpu)`. Baseline 222 + new ~46 = ~268 tests; all pass. No regressions. Verification step, no commit. |
| M.2 | **HL2 bench test (deferred to JJ).** VFO TX badge appears + clears cleanly during MOX. 2-TONE continuous + pulsed at 14.200 LSB; spectrum analyser shows two clean tones at ±700 / ±1900 Hz. Profile save / load / restart-app round-trip. Document in matrix `[3M-1c-bench-HL2]`. |
| M.3 | **G2 bench test (deferred to JJ).** Same matrix as HL2, plus mic-jack flag persistence in profile. Document in `[3M-1c-bench-G2]`. |
| M.4 | **Push-driven TX timer 30-min SSB transmission (deferred to JJ).** No zero-filled frames in audio capture. Bench-verify with audio recorder. |
| M.5 | **Mic-gain bench-tune (chunk 9, deferred to JJ).** Across USB headset / dynamic / condenser / built-in mic. If default changes, update factory "Default" profile. |
| M.6 | Verification matrix update at `docs/architecture/phase3m-0-verification/README.md`. Add ~7 new rows (VFO badge, 2-tone continuous, 2-tone pulsed, profile save/load, push-driven TX timer, mic-gain bench, persistence audit round-trip). Commit: `docs(3m-1c): verification matrix update`. |
| M.7 | Post-code review. Author `phase3m-1c-post-code-review.md` — for each pre-code §, did the impl match the cited Thetis behaviour? Note any deltas. Bench results summary. Follow-up issues to file. Commit: `docs(3m-1c): post-code Thetis review`. |

---

## 4. Estimated task count and ordering

**Total tasks:** ~46 (A.1-A.6 done + B.1-B.3 + C.1-C.4 + D.1-D.2 +
E.1-E.6 + F.1-F.6 + G.1-G.2 + H.1-H.3 + I.1-I.5 + J.1-J.4 + K.1-K.10 +
L.1-L.3 + M.1-M.7).

**Strict ordering** for dependent layers:

- ✅ Phase A (docs) is the entry point and is complete.
- Phase B (TransmitModel) before Phases C, D, E, F, J, L (everything
  reads model state).
- Phase C (MoxController) before Phase G (VFO badge subscribes to
  moxStateChanged).
- Phase D (AudioEngine signal) before Phase E (TxChannel slot).
- Phase E (TxChannel WDSP setters) before Phase I (two-tone handler
  wires the setters).
- Phase F (MicProfileManager) before Phase J (TxApplet profile combo
  consumes it).
- Phase H (Setup page skeleton) before Phase I (handler attaches Setup
  controls).
- Phase J (TxApplet) and H (Setup page) can run in parallel.
- Phase K (initial-state-sync audit) can run in parallel with B-J once
  L.1 lands the ownership wiring.
- Phase L (RadioModel integration) after C/D/E/F/G land.
- Phase M (verification) gated on all impl tasks green.

**Parallelism opportunity:** Phases B, C, D, F, H, K can all run in
parallel by separate sub-agents within `superpowers:subagent-driven-
development`. Phase E depends on B + D; Phase I depends on B + E + H;
Phase J depends on B + F + H; Phase G depends on C; Phase L depends on
B + C + D + E + F + G.

**Per-task review cadence:** subagent-driven-development manages a two-
stage review per task (spec compliance + code quality). Fresh subagent
per task; no shared state. Expect one review checkpoint per task in chat.

---

## 5. Verification matrix delta

Updates to `docs/architecture/phase3m-0-verification/README.md`. ~7 new
rows (5 manual bench + 2 unit-test gate):

**New rows (manual bench):**

- `[3M-1c-bench-HL2]` VFO TX badge appears + clears cleanly during MOX.
- `[3M-1c-bench-HL2]` Two-tone continuous mode at 14.200 LSB; spectrum
  analyser shows clean tones.
- `[3M-1c-bench-HL2]` Two-tone pulsed mode; spectrum analyser shows
  pulse cadence.
- `[3M-1c-bench-HL2]` Profile save / load / restart-app round-trip.
- `[3M-1c-bench-G2]` Mic-jack flag persistence in profile.
- `[3M-1c-bench]` Push-driven TX timer: 30-minute SSB transmission, no
  zero-filled frames.
- `[3M-1c-bench]` Mic-gain bench-tune across mic types.

**New rows (unit-test):**

- `[3M-1c]` Persistence audit: ~30 keys round-trip; 16 old 3M-1b key
  names absent.
- `[3M-1c]` MoxChangeHandlers Pre + Post emit ordering verified.

**Carry-forward 3M-1b rows (no change required from 3M-1c):** M.2 / M.3
/ M.4 / M.5 stay open in the matrix; independent of 3M-1c.

---

## 6. Rollback strategy

**Pre-merge:** "don't merge yet" — the bench-test gate is the safety
belt. Branch can keep iterating with rebase / fixup commits before PR
opens.

**Post-merge (if regression surfaces in main):** revert the merge commit
on main; the 3M-1c branch can be re-rebased + fix + re-merged. 3M-1a/1b
TX path doesn't depend on any 3M-1c work, so reverting 3M-1c leaves SSB
voice TX functional.

**Risk-bounded follow-ups (no rollback needed):**

- If push-driven TX timer (chunk 7 / Phase E) introduces a subtle
  latency or starvation issue: revert E.1 only and stay on the QTimer
  pull model + zero-fill workaround. Other chunks unaffected.
- If MoxController signal generalisation (Phase C) breaks an unforeseen
  observer: revert C.1-C.4; switch back to single-arg `moxChanged(bool)`.
  G/L wiring would need a rollback too.
- If profile schema (Phase F) needs a column added mid-development:
  AppSettings is schema-less, so adding keys is free.

---

## 7. Open follow-up issues to file at PR-merge time

Combined from chunk 0 deliverable (B1-B8) + chunks 1-9 (where surfaced):

| Item | Phase | Notes |
|---|---|---|
| HL2 RX-side step attenuator `31 - N` inversion (B1 from chunk 0) | 3M-1c follow-up | RX-only out of TX scope; verify mi0bot console.cs:11075/11251/19380 applies to NereusSDR HL2 RX path |
| HL2 LRAudioSwap bench-verify (B2 from chunk 0) | 3M-1c bench follow-up | Implement only if HL2 TX audio is silent / swapped without it |
| HL2 CWX bit-3 `cwx_ptt` wire emission (B3 from chunk 0) | 3M-2 (CW TX) | Defer with epic |
| HL2 PureSignal sample-rate matching (B4 from chunk 0) | 3M-4 | Defer with epic |
| HL2 per-band PA reference gains table (B5 from chunk 0) | 3M-1c follow-up | mi0bot clsHardwareSpecific.cs:766-795 |
| HL2 TX power slider 15-step quantisation (B6 from chunk 0) | 3M-3 (TX processing) | Defer with epic |
| Cross-check HL2 antenna routing vs mi0bot Alex.cs (B7 from chunk 0) | 3M-1c follow-up audit | NereusSDR's 3P-I-a may already handle |
| Extended discovery fields FixedIpHL2 / EeConfigHL2 (B8 from chunk 0) | post-3M | HL2 polish epic |
| chkAutoSaveTXProfile auto-save toggle | 3M-1c follow-up | Pre-code §4.7 decision 7; defer to a later polish pass |
| HighlightTXProfileSaveItems toggle | 3M-1c follow-up | Pre-code §4.5; nice-to-have polish |
| 19 additional factory profiles (DX/Contest, ESSB, D-104, PR40, CFC etc.) | 3M-3a-i / -ii / -iii | Land alongside their backends |

---

End of implementation plan. TDD execution next.
