# WDSP 2.10 / NNR / PS3 execution ledger

Execution authorized by the user after approval of the design, plan, and
explicit persistence requirements. Use yonder-cost-aware-execution.

## Coordination

- Feature checkout: `codex/wdsp210-nnr-ps3-design`; initial HEAD `66cb0eb8`.
- Combined integration: `codex/integrate-r2-main`, owned by the existing
  “Resume Nereus core split” task. Accepted checkpoint reported: `200d2a0e`.
- The colleague is completing TGXL Task 4d in shared model/session/CMake files,
  followed by R-R3-35 traffic and client-buffer telemetry. Hold rebase/shared
  interface edits until the next clean signed checkpoint is supplied.
- Their protocol minor 4 and TGXL capability/verbs remain reserved. Coordinate
  subsequent NNR/PS3 capabilities and transport accounting on top of that work.
- Combined landing is reserved until WDSP/NNR/PS3 software gates pass. No merge,
  deployment, live RF, or remote transmit expansion is implied.

## Work ownership and state

| Task | Owner | State / evidence |
| --- | --- | --- |
| 1. Baseline and vendor inventory | Lead + bounded vendor specialist | Passed software baseline; 170-file manifest/hash audit and fixed CFC fixture; clean integration rebase remains coordinated |
| 2. Pinned vendor / compatible ABI | Vendor specialist + PS binding specialist | Import and C++ ABI/display migration in progress |
| 3. NNR settings and DSP | Lead | Accepted configuration/readback adapter in progress |
| 4. PS3 coordinator and preferences | Lead | Pending stable vendor boundary |
| 5. Station assets | Asset specialist + lead integration | Source-format parsers and atomic store in progress; live application follows engine boundary |
| 6. Session services and daemon persistence | Lead + integration colleague coordination | Pending clean shared-interface checkpoint |
| 7. NNR popup / Setup | Lead | Pending accepted model/service interfaces |
| 8. PS dialog / AmpView | Lead | Pending accepted coordinator/display interfaces |
| 9. Combined verification | Lead + integration colleague | Pending implementation; hardware/operator evidence unobserved |

## Evidence

- Design/plan: signed commits `73eaad4b`, `66cb0eb8`; 24 requirements mapped to
  nine tasks; document validation and existing pre-commit gates passed.
- Colleague-reported baseline: integration `200d2a0e`, 692/692 tests passed
  before its receive-only Core/GUI deployment. This is evidence for that
  unchanged integration revision, not evidence for the new WDSP feature.
- Feature baseline: configured from `66cb0eb8` using Ninja, RelWithDebInfo,
  `NEREUS_BUILD_TESTS=ON`, AppleClang 21, CMake 4.3.1, Qt 6, macOS 27.0 arm64.
  DFNR and MNR are enabled; the already cached DFNR library/model were copied
  into ignored dependency paths and their SHA-256 hashes checked unchanged.
  No radio was connected or operated by this task.
- Baseline build passed. Eleven selected existing NR/PS/CFC tests passed
  (27.25 seconds): TX PS setters, PS symbol smoke, PS coordinator, paired pump,
  feedback channel, RX/Slice EMNR, CFC setters/display, and microphone-profile
  CFC persistence/live path. The fixed CFC characterization captures 65
  compression/equalization pairs from the original linked engine, covering the
  constructor profile and all four independent Qg/Qe combinations. Reference
  tolerance is relative `1e-11`; tests cannot regenerate the reference.
- Relevant vendor/DSP files are unchanged from the feature baseline through
  the colleague's stable `200d2a0e`, verified by Git diff. Root/test CMake files
  differ and will be reconciled after their next clean signed checkpoint.
- Platform/RF/performance acceptance for WDSP 2.10: pending. No PS3 distortion
  improvement or Rock 5C NNR capacity result is claimed.

## Next action

Implement the audited vendor compatibility changes, PS3 ABI/display boundary,
and non-actuating asset parser/store in disjoint files while the lead implements
NNR accepted settings. Reconcile the colleague's next signed checkpoint before
touching shared model/session interfaces. Root/test CMake and Git remain
lead-owned. Per-field NNR apply/readback will occur under one native DSP lock
so a refused model selection cannot partially apply the accompanying tuning.
