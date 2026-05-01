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

| Batch | Commit | Summary |
|---|---|---|
| Plan | `ca42473` | docs(3M-3a-ii-followup): ParametricEq port plan (10 batches) |
| 0 | `0522cdd`, `a377ccb` | docs(plan): refinements before execution |
| 1 | `ca4c357` | feat(widget): ParametricEqWidget skeleton + palette + ctor (Batch 1) |
| 2 | `b1499c3`, `29fc966`, `334640b` | Batch 2: axis math + ordering + reset + cleanup + 3 fixes |
| 3 | `07d901b`, `3ff96a7` | Batch 3: paintEvent + 10 draw helpers + bar chart timer + Qt rect doc |
| 4 | `daf6300`, `6e72699`, `bb1e15d` | Batch 4: mouse + wheel + 6 signals + cleanup + 4 fixes |
| 5 | `ad4e020`, `7d7b28b` | Batch 5: JSON + public API + PROVENANCE + 4 review fixes |
| 6 | `784fcd3` | Batch 6: ParaEqEnvelope + TransmitModel + MicProfileManager round-trip |
| 7 | `6883306` | Batch 7: TxChannel::getCfcDisplayCompression WDSP wrapper |
| 8 | `8fd599d` | Batch 8: TxCfcDialog full Thetis-verbatim rewrite |
| 9 | `3ce9cb3`, `dbbcbfa` | Batch 9: TxEqDialog parametric panel + style fix + cleanup |
| 10 | (this commit) | docs: verification matrix + CHANGELOG (Batch 10) |

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

## Test coverage

- Full ctest: **261 / 261 green** (was 256 before this PR; +5 new test
  executables, 1 widget skeleton + axis + paint + interaction + json,
  plus Para EQ envelope, MicProfileManager round-trip, TxChannel CFC
  display, TxCfcDialog, TxEqDialog)
- New automated test slots: **102** across 10 test files
  (4 + 10 + 7 + 10 + 16 + 9 + 8 + 6 + 21 + 11)
- Bench verification on ANAN-G2 **pending JJ** — see
  [`docs/architecture/phase3m-3a-ii-followup-parametriceq-verification/README.md`](docs/architecture/phase3m-3a-ii-followup-parametriceq-verification/README.md)

## Test plan

- [ ] CI green (lint + ctest + provenance + inline-tag verifiers)
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

🤖 Generated with [Claude Code](https://claude.com/claude-code)
