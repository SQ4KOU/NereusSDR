# Phase 3M-1c — Post-Code Thetis Review

**Status:** complete; ready for PR.
**Date:** 2026-04-29.
**Branch:** `feature/phase3m-1c-polish-persistence`.
**HEAD at review:** `50091ba` (M.6 verification matrix update; M.7 will be the very next commit, this document).
**Tests:** 236/236 green.
**Pre-code review:** [`phase3m-1c-thetis-pre-code-review.md`](phase3m-1c-thetis-pre-code-review.md).
**Plan:** [`phase3m-1c-polish-persistence-plan.md`](phase3m-1c-polish-persistence-plan.md).
**Design spec:** [`phase3m-1c-polish-persistence-design.md`](phase3m-1c-polish-persistence-design.md).
**HL2 desk-review:** [`phase3m-1c-hl2-tx-path-review.md`](phase3m-1c-hl2-tx-path-review.md).

This document closes Phase M Task M.7. For each pre-code review chunk
§1–§9 it documents whether the implementation matched the cited Thetis
behaviour, calls out any deltas, and surfaces follow-up issues to file at
PR-merge time.

---

## 0. Brainstorm-locked decisions (recap from design spec §2)

| # | Decision | Choice | Honoured? |
|---|---|---|---|
| Q1 | 3M-1c scope | 10 chunks (chunk 0 HL2 desk-review precursor + chunks 1-9) | Yes — 25 commits, every chunk landed |
| Q2 | Mic profile schema scope | Live-fields-only with Thetis column names; 1 default factory profile; SSB-only mode-family pointer | Yes — `MicProfileManager` (F.1) seeds "Default", persists ~23 keys including 8 two-tone keys (B.2/B.3 expanded the original ~20 to 23) |
| Q3 | AppSettings rename strategy | Hard cutover — no migration code | Yes — B.1 renamed 15 keys (the design said 16; one of the planned renames was dropped because `voxGainScalar` → `VOX_GainScalar` re-used the same casing pattern) |
| Q4 | Two-tone test scope | Full Thetis port — continuous + pulsed, all 6+1 user-tunable params, Setup → Test → Two-Tone page | Yes — chunks split across B.2/B.3 + E.2-E.6 + H + I + J.2; one caveat: TUN auto-stop scaffolded but `TxChannel::isTuneToneActive()` getter doesn't exist yet, so the 300 ms settle path is a TODO (see §1.2 chunk 2 below) |
| Q5 | TX recording scope | Defer to 3M-6 | Yes — no TX recording code shipped |
| Q6 | HL2 TX path desk-review | Chunk 0 (precursor) — completed 2026-04-28; absorbed 2 ship-blocking fixes | Yes — `cf93ab6` (HL2 PA scaling) + `69c4054` (HL2 TX step att 31-N inversion) landed before chunks 1-9 began |

All six locked decisions held through implementation. The single deviation
worth flagging (Q4 / TUN auto-stop) is documented as a known TODO and was
explicitly approved during code review.

---

## 1. Per-chunk implementation review

For each pre-code §1-§9, this section records what the pre-code review
predicted, what was actually implemented, deltas (if any) with rationale,
and the contributing commit SHA(s).

### 1.1 Chunk 1 — VFO Flag TX badge wire-up (pre-code §1)

**Predicted:** Wire `VfoDisplayItem::setTransmitting(bool)` (already
implemented in 3G-8) to `MoxController` MOX state changes. Slot reuses the
existing 3G-8 widget machinery (`m_transmitting`, `m_txColour`, render-
time swap). Active VFO determined by the `int rx` argument from chunk 6's
3-arg `moxChanged(int, bool, bool)` — `rx == 1` → VFO-A flag, `rx == 2` →
VFO-B flag.

**Implemented:** Chunk 1 split across two phases. Phase G (`bb51486`)
wired the basic per-flag colour swap. Phase L.3 (`6aefc6f`) added the
broadcast routing pattern (single `MoxController::moxChanged(int, bool,
bool)` connection to all `VfoDisplayItem` instances).

**Delta:** L.3 broadcasts the moxChanged payload to **all**
`VfoDisplayItem` instances rather than per-instance routing on `int rx`.
For 3M-1c's single-VFO scope this is correct (only one VFO has its slice
bound to the active TX path), but per-pan-per-VFO routing for multi-pan
3F is a TODO in the L.3 commit message. The design intent was preserved;
the implementation is a simpler form that's correct under 3M-1c
constraints.

**Tests:** `tst_vfo_display_item_tx_badge.cpp` — 222 lines covering badge
on/off, colour-swap, render-output verification.

**Risk realised:** None. Pure UI wire-up matched the pre-code prediction.
Bench verification deferred to verification matrix row 52.

### 1.2 Chunk 2 — Two-tone test full port (pre-code §2)

**Predicted:** Full Thetis port of the two-tone IMD test. 6+1 user-tunable
params, Setup → Test → Two-Tone page, mode-aware tone inversion (LSB-
family conditional sign-flip), Freq2 delay, magnitude scaling
`0.49999 * pow(10, level/20)` byte-for-byte, TUN auto-stop with 300 ms
settle, BandPlanGuard rejection in CW modes, TwoToneDrivePowerOrigin
enum (FIXED / SLIDER), Setup page presets (Defaults / Stealth), TxApplet
2-TONE button with mutual exclusion vs TUN.

**Implemented:** Chunk 2 split across **five execution phases**:
- B.2 (`10098ed`) — 7 properties on `TransmitModel`
- B.3 (`ecc456b`) — `DrivePowerSource` enum + property
- E.2-E.6 (`4be9e63`) — 12 TXA PostGen wrapper setters on `TxChannel`
- H (`1b6e1ad`) — Setup → Test → Two-Tone page (`SetupCategoryTest` +
  `TestTwoTonePage`)
- I (`687e63d`) — `TwoToneController` activation handler (the
  orchestration class)
- J.2 (`d1df70d`) — TxApplet 2-TONE button
- L.2 (`6aefc6f` + `c26358e`) — RadioModel ownership + 5 missing
  signal connects + initial pushes

**Deltas (acknowledged at review time):**

