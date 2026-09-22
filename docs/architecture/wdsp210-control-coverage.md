# WDSP 2.10 control coverage

Pinned source: TAPR `b02d5bac675dd2f33ec2bab2b339f79a597c47dd`,
`wdsp 2.10/Source`. This is the implementation inventory for
[the approved design](2026-09-21-wdsp210-nnr-ps3-design.md) and
[plan](2026-09-21-wdsp210-nnr-ps3-plan.md). Entries are acceptance contracts;
implementation/verification is pending until recorded below.

## NNR

| Upstream capability | UI placement and behavior | Domain/default or ownership |
| --- | --- | --- |
| `SetRXANNRRun` | NNR selection/toggle; actual processing/bypass state in status | Off/on; new-config default off |
| `SetRXANNRModel`, `GetRXANNRModel` | Model selector in popup and Setup; show actual returned selection | Slot 0 Standard, slot 1 Premium; default 0 |
| `SetRXANNRMaskFloor` | Suppression slider plus precise numeric entry | Manual recommends -50 to -10 dB; default -25 dB |
| `SetRXANNRPosition` | Advanced pre-/post-AGC selector; explain upstream's post-AGC recommendation | 0 pre, 1 post; default 1 |
| `SetRXANNRAlpha` | Advanced deep-filter alpha; reshapes gain below the knee, with 1 leaving that shaping neutral | 0-4; default 1 |
| `SetRXANNRAlphaKnee` | Advanced alpha knee / attenuation depth, positive dB | 0-40 dB; default 10 dB; preserve upstream sign convention |
| `SetRXANNRTau` | Advanced input-normalization time constant | 0.05-30 seconds; default 2 |
| `SetRXANNRMaxGain` | Advanced maximum filter gain | 0-24 dB; default 12 |
| `SetRXANNRSmooth` | Separate attack and release numeric/slider controls | Each 0-500 ms; default 0/0 |
| `SetRXANNRTestMode` | Diagnostics selector: Network, Identity, Low-pass; prominent diagnostic-state badge | Modes 0/1/2; normal 0; session-only |
| `SetRXANNRcmode` | Diagnostics output layout: duplicate real output to I/Q, or Q zero; explain that this is DSP layout, not speaker stereo pan | 0 duplicates, 1 Q zero; normal 1; session-only |
| `SetNNRModelPathSlot` | Models page: per-slot bundled model or explicit override; source/readiness/next-apply status | Process-wide paths set before receiver creation |
| `SetNNRModelPath` | Covered by slot-0 model management; no redundant conflicting path field | Alias for slot 0 |
| `getDelay_nnr`, `getRun_nnr`, readiness/model metadata accessors | Read-only diagnostics through a narrow safe WDSP adapter | Actual latency, active/bypassed state, sample rate, model readiness and identity |
| Profiling support | Diagnostic status/export when profiling is compiled in; show unavailable reason otherwise | Do not promise profiling data in builds where macros compile it out |


Normal tuning belongs to the radio-owning process and stable slice. The
AppSettings prefix is `hardware/<mac>/slices/<slice-id>/nnr/`; leaf keys and
properties are fixed by the plan's shared-interface table. `NrActive` stores
selection; normal settings load before enabling the DSP. TestMode and cmode
are transient. Slot assets are station/process owned, selected by asset ID,
loaded before receiver creation, with pending versus applied state exposed.
Readback/profiling entries are never persisted as configuration.

## PureSignal 3

