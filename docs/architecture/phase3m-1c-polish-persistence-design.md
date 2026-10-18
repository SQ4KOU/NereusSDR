# Phase 3M-1c — Polish & Persistence: Design Spec

**Status:** approved by JJ, ready for pre-code Thetis review.
**Date:** 2026-04-28.
**Branch:** `feature/phase3m-1c-polish-persistence`.
**Base:** `origin/main` @ `bfce1cf` (post-3M-1b merge).
**Master design:** [`phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.3.
**Risk:** Low–Medium (1 desk-review chunk + 3 Low chunks + 3 Low chunks + 3 Medium chunks = 10 chunks total).

> **For agentic workers:** REQUIRED SUB-SKILL — use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement the plan task-by-task once
> the pre-code review and plan documents are written.

---

## 1. Goal

Wrap Phase 3M-1 with the polish layer: HL2 TX path desk-review vs
mi0bot-Thetis (precursor — the bench rows M.2/M.3/M.4 are
HL2-hardware-gated; desk-review against the authoritative HL2 reference
catches divergence before hardware does), VFO Flag TX badge wiring, full-
Thetis two-tone test port (continuous + pulsed), mic profile schema using
Thetis column names (live fields only), TxApplet profile-combo polish,
persistence audit with hard-cutover key rename, generalised
`MoxChangeHandlers` / `MoxPreChangeHandlers` Qt signals matching Thetis
multicast delegate semantics, plus three carry-forward fixups from 3M-1b.
TX recording **deferred** to 3M-6 where it lives natively alongside its
three siblings (I/Q recording, scheduled recording, DVK record-backend).

Phase 3M-1c **completes 3M-1**. After it ships, the radio TX path is
production-quality for SSB voice (LSB / USB / DIGL / DIGU) on Saturn G2 /
Anan-G2 boards and the HL2 TX path is documented + cross-checked against
mi0bot-Thetis. 3M-2 (CW TX) and 3M-3 (TXA speech-processing depth) come
next per master-design plan order.

---

## 2. Locked decisions (brainstorm output)

Recorded here so the pre-code review can lock-in implementation specifics
without re-deriving the scope.

| # | Decision | Choice |
|---|---|---|
| Q1 | 3M-1c scope | **10 chunks** (master-design §5.3 minus TX recording, plus 3 carry-forwards from 3M-1b, plus chunk 0 HL2 TX path desk-review) |
| Q2 | Mic profile schema scope | **Live-fields-only with Thetis column names** (~20 columns named per `database.cs:AddTXProfileTable`); 1 default factory profile; SSB-only mode-family pointer for 3M-1c (Digital/FM/AM pointers land in 3M-3b) |
| Q3 | AppSettings rename strategy | **Hard cutover** — no migration code. 3M-1b 16-key naming hasn't shipped to a tagged release; users have no settings files with the old keys yet |
| Q4 | Two-tone test scope | **Full Thetis port** — continuous + pulsed modes, all 6 user-tunable params, Setup → Test → Two-Tone page with defaults/stealth preset buttons, mode-aware tone inversion, Freq2 delay, power-source enum, TUN auto-stop, TxApplet 2-TONE button |
| Q5 | TX recording scope | **Defer to 3M-6** (already lists WAV record/playback as a known phase with three siblings). DVK applet stays UI-only stub (no regression — current state since Phase 3-UI) |
| Q6 | HL2 TX path desk-review | **Chunk 0 (precursor)** — runs before chunks 1-9 so findings inform implementation. Output: `docs/architecture/phase3m-1c-hl2-tx-path-review.md`. mi0bot-Thetis is **promoted to primary** for chunk 0 (it's the HL2 reference); Thetis remains primary for chunks 1-9 |

**Folded carry-forwards from 3M-1b post-code review §14:**

- **B.1 DEXP-as-gate** — NOT in 3M-1c. Lands in 3M-3a-iii (its native phase).
- **B.2 TX timer push-driven refactor** — chunk 7 in 3M-1c.
- **B.3 PTT-source arbitration audit** — NOT in 3M-1c. Lands in 3M-2a startup ("priority order before 3M-2" per master-design).
- **B.4 Initial-state-sync audit** — chunk 8 in 3M-1c.
- **B.5 Mic-gain default refinement** — chunk 9 in 3M-1c.

---

## 3. Scope (10 chunks)

| # | Chunk | Source-first cite | Risk |
|---|---|---|---|
| 0 | **HL2 TX path desk-review vs mi0bot-Thetis** (precursor) | mi0bot-Thetis `[v2.10.3.13-beta2]` / `[@c26a8a4]` primary for this chunk; cross-check against Thetis upstream | Low (analysis only; output is a doc) |
| 1 | VFO Flag TX badge wire-up | AetherSDR `VfoWidget.{h,cpp}` floating-flag pattern + NereusSDR-native `VfoDisplayItem::setTransmitting(bool)` already implemented in 3G-8 | Low |
| 2 | Two-tone test full port (continuous + pulsed) | `console.cs:44728-44760` + `setup.cs:11019-11200ish` `[v2.10.3.13]` | Medium |
| 3 | Mic profile schema (live fields, Thetis names, SSB-only pointer) | `database.cs:AddTXProfileTable` `[v2.10.3.13]` (~20-column subset of Thetis's 206) | Medium |
| 4 | TxApplet completes (profile combo + save/load/delete/rename + 2-TONE button) | various Thetis `[v2.10.3.13]` | Low |
| 5 | Persistence audit + hard-cutover rename of 16 3M-1b keys | `database.cs` column names `[v2.10.3.13]` | Low |
| 6 | `MoxChanging`/`moxChanged(int,bool,bool)` Qt signals; existing `moxChanged(bool)` renames to `moxStateChanged(bool)` | `console.cs:44851/45012/29324/29677` `[v2.10.3.13]` | Low |
| 7 | B.2 TX timer → push-driven (`AudioEngine::micBlockReady` → `TxChannel::driveOneTxBlock`) | NereusSDR-native; mirror Thetis MoxFifo timing where applicable | Medium |
| 8 | B.4 Initial-state-sync audit (TX monitor / anti-VOX gain / VOX threshold) | follows 3M-1b `1841462` `micPreamp` precedent | Low |
| 9 | B.5 Mic-gain default refinement (bench-tune across mic types) | bench-driven; default may move from `−6` to a different value | Low |

---

## 4. Architecture

### 4.1 New files

| File | Purpose |
|---|---|
| `docs/architecture/phase3m-1c-hl2-tx-path-review.md` | **Chunk 0 deliverable.** HL2 TX path desk-review vs mi0bot-Thetis. Findings list with file paths, line refs, descriptions, fix proposals or follow-up-issue suggestions. Each delta categorised as either (a) absorb into a 3M-1c chunk inline, (b) file as separate follow-up issue, or (c) document as known-divergence with rationale. |
| `src/core/MicProfileManager.{h,cpp}` | Profile bank: load / save / delete / rename / set-active. Holds `QVector<MicProfile>`, fires `profileListChanged` / `activeProfileChanged` signals. Persists to AppSettings under `hardware/<mac>/tx/profile/<name>/...` and `hardware/<mac>/tx/profile/active`. |
| `src/gui/setup/TestTwoTonePage.{h,cpp}` | 6-param Setup page: Freq1, Freq2, Level, Power, Freq2 delay, Invert tones, Pulsed mode toggle, defaults / stealth preset buttons |
| `src/gui/setup/SetupCategoryTest.{h,cpp}` | New top-level Setup → Test category parent (first test page; future siblings: PA scaling, IMD analyzer, calibration probes) |

### 4.2 Modified files

| File | Change |
|---|---|
| `src/core/MoxController.{h,cpp}` | Rename existing `moxChanged(bool)` → `moxStateChanged(bool)`. Add `moxChanging(int rx, bool oldMox, bool newMox)` (pre) + new 3-arg `moxChanged(int rx, bool oldMox, bool newMox)` (post matching Thetis). Migrate ~30 call-sites. The `int rx` argument is the active receiver index per Thetis (`rx2_enabled && VFOBTX ? 2 : 1`). |
| `src/models/TransmitModel.{h,cpp}` | **Rename 16 keys** to Thetis column names (`micGainDb`→`MicGain`, `voxHangTimeMs`→`VOX_HangTime`, `antiVoxGainDb`→`AntiVox_Gain`, `monitorVolume`→`MonitorVolume`, `micBoost`→`Mic_Input_Boost`, `micXlr`→`Mic_XLR`, `lineIn`→`Line_Input_On`, `lineInBoost`→`Line_Input_Level`, `micTipRing`→`Mic_TipRing`, `micBias`→`Mic_Bias`, `micPttDisabled`→`Mic_PTT_Disabled`, `voxThresholdDb`→`Dexp_Threshold`, `voxGainScalar`→`VOX_GainScalar`, `antiVoxSourceVax`→`AntiVox_Source_VAX`, `micSource`→`Mic_Source`). Add 2-tone properties (`TwoToneFreq1`, `TwoToneFreq2`, `TwoToneLevel`, `TwoTonePower`, `TwoToneFreq2Delay`, `TwoToneInvert`, `TwoTonePulsed`). Wire to `MicProfileManager`. |
| `src/core/TxChannel.{h,cpp}` | **B.2:** replace timer-driven `driveOneTxBlock` with push-driven slot connected to `AudioEngine::micBlockReady(const float*, int)`. Drop `1b353f4` zero-fill workaround. Add `TXPostGen*` setters for two-tone (`setTxPostGenMode`, `setTxPostGenTTFreq1/2`, `setTxPostGenTTMag1/2`, `setTxPostGenTTPulseToneFreq1/2`, `setTxPostGenTTPulseMag1/2`, `setTxPostGenTTPulsePeriod`, `setTxPostGenTTPulseDur`, `setTxPostGenTTPulseTransition`, `setTxPostGenRun`). |
| `src/core/AudioEngine.{h,cpp}` | Add `micBlockReady(const float*, int)` Qt signal emitted when 256 samples accumulate in the TX input bus (`m_txInputBus`). Connection-thread emits → audio-thread `TxChannel::driveOneTxBlock` slot via `Qt::DirectConnection` (audio-thread affinity matches; no queueing penalty). |
| `src/gui/applets/TxApplet.{h,cpp}` | Profile combo with save / load / delete / rename, 2-TONE button (mutually exclusive with TUN, calls `TransmitModel::setTwoTone(true)`), tooltip integration with `BandPlanGuard` reasons (already wired in 3M-1b). |
| `src/gui/meters/VfoDisplayItem.{h,cpp}` | Wire `setTransmitting(bool)` to `MoxController::moxStateChanged(bool)`. Colour-per-mode lookup table (palette TBD in pre-code review — Thetis-style: red for SSB TX, amber for AM/FM, etc.). |
| `src/gui/setup/SetupDialog.cpp` | Register new Setup → Test category + Two-Tone page in the page tree. |
| `src/gui/setup/TransmitSetupPages.cpp` | TX Profiles section: profile bank list with save / load / delete / rename, 1 default factory preset shipped, "load profile" populates currently-wired controls only. |

### 4.3 Data flow additions

```
Mic input → AudioEngine TX bus (256-sample accumulator) → micBlockReady signal
    ↓ Qt::DirectConnection (audio thread → audio thread)
TxChannel::driveOneTxBlock(samples, 256) → fexchange2 → TXA pipeline
    ↓
[TXPostGen stage active iff TwoTone=true]
    ↓
sip1OutputReady → AudioEngine::txMonitorBlockReady (existing, unchanged)
    ↓
Output → IF radio (TX I/Q via P1/P2 wire path, unchanged)
    ↓
MOX state change → MoxController emits moxStateChanged(bool) AND
                   moxChanging(int rx, bool old, bool new) [pre] AND
                   moxChanged(int rx, bool old, bool new) [post]
    ↓
VfoDisplayItem::setTransmitting → colour-per-mode badge wires up
TxApplet::setMoxBadge → tooltip + button highlight (existing, plus 3-arg observers)
[Plugin / observer slots can connect to any of the 3 signals]
```

### 4.4 Profile-bank persistence layout

```
hardware/<mac>/tx/profile/active = "Default"
hardware/<mac>/tx/profile/Default/MicGain = -6
hardware/<mac>/tx/profile/Default/Mic_Input_Boost = True
hardware/<mac>/tx/profile/Default/VOX_HangTime = 500
hardware/<mac>/tx/profile/Default/Dexp_Threshold = -40
... (all ~20 live keys)
hardware/<mac>/tx/profile/Default/TwoToneFreq1 = 700
hardware/<mac>/tx/profile/Default/TwoToneFreq2 = 1900
hardware/<mac>/tx/profile/Default/TwoToneLevel = -6
hardware/<mac>/tx/profile/Default/TwoTonePower = 50
hardware/<mac>/tx/profile/Default/TwoToneFreq2Delay = 0
hardware/<mac>/tx/profile/Default/TwoToneInvert = False
hardware/<mac>/tx/profile/Default/TwoTonePulsed = False
```

When user saves a new profile (e.g. "Heil PR40"), all currently-wired
control values are written under `hardware/<mac>/tx/profile/Heil PR40/...`.
3M-3a sub-PRs add new column names as their backends ship — no migration
needed because AppSettings is schema-less.

---

## 4bis. Chunk 0 — HL2 TX path desk-review scope

mi0bot-Thetis is the authoritative HL2 reference. The 3M-1a / 3M-1b TX
work shipped without HL2 hardware in hand; bench rows M.2 / M.3 / M.4 are
still open. Chunk 0 closes the desk-review gap so we don't carry HL2
divergence into 3M-1c implementation.

**Areas to compare** (mi0bot-Thetis vs NereusSDR + upstream Thetis):

1. **HL2 P1 EP2 zone TX timing** — sample rate / buffer alignment / SPSC ring
   sizing. Compare mi0bot's HL2 path with our `P1RadioConnection::sendTxIq`
   from 3M-1a (commit `0b557e9`).
2. **HL2 mic-jack handling** — 3M-1b set `hasMicJack=false` for HL2.
   mi0bot's HL2 UI: do they show mic-jack controls at all? Are the
   `Mic_Input_Boost` / `Line_Input_*` columns honoured on HL2?
3. **HL2 watchdog (RUNSTOP `pkt[3]` bit 7)** — already wired in 3M-1a E.5
   per HL2 firmware `dsopenhpsdr1.v:399-400 [@7472bd1]`. Cross-check
   mi0bot's setting at MOX boundaries.
4. **HL2 step attenuator on TX** — 3G-13 wired `shouldForce31Db` (PS-off OR
   CWL/CWU). Verify mi0bot's HL2 force-31dB logic matches.
5. **HL2 codec path vs Saturn G2 path** — TXA channel ID, sample rate,
   buffer size differences. NereusSDR uses `WdspEngine::createTxChannel(WDSP.id(1,0))`
   with channel ID 1 (3M-1a §5.1.1). mi0bot's HL2 channel setup?
6. **HL2 firmware quirks** — CWX LSB-clear workaround (already noted in
   3M-1a `tst_p1_tx_iq_wire`). Other HL2-only quirks mi0bot patches?
7. **HL2 power scaling** — `safety_constants.cpp` HL2 scaling values
   (3M-0). Verify mi0bot's HL2 PA scaling reference values match.
8. **HL2 PTT path** — `mic_ptt` extraction (3M-1b H.5). Does HL2 have any
   different PTT semantics in mi0bot's fork?
9. **HL2-specific patches not upstream** — anything mi0bot has that
   upstream Thetis doesn't, that affects TX. List with rationale.

**Output document structure:**

- §1 Scope and method (which mi0bot files inspected, mi0bot version stamp)
- §2 Findings (one section per area 1-9 above; each with delta + proposed action)
- §3 Categorised action plan:
  - (a) Absorb into 3M-1c chunk N inline — with target chunk
  - (b) File as separate follow-up issue (post-3M-1c)
  - (c) Document as known-divergence with rationale
- §4 Open questions (anything mi0bot does that we don't understand)

**Non-goals for chunk 0:** no code changes; no unit tests; no protocol
captures; no hardware testing. Pure desk-review producing one Markdown
document. If a finding requires immediate fix and would otherwise
ship-block 3M-1c, it gets added to a target chunk's task list and is
implemented there.

**Estimated effort:** 1-2 days of session work. ~5-10 mi0bot files to
read (Console/console.cs, Console/setup.cs, Console/cmaster.cs,
Console/NetworkIO.cs, Console/HPSDRHardware/HermesLite2*.cs if present,
Console/audio.cs).

**Source-first stamp:** chunk 0 cites use mi0bot-Thetis primary stamps
(`[v2.10.3.13-beta2]` for tag-aligned values; `[@c26a8a4]` for post-tag).
If chunk 0 ports anything (unlikely for a desk-review), the one-time
setup of `MI0BOT-THETIS-PROVENANCE.md` + `mi0bot-thetis-author-tags.json`
+ `discover-mi0bot-thetis-author-tags.py` happens as part of the chunk 0
commit.

---

## 5. Carry-forward integration

### 5.1 B.2 TX timer push-driven (chunk 7)

**Problem** (per 3M-1b post-code review §14b note 5 — handoff section B.2):
The current `TxChannel::driveOneTxBlock` runs on a 5 ms QTimer pulling from
`TxMicRouter::pullSamples`. PC mic delivers 5.33 ms blocks (256 samples at
48 kHz). Producer / consumer mismatch causes occasional empty pulls; the
zero-fill mitigation in `1b353f4` silences a frame instead of letting it
drift. Architecture is still pull-driven and timing is fragile.

**Fix:**
- Drop the QTimer.
- Add `AudioEngine::micBlockReady(const float* samples, int frames)` signal,
  emitted when the TX input bus has at least 256 samples accumulated.
- Connect to `TxChannel::driveOneTxBlock(samples, 256)` via
  `Qt::DirectConnection` (audio-thread affinity matches both sides).
- `TxChannel::driveOneTxBlock` becomes a slot taking the explicit sample
  buffer instead of pulling from a router.
- `TxMicRouter` still owns the source-selection (PC vs Radio), but the
  drive comes from the bus accumulating, not from a clock tick.

**Cross-check with mi0bot-Thetis:** verify HL2 timing isn't materially
different from upstream Thetis (mi0bot may have HL2-specific tuning we'd
want to preserve).

### 5.2 B.4 Initial-state-sync audit (chunk 8)

**Problem** (handoff section B.4): 3M-1b's `1841462` fixed `micPreamp` not
firing on `TxChannel` attach because Qt signal-connect doesn't fire for
the current value. Other L.1 wires (TX monitor enabled, TX monitor volume,
anti-VOX gain, VOX threshold) may have the same gap.

**Fix:** Audit pass over every signal connected in
`RadioModel::onConnected()`. For each, verify the slot fires on attach OR
the model state is explicitly pushed in the constructor. Targets (per the
handoff list):
- `TX monitor enabled` (`AudioEngine::setTxMonitorEnabled`)
- `TX monitor volume` (`AudioEngine::setTxMonitorVolume`)
- `anti-VOX gain` (`MoxController::setAntiVoxGain`)
- `VOX threshold` (`MoxController::setVoxThreshold`)
- Any new attach paths added by 3M-1c.

### 5.3 B.5 Mic-gain default refinement (chunk 9)

**Problem** (handoff section B.5): The `−6` default may need refinement.
JJ noted at the bench he had to use macOS-system + slider-min to get clean
SSB on a hot mic. Current default with Leveler now wired (`c261a53`) sounds
OK but bench-verify across mic types is needed before changing the default.

**Plan:**
- Bench-test pass across multiple mic types (USB headset, dynamic, condenser,
  built-in laptop mic).
- If default change is warranted, persist new default in the factory
  "Default" profile (chunk 3).
- Document the chosen default in the post-code review with bench data.

---

## 6. Source-first priority

**Order:**

1. **Thetis primary** — `v2.10.3.13` / `[@501e3f51]` for post-tag commits.
   Cite stamp grammar: `// From Thetis <file>:<line> [v2.10.3.13]` for
   tag-aligned values; `[@501e3f51]` for post-tag.
2. **deskhpsdr secondary** — `[@120188f]` when Thetis is incomplete or
   unclear. Precedent: 3M-1b P2 byte-50 mic-jack polarity (Thetis
   `networkproto1.c` covered some bits, deskhpsdr `new_protocol.c:1480-1502`
   covered the full table). 3M-1c lookup most likely on chunk 2 (two-tone
   pulsed mode parameters) and chunk 7 (push-driven TX timer architecture).
3. **mi0bot-Thetis** — `v2.10.3.13-beta2` / `[@c26a8a4]`.
   - **Primary for chunk 0** (HL2 TX path desk-review — see §4bis).
   - **Tertiary for chunks 1-9** for HL2-functional verification and
     HL2-specific patches. Precedent: Phase 3I RadioDiscovery port. HL2
     is mi0bot's primary focus, so HL2 paths get cross-checked here.
     Lookup most likely on chunks 1 (VFO TX badge HL2 visual), 2 (HL2
     two-tone support), 3 (HL2 profile schema cross-check), 7 (HL2
     timing tweaks), 8/9 (HL2 smoke).

**Multi-source cite format examples:**

```cpp
// From Thetis console.cs:N [v2.10.3.13] + deskhpsdr <file>:<line> [@120188f] — pulse-profile defaults
// From mi0bot-Thetis ChannelMaster.cs:N [v2.10.3.13-beta2] — HL2-only patch (not in upstream Thetis)
// From Thetis console.cs:N [v2.10.3.13] + mi0bot-Thetis console.cs:N [v2.10.3.13-beta2] — HL2 timing cross-check
```

**One-time setup (if 3M-1c ports from mi0bot-Thetis):** create
`docs/attribution/MI0BOT-THETIS-PROVENANCE.md` (mirror of
`DESKHPSDR-PROVENANCE.md`) and `mi0bot-thetis-author-tags.json` corpus via
new `scripts/discover-mi0bot-thetis-author-tags.py` (mirror of existing
deskhpsdr / Thetis discover scripts). Verifier
`verify-inline-tag-preservation.py` already accepts `../mi0bot-Thetis/`
paths per CLAUDE.md. If all mi0bot-Thetis usage in 3M-1c is
**verification-only** (no code ported), the PROVENANCE file isn't
required for this phase.

---

## 7. Discipline (mirror 3M-1b precedent #149)

- **Single feature branch:** `feature/phase3m-1c-polish-persistence`
  (created from `origin/main` `bfce1cf`).
- **Single PR.**
- **Document sequence:**
  1. This design spec → committed first (already at `81ff57e`).
  2. **Chunk 0 deliverable** — `phase3m-1c-hl2-tx-path-review.md` (HL2
     desk-review vs mi0bot-Thetis). Lands as its own commit before
     pre-code review. Findings inform pre-code review and chunks 1-9.
  3. `phase3m-1c-thetis-pre-code-review.md` → Thetis source quotes,
     behaviour analysis, deferral table, risk surface (incorporates
     chunk 0 findings where they affect chunks 1-9).
  4. `phase3m-1c-polish-persistence-plan.md` → TDD task list with cite
     stamps and per-task tests.
  5. TDD execution per task with `superpowers:subagent-driven-development`
     two-stage review (spec compliance + code quality).
  6. Verification matrix update (`docs/architecture/phase3m-0-verification/`).
  7. `phase3m-1c-post-code-review.md` → for each pre-code §, did the
     implementation match the analysis? Note deltas; surface follow-ups.
- **TDD:** test-driven per task. Failing test or failing source-cited
  assertion first; implement to green; commit.
- **Inline cite stamps:** `[v2.10.3.13]` for Thetis tag-aligned values,
  `[@501e3f51]` for Thetis post-tag, `[@120188f]` for deskhpsdr,
  `[v2.10.3.13-beta2]` or `[@c26a8a4]` for mi0bot-Thetis.
- **GPG-signed commits.** No `--no-gpg-sign`. No `--no-verify`.
- **Pre-commit hook chain** runs:
  `verify-thetis-headers.py`, `check-new-ports.py` (diff + full-tree),
  `verify-inline-cites.py`, `compliance-inventory.py`,
  `verify-inline-tag-preservation.py`. All must pass.
- **Environment:** `NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis` set in
  shell before each commit.

---

## 8. Verification

### 8.1 Unit tests (~30 new)

Chunk 0 contributes no unit tests (it's an analysis deliverable). The
~30 figure covers chunks 1-9. Per chunk, parametrised where applicable.
New test files (illustrative):

- `tst_mic_profile_manager.cpp` — load / save / delete / rename / set-active round-trip
- `tst_transmit_model_thetis_key_rename.cpp` — all 16 keys persist under Thetis names; old NereusSDR-original names absent (hard cutover)
- `tst_two_tone_continuous.cpp` — `TXPostGenMode=1` + `TXPostGenRun=1` wire-up
- `tst_two_tone_pulsed.cpp` — `TXPostGenMode=7` + pulse-profile setters
- `tst_two_tone_invert_modes.cpp` — invert-tones gate for LSB / CWL / DIGL
- `tst_two_tone_freq2_delay.cpp` — Freq2 magnitude delayed to defeat amp freq-counters
- `tst_mox_change_handlers.cpp` — `moxChanging` and `moxChanged(int,bool,bool)` fire in order, with correct `rx` index per VFOBTX state
- `tst_mox_state_changed_rename.cpp` — existing call-sites migrated; `moxStateChanged(bool)` fires equivalently
- `tst_audio_engine_mic_block_ready.cpp` — signal fires on every 256-sample accumulation; underrun returns 0
- `tst_tx_channel_push_driven.cpp` — `driveOneTxBlock(samples, 256)` slot fires only on `micBlockReady`; no QTimer
- `tst_initial_state_sync_audit.cpp` — every L.1 attach path fires its slot with current model state
- `tst_vfo_tx_badge.cpp` — `setTransmitting(true)` → badge visible; `setTransmitting(false)` → badge hidden; colour-per-mode lookup
- `tst_tx_applet_profile_combo.cpp` — save / load / delete / rename via combo
- `tst_tx_applet_two_tone_button.cpp` — 2-TONE mutually exclusive with TUN; calls `TransmitModel::setTwoTone(true)`

### 8.2 Manual bench rows

Added to `docs/architecture/phase3m-0-verification/README.md`. All
`[3M-1c-bench-*]` tagged.

- `[3M-1c-bench-HL2]` — VFO TX badge appears + clears cleanly during MOX
- `[3M-1c-bench-HL2]` — Two-tone continuous at 14.200 MHz LSB; spectrum analyser shows two clean tones at ±700 / ±1900 Hz; 3rd-order intermod products visible
- `[3M-1c-bench-HL2]` — Two-tone pulsed mode: pulse rate audible / visible on spectrum analyser
- `[3M-1c-bench-HL2]` — Profile save / load / restart-app round-trip
- `[3M-1c-bench-G2]` — same matrix as HL2, plus mic-jack flag persistence in profile
- `[3M-1c-bench]` — Push-driven TX timer: 30-minute SSB transmission; no zero-filled frames in audio capture
- `[3M-1c-bench]` — Mic-gain bench-tune across mic types (USB headset, dynamic, condenser, built-in laptop)

### 8.3 Carry-forward 3M-1b bench rows

M.2 / M.3 / M.4 / M.5 stay open in the verification matrix. They are
**independent of 3M-1c** and can complete at the bench at any time without
blocking 3M-1c work.

---

## 9. Estimated work + Risk

**Estimated session work:** ~2.5–3 weeks. About half of 3M-1b's 51-task
scope. Most chunks are well-bounded; the largest single chunk is the
two-tone full port (chunk 2, ~250-300 lines + Setup page + ~10 tests).
Chunk 0 (HL2 desk-review) adds 1-2 days at the front of the phase.

**Risk profile:** Low–Medium overall.

| Risk | Source | Mitigation |
|---|---|---|
| HL2 path divergence undiscovered until bench | mi0bot may have HL2-specific tweaks not in upstream | **Chunk 0 desk-review** vs mi0bot-Thetis (precursor); HL2 bench rows confirm |
| Chunk 0 surfaces a substantive delta requiring its own PR | unknown until the desk-review runs | If 2+ substantive HL2 deltas: split chunk 0 off as its own pre-3M-1c branch and merge first; chunks 1-9 rebase on the merged HL2 fixes |
| Two-tone pulsed-mode parameter range mistakes | `setupTwoTonePulse()` not fully self-documenting | Cross-check deskhpsdr secondary; bench-verify with spectrum analyser |
| Push-driven TX timer regression | B.2 architecture change | TDD harness simulates underrun / overrun; bench-verify with 30-min SSB transmission; chunk 0 may surface HL2-specific timing constraints |
| Hard-cutover key rename leaves orphans | 16 keys × per-MAC scope | Persistence audit test asserts old key names absent after first 3M-1c launch |
| MoxController call-site migration breaks observers | ~30 call-sites for the rename | Compile-time enforced (signature change); test sweep catches behavioural regressions |

---

## 10. Out of scope (deferred)

| Item | Where it goes |
|---|---|
| TX recording (mic-side WAV output) | 3M-6 — TX Polish |
| DVK applet record-backend | 3M-6 — same `WavWriter` |
| Per-mode-family profile pointers (Digital / FM / AM) | 3M-3b — when AM / FM TX modes ship |
| Full 21-factory-preset set (DX / Contest, ESSB, D-104, PR40, etc.) | 3M-3a-i (when EQ + CFC + DEXP backends exist) — presets shipped without backends would be misleading |
| `TXProfileDef` factory-reference table | 3M-3a-i (when full schema lands) |
| Per-mode DSP buffer / filter size / type columns from Thetis schema | 3M-3a-i / -ii / -iii (each sub-PR adds its slice) |
| 4 separate active-profile pointers (`comboTXProfile`, `comboDigTXProfile`, `comboFMTXProfile`, `comboAMTXProfile`) | 3M-3b |

---

## 11. References

### NereusSDR

- Master design: [`phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.3
- 3M-1b plan: [`phase3m-1b-mic-ssb-voice-plan.md`](phase3m-1b-mic-ssb-voice-plan.md)
- 3M-1b post-code review: [`phase3m-1b-post-code-review.md`](phase3m-1b-post-code-review.md)
- 3M-1b verification matrix: [`phase3m-0-verification/README.md`](phase3m-0-verification/README.md)
- Attribution / source-first protocol: [`docs/attribution/HOW-TO-PORT.md`](../attribution/HOW-TO-PORT.md)

### Upstream

- Thetis: `https://github.com/ramdor/Thetis` @ `v2.10.3.13` / `@501e3f51`
- deskhpsdr: `@120188f` (cloned at `/Users/j.j.boyd/deskhpsdr`)
- mi0bot-Thetis: `v2.10.3.13-beta2` / `@c26a8a4` (cloned at `/Users/j.j.boyd/mi0bot-Thetis`)

### Key Thetis source files for 3M-1c

- `Project Files/Source/Console/console.cs` — `chk2TONE_CheckedChanged` (44728-44760), `MoxChangeHandlers` (44851 / 45012), MOX state machine (29017-29677)
- `Project Files/Source/Console/setup.cs` — `chkTestIMD_CheckedChanged` (11019-11200ish)
- `Project Files/Source/Console/database.cs` — `AddTXProfileTable` (4299-end of method) — column-name reference for chunk 3 / chunk 5

---

End of design spec.
