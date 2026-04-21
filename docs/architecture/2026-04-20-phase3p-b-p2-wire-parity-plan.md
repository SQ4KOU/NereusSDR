# Phase 3P Sub-Phase B — P2 Wire-Bytes Parity + Alex Filter UI: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task.

**Goal:** Mirror the Phase A per-board codec architecture into `P2RadioConnection` (covering Orion-MKII / 7000D / 8000D / AnvelinaPro3 + Saturn / ANAN-G2 / G2-1K), populate Saturn's BPF1 band-edge override (G8NJJ contribution), split RxApplet's ADC OVL badge into per-ADC variants for dual-ADC boards, and ship the first pass of the Alex-1 / Alex-2 Filters Setup sub-sub-tabs (UI shells; live LED indicators land in Phase H).

**Architecture:** Reuses Phase A's `IP1Codec` + `CodecContext` + `AlexFilterMap` foundation; introduces a parallel `IP2Codec` interface with two subclasses (`P2CodecOrionMkII` base, `P2CodecSaturn` extends with BPF1 edges). `P2RadioConnection` cutover follows the same pattern as Phase A's `P1RadioConnection` cutover (Task 12 of Phase A) — feature-flag gated rollback via `NEREUS_USE_LEGACY_P2_CODEC=1`.

**Tech Stack:** Same as Phase A (C++20, Qt6, CMake/Ninja, `nereus_add_test()`, GPG-signed commits, pre-commit attribution gates).

**Spec:** [`docs/architecture/2026-04-20-all-board-radio-control-parity-spec.md`](2026-04-20-all-board-radio-control-parity-spec.md) §7.

**Phase A reference:** Where this plan says "follow Phase A pattern" or "same as Task X of Phase A", read [`2026-04-20-phase3p-a-p1-wire-parity-plan.md`](2026-04-20-phase3p-a-p1-wire-parity-plan.md). Patterns (TDD red gate, table-driven tests, `[@shortsha]` cite format, `// no-port-check:` markers on test files, regression-freeze JSON baseline, dual-call diagnostic during cutover) are fully established and don't get re-explained here.