1. **B.2/B.3 default values** — option C ("Thetis Designer mostly +
   NereusSDR safer Level/Power numerics"). The pre-code review §2.3 cited
   `setup.cs:11052-11054` as the source of defaults but those line
   references read **Designer.cs initial values** (Freq1=700, Freq2=1900,
   Invert=true), which match. NereusSDR's stored defaults match the
   Designer values for Freq1/Freq2/Freq2Delay/Invert/Pulsed but
   intentionally differ on Level (-6 dB vs Designer 0 dB) and Power
   (50 % vs Designer 10 %). Both safer-for-PA values were JJ-approved at
   B.2 commit time. Visible in `TransmitModel.h:951-957`:
   `m_twoToneLevel = -6.0   // NereusSDR-original; Designer = 0 dB`.

2. **B.3 enum scope** — full 3-value `DrivePowerSource` enum
   (`DriveSlider = 0, TuneSlider = 1, Fixed = 2`) ported instead of the
   pre-code's 2-value `Fixed/Slider` simplification. The Thetis upstream
   has all three; preserving the full enum kept source-first parity and
   forward-compats with 3M-3a TUN-slider integration.

3. **E.2-E.6 cache-and-recall** — pre-code §2.4 listed
   `SetTXAPostGenTTFreq1(channel, freq)` and `SetTXAPostGenTTFreq2(channel,
   freq)` as separate WDSP setters. Reading `wdsp/gen.c:826-833` shows
   WDSP exposes a **combined-pair** function:
   `SetTXAPostGenTTFreq(channel, f1, f2)`. The implementation caches the
   last-set value of the partner and calls the combined setter, mirroring
   the Thetis `radio.cs:3697-3771 [v2.10.3.13]` cache-and-recall pattern.
   Same goes for the magnitude pair (`SetTXAPostGenTTMag(channel, m1, m2)`)
   and the pulse pair. Documented inline at `TxChannel.h:643-682` and
   `TxChannel.cpp:1429-1480`.

4. **I architectural deviation** — `TwoToneController` class instead of
   `TransmitModel::setTwoTone` handler. Orchestration-with-side-effects
   (MOX-off-first, 200 ms settle, mode-aware invert, Freq2 delay
   `QTimer::singleShot`, BandPlanGuard rejection, drive-power-origin
   branch, MOX engage) doesn't belong on a state-only model. The
   controller lives at `src/core/TwoToneController.{h,cpp}` and is owned
   by `RadioModel` (constructed in ctor, deps wired through L.2 setter).

5. **I.3 TUN auto-stop deferred** — pre-code §2.2 specified
   `QTimer::singleShot(300, ...)` for the TUN→2-Tone settle. The
   implementation found that `TxChannel::isTuneToneActive()` getter
   doesn't exist yet (the 300 ms settle is correct *behaviour* but
   needs a state-poll to know when TUN is actually finished). The
   handler scaffolds the timer and chains `continueActivation()`
   downstream; the actual `m_tuneReleaseSettleTimer` trigger is a
   **TODO(3M-1c-polish)** in `TwoToneController.cpp:167-175`. Documented
   in plan §I.3 as DONE_WITH_CONCERNS.

6. **L.2 cache live-update gap** — the L commit `6aefc6f` initially
   skipped the 5 TransmitModel→TxChannel two-tone setter connects. The
   reviewer caught the gap; fixup commit `c26358e` added the 5 connects
   plus 8 initial pushes. Two-tone params now propagate to WDSP both at
   wire-time AND on edits between tests, not just at `setActive(true)`
   time.

**Source-first invariants honoured:**
- `0.49999` magnitude scaling literal preserved verbatim
  (`TwoToneController.cpp:241`).
- `MW0LGE_21a` / `MW0LGE_22b` author tags preserved at every relevant
  callsite (10 instances in `TwoToneController.cpp`).
- LSB-family invert gate (LSB / CWL / DIGL only) implemented exactly per
  `setup.cs:11058-11062`.
- Verbatim Thetis tooltips preserved on `chkInvertTones` and
  `udFreq2Delay` Setup-page widgets (`TestTwoTonePage.cpp`).

**Tests:** ~50 tests across `tst_transmit_model_two_tone_properties.cpp`
(345 lines), `tst_transmit_model_two_tone_drive_origin.cpp` (170),
`tst_two_tone_controller.cpp` (675 lines covering all 5 activation phases
+ rejection + settle + LSB invert + Freq2-delay branches),
`tst_test_two_tone_page.cpp` (298), `tst_tx_channel_tx_post_gen_setters.cpp`
(319), `tst_tx_applet_profile_combo.cpp` (370 — covers the 2-TONE button
slice).

**Risk realised:** Medium. Pulsed-mode parameter ranges still need bench
verification with a spectrum analyser (verification matrix row 47).
`setupTwoTonePulse()` magnitude profile preserved verbatim from Thetis.

### 1.3 Chunk 3 — Mic profile schema (pre-code §3)

**Predicted:** Per-MAC AppSettings profile schema using Thetis column
names. Live fields only (~20 columns); 1 default factory profile; SSB-
only mode-family pointer. `MicProfileManager` class owns load / save /
delete / setActive. Active key at `hardware/<mac>/tx/profile/active`.

**Implemented:** Phase F (`016c2ea`). `src/core/MicProfileManager.{h,cpp}`
holds `QVector<MicProfile>` and emits `profileListChanged` /
`activeProfileChanged`. Persists to AppSettings under
`hardware/<mac>/tx/profile/<name>/...` and `hardware/<mac>/tx/profile/
active`. Mirrors Thetis `setup.cs:9545-9612` save (with comma-strip),
`setup.cs:9615-9656` delete (with last-profile guard), and the active-
pointer write semantics.

**Delta:** None of substance. A minor schema scope expansion — the
pre-code review estimated ~20 columns; the implementation persists 23
keys (15 renamed mic/VOX/MON keys + 8 two-tone keys from B.2/B.3). This
is a side-effect of executing chunks 2 and 3 in the same model, and
matches Q2's "schema-less" lock.

**Source-first invariants honoured:**
- "It is not possible to delete the last remaining TX profile" preserved
  verbatim (`MicProfileManager.cpp:272`, `TxProfileSetupPage.cpp:374`).
- Comma-strip in save flow (`name.replace(",", "_")`) preserved per
  `setup.cs:9554` for forward-compat with 3J TCI server.

**Tests:** `tst_mic_profile_manager.cpp` — 539 lines covering load /
save / delete / setActive / per-MAC isolation / first-launch seed.

**Risk realised:** Low. Schema lock held; schema-less AppSettings makes
the future 3M-3a column additions free (no migration code needed).

### 1.4 Chunk 4 — TxApplet completes (pre-code §4)

**Predicted:** Profile combo (read-only selector) + 2-TONE button on
TxApplet. Mode-family-gated combo (Thetis early-returns for DIGL/DIGU/FM/
AM/SAM — but for 3M-1c SSB-only, no gate yet). Right-click jumps to Setup
→ TX Profile page. Setup page has editing combo (`comboTXProfileName`) +
Save + Delete buttons. NO Rename button (Thetis has none; rename = Save-
new + Delete-old).

**Implemented:** Phase J (`d1df70d`):
- J.1: Profile combo on TxApplet, populated from `MicProfileManager`,
  selecting a profile triggers `setActive`, right-click opens Setup → TX
  Profile.
- J.2: 2-TONE button on TxApplet, mutually exclusive with TUN, calls
  `TwoToneController::setActive(true)`.
- J.3: New `TxProfileSetupPage` with editing combo + Save + Delete
  buttons + last-profile guard + comma-strip on save + focus-gated
  unsaved-changes prompt.
- J.4: Focus-gated unsaved-changes Yes/No/Cancel handling.

**Delta:** None. Implementation tracks pre-code §4 closely.

**Tests:** `tst_tx_applet_profile_combo.cpp` (370 lines) covering combo
populate, select, right-click; `tst_tx_profile_setup_page.cpp` (455
lines) covering save/delete/unsaved-changes flows.

**Deferred-by-design (per pre-code §4.7 decision 7):**
- `chkAutoSaveTXProfile` toggle — defer to 3M-1c follow-up polish.
- `HighlightTXProfileSaveItems` — defer to 3M-1c follow-up polish.
- 19 additional factory presets (Default DX, Digi, AM, ESSB, D-104,
  Heil PR40, etc.) — land alongside their backends in 3M-3a.

**Risk realised:** Low. Focus-gated unsaved-changes prompt was the
trickiest piece; tests confirm it doesn't fire on programmatic profile
changes.

### 1.5 Chunk 5 — Persistence audit + hard-cutover key rename (pre-code §5)

**Predicted:** Rename 16 3M-1b keys to Thetis column names. Hard cutover,
no migration. Persistence audit covers all ~30 TX-side keys (16 renamed +
7 new 2-tone + 7 new profile-related).

**Implemented:** Phase B.1 (`bd9af67`). Renamed 15 keys (the 16th —
`voxEnabled` — is in the persistence-blacklist per 3M-1b decision 8 and
needed no rename). All callsites updated; `tst_transmit_model_persistence`
asserts new names load, old names absent.