| Capability | Operator surface / owner | Required behavior |
| --- | --- | --- |
| `SetPSControl`, `SetPSReset`, `SetPSMancal`, `SetPSAutomode`, `SetPSTurnon` | Coherent Off, Single, Automatic, and Use Current Correction actions | Coordinator owns valid transitions; avoid four independently inconsistent checkboxes |
| `SetPSRunCal` | Advanced collection/calibration processing enable with explicit state | Disabling it suspends `pscc` state-machine processing; an existing TXA correction can remain active. This is distinct from Off/Reset. |
| `SetPSLoopDelay` | Calibration interval / CPU trade-off in Timing | Seconds, existing UI range 0-100; new PS3 default 0; preserve saved supported values |
| `SetPSMoxDelay` | MOX settling wait in Timing | Seconds, retain existing UI range 0.1-10; new PS3 default 0.1; preserve saved supported values |
| `SetPSTXDelay` | Requested amplifier delay plus actual applied delay | Existing nanosecond UI range 0-25,000,000, 20 ns step; show returned applied value |
| `SetPSHWPeak`, `GetPSHWPeak` | Advanced hardware peak, current value, board default/reset | Positive finite value; preserve validated board/protocol scaling, not a universal copied default |
| `GetPSMaxTX` | Measured peak readback beside configured peak | Do not overwrite configuration while merely polling |
| `PSSaveCorr`, `PSRestoreCorr` | Saved-correction manager plus quick Save/Restore | Show pending/completed/error; restore can activate correction and requires actuation permission |
| `GetPSInfo` | Persistent status plus complete decoded/raw diagnostics | Use PS3 field semantics and bit masks |
| `GetPSDisp` | AmpView and its diagnostics | New bounded sample/curve snapshot described below |
| `SetPSFeedbackRate` | Display actual feedback rate and originating board/stream configuration | Core sets it to match the two sample streams; not an unrelated user override |
| `SetPSMox` | Display actual TX/MOX state; normal PTT/TX controller owns it | Never offer an independent checkbox that falsifies actual TX state |
| `pscc` and feedback routing | Read-only TX/RX stream identity, rate/alignment/continuity diagnostics where measured | Core owns sample-correlated processing; no raw-I/Q round trip through GUI |
| Existing auto-/quick-attenuation and feedback controls | Calibration/feedback group and applet shortcuts | Retain host behavior after adapting attempt-count/status interpretation |
| Existing two-tone settings and measurement toggle | Two-tone action and direct route to existing tone controls | Preserve settings and measurement surfaces; every TX entry point retains permission/interlock checks |


Normal configuration belongs to `PureSignalSettings` on the radio-owning
process. It uses `hardware/<mac>/pureSignal/` with existing `autoCalEnabled`
intent preserved and the additional keys fixed by the plan. One-shot actions,
MOX, measured peaks, live corrections, status counters, and pending operations
are never persisted or replayed. Saved file selection remains separate from
restore/apply. Remote actuation is refused until R4 TX authorization.

GUI geometry, On Top, advanced expansion, existing AmpView gain/phase/low-res
choices and series visibility remain GUI-local AppSettings preferences.

## Explicit exclusions and removed interfaces

- Removed PS2 calls `SetPSPinMode`, `SetPSMapMode`, `SetPSStabilize`,
  `SetPSPtol`, and `SetPSIntsAndSpi` receive no fake replacements. Preserve
  inactive legacy settings for rollback and remove their live controls.
- Compile-time algorithm constants, neural weights, raw tensor/allocator
  interfaces and channel/thread lifecycle primitives are implementation-owned.
  Supported runtime tuning discovered during the vendor inventory must be
  added to this inventory and surfaced; it cannot be dismissed as an internal.
- Hardware feedback rate and actual MOX are visible readbacks controlled by
  stream/TX ownership, not independent operator overrides.

## Verification ledger

| Boundary | Required evidence | State |
| --- | --- | --- |
| Upstream symbols and ABI | Public header plus implementation census; all aliases accounted for | Pending vendor inventory |
| NNR engine/settings | Both models, every tuning field, real applied readback, missing model, per-radio/slice restart/reset | Pending implementation |
| NNR UI | Every row accessible via popup/Setup, fractional values, no right-click enable, stale slice/session rejection | Pending implementation |
| PS3 coordinator | Attempts/successes, compound bits, processing pause/off ordering, worker teardown | Pending implementation |
| PS3 files/display | Real completion generation, validated correction round-trip, bounded counts, correct phase/curve transform | Pending implementation |
| Session ownership | Accepted-state result, old peers, receive-only refusal, no action replay, transport bounds | Pending implementation |
| Persistence | Local and daemon restart, inactive slice edit, rejected edit, migration, two radios, GUI preferences | Pending implementation |
| Native UI/operator | Minimum-size/scaled layouts, actual control workflows and listening comparison | Pending implementation/operator evidence |
| Hardware | Rock NNR workload and authorized local PS3 feedback/RF measurements | Pending arranged bench |
