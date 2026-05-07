# Phase 3M-4 PureSignal: Bench-Debug Handoff (Session 2)

> **Round 2 landed 2026-05-06.** See "Round 2 status" below before reading
> the original handoff text.  The five gaps in the table at §"Gaps in the
> port" are all addressed in code; remaining work is the bench-driven
> verification matrix (does GetPk really show 0.6121 on ANAN-G2, does the
> banner number update mid-cal, etc.).

## Round 2 status (2026-05-06)

Three commits landed on `claude/wizardly-poitras-dbab75`:

| SHA | Scope | Commit |
| --- | --- | --- |
| `404ade8` | PureSignal coordinator + PsaIndicatorWidget consolidated PSInfo dispatch | fix(puresignal): consolidated PSInfo dispatch + drop phantom m_correcting |
| `51afae6` | RadioModel applyBoardCapabilities call site | fix(puresignal): wire applyBoardCapabilities() so SetPSHWPeak fires |
| `10f0bc1` | SpectrumWidget IMD overlay gate-state diagnostic logging | fix(spectrum): IMD overlay gate-state logging for bench debug |

Code-level coverage of the 6-gap table:

| Gap | Round 2 disposition |
| --- | --- |
| Split Q_PROPERTY signals | Closed by new `PureSignal::psInfoChanged(level, ok, corrApplied, calChanged, color)` signal — 5 fields atomically, gated on `(m_autoCalEnabled && hasInfoChanged)` per Thetis PSForm.cs:614-619 [v2.10.3.13]. |
| Phantom `m_correcting` field | Removed.  PsaIndicatorWidget's updateDisplay now branches on `m_correctionsApplied` only, byte-for-byte with ucInfoBar.cs:856-865 [v2.10.3.13]. |
| `m_calChangedSinceLastDraw` local mishandling | Closed.  Driven exclusively by psInfo()'s `calibrationAttemptsChanged` parameter; the auto-set-on-setFeedbackLevel side effect is gone. |
| No autocal+HasInfoChanged gate | Closed.  PureSignal::processNewInfo emits psInfoChanged only when both gates pass. |
| No SetPSHWPeak auto-init | Closed.  `RadioModel::connectToRadio` WDSP-init lambda now calls `m_pureSignal->applyBoardCapabilities(boardCapabilities())` after setTxChannel + setPsFeedbackChannel.  Per-board defaults from BoardCapsTable now flow through to calcc. |
| HasInfoChanged delta computation | Already present (PureSignal.cpp `hasInfoChanged()`).  Round 2 added `calAttemptsChanged` derived from `info[5] != m_oldInfo[5]` per PSForm.cs:1097-1098 [v2.10.3.13], emitted alongside `psInfoChanged`. |

Plus a bonus fix found during investigation: `PsaIndicatorWidget` had been
subscribing to `enabledChanged` instead of `autoCalEnabledChanged` — wrong
semantic (PSAEnabled in Thetis tracks `psform.AutoCalEnabled` per
console.cs:2280-2284 [v2.10.3.13], not the master enable).  Round 1 worked
around it by seeding `isEnabled()=true` on the late-bind seam, but the
badge never followed PS-A toggles.  Now wired correctly.

ctest: 366/366 PASS.  +9 new test cases (5 PsaIndicatorWidget psInfo()
variants, 4 PureSignal psInfoChanged gate + applyBoardCapabilities side-
effect tests).  No regressions.

### Bench tester checklist (Round 2 verification)

Connect to ANAN-G2 (Saturn).  Open Tools → PureSignal.

1. **GetPk readout** in PsForm Calibration Information should show
   **`0.6121`** (or whatever the per-board value is — 0.2899 for Orion-MkII,
   0.233 for HL2, 0.4072 for legacy P1 — see BoardCapsTable Task 1 commit
   1bbb85a).  Pre-Round-2 it stuck at `0.0`.
2. **Click PS-A** in TxApplet: bottom-banner FB+PS pair turns **SeaGreen**
   (idle, PS-A enabled, no MOX).  Pre-Round-2 it followed the master
   `enabledChanged` instead of `autoCalEnabledChanged`, so the badge would
   stick at SeaGreen permanently and not gray out on PS-A off.
3. **Click 2-Tone** (engages MOX).  Banner FB color tracks
   FeedbackColourLevel: Red <91, Yellow 91-128, Green 129-181, Blue >181.
   Banner PS turns **Lime "Correcting"** when calcc reports
   `info[14] == 1` (CorrectionsBeingApplied).  Pre-Round-2 the per-field
   correctingChanged lambda mis-routed values: PS could show "Pure
   Signal2"+SeaGreen even when correctionsApplied was true, and FB stayed
   on the word "Feedback" instead of the numeric level.
4. **FB numeric value**: while 2-Tone is running and calcc is iterating,
   FB label should show the integer feedback level (e.g. `163`) and update
   on every cal-attempt cycle (info[5] increments → calAttemptsChanged).
   Pre-Round-2 it showed "Feedback" because the per-tick gate was missing.
5. **Calibration Information group** (PsForm body): all six labels should
   populate with non-zero values.  Pre-Round-2 they stayed at zeros
   because the polling loop was driven but the gate to PSInfo was broken.
6. **PsForm "Show 2Tone measurements" checkbox** + spectrum overlay:
   open ~/.config/NereusSDR/NereusSDR.log (or run from terminal to see
   stderr).  Look for lines like
       `nereus.spectrum: IMD overlay gate: mox=true testIMD=true showImd=true duplex=true`.
   When **all four** are `true` and the overlay still doesn't render, the
   bug is in the data path (m_smoothed during MOX) — file follow-up issue
   with the log lines attached.  When one of the four is `false`, the
   diagnostic tells us which gate to chase.