**Delta:** Pre-code review §5.2 listed 16 entries in the rename table;
B.1 commit message says "rename 15 TransmitModel keys" — the count
discrepancy is bookkeeping, not a missed rename. The persistence
blacklist (voxEnabled, monEnabled, micMute) inherited from 3M-1b is
preserved; the actually-persisted set went from 15 keys before 3M-1c to
23 keys after (15 renamed + 8 new two-tone).

**Tests:** Updated `tst_transmit_model_persistence.cpp` (now 23-key
round-trip) plus `tst_phase3m0_persistence_audit` for the broader sweep.

**Risk realised:** Low. The audit caught no orphan callsites (compile-
time enforcement via key-name typing helped). No old key names appear
post-rename.

### 1.6 Chunk 6 — MoxChangeHandlers / MoxPreChangeHandlers Qt signals (pre-code §6)

**Predicted:** Generalise existing `moxChanged(bool)` to Thetis-style
multicast signals. Rename → `moxStateChanged(bool)`; add
`moxChanging(int, bool, bool)` (pre) and new 3-arg `moxChanged(int, bool,
bool)` (post). `int rx` = `rx2_enabled && VFOBTX ? 2 : 1` (receiver
index that owns TX path).

**Implemented:** Phases C.1-C.4 across two commits:
- C.1 (`48a8234`) — finalize the rename. **Delta:** the rename was
  already 95 % complete from earlier sessions (3M-1b H.5 / 3M-1a). Only
  4 stale doc comments remained; commit `48a8234` is doc-only.
- C.2-C.4 (`516682f`) — add the multicast signals + `int rx` semantics
  + pre/post emit ordering.

**Delta:** None of behaviour. The rename being mostly-done from earlier
sessions just made C.1 a doc-only follow-up. The emit-ordering tests
confirm pre fires before any state change and post fires after all side
effects.

**Source-first invariants honoured:**
- `MW0LGE_21k8` author tag preserved on the moxChanging emit
  (`MoxController.cpp:479`).
- `MW0LGE_21a` author tag preserved on the moxChanged emits
  (`MoxController.cpp:636, 698`).
- `int rx` semantics (`activeRxForTx()` helper) returns 2 iff
  `rx2_enabled && VFOBTX`, else 1.

**Tests:** `tst_mox_controller_multicast_signals.cpp` — 274 lines
covering rename, pre-fire, post-fire, rx semantics, ordering.

**Risk realised:** Low. The signature change is compile-time enforced;
no observer was found to be silently broken.

### 1.7 Chunk 7 — B.2 TX timer push-driven refactor (pre-code §7)

**Predicted:** Replace `TxChannel::driveOneTxBlock` 5 ms QTimer with
push-driven slot connected to `AudioEngine::micBlockReady(const float*,
int)` via `Qt::DirectConnection`. Drop the `1b353f4` zero-fill workaround.
Accumulator size = 720 samples (matches Thetis `cmaster.cs:495 [v2.10.3.13]`).

**Implemented:** Phases D.1/D.2 + E.1:
- D.1/D.2 (`12130cf`) — `AudioEngine::micBlockReady` signal at
  720-sample accumulation; `clearMicBuffer` slot.
- E.1 (`63d54a5`) — `TxChannel::driveOneTxBlock` becomes a slot;
  QTimer dropped; zero-fill code path removed.
- L.4 (`6aefc6f`) — **NEW** `MicReBlocker` class to bridge
  AudioEngine's 720-sample emit to TxChannel's 256-sample contract.

**Delta (significant — surfaced during E.1 review):** The pre-code review
§7.5 decision 2 said "Accumulator size = 720 samples (matches Thetis
exactly)". Implementation kept `TxChannel::m_inputBufferSize=256`
(`TxChannel.h:990`) because **WDSP's r2-ring requires
`in_size | OUTBUFSIZE_2 == 0` and the OUTBUFSIZE_2 is 2048**. 256 divides
2048 cleanly; 720 doesn't (720 × n ≠ 2048 for any integer n). Forcing
`in_size = 720` would silently corrupt the ring boundary.

**Mitigation:** Phase L.4 introduced `src/core/audio/MicReBlocker.{h,cpp}`
to bridge the contracts:
- AudioEngine emits 720-sample blocks (matches Thetis cadence).
- MicReBlocker buffers + emits 256-sample blocks (matches WDSP r2-ring
  alignment).
- TxChannel's slot gets exactly 256 samples per push.

The bridge keeps the source-first cadence (15 ms accumulation) while
respecting WDSP's geometry. Documented inline at `MicReBlocker.h:1-40`
with worked example: "720 → emit 256, 256, leftover 208 in FIFO. Next
720 → 208+720=928 → emit 256, 256, 256, leftover 160."

**Tests:** `tst_audio_engine_mic_block_ready.cpp` (227 lines),
`tst_tx_channel_push_driven.cpp` (260), `tst_tx_channel_no_zero_fill.cpp`
(259), `tst_mic_re_blocker.cpp` (199 — covers the 720→256 boundary
arithmetic).

**Risk realised:** Medium-high. The buffer-size mismatch was the most
substantive surprise of 3M-1c; the L.4 mitigation took the form of a new
class rather than a tweak. Bench-verify with 30-min SSB transmission is
verification matrix row 24 carry-forward.

### 1.8 Chunk 8 — B.4 Initial-state-sync audit (pre-code §8)

**Predicted:** Audit pass over every signal connected in
`RadioModel::onConnected()`. For each, verify the slot fires on attach OR
the model state is explicitly pushed in the constructor. Targets: TX
monitor enable, TX monitor volume, anti-VOX gain, VOX threshold, etc.

**Implemented:** Phase K (`5c4ab13`). Audit found 2 missing pushes:
- `TransmitModel::txMonitorEnabledChanged` → `AudioEngine::setTxMonitorEnabled`
- `TransmitModel::txMonitorVolumeChanged` → `AudioEngine::setTxMonitorVolume`

The 8 other audit targets (anti-VOX gain / VOX threshold / VOX gain
scalar / VOX hang time / VOX enabled / anti-VOX source / mic mute /
mic-jack flag set) **already had correct pushes** from 3M-1b's
`1841462` precedent and from B.1 callsites. Plan tasks K.3-K.10 became
no-op verifications: the test asserts the push happens and finds it
already does.

**Delta:** Smaller scope than predicted. Pre-code §8.3 estimated ~10-15
tests; K shipped 2 new pushes + verification tests for the others (all
green).

**Tests:** Inline within `tst_audio_engine_tx_monitor_state.cpp` and the
existing initial-state-sync suite.

**Risk realised:** Low. Mechanical audit found exactly what 3M-1b's
post-code review §14b note 4 had flagged.

### 1.9 Chunk 9 — B.5 Mic-gain default refinement (pre-code §9)

**Predicted:** Bench-tune the −6 dB mic-gain default from 3M-1b across
multiple mic types. Update default if warranted.

**Implemented:** **Deferred to bench gate.** Chunk 9 is bench-driven by
design (pre-code §9.5 says "no unit tests"). The default stays at −6 dB
in the factory "Default" profile (chunk 3); if bench testing surfaces a
better default, a single-line change in `MicProfileManager::seedDefault`
updates it.

**Delta:** No code change in 3M-1c, as planned. The verification matrix
includes row 52 / 24 for the bench validation.

**Risk realised:** Low. Documentation deliverable only; deferred to the
bench gate post-merge.

---

## 2. Architectural deviations from plan

These are the deliberate deviations approved by JJ during execution. Each
has a short reasoning paragraph.

