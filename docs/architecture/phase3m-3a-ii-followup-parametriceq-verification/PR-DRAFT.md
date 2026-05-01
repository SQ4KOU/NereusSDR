# Phase 3M-3a-ii Follow-up — `ucParametricEq` Qt6 Port (10 batches)

## Summary

Full Qt6 port of Thetis's `ucParametricEq.cs` (3396 LOC, sole author
Richard Samphire MW0LGE) into a new `src/gui/widgets/ParametricEqWidget`,
plus the plumbing (`ParaEqEnvelope` gzip+base64url, `TransmitModel.txEqParaEqData`
field, `MicProfileManager.TXParaEQData` bundle key,
`TxChannel::getCfcDisplayCompression` wrapper) and the two consuming
dialog rewrites (`TxCfcDialog` + `TxEqDialog`).

This addresses the brief at
[`docs/architecture/phase3m-3a-ii-cfc-eq-parametriceq-handoff.md`](docs/architecture/phase3m-3a-ii-cfc-eq-parametriceq-handoff.md)
— the 3M-3a-ii-shipped TxCfcDialog landed "scalar-complete and
spartan" (a profile combo, two global spinboxes, and a 10×3 spinbox
grid for F/COMP/POST-EQ); JJ asked for true Thetis parity. This PR
delivers.

## Per-batch commits

(SHAs are post-rebase onto current `main` at `f40f5a0`.)

| Batch | Commit | Summary |
|---|---|---|
| Plan | `ef68d04` | docs(3M-3a-ii-followup): ParametricEq port plan (10 batches) |
| 0 | `7469279`, `c37cb4b` | docs(plan): refinements before execution |
| 1 | `fe1afd0` | feat(widget): ParametricEqWidget skeleton + palette + ctor (Batch 1) |
| 2 | `d745611`, `48f2ae0`, `4d85c5f` | Batch 2: axis math + ordering + reset + cleanup + 3 fixes |
| 3 | `656c9ab`, `246e51a` | Batch 3: paintEvent + 10 draw helpers + bar chart timer + Qt rect doc |
| 4 | `78aa788`, `b866f86`, `e6792fc` | Batch 4: mouse + wheel + 6 signals + cleanup + 4 fixes |
| 5 | `cb64ef4`, `e8d02de` | Batch 5: JSON + public API + PROVENANCE + 4 review fixes |
| 6 | `9794a5b` | Batch 6: ParaEqEnvelope + TransmitModel + MicProfileManager round-trip |
| 7 | `e7809a0` | Batch 7: TxChannel::getCfcDisplayCompression WDSP wrapper |
| 8 | `8e103f8` | Batch 8: TxCfcDialog full Thetis-verbatim rewrite |
| 9 | `133be1e`, `3850097` | Batch 9: TxEqDialog parametric panel + style fix + cleanup |
| 10 | `c93506b` | docs: verification matrix + CHANGELOG (Batch 10) |
| Bench-1 | `0413f1f` | fix(applet): apply project styles to TxCfc/TxEq dialog widgets |
| Bench-2 | `e733fa6` | fix(applet): TxEqDialog parametric Reset button now actually resets |

## What changed

(See [`CHANGELOG.md`](CHANGELOG.md) "Phase 3M-3a-ii follow-up" entry for the
full breakdown.)

High-level:

- **`src/gui/widgets/ParametricEqWidget`** (NEW, ~3160 LOC h+cpp) — full
  Qt6 port of Thetis's `ucParametricEq.cs` (Richard Samphire MW0LGE,
  3396 LOC C# WinForms control). Five-batch port: skeleton + palette
  (B1), axis math + ordering + reset (B2), paintEvent + 10 draw helpers
  + 33 ms peak-hold bar-chart timer (B3), mouse + wheel + 6 Qt signals
  (B4), JSON marshal + 34-property public API (B5). All ports cite
  `ucParametricEq.cs [v2.10.3.13]` inline; GPLv2 + Samphire dual-
  license header preserved byte-for-byte.

- **`src/core/ParaEqEnvelope`** (NEW, ~330 LOC) — gzip + base64url
  envelope helper mirroring Thetis `Common.cs Compress_gzip /
  Decompress_gzip [v2.10.3.13]`. Byte-identical encode output with
  Thetis (verified via Python-fixture decode test).

- **`src/models/TransmitModel`** — new `txEqParaEqData` field (mirror of
  existing `cfcParaEqData` pattern), with getter / setter / signal.

- **`src/core/MicProfileManager`** — new `TXParaEQData` bundle key
  wired into capture + apply paths (50 → 51 keys; 91 → 92 total bundle
  keys).