**Upstream stamps:** `[@501e3f5]` (ramdor) · `[@c26a8a4]` (mi0bot — most P2 logic doesn't differ on mi0bot; cite ramdor canonical).

**Worktree:** `/Users/j.j.boyd/NereusSDR/.worktrees/phase3p-b-p2-wire-parity` (already created, branched off `phase3p-a-p1-wire-parity` tip — has all of Phase A's codec files available).

---

## Definition of Done

1. Branch `phase3p-b-p2-wire-parity` merged into `main` via PR (after Phase A merges, rebase onto main first).
2. `tst_p2_regression_freeze` PASSES — every (board × MOX × command-packet × byte-region) tuple matches pre-refactor baseline byte-for-byte.
3. `IP2Codec` chosen at `P2RadioConnection::applyBoardQuirks()` from `m_hardwareProfile.model`.
4. ANAN-G2 / G2-1K, when configured with custom Saturn BPF1 band edges, emit those edges instead of Alex defaults.
5. RxApplet ADC OVL badge splits into `OVL₀` + `OVL₁` for dual-ADC boards (OrionMKII family, Saturn family); single badge for single-ADC boards (HL2, Hermes).
6. Hardware → Antenna/ALEX → Alex-1 Filters and Alex-2 Filters sub-sub-tabs render the correct controls per spec mockups (`docs/architecture/2026-04-20-radio-control-mockups/page-alex1-filters.html` and `page-alex2-filters.html`). Live LED indicators are stubs; Phase H wires them.
7. Per-MAC persistence under `hardware/<mac>/alex/{hpf,lpf,bpf1}/<band>/{enabled,start,end}` and `.../alex2/{hpf,lpf}/<band>/...`.
8. All 4 attribution gates pass; PROVENANCE rows for every new ported file; inline cites in `[@shortsha]` form.
9. `CHANGELOG.md` Unreleased section has Phase 3P-B entries.
10. `NEREUS_USE_LEGACY_P2_CODEC=1` env-var feature flag preserved as rollback hatch through one release cycle.

---

## File structure

**Created (codec):**
- `src/core/codec/IP2Codec.h` — interface (NereusSDR-original)
- `src/core/codec/P2CodecOrionMkII.{h,cpp}` — base P2 codec, ports `ChannelMaster/network.c:821-1248` `[@501e3f5]`. Variant `thetis-no-samphire`.
- `src/core/codec/P2CodecSaturn.{h,cpp}` — extends `P2CodecOrionMkII` with Saturn BPF1 band edge override. Ports `Console/console.cs:6944-7040` `[@501e3f5]` (G8NJJ `setBPF1ForOrionIISaturn`). Variant `multi-source`.

**Created (UI):**
- `src/gui/setup/hardware/AntennaAlexAlex1Tab.{h,cpp}` — Alex-1 Filters page. Ports `Console/setup.designer.cs:tpAlexFilterControl` (~lines 23385-25538) `[@501e3f5]`. Variant `thetis-samphire`.
- `src/gui/setup/hardware/AntennaAlexAlex2Tab.{h,cpp}` — Alex-2 Filters page. Ports `Console/setup.designer.cs:tpAlex2FilterControl` (~lines 25539-26857) `[@501e3f5]`. Variant `thetis-samphire`.

**Created (tests):**
- `tests/tst_p2_regression_freeze_capture.cpp` — temporary, deleted in Task 7.
- `tests/data/p2_baseline_bytes.json` — captured in Task 1, becomes contract.
- `tests/tst_p2_codec_orionmkii.cpp`
- `tests/tst_p2_codec_saturn.cpp`
- `tests/tst_p2_regression_freeze.cpp` — the gate (Task 7).
- `tests/tst_rxapplet_adc_ovl.cpp` — per-ADC OVL split.
- `tests/tst_alex1_filters_tab.cpp` (smoke test only — controls render, persistence round-trips).
- `tests/tst_alex2_filters_tab.cpp` (smoke test only).

**Modified:**
- `src/core/P2RadioConnection.{h,cpp}` — cutover similar to Phase A Task 12. Add `m_codec`, `selectCodec()`, `buildCodecContext()`, dual-call diagnostic. `NEREUS_USE_LEGACY_P2_CODEC=1` env var.
- `src/core/BoardCapabilities.{h,cpp}` — add `p2SaturnBpf1Edges` (vector of band×{start,end} or struct), `p2PreampPerAdc` bool, `ditherByDefault[3]` and `randomByDefault[3]` (likely already present — verify).
- `src/gui/setup/hardware/AntennaAlexTab.{h,cpp}` — turn into a parent with sub-sub-tabs (Antenna Control / Alex-1 Filters / Alex-2 Filters). Antenna Control sub-tab placeholder lands in Phase F.
- `src/gui/applets/RxApplet.{h,cpp}` — split ADC OVL badge per-ADC for boards with `BoardCapabilities::adcCount > 1`.
- `tests/CMakeLists.txt` — register new test executables.
- `docs/attribution/THETIS-PROVENANCE.md` — 5 new rows (1 IP2Codec interface excluded — NereusSDR-original).
- `CHANGELOG.md`

---

## Task 1: Setup + capture P2 wire-byte regression-freeze baseline

**Files:**
- Create: `tests/tst_p2_regression_freeze_capture.cpp` (temporary)
- Create: `tests/data/p2_baseline_bytes.json`
- Modify: `tests/CMakeLists.txt`

**Pattern:** Copy Phase A Task 1's structure exactly. P2 has multiple command packets (CmdGeneral 60 bytes, CmdRx 1444, CmdTx 60, CmdHighPriority 1444). Snapshot relevant byte regions per packet per board state.

**P2 boards in scope (HPSDRHW values):** `OrionMKII`, `Saturn`, `SaturnMKII` — single-ADC HL2 / Hermes-family aren't P2.

**Capture state matrix:**
- Boards: OrionMKII, Saturn, SaturnMKII (3)
- Connection state combinations: idle (no MOX), TX (MOX=true), 1 receiver vs 2 receivers, RX freq spanning bands (160m / 20m / 6m to exercise Alex HPF/LPF buckets)
- Packets per state: serialize CmdGeneral, CmdHighPriority, CmdRx, CmdTx into byte arrays; record relevant byte regions

Aim for ~30-50 captured tuples covering the byte regions Phase B touches (Alex HPF/LPF in CmdHighPriority bytes 1428-1435, RX/TX ATT in CmdHighPriority 1442-1443 and CmdTx 57-59, OC PTT in CmdHighPriority byte 4).

**Implementer notes:**
- `P2RadioConnection` already has the test seam pattern from Phase 3I (`setBoardForTest`, etc.). Use it.
- The capture helper writes JSON same shape as Phase A's: `{"board": int, "mox": bool, "rxCount": int, "rxFreqHz": int, "packet": "CmdHighPriority", "bytes_offset": int, "bytes": [int...]}`.
- Use `QStringLiteral(NEREUS_TEST_DATA_DIR)` per Phase A.
- Commit message: `test(p2): capture pre-refactor regression-freeze baseline (Phase 3P-B Task 1)`.

---

## Task 2: `IP2Codec` interface + extend `CodecContext`

**Files:**
- Create: `src/core/codec/IP2Codec.h` (NereusSDR-original, no Thetis port)
- Modify: `src/core/codec/CodecContext.h` (add P2-specific fields)

**Interface shape:**

```cpp
class IP2Codec {
public:
    virtual ~IP2Codec() = default;
    virtual void composeCmdGeneral(const CodecContext& ctx, quint8 buf[60]) const = 0;
    virtual void composeCmdHighPriority(const CodecContext& ctx, quint8 buf[1444]) const = 0;
    virtual void composeCmdRx(const CodecContext& ctx, quint8 buf[1444]) const = 0;
    virtual void composeCmdTx(const CodecContext& ctx, quint8 buf[60]) const = 0;
};
```

**CodecContext additions** (P2-specific):
- `bool ditherPerAdc[3]{true, true, true}` (move from existing `dither` if needed; Phase A added one for ADC0)
- `bool randomPerAdc[3]{true, true, true}` (similar)
- `quint8 p2SaturnBpfHpfBits{0}` and `p2SaturnBpfLpfBits{0}` — populated by `setReceiverFrequency` when Saturn caps say BPF1 edges differ from Alex defaults
- `int p2RxAnt[3]{0}` — per-receiver antenna; Phase F antenna assignment will populate
- Other fields as discovered during P2CodecOrionMkII implementation.

Commit: `feat(codec): IP2Codec interface + CodecContext P2 fields (Phase 3P-B Task 2)`.

---

## Task 3+4: `P2CodecOrionMkII` (TDD pair, base P2 codec)

**Files:**
- Create: `src/core/codec/P2CodecOrionMkII.{h,cpp}`
- Create: `tests/tst_p2_codec_orionmkii.cpp`
- Modify: `tests/CMakeLists.txt`, `src/CMakeLists.txt`, `docs/attribution/THETIS-PROVENANCE.md`

**Pattern:** Same TDD pair as Phase A Task 5+6 (failing test → implementation → green → commit). Verbatim upstream header from `ChannelMaster/network.c:1-45` `[@501e3f5]`. Variant `thetis-no-samphire`.

**Test coverage** (table-driven via `_data()` slot):
- `composeCmdGeneral`: byte 4 PTT bit, byte 5 sample rate
- `composeCmdHighPriority`: byte 4 PTT/run, bytes 1428-1435 Alex HPF/LPF, bytes 1442-1443 RX ATT
- `composeCmdRx`: bytes 5-6 dither/random per ADC
- `composeCmdTx`: bytes 57-59 TX ATT per ADC

**Implementation:** Lift the existing inline P2 byte-packing from `P2RadioConnection.cpp` into `P2CodecOrionMkII::composeCmd*` methods. Should produce byte-identical output (verified by Task 7 regression-freeze gate).

Commit: `feat(codec): P2CodecOrionMkII — base P2 wire codec (Phase 3P-B Task 3+4)`.

---

## Task 5: `P2CodecSaturn` (extends OrionMkII with BPF1 edges)

**Files:**
- Create: `src/core/codec/P2CodecSaturn.{h,cpp}`
- Create: `tests/tst_p2_codec_saturn.cpp`
- Modify: same pattern as Task 3+4

**Multi-source port** — header includes both `network.c:1-45` AND `console.cs:1-50` (Saturn BPF1 logic comes from console.cs:6944-7040, the G8NJJ `setBPF1ForOrionIISaturn` function, while the rest of P2 packet shape comes from network.c). Use `// --- From network.c ---` and `// --- From console.cs ---` markers per CLAUDE.md §"Byte-for-byte headers and multi-file attribution".

Variant `multi-source`. PROVENANCE row notes both upstream files.

**Override:** `composeCmdHighPriority` — when `ctx.p2SaturnBpfHpfBits` and `p2SaturnBpfLpfBits` differ from default Alex bits, emit Saturn BPF1 bits in the same byte region. Falls through to OrionMkII parent for everything else.

**Test coverage:**
- Saturn BPF1 edges populated → bytes 1432-1435 reflect Saturn config
- Saturn BPF1 edges empty → bytes 1432-1435 reflect Alex defaults (matches OrionMkII output)
- All non-BPF1 bytes inherit from OrionMkII unchanged

Commit: `feat(codec): P2CodecSaturn — extends OrionMkII with BPF1 band edges (Phase 3P-B Task 5)`.

---

## Task 6: `BoardCapabilities` P2 expansion

**Files:**
- Modify: `src/core/BoardCapabilities.{h,cpp}`
- Modify: `tests/tst_board_capabilities.cpp`

**New fields:**

```cpp
struct SaturnBpf1Edge { double startMhz; double endMhz; };
QList<SaturnBpf1Edge> p2SaturnBpf1Edges;  // empty → use Alex defaults
bool p2PreampPerAdc{false};               // OrionMKII family: true
bool ditherByDefault[3]{true, true, true};
bool randomByDefault[3]{true, true, true};
```

**Per-board population:**
- Saturn / SaturnMKII: `p2PreampPerAdc=false`, `p2SaturnBpf1Edges=empty` (user populates via Alex-1 Filters page)
- OrionMKII (and AnvelinaPro3 / 7000D / 8000D mapping to OrionMKII at HPSDRHW): `p2PreampPerAdc=true`, `p2SaturnBpf1Edges=empty`
- All other boards (Atlas / Hermes / HermesII / Angelia / Orion / HermesLite): `p2PreampPerAdc=false`, defaults fine

**Tests:**
- 3 new assertions confirming Saturn/OrionMKII/HL2 caps are correct.

Commit: `feat(caps): expand BoardCapabilities with P2-specific fields (Phase 3P-B Task 6)`.

---

## Task 7: `P2RadioConnection` cutover + regression-freeze gate

**Files:**
- Modify: `src/core/P2RadioConnection.{h,cpp}` (cutover)
- Create: `tests/tst_p2_regression_freeze.cpp` (gate)
- Delete: `tests/tst_p2_regression_freeze_capture.cpp` (temporary helper from Task 1)
- Modify: `tests/CMakeLists.txt`

**Cutover pattern** (mirror Phase A Task 12):
- Add `std::unique_ptr<IP2Codec> m_codec` to private section
- Add `selectCodec()` method, called from `applyBoardQuirks()`
- Add `buildCodecContext()` method snapshotting all live state
- Replace each existing `composeCmd*` instance method body with delegation to `m_codec->composeCmd*(buildCodecContext(), buf)`
- Preserve legacy bodies under `composeCmd*Legacy` for the rollback flag
- Honor `NEREUS_USE_LEGACY_P2_CODEC=1` env var
- Dual-call diagnostic during transition: warn on any divergence for non-Saturn boards (Saturn divergence is expected when BPF1 edges populated)

**Regression-freeze gate test:**
- Same shape as Phase A Task 16's `tst_p1_regression_freeze`
- Loads `tests/data/p2_baseline_bytes.json`
- Asserts byte-identical for every captured tuple where Saturn BPF1 edges aren't populated (because the codec WILL diverge for Saturn-with-BPF1-edges by design)

Commit: `feat(p2): cutover to per-board codec + regression-freeze gate (Phase 3P-B Task 7)`.

---

## Task 8: Antenna/ALEX → Alex-1 Filters sub-sub-tab

**Files:**
- Create: `src/gui/setup/hardware/AntennaAlexAlex1Tab.{h,cpp}`
- Modify: `src/gui/setup/hardware/AntennaAlexTab.{h,cpp}` — turn into parent with sub-sub-tabs (Antenna Control placeholder, Alex-1 Filters, Alex-2 Filters)
- Create: `tests/tst_alex1_filters_tab.cpp` (smoke + persistence round-trip)

**UI shape:** Per mockup `docs/architecture/2026-04-20-radio-control-mockups/page-alex1-filters.html` — three columns:
1. **Alex HPF Bands** — 5 master toggles (HPF Bypass, HPF Bypass on TX, HPF Bypass on PureSignal, Disable 6m LNA on TX/RX) + 6 HPF rows (1.5/6.5/9.5/13/20 MHz + 6m bypass) with bypass checkbox + Start/End freq editors
2. **Alex LPF Bands** — 7 LPF rows (160/80/60-40/30-20/17-15/12-10/6m) with Start/End editors
3. **Saturn BPF1 Bands** — same shape as HPF, dimmed/hidden when board ≠ ANAN-G2 / G2-1K (gate on `BoardCapabilities::p2SaturnBpf1Edges` capability presence)

**Verbatim upstream header:** `setup.designer.cs:1-50`. Variant `thetis-samphire`.

**Persistence:** per-MAC under `hardware/<mac>/alex/hpf/<band>/{enabled,start,end}` and similar for LPF + BPF1.

**Smoke test:** construct widget, call all setter signals, verify AppSettings round-trip.

Commit: `feat(setup): Hardware → Antenna/ALEX → Alex-1 Filters sub-sub-tab (Phase 3P-B Task 8)`.

---

## Task 9: Antenna/ALEX → Alex-2 Filters sub-sub-tab

**Files:**
- Create: `src/gui/setup/hardware/AntennaAlexAlex2Tab.{h,cpp}`
- Modify: `src/gui/setup/hardware/AntennaAlexTab.{h,cpp}` — register the new sub-sub-tab
- Create: `tests/tst_alex2_filters_tab.cpp`

**UI shape:** Per mockup `page-alex2-filters.html` — two columns:
1. **Alex-2 HPF Bands** — LED row (live filter selection — STUBS in Phase B; Phase H wires them), master "ByPass / 55 MHz BPF" toggle, 6 HPF rows with bypass + Start/End editors
2. **Alex-2 LPF Bands** — LED row stubs, 7 LPF rows with Start/End editors, no master toggles

**Active-status bar at top:** "Alex-2 board: Active" (green when board has Alex-2, else dimmed). Hidden entirely when board model has no Alex-2 (HL2, Hermes, Angelia).

**Persistence:** per-MAC under `hardware/<mac>/alex2/{hpf,lpf}/<band>/{enabled,start,end}`.

Same variant + attribution pattern as Task 8.

Commit: `feat(setup): Hardware → Antenna/ALEX → Alex-2 Filters sub-sub-tab (Phase 3P-B Task 9)`.

---

## Task 10: RxApplet ADC OVL split + per-ADC RX1 preamp

**Files:**
- Modify: `src/gui/applets/RxApplet.{h,cpp}`
- Modify: `src/core/StepAttenuatorController.{h,cpp}` (or wherever ADC OVL signals route)
- Create: `tests/tst_rxapplet_adc_ovl.cpp`

**ADC OVL split:** Today there's a single `OVL` badge driven by an aggregated overflow signal. After this task, dual-ADC boards (`BoardCapabilities::adcCount > 1`) show `OVL₀` + `OVL₁` (per-ADC). Single-ADC boards keep one badge.

**Per-ADC RX1 preamp:** OrionMKII-family + Saturn-family dual-ADC boards. Add `setRx1Preamp(bool)` to `P2RadioConnection`; route to byte 1403 bit 1 in CmdHighPriority. Wire to a new RxApplet preamp combo for RX1 (visible only when `p2PreampPerAdc=true`).

**Test:**
- HL2 board → 1 OVL badge
- OrionMKII board → 2 OVL badges (OVL₀ + OVL₁)
- OrionMKII RX1 preamp toggle → byte 1403 bit 1 set/clear

Commit: `feat(rx-applet): per-ADC OVL split + RX1 preamp for dual-ADC boards (Phase 3P-B Task 10)`.

---

## Task 11: PROVENANCE + CHANGELOG + push + PR

**Files:**
- Modify: `docs/attribution/THETIS-PROVENANCE.md` (verify all 5 new ported files have rows)
- Modify: `CHANGELOG.md`

**CHANGELOG entries** under `## [Unreleased]`:

```markdown
### Added
- Hardware → Antenna / ALEX page split into three sub-sub-tabs (Antenna Control / Alex-1 Filters / Alex-2 Filters), matching Thetis's General → Alex IA. Alex-1 Filters page exposes per-band HPF/LPF bypass + edge editors and the Saturn BPF1 panel (gated on ANAN-G2 / G2-1K). Alex-2 Filters page exposes per-band HPF/LPF for the RX2 board. (Phase 3P-B)
- ADC OVL badge in RxApplet now splits into OVL₀ + OVL₁ for dual-ADC boards (Orion-MKII family, Saturn family). Single-ADC boards (HL2, Hermes) keep a single badge. (Phase 3P-B)

### Changed
- `P2RadioConnection`'s C&C compose layer refactored into per-board codec subclasses (`P2CodecOrionMkII`, `P2CodecSaturn`) behind `IP2Codec`. Behavior byte-identical for non-Saturn-with-BPF1 cases. `NEREUS_USE_LEGACY_P2_CODEC=1` env var reverts to pre-refactor compose path for one release cycle as a rollback hatch.
- `BoardCapabilities` extended with `p2SaturnBpf1Edges`, `p2PreampPerAdc`, and per-ADC `ditherByDefault` / `randomByDefault` fields.
- ANAN-G2 / G2-1K can now use user-configured Saturn BPF1 band edges instead of Alex defaults via the new Hardware → Antenna/ALEX → Alex-1 Filters page.
```

**Push + PR** (only after Phase A PR #85 has merged AND this branch has been rebased onto main):

```bash
# After Phase A merges to main:
git fetch origin main
git rebase origin/main
git push --force-with-lease -u origin phase3p-b-p2-wire-parity
gh pr create --title "Phase 3P-B: P2 wire-bytes parity + Alex filter UI sub-sub-tabs" --body "$(cat <<'EOF'
[Same body shape as Phase A PR #85; reference spec §7]
EOF
)"
gh pr view --web
```

**If Phase A hasn't merged yet:** `gh pr create --base phase3p-a-p1-wire-parity` (stack the PR).

Commit: `docs: CHANGELOG + final PROVENANCE pass (Phase 3P-B Task 11)`.

---

## Self-review checklist (controller-level)

Before reporting Phase B DONE:
1. Every spec §7 requirement has a task implementing it.
2. Every new ported file has verbatim upstream header(s) + PROVENANCE row.
3. `tst_p2_regression_freeze` PASS — non-Saturn-BPF1 cases byte-identical.
4. `tst_p2_codec_orionmkii` and `tst_p2_codec_saturn` both PASS.
5. `tst_alex1_filters_tab`, `tst_alex2_filters_tab`, `tst_rxapplet_adc_ovl` all PASS.
6. All 4 attribution gates PASS.
7. Every commit GPG-signed.
8. PR opened (or stacked on Phase A PR if A hasn't merged).

---

## Execution handoff

**Plan complete and saved to `docs/architecture/2026-04-20-phase3p-b-p2-wire-parity-plan.md`.**

Recommend continuing the Phase A pattern: dispatch implementer subagents per task via `superpowers:subagent-driven-development`, in the worktree at `/Users/j.j.boyd/NereusSDR/.worktrees/phase3p-b-p2-wire-parity`.

Tasks 8 + 9 (Setup pages) are larger than the wire-byte tasks — consider giving them their own subagent dispatches rather than batching.