### 2.1 B.2 / B.3 default values — option C ("Designer + safer Level/Power")

The pre-code review §2.3 derived Designer defaults from `setup.cs:11052-
11054` line refs. Those references are read-points; the **initial values**
in `setup.Designer.cs` are Freq1=700 / Freq2=1900 / Level=0 dB / Power=10
%. NereusSDR adopts Designer values for Freq1/Freq2/Freq2Delay/Invert/
Pulsed but intentionally diverges on Level (-6 dB vs Designer 0 dB) and
Power (50 % vs Designer 10 %). Both NereusSDR-safer values were
JJ-approved at B.2 commit time. Visible in `TransmitModel.h:951-957`.
This is a **deliberate UX deviation** for safer first-press behaviour;
the Defaults preset button on the Setup page still restores Thetis-true
700/1900/-6/50/0/false/false (per H.3 implementation choice — not the
Thetis Designer values, the pre-code §2.7 preset values).

### 2.2 B.3 enum scope — full 3-value DrivePowerSource

Pre-code §2.4 said "TwoToneDrivePowerOrigin enum ports as
`DrivePowerSource::Fixed` and `::Slider`". The implementation ported the
**full 3-value enum** (`DriveSlider = 0, TuneSlider = 1, Fixed = 2`) per
`TransmitModel.h:142-148` and `:962`. Thetis upstream has all three;
preserving the full enum kept source-first parity and forward-compats
with 3M-3a's TUN-slider integration. The Setup page exposes only Fixed /
Slider radio buttons in 3M-1c (the TuneSlider option is hidden until 3M-3
TUN integration lands), but the model enum carries all three values.

### 2.3 C.1 finalize — rename was already done

Pre-code §6 listed the rename + multicast as one chunk (chunk 6). When
plan execution reached C.1, the audit found the rename was already
~95 % complete from 3M-1b H.5 work. Only 4 stale doc comments needed
updating (`MoxController` header refs to `moxChanged` that should have
been `moxStateChanged`). C.1 became a doc-only commit (`48a8234`), and
C.2-C.4 added the multicast signals on top.

### 2.4 E.1 buffer-size decision — kept m_inputBufferSize=256, added L.4 re-blocker

Pre-code §7.5 decision 2 said "Accumulator size = 720 samples (matches
Thetis exactly)". Implementation kept `TxChannel::m_inputBufferSize=256`
because WDSP's `r2-ring` requires `in_size | 2048` (256 divides cleanly,
720 does not). Phase L.4 introduced the new `MicReBlocker` class to
bridge AudioEngine's 720-sample emit to TxChannel's 256-sample contract.
This preserves Thetis's 15 ms producer cadence while respecting WDSP's
geometry. **Significant deviation; documented in §1.7 above.**

### 2.5 E.2-E.6 cache-and-recall — combined-pair WDSP function

Pre-code §2.4 listed individual setters
(`SetTXAPostGenTTFreq1(channel, freq)` and `SetTXAPostGenTTFreq2`).
Reading `wdsp/gen.c:826-833` showed WDSP exposes a combined-pair function
`SetTXAPostGenTTFreq(channel, f1, f2)`. The implementation caches the
last-set value of the partner and calls the combined setter — mirrors
the `radio.cs:3697-3771 [v2.10.3.13]` cache-and-recall pattern.
Documented inline at `TxChannel.h:643-682`.

### 2.6 I architectural deviation — TwoToneController class

Pre-code §2 framed the two-tone activation as a `TransmitModel::setTwoTone`
handler. Implementation chose to put the orchestration in a dedicated
class (`TwoToneController` at `src/core/TwoToneController.{h,cpp}`)
because the activation walk has multi-stage state (MOX-off-first, 200 ms
settle, mode-aware invert, Freq2 delay timer, BandPlanGuard rejection,
drive-power-origin branch, MOX engage) and side effects against multiple
collaborators (TransmitModel, MoxController, TxChannel, BandPlanGuard).
A state-only model setter wouldn't be the right home. RadioModel owns
the controller (constructed in ctor, deps wired through L.2 setter).

### 2.7 I.3 TUN auto-stop — scaffolded with TODO

Pre-code §2.2 specified `QTimer::singleShot(300, ...)` for the TUN→2-Tone
settle. Implementation found that `TxChannel::isTuneToneActive()` getter
doesn't exist yet (the 300 ms settle is correct *behaviour* but needs a
state-poll to know when TUN is finished). The activation walk scaffolds
the timer and chains `continueActivation()` downstream; the actual
`m_tuneReleaseSettleTimer` trigger is a **TODO(3M-1c-polish)** at
`TwoToneController.cpp:167-175`. Approved at I commit review as
DONE_WITH_CONCERNS; bench testing will confirm whether this matters in
practice (TUN-then-2-TONE rapid sequences are rare).

### 2.8 L.4 MicReBlocker — new class

Mitigation for §2.4 above. New class
`src/core/audio/MicReBlocker.{h,cpp}` bridges AudioEngine's 720-sample
emit to TxChannel's 256-sample contract. NereusSDR-original; documented
with worked example in the header. 199 lines of test in
`tst_mic_re_blocker.cpp`.

### 2.9 TxChannel virtuals — 13 setters made virtual for test mocking

The two-tone activation tests need a `RecordingTxChannel` mock that
captures setter calls. The 12 TXPostGen setters from E.2-E.6 plus
`setTxPostGenTTPulseFreq` (later renamed `setTxPostGenTTPulsePeriod` in
test contracts) were marked `virtual` to support test seam. Inline
comment at `TxChannel.h:667-680`:
`'virtual' for the I.1 TwoToneController test seam — TestableTxChannel`.
The use of `virtual` in production code is narrowly scoped (test
mocking only) and documented at every site.

---

## 3. Source-first invariant audit

These confirm key invariants per CLAUDE.md were preserved end-to-end.

### 3.1 Magnitude scaling literal

`0.49999` preserved verbatim from `setup.cs:11056 [v2.10.3.13]`.
Reference at `TwoToneController.cpp:241`:
```cpp
const double ttmag1 = 0.49999 * std::pow(10.0, ttLevel / 20.0);
// setup.cs:11056 [v2.10.3.13]
```

### 3.2 Author tag preservation

| Tag | Preserved at | Source upstream |
|---|---|---|
| `MW0LGE_21a` | `TwoToneController.cpp:151, 192, 305, 315, 357, 455, 458` (10× total) | `console.cs:44728-44760` and elsewhere |
| `MW0LGE_22b` | `TwoToneController.cpp:331, 332, 460` | `console.cs:44755-44757` and elsewhere |
| `MW0LGE_21k8` | `MoxController.cpp:92, 330, 367, 468, 479` | `console.cs:29324, 30083, 30090` |
| `MW0LGE_21a` (second context) | `MoxController.cpp:97, 633, 698` | `console.cs:29677` |
| `MI0BOT: HL2` | `safety_constants.cpp:101` (already from chunk 0 fixup) | `console.cs:25195-25199` |
| `MI0BOT: Greater range for HL2` | `P1CodecHl2.cpp:155` (already from chunk 0 fixup) | `console.cs:10657-10663` |

### 3.3 Verbatim Thetis tooltips

- `chkInvertTones` Setup-page tooltip — verbatim from Thetis `setup.cs`
  per pre-code §2.5.
- `udFreq2Delay` Setup-page tooltip — verbatim per pre-code §2.5.
- "It is not possible to delete the last remaining TX profile" — verbatim
  in `MicProfileManager.cpp:272` and `TxProfileSetupPage.cpp:374` per
  pre-code §4.4.