Spec-driven items NOT verified by ctest because they need calcc to be
running:

- The Lime "Correcting" badge transition only fires when calcc actually
  asserts `info[14] == 1`.  Smoke-test by running 2-Tone for ~30 s; the
  PS label should toggle Lime Correcting → SeaGreen Pure Signal2 as
  calibration cycles through Setup → Collect → Calc → StayOn.
- GetPk drift across Save/Restore — verify the 0.6121 value persists
  after PS Save Corr, app restart, PS Restore Corr.

## Task 17 status (2026-05-07) — calcc data path + SATT verbatim port

Six commits landed on `claude/wizardly-poitras-dbab75` (29 commits ahead
of main).  Closes the data-path gap (calcc was unwired from the P2
multi-stream UDP) and the SATT auto-attenuate gap (was firing on every
100 ms tick instead of once per calcc cycle):

| SHA | Scope | Commit |
| --- | --- | --- |
| `1972888` | TransmitModel | fix(transmit): TwoToneLevel default 0 dB (Thetis-faithful) |
| `8c5132a` | CodecContext + P2CodecOrionMkII | feat(codec): puresignalRun freq override on CmdHighPriority DDC0/DDC1 |
| `9943c68` | P2RadioConnection | feat(p2): PS DDC config, multi-stream sync de-interleave, CmdTx for SATT |
| `fdd22c2` | PsccPump (new) + CMakeLists | feat(core): PsccPump driver — pscc(channel, size, tx, rx) for calcc |
| `7012814` | PureSignal coordinator | feat(puresignal): runcal gates + AutoAtt CalibrationAttemptsChanged guard |
| `f29ac6d` | RadioModel wiring | feat(radio-model): wire PureSignal/StepAtt/PsccPump for calcc convergence |

### Bench convergence

Latest run (2026-05-07): `state=6` (LCALC), `corrApplied=1`,
`calCount=872`, `feedbackLevel=164`.  Inside Thetis's
`IsFeedbackLevelOK` target window `[128, 181]` (PSForm.cs:1109-1112
[v2.10.3.13]).  PureSignal calibration converges from cold MOX-on
within ~2 seconds on ANAN-G2 (Saturn).

### SATT verbatim-port review

Reviewed `autoAttentionTick` against `PSForm.timer2code` (PSForm.cs:728-784
[v2.10.3.13]) and `shouldForce31Db` against `HdwMOXChanged` force-31
logic (console.cs:29563-29565 [v2.10.3.13]).

**Auto-attenuate three-state machine** (Monitor → SetNewValues →
RestoreOperation → Monitor): byte-for-byte port.  All Thetis constants
preserved verbatim (`152.293`, `31.1`, `±100`).  `std::lround` mirrors
`Math.Round(..., MidpointRounding.AwayFromZero)`.  Inline tags
(`//MW0LGE`, `//[2.10.3.12]MW0LGE`) preserved per CLAUDE.md "Inline
comment preservation".

**Force-31 predicate**: `shouldForce31Db(dspMode, isPsOff)` is a verbatim
port of:

```csharp
if ((!chkFWCATUBypass.Checked && _forceATTwhenPSAoff) ||
    (CWL || CWU)) txAtt = 31;
```

with the mapping:

| Thetis | NereusSDR |
| --- | --- |
| `!chkFWCATUBypass.Checked` (PS-A NOT active — checkbox repurposed at console.cs:43714) | `!m_psActive` |
| `_forceATTwhenPSAoff` | `m_forceAttWhenPsOff` |
| `CWL || CWU` | `dspMode == DSPMode::CWL || DSPMode::CWU` |

### Verbatim-port closure: PSForm.cs:738 (commit `6094024`)

The previously documented gap (`if (!console.ATTOnTX) AutoAttenuate = true;`)
is now ported verbatim in `PureSignal::autoAttentionTick`'s Monitor →
SetNewValues transition.  When the ATT-on-TX master toggle is OFF on
entry and the calcc cycle has advanced + needs recalibration, the tick
force-enables the master toggle to mirror Thetis's
`AutoAttenuate.setter → console.ATTOnTX = true` chain.  The setter is
conditional (`if (!attOnTxEnabled())`) so it's a no-op when already ON,
matching Thetis's silent-accept semantics at console.cs:19048.

Two regression tests in `tst_puresignal_coordinator`:

- `autoAttentionTick_forceEnablesAttOnTxMaster` — precondition OFF →
  tick advances to SetNewValues → postcondition ON.
- `autoAttentionTick_leavesAttOnTxMasterAloneWhenAlreadyOn` — idempotent
  when the toggle is already ON.

With this commit, the SATT auto-attenuate path matches Thetis byte-for-
byte across all six Thetis cite sites:

| Thetis cite | NereusSDR mirror |
| --- | --- |
| PSForm.cs:728-784 timer2code | PureSignal::autoAttentionTick (Monitor → SetNewValues → RestoreOperation) |
| PSForm.cs:735 `if (_autoattenuate && CalibrationAttemptsChanged && NeedToRecalibrate)` | m_autoAttenuate + m_aaLastSeenCalCount + needRecal predicate |
| PSForm.cs:738 `if (!console.ATTOnTX) AutoAttenuate = true; //MW0LGE` | `m_stepAtt->setAttOnTxEnabled(true)` force-enable |
| PSForm.cs:743-754 (deltaDb formula + IsFeedbackLevelOK) | `20.0 * std::log10(fbLevel / 152.293.0)` + `±100` clamp + `31.1` ceiling |
| PSForm.cs:756 `Math.Round(ddB, MidpointRounding.AwayFromZero) //[2.10.3.12]MW0LGE` | `std::lround(ddB)` |
| console.cs:29563-29566 force-31 predicate | StepAttenuatorController::shouldForce31Db |

