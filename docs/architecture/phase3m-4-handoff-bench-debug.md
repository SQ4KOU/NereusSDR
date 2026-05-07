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