### 3.4 LSB-family invert gate

Implementation honours the conditional (`mode in {LSB, CWL, DIGL}` only)
exactly per `setup.cs:11058-11062`. Confirmed by
`tst_two_tone_controller.cpp` LSB-invert test cases.

### 3.5 Inline cite stamp grammar

Cite stamps consistent throughout 3M-1c:
- `[v2.10.3.13]` — tag-aligned values (Thetis chunks 1-9).
- `[@501e3f51]` — Thetis post-tag commits (none used in 3M-1c — none
  were needed; spot-check confirmed).
- `[@120188f]` — deskhpsdr cross-checks (used in chunk 7 cross-check, no
  ports).
- `[v2.10.3.13-beta2]` / `[@c26a8a4]` — mi0bot-Thetis (chunk 0 ship-
  blocking absorptions, plus inline comments in `safety_constants.cpp`
  and `P1CodecHl2.cpp`).

---

## 4. Code-quality minor notes (consolidated for follow-up)

These are the polish items surfaced during the per-task two-stage code
reviews (16 items). Group by origin file; recommend rolling them into a
single follow-up branch (`3m-1c-polish`) post-merge.

### 4.1 Phase F (MicProfileManager)

1. `src/core/MicProfileManager.cpp:37` — dead
   `constexpr const char* kProfileSubpath` constant (never referenced).
   Delete or wire into AppSettings path construction.
2. `src/core/MicProfileManager.cpp:240-244` — empty-name handling order.
   Comma-strip happens before empty-check, so `","` saves as `"_"`.
   Document or invert order.
3. `MicProfileManager::load()` precondition undocumented for callers.
   Operations are storage-backed; `load()` is only required for first-
   launch seeding. Add a doc comment.
4. `tests/tst_mic_profile_manager.cpp:342` — unused local `QString
   lastWarning`.

### 4.2 Phase H (Setup → Test → Two-Tone page)

5. `src/gui/setup/TestTwoTonePage.cpp:103-104` —
   `setSingleStep(0.001)` on Level spinbox with -96..0 range = 96000 steps
   via arrow click. UX check warranted (user might want 0.5 dB steps).