## P1 / HL2 PureSignal bring-up

Task 17 P1 follow-up extends PureSignal coverage from P2-only (Saturn /
ANAN-G2 family) to the full P1 PureSignal-capable lineup: HL2, plain Hermes,
ANAN-10, ANAN-100 (all nddc=4) and HermesII / ANAN-10E / ANAN-100B
(nddc=2).

### Source-first cite map

| Thetis cite | NereusSDR mirror |
| --- | --- |
| `mi0bot networkproto1.c:982-993` [v2.10.3.13-beta2] case 2 — `if ((nddc == 2) && XmitBit && puresignal_run) ddc_freq = tx_freq` | `P1CodecStandard::composeCcForBank` case 2/3 + `P1CodecHl2::composeCcForBank` case 2/3 (gated on `ctx.p1PsNDdc == 2 && ctx.mox && ctx.p1PuresignalRun`) |
| `mi0bot console.cs:8408-8488` [v2.10.3.13-beta2] HL2 PS-on UpdateDDCs branch (`P1_DDCConfig=6, DDCEnable=DDC0, SyncEnable=DDC1, cntrl1=4`) | `P1CodecHl2::applyPureSignalDdcConfig` (already ported in Phase 3M-4 Task 5; was unwired pre-Task-17 P1 follow-up) |
| `Thetis console.cs:8527-8534 UpdateDDCs()` [v2.10.3.13] — `EnableRxs/EnableRxSync/SetDDCRate/SetADC_cntrl1/SetADC_cntrl2/Protocol1DDCConfig` | `P1RadioConnection::applyPsDdcConfig` writes `m_adcCtrl` (cntrl1+cntrl2 packed), `m_psNDdc` (PS gate), `m_activeRxCount` (EP6 layout) — plus arms bank 0 + bank 4 flush flags |
| `Thetis netInterface.c:1006-1016 SetTxAttenData()` [v2.10.3.13] — explicit `CmdTx()` flush | `P1RadioConnection::setTxStepAttenuation` arms `m_forceBank4Next=true` (Codex P2 ordering: flush before idempotent guard) |
| `Thetis PSForm.cs:246-247` [v2.10.3.13] — `SetPureSignal(1) + SendHighPriority(1)` explicit flush | `P1RadioConnection::setPuresignalRun` arms `m_forceBank0Next + m_forceBank11Next` so freq-override gate state lands within ≤2 frames |

### Per-board PS support matrix