- **`src/core/TxChannel::getCfcDisplayCompression`** (NEW) — WDSP
  wrapper for live CFC compression display data, used by `TxCfcDialog`'s
  50 ms bar-chart timer.

- **`src/gui/applets/TxCfcDialog`** — full Thetis-verbatim rewrite.
  Drops the spartan profile combo (TxApplet hosts profile management).
  Embeds two cross-synced `ParametricEqWidget` instances (compression
  + post-EQ curves) with 50 ms live bar chart, 5/10/18-band radios,
  freq-range spinboxes, 3 checkboxes, 2 reset buttons, OG CFC Guide
  LinkLabel. Hide-on-close.

- **`src/gui/applets/TxEqDialog`** — adds parametric panel behind
  `Legacy EQ` toggle (persists via AppSettings
  `TxEqDialog/UsingLegacyEQ`). Drops profile combo. Fixes legacy
  band-column slider/spinbox/header styling regression with
  `Style::sliderVStyle()` / `kSpinBoxStyle` / `kTextPrimary`.

- **`CMakeLists`** — adds `find_package(ZLIB REQUIRED)` + `ZLIB::ZLIB`
  link.

## Source-first compliance

- Every ported function carries a `// From Thetis <file>:N-M [v2.10.3.13]` cite
- 1739 inline cites validated by `verify-inline-tag-preservation.py`
- GPLv2 + Samphire dual-license header preserved byte-for-byte in
  `ParametricEqWidget.cpp` and `ParametricEqWidget.h`
- `THETIS-PROVENANCE.md` rows added for both new file pairs
  (`ParametricEqWidget` and `ParaEqEnvelope`)

## Bench feedback (already addressed)

JJ bench-tested on ANAN-G2 against this branch and flagged two issues
that were fixed in-branch before opening this PR:

- **Dialog widgets rendered dark-on-dark** against the project's dark
  theme — both new dialogs were inheriting the system Qt6 default style
  for spinboxes/combos/radios/checkboxes/group-boxes. Fixed by adding a
  dialog-level QSS block in each constructor that pulls in the project's
  `Style::k*Style` helpers via QSS selectors. Audit found 0/30 widget
  constructions in TxCfcDialog and 0/22 in TxEqDialog parametric panel
  had been styled. Commit `0413f1f`.
- **TxEq parametric Reset button was a no-op.** Root cause: the slot
  called `setBandCount(currentCount)`, but `setBandCount` early-returns
  at `ucParametricEq.cs:583 [v2.10.3.13]` when `v == m_bandCount`. Fix
  mirrors `TxCfcDialog::onResetCompClicked` — synthesizes flat-default
  arrays inline (gain=0, q=4, evenly spaced freq) and calls
  `setPointsData` directly. Commit `e733fa6`.

## Test coverage

- Full ctest: **280 / 280 green** at HEAD `e733fa6` (post-rebase: was
  256 before this branch; +5 new test executables from this PR, +19 new
  tests pulled in from main via the rebase).
- New automated test slots from this PR: **102** across 10 test files
  (4 + 10 + 7 + 10 + 16 + 9 + 8 + 6 + 21 + 11).
- Bench verification on ANAN-G2: profile round-trip + dialog opens +
  styling + Reset button confirmed live by JJ on `0573fc5` /
  pre-rebase build (logically equivalent to the post-rebase
  `e733fa6`). Outstanding bench items below.

## Test plan

- [ ] CI green (lint + ctest + provenance + inline-tag verifiers)
- [x] Manual bench on ANAN-G2: dialogs open + widgets visible against dark theme
- [x] Manual bench on ANAN-G2: TxEq parametric Reset button actually resets
- [ ] Manual bench on ANAN-G2: profile round-trip (CFC + TX EQ) — see verification README
- [ ] Manual bench on ANAN-G2: live bar chart visible during TX
- [ ] Manual bench on ANAN-G2: `Legacy EQ` persistence across restart
- [ ] Manual bench on ANAN-G2: cross-sync between the two `ParametricEqWidget` instances in TxCfcDialog
- [ ] Manual bench on ANAN-G2: Live Update on/off behavior
- [ ] (Optional, when convenient): cross-tool round-trip (Thetis Windows VM)

## Out of scope (future work)

- RX EQ dialog parity (separate sub-PR; `EqApplet` covers RX scalar controls)
- ALC / Lev curve overlay
- TX-chain multi-stage envelope visualizer
- 5/18-band per-band push to TM (currently scalar-only; TM holds fixed
  10-element CFC arrays — works because the widget round-trips
  through the gzip+base64url blob; only the Setup → CFC scalar grid is
  10-band)
- Re-skinning to NereusSDR palette (verbatim port; aesthetic tweaks
  if asked)

---

J.J. Boyd ~ KG4VCF