6. Test gap (minor) — no explicit "Model→UI doesn't loop back" test (the
   idempotent setter makes this safe but it's not tested).
7. `src/gui/setup/TestTwoTonePage.cpp:225-239` —
   `selectDriveRadioFor()`'s defensive `if (!target) return` is
   unreachable given the switch covers all enum values. Compiler may
   warn under strict flags.

### 4.3 Phase I (TwoToneController)

8. `src/core/TwoToneController.cpp:108-156` — re-entrancy during 200 ms
   cool-down is bounded but not idempotent. A second `setActive(true)`
   call wastefully restarts the settle window. Cleaner guard:
   `if (m_activationInFlight && on) return;`.
9. `src/core/TwoToneController.cpp:380-383` — synchronous-rejection
   escape route (`if (!m_activationInFlight) return;`) is subtle;
   clearer with explicit `m_rejectedDuringActivation` flag.
10. `src/core/TwoToneController.h:265-298` — explicit
    `enum class State { Idle, ActivatingMoxRelease,
    ActivatingTuneRelease, Active, Deactivating }` would replace four
    implicit-state booleans. Refactor candidate.
11. **TUN auto-stop completion** (already noted as DONE_WITH_CONCERNS) —
    expose `TxChannel::isTuneToneActive()` getter so the 300 ms settle
    timer can chain into `continueActivation()` properly.

### 4.4 Phase J (TxApplet + TX Profile page)

12. `src/gui/applets/TxApplet.cpp:1057, 1097` —
    `disconnect(m_micProfileMgr, nullptr, this, nullptr)` is too broad;
    future patches adding other connections to the manager would
    silently lose them. Track via `QMetaObject::Connection` handles.
13. `src/gui/applets/TxApplet.h:150-151` — stale comment ("hidden until
    Phase 3M-3 (out-of-phase)") now incorrect since J.2 made the
    2-TONE button visible. Remove.
14. `src/gui/setup/TxProfileSetupPage.cpp:43, 76` — visual nit: page
    title and group box both labeled "TX Profile", causing redundant
    header stack.
15. Test J.1.2 coupling — `setMicProfileManager_populatesCombo` asserts
    `combo->count() >= 2` instead of `== 2`. Tighten.

### 4.5 Phase L (cross-cutting + re-blocker)

16. L.3 broadcasts moxChanged payload to **all** VfoDisplayItem
    instances — correct for single-VFO 3M-1c scope. Already documented
    as TODO[3F] for multi-pan per-instance routing.
17. L.2 doesn't live-update WDSP TXPostGen setters during an active
    two-tone test — deferred per plan caveat to 3M-3a (live edits during
    a running test would need WDSP atomicity work).

---

## 5. Bench rows still pending

Per verification matrix §3M-1c rows 46-52, the following bench rows
require hardware and are deferred to JJ:

| Row | Bench scope | Tag |
|---|---|---|
| 46 | Two-tone continuous mode at 14.200 LSB; spectrum analyser shows two clean tones at +700 / +1900 Hz; 3rd-order intermod products visible | `[3M-1c-bench-G2]` |
| 47 | Two-tone pulsed mode; pulse cadence audible / visible on spectrum analyser | `[3M-1c-bench-G2]` |
| 48 | LSB-family invert: tones flip on Invert toggle in LSB; no change in USB | `[3M-1c-bench-G2]` |
| 49 | BandPlanGuard rejection in CW: 2-TONE button visually un-checks; toast shown | `[3M-1c-bench]` |
| 50 | Profile save / load / restart-app round-trip on G2 | `[3M-1c-bench-G2]` |
| 51 | Mic-jack flag persistence in profile (G2 + wireshark cross-check) | `[3M-1c-bench-G2]` |
| 52 | VFO Flag TX badge live colour swap during MOX | `[3M-1c-bench]` |

Plus carry-forward 3M-1b row 24 (PC mic SSB out — re-verify with the
push-driven TX timer + MicReBlocker bridge), and 3M-1c chunk 9 mic-gain
bench-tune (deferred-by-design to bench).

---

## 6. Follow-up issues to file at PR-merge time

Combined from chunk 0 desk-review (B1-B8) + deferred-by-design items +
key minor notes from §4.

### 6.1 HL2-specific (B1-B8 from chunk 0 desk-review)

| # | Item | Target phase | Notes |
|---|---|---|---|
| B1 | HL2 RX-side step attenuator `31 - N` inversion | 3M-1c follow-up | RX-only, out of TX scope; verify mi0bot console.cs:11075/11251/19380 applies to NereusSDR HL2 RX path |
| B2 | HL2 LRAudioSwap bench-verify | 3M-1c bench follow-up | Implement only if HL2 TX audio is silent / swapped without it |
| B3 | HL2 CWX bit-3 `cwx_ptt` wire emission | 3M-2 (CW TX) | Defer with epic |
| B4 | HL2 PureSignal sample-rate matching | 3M-4 | Defer with epic |
| B5 | HL2 per-band PA reference gains table | 3M-1c follow-up | mi0bot clsHardwareSpecific.cs:766-795 |
| B6 | HL2 TX power slider 15-step quantisation | 3M-3 (TX processing) | Defer with epic |
| B7 | Cross-check HL2 antenna routing vs mi0bot Alex.cs | 3M-1c follow-up audit | NereusSDR's 3P-I-a (PR #116) may already handle |
| B8 | Extended discovery fields FixedIpHL2 / EeConfigHL2 | post-3M | HL2 polish epic |

### 6.2 3M-1c polish (consolidated minor-notes from §4)

| # | Item | Notes |
|---|---|---|
| P1 | `chkAutoSaveTXProfile` auto-save toggle | Pre-code §4.7 decision 7; defer to a later polish pass |
| P2 | `HighlightTXProfileSaveItems` toggle | Pre-code §4.5; nice-to-have polish |
| P3 | TwoToneController re-entrancy guard refactor + explicit State enum | Minor notes 8-10 above |
| P4 | TUN auto-stop completion (expose `TxChannel::isTuneToneActive()`) | Minor note 11 above; was DONE_WITH_CONCERNS at I commit |
| P5 | TxApplet disconnect-by-handle audit | Minor note 12 above |
| P6 | Setup-page UX polish (Level spinbox step, redundant TX Profile heading) | Minor notes 5, 14 above |
| P7 | Dead `kProfileSubpath` constant + unused test local | Minor notes 1, 4 above |
| P8 | Test tightening (combo count `==` vs `>=`, model→UI loop-back coverage) | Minor notes 6, 15 above |

### 6.3 Deferred to specific future phases

| Item | Phase |
|---|---|
| 19 additional factory profiles (DX/Contest, ESSB, D-104, PR40, CFC etc.) | 3M-3a-i / -ii / -iii (land alongside their backends) |
| 4 separate active-profile pointers (comboTXProfile / Dig / FM / AM) | 3M-3b |
| Per-mode DSP buffer / filter size / type columns | 3M-3a-i / -ii / -iii |
| TX recording (mic-side WAV output) | 3M-6 |
| DVK applet record-backend | 3M-6 |
| Live WDSP TXPostGen setter updates during active two-tone test | 3M-3a (atomicity work) |
| Multi-pan per-instance VFO badge routing (TODO[3F]) | 3F |

Recommendation: roll P1-P8 into a single `3m-1c-polish` follow-up
branch post-merge; B1, B5, B7 into a `hl2-rx-and-pa-polish` branch.
B3/B4/B6 file as separate issues tagged for their target phase.

---

## 7. Risk surface review (recap from pre-code §10)

Each cross-cutting risk identified in pre-code §10, with realised
outcome:

| # | Risk | Mitigation realised |
|---|---|---|
| R1 | Two-tone pulsed mode parameter ranges | Cross-checked `wdsp/gen.c:826-833` for combined-pair function; bench-verify with spectrum analyser is row 47 |
| R2 | Push-driven TX timer regression | TDD harness (E.1 tests + L.4 MicReBlocker tests) covers underrun / overrun; bench-verify is row 24 carry-forward |
| R3 | Hard-cutover key rename leaves orphans | Persistence audit test (`tst_phase3m0_persistence_audit`) asserts old key names absent post-rename — passes |
| R4 | MoxController callsite migration breaks observers | Compile-time enforced (signature change); test sweep (`tst_mox_controller_multicast_signals.cpp`) catches behavioural regressions |
| R5 | HL2 path divergence | Chunk 0 desk-review absorbed 2 critical fixes (PA scaling + TX step att inversion); 8 follow-ups filed; bench rows confirm |
| R6 | `setupTwoTonePulse()` magnitude profile non-trivial | Pulse profile uses Thetis defaults verbatim; cross-check with deskhpsdr was unnecessary (Thetis was clear) |
| R7 | Profile schema scope creep | Live-fields-only locked at Q2; 3M-3a sub-PRs add their slices via schema-less AppSettings |

**One additional risk surfaced during execution** (not in pre-code §10):
the **buffer-size mismatch** between Thetis's 720-sample mic accumulator
and WDSP's r2-ring 256-sample alignment. Mitigated by L.4 MicReBlocker
class. Documented in §1.7 and §2.4 above.

---

## 8. Test count

Pre-code estimated ~46 new unit tests across chunks 1-9. Actual:

| Phase | New test files | Approx new test cases |
|---|---|---|
| B (TransmitModel rename + 2-tone properties + enum) | 2 new files (`tst_transmit_model_two_tone_properties.cpp`, `tst_transmit_model_two_tone_drive_origin.cpp`) + extensions | ~20 |
| C (MoxController multicast) | 1 new file (`tst_mox_controller_multicast_signals.cpp`) | ~12 |
| D (AudioEngine micBlockReady) | 1 new file (`tst_audio_engine_mic_block_ready.cpp`) | ~8 |
| E (TxChannel push-driven + TXA PostGen wrappers) | 2 new files (`tst_tx_channel_push_driven.cpp`, `tst_tx_channel_no_zero_fill.cpp`, plus `tst_tx_channel_tx_post_gen_setters.cpp`) | ~22 |
| F (MicProfileManager) | 1 new file (`tst_mic_profile_manager.cpp`) | ~25 |
| G (VFO TX badge) | 1 new file (`tst_vfo_display_item_tx_badge.cpp`) | ~10 |
| H (Setup page) | 1 new file (`tst_test_two_tone_page.cpp`) | ~10 |
| I (TwoToneController) | 1 new file (`tst_two_tone_controller.cpp`) | ~25 |
| J (TxApplet + TX Profile page) | 2 new files (`tst_tx_applet_profile_combo.cpp`, `tst_tx_profile_setup_page.cpp`) | ~22 |
| K (initial-state-sync audit) | extensions to existing files | ~8 |
| L (cross-cutting + re-blocker) | 1 new file (`tst_mic_re_blocker.cpp`) | ~10 |
| **Total new test files** | **~13** | **~170 new test cases** |
| **ctest target count progression** | 222 (3M-1b baseline) → 236 (3M-1c HEAD) | +14 ctest targets |

The +14 ctest target delta (vs the ~13 new test files) lines up with
existing-file extensions counted as the same ctest target. The internal
QTest case count expansion (~170) is well beyond the pre-code estimate
of ~46 — most of the inflation is the controller activation walk in
`tst_two_tone_controller.cpp` (every state-machine branch gets its own
parametrised case) and the MicProfileManager round-trip / per-MAC
isolation tests.

All 236/236 tests pass at the M.6 commit (`50091ba`).

---

## 9. Commit map

25 commits on `feature/phase3m-1c-polish-persistence`, in chronological
order:

| # | SHA | Phase | Summary |
|---|---|---|---|
| 1 | `81ff57e` | A | docs: design spec |
| 2 | `a8fb2d3` | A | docs: chunk 0 design addition |
| 3 | `a79a158` | A | docs: chunk 0 HL2 desk-review deliverable |
| 4 | `cf93ab6` | A | fix: HL2 PA scaling absorption (Bug 2) |
| 5 | `69c4054` | A | fix: HL2 TX step att 31-N inversion (Bug 1) |
| 6 | `45ae619` | A | docs: pre-code Thetis review for chunks 1-9 |
| 7 | `4a17b05` | A | fix: correct chunk 1 attribution (AetherSDR + NereusSDR-native) |
| 8 | `81ccb7e` | A | docs: implementation plan with TDD task list |
| 9 | `bd9af67` | B.1 | feat: rename 15 TransmitModel keys to Thetis column names |
| 10 | `10098ed` | B.2 | feat: add 7 two-tone test properties |
| 11 | `ecc456b` | B.3 | feat: add DrivePowerSource enum + property |
| 12 | `48a8234` | C.1 | docs: finalize moxChanged → moxStateChanged rename (4 stale refs) |
| 13 | `516682f` | C.2-C.4 | feat: multicast Pre/Post MOX state-change signals |
| 14 | `12130cf` | D.1/D.2 | feat: AudioEngine micBlockReady signal + clearMicBuffer |
| 15 | `63d54a5` | E.1 | feat: TxChannel push-driven refactor (drop QTimer + zero-fill) |
| 16 | `4be9e63` | E.2-E.6 | feat: 12 TXA PostGen wrapper setters |
| 17 | `016c2ea` | F | feat: MicProfileManager class |
| 18 | `bb51486` | G | feat: VFO Flag TX badge wire-up + Phase L routing demo |
| 19 | `1b6e1ad` | H | feat: Setup → Test → Two-Tone page |
| 20 | `687e63d` | I | feat: TwoToneController activation handler (I.1-I.5) |
| 21 | `d1df70d` | J | feat: TxApplet 2-TONE button + profile combo + Setup TX Profile page |
| 22 | `5c4ab13` | K | fix: initial-state-sync audit (TX monitor enable + volume) |
| 23 | `6aefc6f` | L | feat: cross-cutting wiring + 720→256 mic re-blocker |
| 24 | `c26358e` | L | fix: Phase L fixup — add 5 missing 2-tone signal connects + initial pushes |
| 25 | `50091ba` | M.6 | docs: verification matrix update (22 new rows) |

This document (M.7) will be commit #26.

---

## 10. Next steps

1. **Open PR as DRAFT** per `feedback_no_public_posts_without_review`.
   Title: `feat(3m-1c): polish & persistence — VFO badge, two-tone test,
   mic profiles, push-driven TX timer`. Body draft in chat first; await
   "post it".
2. **Bench-test rows 46-52** post-merge per the verification matrix.
   Specifically:
   - Row 46-48: Two-tone continuous + pulsed + LSB-invert at 14.200 LSB.
   - Row 49: BandPlanGuard rejection in CW.
   - Row 50-51: Profile save / load / restart on G2 + wireshark.
   - Row 52: VFO Flag badge live colour swap.
3. **Spawn `3m-1c-polish` branch** for the consolidated minor-notes
   follow-up (P1-P8 from §6.2). Estimated ~1 day.
4. **File 8 HL2 follow-up issues** (B1-B8 from §6.1) at PR-merge time.
5. **Re-verify carry-forward 3M-1b row 24** (PC mic SSB out) under the
   new push-driven TX timer + MicReBlocker bridge before declaring
   3M-1 epic shipped.

After 3M-1c merges and bench rows are green, **3M-2 (CW TX) is the next
major epic** per the master design plan.

---

## 11. Conclusion

3M-1c shipped 25 commits across 13 phases (A-L + M.6), 13 new test files,
~170 new QTest cases (236/236 ctest targets). All Thetis sources cited
with `[v2.10.3.13]` stamps; mi0bot-Thetis cited with `[v2.10.3.13-beta2]`
and `[@c26a8a4]` stamps; deskhpsdr cited with `[@120188f]` where used.

**Significant deviations from the pre-code review:**

1. **Buffer-size mismatch** (§1.7 / §2.4) — kept TxChannel input buffer
   at 256 samples for WDSP r2-ring alignment; added L.4 MicReBlocker
   class to bridge AudioEngine's Thetis-true 720-sample emit to the 256-
   sample contract.
2. **TwoToneController class** (§2.6) — orchestration-with-side-effects
   moved off TransmitModel into a dedicated controller.
3. **Combined-pair WDSP function** (§2.5) — `SetTXAPostGenTTFreq(channel,
   f1, f2)` instead of pre-code's separate setters; cache-and-recall
   pattern mirrors `radio.cs:3697-3771`.
4. **TUN auto-stop deferred** (§2.7) — DONE_WITH_CONCERNS; needs
   `TxChannel::isTuneToneActive()` getter from a polish follow-up.

**Source-first invariants honoured end-to-end:** `0.49999` literal,
LSB-invert gate, `MW0LGE_*` author tags (15+ instances), `MI0BOT:`
tags from chunk 0, verbatim Thetis tooltips, "It is not possible to
delete the last remaining TX profile" string, comma-strip on save.

**8 HL2 follow-up issues to file** (B1-B8) at PR-merge time. **8 polish
items** (P1-P8) consolidated into a recommended `3m-1c-polish` branch
post-merge. **7 deferred-by-design items** flagged for their target
future phases.

The bench-test gate (rows 46-52 + carry-forward row 24) is the safety
belt before 3M-1 epic-complete declaration. 3M-2 (CW TX) is the next
major epic.

---

## 12. Phase 3M-1c bench regression + architectural pivot (2026-04-29 amendment)

**Status:** PR draft paused; redesign plan landed; execution deferred
to a fresh session per the discipline of catching cross-thread Qt
subtleties without compounding-error fatigue.

### 12.1 What happened

Within an hour of the M.7 commit (`8b6a4fb`), JJ took the build to the
bench (Saturn G2, dummy load) and reported: **"1 tone tune stopped
working"**.

The investigation surfaced a regression-cascade that turned out to be
architectural, not a tuning issue:

1. **Initial diagnosis (correct):** E.1 (commit `63d54a5`) had
   dropped `TxChannel`'s 5 ms `QTimer` that was the production-side
   pump for `m_micRouter->pullSamples` → `PcMicSource::pullSamples` →
   `AudioEngine::pullTxMic`. After E.1, **nothing called `pullTxMic`
   in production**; the 720-sample accumulator (D.1) never filled,
   `micBlockReady` never fired, the L.4 `MicReBlocker` never delivered,
   and `TxChannel::driveOneTxBlock` never ran. Both TUN and SSB voice
   TX were silent. The 236/236 ctest suite passed because tests
   exercised `driveOneTxBlock` directly with synthetic samples,
   never the production pump.

2. **Bench-fix attempt A** (commit `418e155`, JJ option B from a
   bench-pause): added a 5 ms `QTimer` in AudioEngine that calls
   `pullTxMic`. Restores TUN. Also added a 5 ms silence-drive timer
   in TxChannel for the no-mic case (`pullTxMic` returns 0 when
   `m_txInputBus` is null).

3. **Bench regression #2:** JJ at the bench (post-`418e155`) reports
   TUN works, but **"ssb audio with the mic all the way down sounds
   distorted and gravelly"**.

4. **Diagnosis #2:** the silence-drive timer (8 ms stale threshold)
   fires *during the natural mic-driven burst gap*. AudioEngine emits
   `micBlockReady` every ~15 ms (when accumulator fills); MicReBlocker
   re-chunks 720 → 256 and fires `driveOneTxBlock` 2-3 times in <1 ms;
   then 14 ms idle until next emit. The silence timer's 8 ms threshold
   triggers during that 14 ms gap, adding an extra `fexchange2` call
   per 15 ms cycle (~4 calls vs ~2.81 expected). Output rate runs
   ~42 % over the radio's 192 kHz expectation → SPSC ring overflows
   and silence frames interleave with mic audio → audible "gravelly"
   distortion.

5. **Bench-fix attempt C** (commit `03ec584`): bumped silence-drive
   threshold from 8 ms to 20 ms. Stops mid-burst-gap firing but does
   NOT address the root architectural mismatch.

6. **Architectural review** (this section + sibling plan doc):
   re-read Thetis `cmaster.c` / `cmbuffs.c` and deskhpsdr's
   `transmitter.c`. Found that **the entire pump model adopted in
   D.1 / E.1 / L.4 is built on a misread of Thetis's
   `cmInboundSize[5]=720`**.

### 12.2 The misread

Pre-code review §0.5 locked: *"Thetis uses 720 samples @ 48 kHz =
15 ms (cmaster.cs:495). NereusSDR's TxChannel accumulator can match
Thetis exactly (720). Locking 720 samples for source-first parity."*

This was wrong. `cmInboundSize[5]=720` is the **inbound network block
size from the radio** for stream 5 (mic input) — what arrives in a
P1 EP6 mic byte zone or P2 mic packet. It is not the DSP block size.

Thetis's actual DSP block size is `xcm_insize = getbuffsize(rate) = 64`
samples at 48 kHz (per `cmsetup.c:106-110 [v2.10.3.13]`). `cmaster`
**re-chunks 720 → 64 internally on the network side** before
fexchange0; the DSP side runs at the much smaller 64-sample cadence
(every 1.33 ms).

The proper Thetis invariant is:
```
r1_outsize == xcm_insize == WDSP in_size
   (all = getbuffsize(rate); 64 at 48 kHz)
```

NereusSDR's attempt to mirror "720" as the DSP block size required a
re-blocker (L.4) to bridge to the actual WDSP block size (256 in
NereusSDR, dictated by the WDSP r2-ring's 2048-divisor constraint).
That re-blocker creates a burst pattern (2-3 fexchange calls every
~15 ms) that fights the radio's expected steady output cadence.

### 12.3 The corrected design

Single uniform block size end-to-end (matching Thetis's
`r1_outsize == xcm_insize == in_size` invariant). For NereusSDR:
**256 samples** (not 720, not 64) — the value already used by
TxChannel's `m_inputBufferSize`, dictated by WDSP r2-ring divisibility.

Pump driver: dedicated `TxWorkerThread` (QThread). Mirrors Thetis's
`cm_main` worker-thread pattern at `cmbuffs.c:151-168 [v2.10.3.13]`,
adapted to Qt's event-loop conventions. Worker thread runs a 5 ms
QTimer; each tick:

```cpp
void onPumpTick() {
    static thread_local std::array<float, 256> buf;
    const int got = audioEngine->pullTxMic(buf.data(), 256);
    if (got < 256) {
        // Partial / null bus — zero-fill the gap.
        // Silence path falls out for free; no separate timer.
        std::fill(buf.begin() + got, buf.end(), 0.0f);
    }
    txChannel->driveOneTxBlock(buf.data(), 256);
}
```

`fexchange2` runs on TxWorkerThread (off main, off audio callback).
Steady cadence (one fexchange per 5 ms tick = ~192 kHz output rate).
**No re-blocker. No silence-drive timer. No 720-sample accumulator.**

### 12.4 What gets undone in the redesign

D.1 (`micBlockReady` signal + 720-sample accumulator), D.2
(`clearMicBuffer`), L.4 (`MicReBlocker` class entirely), bench-fix-A
(`AudioEngine::pumpMic` timer, commit `418e155`), bench-fix-B
(TxChannel silence-drive timer, refined in `03ec584`).

D.1 / E.1 / L.4 leave behind a small refactor footprint
(slot-signature on `driveOneTxBlock(samples, frames)` is kept — it's
how the worker now invokes the channel) but the surrounding
infrastructure is deleted.

### 12.5 What the redesign preserves

- Phase B (TransmitModel rename + 7 two-tone properties + DrivePowerSource enum) — unaffected.
- Phase C (MoxController multicast Pre/Post signals) — unaffected.
- Phase E.2-E.6 (12 TXA PostGen wrappers, cache-and-recall pattern) — unaffected.
- Phase F (MicProfileManager) — unaffected.
- Phase G (VFO Flag TX badge wire-up) — unaffected.
- Phase H (Setup → Test → Two-Tone page) — unaffected.
- Phase I (TwoToneController activation handler) — its connections
  to TxChannel slots may become Queued (since TxChannel moves to
  TxWorkerThread); the class itself doesn't change.
- Phase J (TxApplet 2-TONE button + profile combo + Setup TX Profile
  page) — unaffected.
- Phase K (initial-state-sync audit) — unaffected.
- Phase L.1, L.2, L.3 — unaffected. Only L.4 (MicReBlocker) goes away.

### 12.6 Why a fresh session

Cross-thread Qt is subtle. `moveToThread`, `Qt::QueuedConnection`,
parameter-mutation safety, lifecycle on disconnect — these are
exactly the issues that ship as race conditions if rushed in a tired
session. The redesign is well-scoped (~300 lines new, ~200 deleted)
but needs proper TDD + two-stage review + bench-verify discipline.

The current branch is `28 commits ahead of origin/main` after this
amendment + plan doc commit. The git log shows the journey clearly:
build the wrong thing → bench regression → diagnose → architectural
review → plan doc → fresh-session execution → ship clean. Future
maintainers reading the history get the architectural lesson for
free, which is more valuable than a clean log that hides the
back-and-forth.

### 12.7 Plan handoff

Complete implementation spec: `phase3m-1c-tx-pump-architecture-plan.md`.
Includes Thetis cite line numbers, deskhpsdr comparison, full file
list (add/modify/delete), test plan, subagent dispatch prompt,
bench-row acceptance criteria.

---

## 13. 2026-04-29 redesign execution status

The plan from §12 was executed in a focused subagent session on
2026-04-29.  Outcome:

**Files added:** `src/core/TxWorkerThread.{h,cpp}`,
`tests/tst_tx_worker_thread.cpp` (8 cases — lifecycle, cadence,
zero-fill gap, cross-thread setter race).

**Files modified:**
- `src/core/TxChannel.{h,cpp}` — silence-drive timer +
  `m_lastDriveTimer` + `onSilenceTimer()` deleted; `m_running`
  becomes `std::atomic<bool>`.
- `src/core/AudioEngine.{h,cpp}` — `kMicBlockFrames` accumulator +
  `clearMicBuffer` + `micBlockReady` signal + `m_micPumpTimer` +
  `pumpMic()` deleted; `pullTxMic` returns to a pure drain.
- `src/models/RadioModel.{h,cpp}` — `MicReBlocker` construction +
  `AudioEngine::micBlockReady` connection deleted; replaced with
  `std::unique_ptr<TxWorkerThread> m_txWorker` lifecycle managed in
  `connectToRadio` / `teardownConnection`.
- `tests/tst_tx_channel_push_driven.cpp` — silence-timer assertion
  dropped (timer no longer exists).

**Files deleted:**
- `src/core/audio/MicReBlocker.{h,cpp}` — class no longer needed.
- `tests/tst_audio_engine_mic_block_ready.cpp` — obsolete.
- `tests/tst_mic_re_blocker.cpp` — obsolete.

**Test result:** 235/235 passing (236 baseline minus 2 dropped plus
1 added).  Stable under `-j8` parallel runs (verified across two
full-suite executions back-to-back).

**Bench verification:** deferred to next user session.  Acceptance
rows from `phase3m-0-verification/README.md`:
- TUN clean carrier on dummy load
- SSB voice TX low mic gain — clean (was gravelly)
- SSB voice TX normal mic gain — clean speech reproduction
- Mic-mute / no-mic / TUN-without-mic — TUN clean carrier
- 30-min SSB transmission — no zero-filled frames / glitches /
  SPSC overflows

**Source-first audit confirmed:**
- `xcm_insize == r1_outsize == in_size` invariant from Thetis
  `cmaster.c:460-487 [v2.10.3.13]` reflected in NereusSDR's
  `kBlockFrames = 256 == TxChannel::m_inputBufferSize ==
  fexchange2 in_size` end-to-end.
- `cm_main` worker-thread pattern from `cmbuffs.c:151-168
  [v2.10.3.13]` reflected in `TxWorkerThread::run` + QTimer-driven
  `onPumpTick`.
- `cmInboundSize[5]=720` correction documented in this file (§12)
  and in `phase3m-1c-thetis-pre-code-review.md` §0.5 lock #4.

---

End of post-code review.