| Board family | nddc | Bank 2/3 freq override applies? | NereusSDR `hasPureSignal` | Notes |
| --- | --- | --- | --- | --- |
| Atlas (Metis kit) | 1 | n/a (single ADC, no PS) | false | Correctly disabled (no PS path in Thetis) |
| HermesLiteRxOnly | n/a | n/a (RX-only SKU) | false | Correctly disabled (no TX) |
| Hermes (orig) | 4 | No (firmware handles cntrl1=4) | **true** (flipped Task 17) | mi0bot console.cs:8408-8413 grouping — same UpdateDDCs branch as HL2 |
| HermesLite (HL2) | 4 | No (firmware handles cntrl1=4) | **true** (flipped Task 17) | psDefaultPeak=0.233; psSampleRate=0 sentinel = "use rx1_rate" (HL2 high-rate path) |
| HermesII / ANAN-10E / ANAN-100B | 2 | YES — host-side override on bank 2 + bank 3 | true (was already on) | Was wire-broken before Task 17 (bank 2/3 didn't honor puresignal_run); now fixed |
| Angelia / Orion / OrionMKII / Saturn / SaturnMkII / Andromeda | 5 | No (DDC0+DDC2 dedicated PS, host doesn't override) | true | P2 path covered by main Task 17 commits (commit `f29ac6d` etc.) |

### Out-of-scope / future work

- **EP6 frame layout vs nddc=4 boards**: P1 EP6 frame slot count
  (`slotBytes = 6*nddc + 2`) is driven by `m_activeRxCount`.  HL2 PS-MOX
  in Thetis sets `nddc=4` at the C# layer but only DDC0+DDC1 carry real
  data (DDC2/DDC3 are effectively zeros).  NereusSDR's
  `applyPsDdcConfig` updates `m_activeRxCount` to `cfg.p1RxCount` (4 for
  HL2) — so EP6 parses 4 slots.  This may produce zero-valued samples
  for DDC2/3 during MOX, which is harmless for PsccPump (only consumes
  DDC0 + DDC1).  Bench-validate that frame parsing remains stable at
  nddc=4 with only DDC0+DDC1 populated; if not, lower `m_activeRxCount`
  back to 2 in PS-MOX (mi0bot bench traces show nddc=2 on the wire for
  HL2 PS — likely a firmware compaction).
- **HL2 6-bit step ATT field**: P1RadioConnection clamps TX ATT to
  `[0, 63]` (6-bit field per mi0bot WriteMainLoop_HL2) but bank-4 C3
  emits `& 0x1F` (5-bit). HL2 codec needs to override case 4 to widen
  the mask if HL2 ATT > 31 is desired.
- **Plain Hermes bench validation**: the `kHermes` flag flip is
  source-faithful but no bench has been run against an actual original
  Hermes board.  ANAN-10/100 share the same `kHermes` family caps —
  they piggyback on the bench validation target.

### Bench tester checklist (P1 / HL2 PS validation)

Connect to HL2.  No firmware floor is enforced — both ramdor and mi0bot
accept any HL2 firmware version (Thetis NetworkIO.cs:136-143 [v2.10.3.13]
guards only `HermesII && CodeVersion < 103`, no HL2 branch).  Bench tester
should record the actual HL2 firmware version they tested with for
reproducibility.  Open Tools → PureSignal.

1. **PS-A button visible** in TxApplet — pre-Task-17 it was hidden because
   `caps.hasPureSignal=false`.  Now visible per `kHermesLite.hasPureSignal=true`.
2. **PS Setup → Calibration Information populates** non-zero values within
   ~2 s of MOX-on.  Pre-Task-17 these stayed at zero because
   `applyPsDdcConfig` was a dead wire on P1 — the codec path was
   correctly computing `cntrl1=4` but no consumer applied it to the
   wire.
3. **TX 2-tone → bottom-banner FB number tracks calcc**: should show an
   integer feedback level (e.g. `163`) and update on every
   `info[5]` increment.
4. **SATT auto-attenuate engages on HL2**: feedbackLevel converges into
   `[128, 181]` within ~2-5 s of MOX-on.  HL2's `m_minAttDb=-28`
   widens the lower bound vs other boards (signed range).
5. **EP6 frames still parse cleanly during PS-MOX**: log
   `nereus.connection: P1: parseEp6Frame ...` lines should NOT show
   `parseEp6Frame rejected frame`.  If they do, see "Out-of-scope"
   note above re: nddc=4 vs DDC1-only data layout.
6. **HermesII bench validation** (if hardware available): bank 2 + bank 3
   wire-byte capture should show TX freq during PS-MOX.  pcap with
   Wireshark + filter `udp.port == 1024`, watch for C0=0x04 / 0x06 →
   frequency bytes match TX VFO.

## mi0bot source-first audit (per-board PS DDC pair + HL2 ATT bounds)

Discovered during the post-Task-17-P1 audit (2026-05-07):

### Gap 1: per-board PS DDC pair indices

mi0bot's `MetisReadThreadMainLoop` dispatch (networkproto1.c:380-392
[v2.10.3.13-beta2]) places the PureSignal pair on different DDC slots
per `nddc` value:

| `nddc` | Boards | PS pair (psFb / txMon) | Source |
| --- | --- | --- | --- |
| 2 | HermesII / ANAN-10E / ANAN-100B | DDC0 / DDC1 | `twist(spr, 0, 1, 0)` line 380-381 |
| 4 | Hermes / HL2 / ANAN-10 / ANAN-100 | **DDC2 / DDC3** | `twist(spr, 2, 3, 1)` line 385 + 551 |
| 5 P1 | Orion-class P1 (rare) | DDC3 / DDC4 | `twist(spr, 3, 4, 1)` line 390 |
| 5 P2 | Saturn / Orion / etc. | DDC0 / DDC1 | `network.c:936-945` freq override [v2.10.3.13] |

Cross-referenced with mi0bot `console.cs:8579+ GetDDC()` table:
HL2 P1 case 5 (MOX+PS): `rx1=0, rx2=1, psrx=2, pstx=3`.
HermesII P1 case 5: `psrx=0, pstx=1`.
Orion-class P1 case 5: `psrx=3, pstx=4`.

Pre-audit, NereusSDR's `PsccPump` defaulted to `psFbDdc=0, txMonDdc=1`
unconditionally (cmaster.cs:533-534 [v2.10.3.13]) — correct for nddc=2
boards and Saturn-class P2, but **silently broken on HL2 / Hermes /
ANAN-10 / ANAN-100** (would have paired DDC0+DDC1 — both at RX1/RX2
freq during PS-MOX, no PS feedback signal in the stream).

**Fix landed:** `PsDdcConfig` gains `psFbDdc / txMonDdc` fields; each
per-board codec's `applyPureSignalDdcConfig` populates them per the
Thetis dispatch table; `PsccPump::onDdcConfigChanged` reads them
instead of hard-coding (0, 1).  Codecs updated:

- `P1CodecHl2` PS-on branch → (2, 3)
- `P1CodecStandard::psDdcConfigHermesClass` → (2, 3)
- `P1CodecStandard::psDdcConfigHermesIIClass` → (0, 1) explicit
- `P1CodecStandard::psDdcConfigG2Class` → (3, 4)
- `P1CodecRedPitaya` → (3, 4)
- `P2CodecOrionMkII` → (0, 1) explicit

8 new tests in `tst_ps_ddc_indices_per_board` pin the per-board
indices.

### Gap 2: HL2 NeedToRecalibrate lower-bound

mi0bot has a separate HL2 variant of NeedToRecalibrate at
`PSForm.cs:1142-1144 [v2.10.3.13-beta2]`:

```csharp
// MI0BOT: Needed seperate function for HL2
public static bool NeedToRecalibrate_HL2(int nCurrentATTonTX) {
    return (FeedbackLevel > 181 ||
            (FeedbackLevel <= 128 && nCurrentATTonTX > -28));
}
```

`> -28` instead of the legacy `> 0` because HL2's signed ATT range is
`[-28, +31]`.  Pre-audit, NereusSDR used hardcoded `> 0` in
`autoAttentionTick`'s needRecal predicate — **HL2 with ATT in [-28, -1]
would silently skip recalibration** even when feedbackLevel was ≤128.

**Fix landed:** unified predicate
`currentAttOnTx > m_stepAtt->minAttenuation()`.  StepAttenuator's
`minAttenuation()` returns the per-board floor (0 for legacy, -28 for
HL2 set in BoardCapsTable's attenuator min), so the unified predicate
is byte-for-byte equivalent to both Thetis branches without an
explicit board check.

### Gap 3: HL2 SetNewValues clamp

mi0bot HL2 bypasses the `> 0` clamp entirely at PSForm.cs:786-790:

```csharp
if (HPSDRModel.HERMESLITE == HardwareSpecific.Model)
{
    newAtten = oldAtten + _deltadB;
    //MI0BOT: HL2 can handle negative up to -28, just let it be handled
    //in ATTOnTx section
}
else
{
    if ((oldAtten + _deltadB) > 0) newAtten = oldAtten + _deltadB;
    else                           newAtten = 0;
}
```

**Fix landed:** unified clamp `max(oldAtten + deltaDb, minAttenuation())`.
Same `minAttenuation()` per-board floor.  HL2 lets the value pass
through to `setAttOnTxValue`, which has its own [-28, +31] clamp.

### Verified-correct (no gap)

- **calcc.c / iqc.c / sync.c** — byte-identical between mi0bot and
  ramdor; no HL2-specific divergence.
- **PSDefaultPeak** — already 0.233 for HL2 in `kHermesLite`
  (Phase 3M-4 Task 1 commit `1bbb85a`).
- **psSampleRate=0 sentinel** — already handled in `P1CodecHl2`'s
  PS-on branch (rate[0]=rate[1]=rx1Rate per mi0bot console.cs:8478-8479).
- **HL2 cntrl1=4 ADC steering** — already populated by
  `P1CodecHl2::applyPureSignalDdcConfig`; now actually reaches the wire
  via `P1RadioConnection::applyPsDdcConfig` (Task 17 P1 follow-up).
- **HL2 6-bit step ATT field** — bank 4 C3 mask `& 0x1F` (5-bit) is
  correct for HL2 because the HL2 codec subclass's bank 4 doesn't
  override.  HL2 firmware accepts 0-31; signed range `-28..-1` is
  encoded via `31 - userDb` in `setAttOnTxValue` chain (BoardCaps
  attenuator wire-encoding).

### Verification matrix updates

After this audit, the bench tester checklist gains two new checks:

7. **HL2 PS-MOX pscc consumes DDC2+DDC3** (not DDC0+DDC1).  Confirm via
   `nereus.dsp: PureSignal info[]: state=...` log lines fluctuating —
   if calcc never advances past state=2 (LSETUP), PsccPump may still be
   wired to wrong DDCs.  Cross-check: log
   `nereus.connection: P1: applyPsDdcConfig nDdc=4 ...` should appear
   on first MOX with PS-A enabled, AND `PsccPump: setActive(true)
   txMonDdc=3 psFbDdc=2` should appear immediately after.
8. **HL2 ATT goes negative under auto-att**: with PS active and a
   well-tuned PA, `nereus.dsp: PureSignal: AutoAtt setAttOnTx X → Y dB`
   may show Y in `[-28, -1]`.  Pre-audit, the clamp would have stopped
   at 0 — so seeing a negative postcondition confirms the new
   `minAttenuation()` clamp path is live.

---

## What's done (original handoff text below)

Phase 3M-4 PureSignal port. **15 task-commits + 1 bench-fix round** landed on
branch `claude/wizardly-poitras-dbab75` (ahead of `main` by 18 commits).
Latest SHA: `55dd43c`.

| # | Topic | Commit |
| --- | --- | --- |
| Foundation | Design doc | `40a1d25` |
| Foundation | Implementation plan | `4b78580` |
| 1 | BoardCapabilities (psDefaultPeak + psSampleRate) | `1bbb85a` |
| 2 | Vendor calcc.c + iqc.c verbatim | `b57e489` |
| 3 | TxChannel 22 PS API wrappers + ps_sync_stub.c | `611ff86` |
| 4 | PsFeedbackChannel | `c6f6857` |
| 5 | Per-board codec applyPureSignalDdcConfig | `909dd2a` |
| 6 | ReceiverManager UpdateDDCs PS branch | `4739b4a` |
| 7 | PureSignal coordinator + TransmitModel seam | `985c0a5` |
| 8 | PsForm modeless dialog | `faf9369` |
| 9 | AmpView + AmpViewChart | `53ff9b0` |
| 10 | PsaIndicatorWidget bottom banner | `315c22d` |
| 11 | Setup deltas + IMD warning + 2 General Options checkboxes | `4e4c573` |
| 12 | SpectrumWidget IMD overlay | `c9286c0` |
| 13 | Applet wiring + RadioModel late-bind seam | `2271f8a` |
| 14 | Retire 2 NereusSDR-only Setup pages | `9afa279` |
| 15 | Verification matrix + persistence-gap fix | `c87ec89` |
| Bench-fix round 1 | Visibility + lifecycle (NOT data path) | `55dd43c` |

ctest: 366/366 PASS at `c87ec89`. Bench-fix round 1 touched the tests
slightly but the suite is still green at `55dd43c`.

---

## What's broken (your starting point)

Bench testing on ANAN-G2 + Saturn revealed PureSignal does not actually
work end-to-end. User-visible symptoms:

1. **PsForm Calibration Information group all zeros** (state, sln.chk, cor.cnt,
   dg.cnt, GetPk, SetPk, feedbk all stuck at 0).
2. **PureSignalApplet doesn't animate** (gauges/LEDs/info labels static).
3. **Bottom-banner FB+PS pair shows no numeric feedback level or
   "Correcting" text** even when PS-A on + 2-Tone TX.
4. **IMD overlay doesn't appear** when user clicks 2-Tone in PsForm + checks
   "Show 2Tone measurements" in Advanced section.
5. **GetPk / SetPk readouts are 0.0** instead of 0.2899 (G2) / 0.6121
   (Saturn) / 0.233 (HL2) per-board defaults.

Root cause is architectural, not a tweak.

---

## Source-first read findings (verified)

**Thetis canonical data flow** (verified end-to-end against
`v2.10.3.13 @ 501e3f51`):

### 1. Banner state model (one consolidated method, not 5 signals)

`ucInfoBar.cs:802-825` declares 5 private state fields PLUS the canonical
mutator method:

```csharp
private bool _bCorrectionsBeingApplied = false;
private bool _bCalibrationAttemptsChanged = false;
private bool _bFeedbackLevelOk = false;
private Color _feedbackColour = Color.Black;
private int _nFeedbackLevel = 0;

public void PSInfo(int level, bool bFeedbackLevelOk,
                   bool bCorrectionsBeingApplied,
                   bool bCalibrationAttemptsChanged,
                   Color feedbackColour)
{
    if (_shutDown) return;

    _bCalibrationAttemptsChanged = bCalibrationAttemptsChanged;

    if (_bCalibrationAttemptsChanged && _mox)
    {
        _nFeedbackLevel = level;
        _feedbackColour = feedbackColour;
        _bCorrectionsBeingApplied = bCorrectionsBeingApplied;
        _bFeedbackLevelOk = bFeedbackLevelOk;

        updatePSDisplay();

        _psTimer.Start();   // restart fade timer
    }
}
```

**5 fields update atomically in one method call.** No separate
Q_PROPERTY signals per field.

### 2. updatePSDisplay state machine (`ucInfoBar.cs:839-895`)

```csharp
private void updatePSDisplay()
{
    if (!_psEnabled)
    {
        // PS-A off: DimGray on both labels, "Feedback" / "Pure Signal2"
    }
    else if (_mox)
    {
        if (_bCorrectionsBeingApplied) {
            lblPS.Text = _useSmallFonts ? "Correct" : "Correcting";
            lblPS.BackColor = Lime;
        } else {
            lblPS.Text = "Pure Signal2";
            lblPS.BackColor = SeaGreen;
        }

        lblFB.BackColor = _feedbackColour;

        if (_hideFeedback || !_bCalibrationAttemptsChanged) {
            lblFB.Text = _useSmallFonts ? "FB" : "Feedback";
        } else {
            lblFB.Text = _nFeedbackLevel.ToString();   // <--- THE NUMBER
        }
    }
    else
    {
        // PS-A on, no MOX: SeaGreen on both, "Feedback" / "Pure Signal2"
    }
}
```

**Critical gate:** the numeric feedback level is shown ONLY when
`_psEnabled && _mox && _bCalibrationAttemptsChanged && !_hideFeedback`.

`_correcting` does NOT exist in ucInfoBar.cs. The port added a phantom
`m_correcting` field that should be removed.

### 3. PSForm timer1code (the calling pattern, `PSForm.cs:555-619`)

```csharp
private void timer1code()
{
    if (!_bPSRunning) return;

    puresignal.GetInfo(_txachannel);

    if (puresignal.HasInfoChanged) {
        // Update PsForm's own labels (lblPSInfo0..15)
    }

    if (puresignal.CorrectionsBeingApplied) {
        btnPSSave.Enabled = true;
        // ... PsForm CO indicator color logic
    }

    if (_autocal_enabled && puresignal.HasInfoChanged) {
        console.InfoBarFeedbackLevel(
            puresignal.FeedbackLevel,
            puresignal.IsFeedbackLevelOK,
            puresignal.CorrectionsBeingApplied,
            puresignal.CalibrationAttemptsChanged,
            puresignal.FeedbackColourLevel);
    }
    // ...
}
```

**Two important gates:**
- `_bPSRunning` (the polling-active flag) — set when PSForm is shown
- `_autocal_enabled && puresignal.HasInfoChanged` — push to ucInfoBar
  ONLY when auto-cal is on AND info actually changed since last poll

### 4. Host dispatch (`console.cs:2307-2313`)

```csharp
public void InfoBarFeedbackLevel(int level, bool ok, bool corrApplied,
                                  bool calChanged, Color feedbackColour)
{
    if (infoBar.InvokeRequired)
        infoBar.Invoke(new Action(() => infoBar.PSInfo(level, ok,
            corrApplied, calChanged, feedbackColour)));
    else
        infoBar.PSInfo(level, ok, corrApplied, calChanged, feedbackColour);
}
```

`InvokeRequired` cross-thread guard — equivalent in Qt is
`QMetaObject::invokeMethod(... Qt::QueuedConnection)` if poll is on a
worker thread, or just direct call if main thread. NereusSDR's
`PureSignal::pollTimerTick` is on the main thread (QTimer with default
parent), so direct call is fine.

### 5. PSAEnabled wiring (`console.cs:2284, 43717`)

```csharp
infoBar.PSAEnabled = psform.AutoCalEnabled;
```

The banner's `_psEnabled` field is set when PS-A toggles, NOT when
master "PureSignal" enable toggles. NereusSDR's `PureSignal::
isAutoCalEnabled()` is the analog.

### 6. Per-board PSDefaultPeak auto-init (`cmaster.cs:566` mi0bot, `:536` ramdor)

```csharp
// mi0bot (canonical for HL2):
puresignal.SetPSHWPeak(txch, HardwareSpecific.PSDefaultPeak);
// MI0BOT: Correct for correct PS value

// ramdor (older):
puresignal.SetPSHWPeak(txch, 0.2899);  // hardcoded P2 default
```

This is called from cmaster's TX channel construction. **NereusSDR's port
does not auto-call `SetPSHWPeak` with `caps.psDefaultPeak` at TX channel
init.** Result: `GetPk` / `SetPk` readouts in PsForm and `puresignal.GetPSHWPeak()`
return 0.0 instead of the per-board default.

`BoardCapabilities::psDefaultPeak` is correctly populated (Task 1, commit
`1bbb85a`):
- ANAN_G2 / G2_1K → 0.6121 (Saturn case)
- ANAN_100D / 200D / 7000D / 8000D / OrionMkII (P2 default) → 0.2899
- HermesLite (HL2) → 0.233
- HermesII / ANAN10 / 10E / 100 / 100B (P1 default) → 0.4072

The value is stored but never written to WDSP via `SetPSHWPeak`.

### 7. HasInfoChanged + CalibrationAttemptsChanged semantics

Defined in Thetis `puresignal` C# class (search for those properties).
Both compute from info[] deltas:
- `HasInfoChanged`: ANY index in info[] differs from previous poll
- `CalibrationAttemptsChanged`: `info[5]` (cal count) differs from previous

NereusSDR's `PureSignal::pollTimerTick` should compute these by
maintaining a previous-frame info[] snapshot and comparing.

---

## Gaps in the port (the work)

| Gap | Thetis cite | NereusSDR file | Fix |
| --- | --- | --- | --- |
| Split Q_PROPERTY signals | `ucInfoBar.cs:808-825` PSInfo() | `PsaIndicatorWidget.h/.cpp`, `PureSignal.h` | Add `PsaIndicatorWidget::psInfo(level, ok, corrApplied, calChanged, color)` consolidated method. Strip per-field signal subscriptions for banner state. |
| Phantom `m_correcting` field | `ucInfoBar.cs:802-806` (only 5 state fields, none is "correcting") | `PsaIndicatorWidget.h/.cpp` | Remove `m_correcting` member + setCorrecting setter. updatePSDisplay branches on `m_correctionsApplied` only. |
| `m_calChangedSinceLastDraw` local computation | `puresignal.CalibrationAttemptsChanged` (computed in coordinator) | `PsaIndicatorWidget.cpp` + `PureSignal.cpp` | Move calibration-attempts-changed computation into PureSignal (compare info[5] vs previous). Pass through PSInfo() call. |
| No `_autocal_enabled && HasInfoChanged` gate | `PSForm.cs:614-619` | `PureSignal::pollTimerTick` | Wrap the InfoBarFeedbackLevel-equivalent call in `if (m_autoCalEnabled && hasInfoChanged) { ... }`. |
| No SetPSHWPeak auto-init | `cmaster.cs:566` (mi0bot) | TxChannel init OR PureSignal coordinator | Call `m_tx->setPSHWPeak(caps.psDefaultPeak)` when TX channel becomes available + caps known. |
| No HasInfoChanged delta computation | `puresignal.HasInfoChanged` property | `PureSignal::pollTimerTick` | Add `m_oldInfo[16]` snapshot field; compute delta in pollTimerTick. |

---

## Suggested fix sequence (one commit per fix is fine, or one big "Task 16")

### Step 1: PureSignal coordinator — info[] delta computation + PSInfo dispatch

In `PureSignal.cpp pollTimerTick`:

```cpp
void PureSignal::pollTimerTick()
{
    if (!m_tx) return;

    int newInfo[16] = {};
    m_tx->getPSInfo(newInfo);

    // From Thetis puresignal class HasInfoChanged property [v2.10.3.13]
    bool hasInfoChanged = std::memcmp(newInfo, m_oldInfo, sizeof(newInfo)) != 0;

    // From Thetis puresignal class CalibrationAttemptsChanged property
    // [v2.10.3.13] (info[5] = cal count, MUST verify exact index from
    // calcc.c GetPSInfo implementation — info[] semantics are calcc-internal).
    bool calAttemptsChanged = newInfo[5] != m_oldInfo[5];

    bool corrApplied = (newInfo[6] != 0);   // VERIFY index from calcc.c
    int  feedbackLevel = newInfo[1];        // VERIFY index from calcc.c

    std::memcpy(m_oldInfo, newInfo, sizeof(newInfo));

    if (!hasInfoChanged) return;

    // From Thetis PSForm.cs:614-619 [v2.10.3.13]:
    //   if (_autocal_enabled && puresignal.HasInfoChanged)
    //       console.InfoBarFeedbackLevel(...);
    if (m_autoCalEnabled) {
        emit psInfoChanged(feedbackLevel,
                           /*feedbackLevelOk=*/feedbackLevel >= 128 && feedbackLevel <= 181,
                           corrApplied,
                           calAttemptsChanged,
                           computeFeedbackColour(feedbackLevel));
    }

    // ... existing per-field signal emissions (kept for PsForm backward-
    //     compat; PsaIndicator should switch to psInfoChanged).
}
```

**Index verification**: read `third_party/wdsp/src/calcc.c` GetPSInfo
function (around line 914) to confirm which info[] indices map to
feedbackLevel, correctionsBeingApplied, calCount. Do NOT guess.

### Step 2: PsaIndicatorWidget — psInfo() consolidated method

```cpp
// Thetis: ucInfoBar.cs:808-825 PSInfo() [v2.10.3.13]
void PsaIndicatorWidget::psInfo(int level, bool feedbackLevelOk,
                                 bool correctionsBeingApplied,
                                 bool calibrationAttemptsChanged,
                                 const QColor& feedbackColour)
{
    m_calChangedSinceLastDraw = calibrationAttemptsChanged;

    if (m_calChangedSinceLastDraw && m_mox) {
        m_feedbackLevel = level;
        m_feedbackColour = feedbackColour;
        m_correctionsApplied = correctionsBeingApplied;
        // m_bFeedbackLevelOk = feedbackLevelOk; // unused for now
        updateDisplay();
    }
}
```

Strip `m_correcting` + setCorrecting setter from PsaIndicator.

Update PsaIndicator wiring: connect `PureSignal::psInfoChanged` →
`PsaIndicator::psInfo`. Remove subscriptions to feedbackLevelChanged /
correctingChanged / calibrationCountChanged (those signals can stay for
PureSignalApplet which uses them differently, or be deprecated).

### Step 3: SetPSHWPeak auto-init at TX channel ready time

When TxChannel is constructed in WdspEngine and PureSignal coordinator is
wired up, push the per-board default:

```cpp
// In PureSignal::setTxChannel(TxChannel* tx) or wherever the binding
// becomes complete:
if (m_tx && m_caps.hasPureSignal) {
    // From Thetis cmaster.cs:566 [v2.10.3.13-beta2] (mi0bot):
    //   puresignal.SetPSHWPeak(txch, HardwareSpecific.PSDefaultPeak);
    m_tx->setPSHWPeak(m_caps.psDefaultPeak);
}
```

Or thread through `applyBoardCapabilities` (which Task 7 added but never
calls SetPSHWPeak from).

### Step 4: Verify in bench

After Steps 1-3 commit:
1. Rebuild + relaunch
2. Connect to ANAN-G2
3. Open PsForm — Calibration Information should show non-zero values once
   2-Tone TX engages
4. GetPk should read 0.6121 (G2 = Saturn case)
5. Click PS-A in TxApplet → bottom banner appears (already fixed in
   round 1)
6. Engage 2-Tone → banner FB label should show numeric feedback level
   (0-255 integer); PS label should show "Correcting" Lime when calcc
   reports correctionsBeingApplied
7. Check "Show 2Tone measurements" in PsForm Advanced section → IMD
   overlay box appears on spectrum

### Step 5: Add diagnostic logs (revert before merge)

```cpp
// In pollTimerTick, log once per second so bench tester sees data flow:
static int frameCounter = 0;
if (++frameCounter % 10 == 0) {
    qCDebug(lcDsp) << "PS poll: state=" << newInfo[0]
                   << "fbLevel=" << feedbackLevel
                   << "corrApplied=" << corrApplied
                   << "calChanged=" << calAttemptsChanged
                   << "hasInfoChanged=" << hasInfoChanged;
}
```

Useful while debugging; revert before merge.

---

## Out of scope for the next session

- Don't touch the per-board codecs (Task 5, commit `909dd2a`) — verified working.
- Don't touch ReceiverManager UpdateDDCs (Task 6, `4739b4a`) — verified working.
- Don't touch the WDSP vendored files (`third_party/wdsp/src/calcc.c` + `iqc.c`) — they're verbatim from upstream.
- Don't touch the duplicate-header bug in DiversityApplet / DigitalApplet / TunerApplet / CwxApplet / CatApplet / DvkApplet — that's a follow-up sweep outside 3M-4 scope.

---

## Source-first directive (binding for next session)

NereusSDR is a port of Thetis to C++/Qt6. Every line of ported logic
must come from upstream source you have READ. Do not infer. Do not
fabricate. Do not improvise.

### Thetis paths

- ramdor: `/Users/j.j.boyd/Thetis/Project Files/Source/`
  (`v2.10.3.13 @ 501e3f51`)
- mi0bot (HL2 deltas): `/Users/j.j.boyd/mi0bot-Thetis/Project Files/Source/`
  (`v2.10.3.13-beta2 @ c26a8a4c`)

### STOP-AND-ASK rule

If you cannot locate a Thetis source for any value, range, default,
behavior, or attribution: STOP. Report back. Do NOT invent. Do NOT
infer from general knowledge.

### Inline cite versioning

Every new `// From Thetis ...` cite carries `[v2.10.3.13]`. Every
`// From mi0bot-Thetis ...` cite carries `[v2.10.3.13-beta2]`. Per
`feedback_inline_cite_versioning.md`.

### License / attribution

- Byte-for-byte upstream license headers when porting a new file
  (HOW-TO-PORT.md template).
- Every inline author tag (`//MW0LGE`, `//W2PA`, `//DH1KLM` etc.)
  preserved within ±5 lines of the cited Thetis line.
- Pre-commit hooks must pass green.

### NereusSDR conventions

- C++20, Qt6, RAII (no naked new/delete; std::unique_ptr or Qt parent).
- AppSettings (NOT QSettings).
- Atomic for cross-thread DSP params.
- GPG-sign all commits.

---

## Reference docs

- [phase3m-4-puresignal-design.md](phase3m-4-puresignal-design.md) — design (846 lines, 17 sections; ratified during brainstorming)
- [phase3m-4-puresignal-plan.md](phase3m-4-puresignal-plan.md) — original 15-task implementation plan
- [phase3m-4-verification.md](phase3m-4-verification.md) — bench acceptance matrix
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/ucInfoBar.cs:802-895` — banner state model
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/PSForm.cs:555-619` — timer1code calling pattern
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/console.cs:2280-2313, 43717` — host dispatch + PSAEnabled wiring
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/cmaster.cs:566` (mi0bot) — SetPSHWPeak per-board init
- `/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/calcc.c:914` — GetPSInfo info[] semantics

---

## Worktree + branch state

- **Worktree**: `/Users/j.j.boyd/NereusSDR/.claude/worktrees/wizardly-poitras-dbab75`
- **Branch**: `claude/wizardly-poitras-dbab75`
- **Latest SHA**: `55dd43c`
- **ctest baseline**: 366/366 PASS at last full run
- **App binary**: `build/NereusSDR.app/Contents/MacOS/NereusSDR`
- **Bench radio**: ANAN-G2 (Saturn) — caps.hasPureSignal=true, psDefaultPeak=0.6121, psSampleRate=192000

---

*Handoff prepared 2026-05-06. The next session should start by reading
this file, then reading the cited Thetis source files end-to-end (no
skimming), then proceed with the fix sequence above.*
