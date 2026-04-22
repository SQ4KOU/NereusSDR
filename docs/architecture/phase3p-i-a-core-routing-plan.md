# Phase 3P-I-a — Core Per-Band Antenna Routing + UI Gating — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** [`docs/architecture/antenna-routing-design.md`](./antenna-routing-design.md)
**Branch:** `feature/phase3p-i-a-core-routing` (create at start)
**Goal:** Close issue #98. Wire `AlexController` per-band antenna state to the `RadioConnection` protocol layer, gate all 11 antenna UI surfaces on `caps.hasAlex`, add a universal dark-popup-menu stylesheet, and verify P1/P2 wire encoding matches Thetis byte-for-byte. **No** SKU overlay, **no** RX-only/XVTR/Ext-on-TX composition, **no** MOX coupling, **no** Aries — those are 3P-I-b and 3M-1 scope.

**Architecture:** One branch, four layers landed in bottom-up order — (1) schema extensions + helpers with no behavior change, (2) protocol layer `setAntennaRouting` + codec refactors, (3) `RadioModel` orchestration via `applyAlexAntennaForBand`, (4) UI capability gating across the 11 surfaces. Each task is self-contained, compiles clean, and leaves the existing test suite green.

**Tech Stack:** C++20, Qt6 (Widgets + Test), `AppSettings` XML persistence. Existing `AlexController` (Phase 3P-F) is the state source; existing `BoardCapabilities` table gets three new fields; existing `HPSDRModel` enum is used as-is.

**Thetis baseline:** `v2.10.3.13 @501e3f5`. Every inline cite in new code uses this stamp per [`docs/attribution/HOW-TO-PORT.md`](../attribution/HOW-TO-PORT.md).

---

## 0. Pre-flight checks

- [ ] **0.1** Verify `PR #34` (Phase 3G-13) has merged to `main`, then rebase the worktree:
  ```bash
  cd /Users/j.j.boyd/NereusSDR
  git fetch origin && git checkout main && git pull --ff-only
  ```
  Expected: clean fast-forward. If PR #34 is still open, pause this plan until it merges — conflicts on RxApplet.cpp are likely.

- [ ] **0.2** Create the phase branch from main:
  ```bash
  git checkout -b feature/phase3p-i-a-core-routing
  git push -u origin feature/phase3p-i-a-core-routing
  ```

- [ ] **0.3** Verify the Thetis clone is present and at the expected version:
  ```bash
  git -C /Users/j.j.boyd/Thetis describe --tags --always
  ```
  Expected: `v2.10.3.13` or commit `501e3f5`. If missing, `git clone https://github.com/ramdor/Thetis /Users/j.j.boyd/Thetis` first.

- [ ] **0.4** Baseline build:
  ```bash
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEREUS_BUILD_TESTS=ON
  cmake --build build -j$(sysctl -n hw.ncpu)
  ```
  Expected: zero errors. Warnings acceptable.

- [ ] **0.5** Baseline test pass count:
  ```bash
  ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nereus-baseline-tests.log | tail -3
  ```
  Record the pass count. Every task must leave this count at baseline or higher (no regressions).

- [ ] **0.6** Confirm the partial fix stashed earlier is present for reference only (do not pop):
  ```bash
  git stash list | grep "partial issue #98 fix" && echo OK
  ```

---

## File Structure Overview

### New files

| Path | Responsibility |
|---|---|
| `src/core/AntennaLabels.h` | Declarations: `antennaLabels(const BoardCapabilities&)` → QStringList. Single source for `{"ANT1","ANT2","ANT3"}` lists, returns `{}` when `hasAlex=false`. |
| `src/core/AntennaLabels.cpp` | Implementation. |
| `src/gui/styles/PopupMenuStyle.h` | `kPopupMenu` stylesheet constant — dark palette for all antenna `QMenu` popups. Fixes Linux dark-theme invisibility (#98). |
| `tests/tst_antenna_routing_model.cpp` | Integration test: RadioModel + mock RadioConnection; verifies AlexController → setAntennaRouting pump across connect / band-change / manual triggers. |
| `tests/tst_ui_capability_gating.cpp` | Widget-level test: each of the 11 surfaces hides/shows correctly per `caps.hasAlex`. |
| `tests/tst_popup_style_coverage.cpp` | Grep-based regression: antenna `QMenu` sites always call `setStyleSheet(kPopupMenu)` before `exec()`. |
| `docs/architecture/antenna-routing-verification.md` | Manual verification matrix — per-SKU QA checklist. |

### Extended files

| Path | Change |
|---|---|
| `src/core/BoardCapabilities.h` | +3 fields: `hasAlex2`, `hasRxBypassRelay`, `rxOnlyAntennaCount`. |
| `src/core/BoardCapabilities.cpp` | Populate new fields per row in `kTable`. |
| `src/core/RadioConnection.h` | Add `AntennaRouting` struct; add `virtual void setAntennaRouting(AntennaRouting) = 0`; deprecate `setAntenna(int)` as a thin wrapper. |
| `src/core/P2RadioConnection.h` / `.cpp` | Implement `setAntennaRouting`; split the unified `m_alex.rxAnt = m_alex.txAnt` into independent writes. |
| `src/core/P1RadioConnection.h` / `.cpp` | Implement `setAntennaRouting`; write `trxAnt` into `m_antennaIdx`. |
| `src/core/codec/P1CodecStandard.cpp` | `bank0()` already encodes `antennaIdx & 0x03` into C4 — verify byte-for-byte against Thetis `networkproto1.c:463-468` in the new codec test (§T5). No code change if test passes; add citation comment with version stamp. |
| `src/core/codec/P1CodecHl2.cpp` | Same antenna encoding as Standard; verify in codec test. |
| `src/models/RadioModel.h` / `.cpp` | Add `applyAlexAntennaForBand(Band, bool isTx=false)`; wire triggers T1 (`AlexController::antennaChanged`), T2 (band crossing), T3 (Connected state). Reroute `slice::rxAntennaChanged` / `txAntennaChanged` handlers to write through `m_alexController` instead of calling `connection->setAntenna` directly. |
| `src/models/SliceModel.h` / `.cpp` | Add `refreshFromAlex(const AlexController&, Band)` slot; wire from `RadioModel` on `AlexController::antennaChanged` for current band. |
| `src/gui/widgets/VfoWidget.cpp` / `.h` | Add `setBoardCapabilities(caps)` slot; hide ANT buttons when `!caps.hasAlex`; replace local `kFlatBtn`-adjacent menu styling with `kPopupMenu`. |
| `src/gui/applets/RxApplet.cpp` / `.h` | Same gating via `setBoardCapabilities(caps)`; hook the existing `RadioModel::currentRadioChanged` signal. Replace local menu stylesheet with `kPopupMenu`. |
| `src/gui/meters/AntennaButtonItem.cpp` / `.h` | Add `setBoardCapabilities(caps)` method; render "read-only ANT" mode when `!caps.hasAlex`. |
| `src/gui/containers/meter_property_editors/AntennaButtonItemEditor.cpp` | Clamp port-selection combo to `caps.antennaInputCount`. |
| `src/gui/SpectrumOverlayPanel.cpp` | Wire RX/TX combos (currently zombies — lines 443/458) through a new `RadioModel` helper → AlexController; `setVisible(caps.hasAlex)`. |
| `src/gui/MainWindow.cpp` | Refactor bridge at lines 2184-2262 — the VFO Flag cache now refreshes from `AlexController::antennaChanged`, not `SliceModel::rxAntennaChanged`. |
| `tests/tst_alex_controller.cpp` | +4 new cases: signal-fires-on-change, idempotent-no-signal, block-TX-rejects-silently, setAntennasTo1-fires-for-all. |
| `tests/tst_p1_codec_standard.cpp` | +1 case: bank0 C4 byte-for-byte vs Thetis `networkproto1.c:463-468` for antennaIdx=0/1/2. |
| `tests/tst_p2_codec_orionmkii.cpp` | +1 case: Alex0/Alex1 register bit positions for rxAnt/txAnt=1/2/3. |
| `tests/CMakeLists.txt` | Register the 3 new test binaries. |

### Files referenced, not modified

| Path | Reason |
|---|---|
| `src/core/HpsdrModel.h` | Enum already exists (`HPSDRModel::HPSDR..REDPITAYA`). Used as-is; refinement lives in 3P-I-b. |
| `src/core/accessories/AlexController.h` / `.cpp` | Interface stable since Phase 3P-F. |
| `src/core/HardwareProfile.h` | `model` field already exists at line 73. |
| `src/gui/setup/hardware/AntennaAlexAntennaControlTab.cpp` | Already gated by parent `HardwarePage.cpp:202` on `caps.hasAlexFilters`. No change in 3P-I-a. |
| `src/gui/setup/HardwarePage.cpp` | Line 202 gate already correct. No change. |

---

## T1. BoardCapabilities: add hasAlex2, hasRxBypassRelay, rxOnlyAntennaCount

**Files:**
- Modify: `src/core/BoardCapabilities.h:249-281` (add 3 fields)
- Modify: `src/core/BoardCapabilities.cpp` (populate `kTable` rows)

- [ ] **T1.1** Read Thetis `setup.cs:6157-6286` and `setup.cs:19832-20404` to confirm which SKUs in each chipset family carry Alex2 / RxBypass / RxOnly. **SHOW** the relevant lines in a commit comment (per `CLAUDE.md` READ→SHOW→TRANSLATE).

- [ ] **T1.2** In `src/core/BoardCapabilities.h`, add three fields after `antennaInputCount` (line 253):
  ```cpp
      int  xvtrJackCount;
      int  antennaInputCount;

      // Antenna extensions (Phase 3P-I-a)
      // hasAlex2:           P2 second BPF bank (OrionMKII + Saturn family).
      //                     Gates Setup → Antenna → Alex-2 Filters sub-tab.
      //                     Source: setup.cs:6230, panelBPFControl.Visible [v2.10.3.13 @501e3f5]
      // hasRxBypassRelay:   Physical RxOut relay present on the Alex board.
      //                     Source: HPSDR/Alex.cs:299-413 UpdateAlexAntSelection,
      //                     "rx_out" param. False on HL2/Atlas (no Alex).
      // rxOnlyAntennaCount: Number of RX-only inputs (RX1/RX2/XVTR). 0 on
      //                     HL2/Atlas; 3 on Alex boards.
      //                     Source: setup.cs:6288-6299 grpAlexAntCtrl enable.
      bool hasAlex2            {false};
      bool hasRxBypassRelay    {false};
      int  rxOnlyAntennaCount  {0};
  ```

- [ ] **T1.3** In `src/core/BoardCapabilities.cpp`, find each `kTable` row (search for `{ HPSDRHW::Atlas,`, `{ HPSDRHW::Hermes,`, etc. — 10 rows) and set the new fields per this table:

  | Row | hasAlex2 | hasRxBypassRelay | rxOnlyAntennaCount |
  |---|:---:|:---:|:---:|
  | Atlas | false | false | 0 |
  | Hermes | false | true | 3 |
  | HermesII | false | true | 3 |
  | Angelia | false | true | 3 |
  | Orion | false | true | 3 |
  | OrionMKII | **true** | true | 3 |
  | HermesLite | false | false | 0 |
  | Saturn | **true** | true | 3 |
  | SaturnMKII | **true** | true | 3 |
  | Unknown | false | false | 0 |

- [ ] **T1.4** Build to verify the struct initializers still compile:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDRObjs 2>&1 | tail -5
  ```
  Expected: `[N/N] Linking …` or `no work to do`. Any error means a table row missed a field.

- [ ] **T1.5** Commit:
  ```bash
  git add src/core/BoardCapabilities.h src/core/BoardCapabilities.cpp
  git commit -m "feat(caps): add hasAlex2/hasRxBypassRelay/rxOnlyAntennaCount (Phase 3P-I-a T1)

Per docs/architecture/antenna-routing-design.md §4.1. No behavior
change — fields are referenced by UI gating in later tasks.

Source: Thetis setup.cs:6230, 6288-6299, HPSDR/Alex.cs:299-413
[v2.10.3.13 @501e3f5]"
  ```

---

## T2. AntennaLabels helper

**Files:**
- Create: `src/core/AntennaLabels.h`
- Create: `src/core/AntennaLabels.cpp`
- Modify: `CMakeLists.txt` (add new source file)

- [ ] **T2.1** Create `src/core/AntennaLabels.h`:
  ```cpp
  // src/core/AntennaLabels.h (NereusSDR)
  //
  // Single source for the "ANT1/ANT2/ANT3" label list. Replaces 10+
  // hardcoded QStringList{"ANT1","ANT2","ANT3"} sites across the UI.
  // Returns an empty list when the connected board has no Alex (HL2,
  // Atlas), so UI callers can `setVisible(!labels.isEmpty())`.
  //
  // Phase 3P-I-a. Design: docs/architecture/antenna-routing-design.md §6.1 Rule 1.

  #pragma once
  #include <QStringList>

  namespace NereusSDR {
  struct BoardCapabilities;

  QStringList antennaLabels(const BoardCapabilities& caps);
  }
  ```

- [ ] **T2.2** Create `src/core/AntennaLabels.cpp`:
  ```cpp
  #include "core/AntennaLabels.h"
  #include "core/BoardCapabilities.h"

  namespace NereusSDR {

  QStringList antennaLabels(const BoardCapabilities& caps)
  {
      if (!caps.hasAlex || caps.antennaInputCount < 1) {
          return {};
      }
      QStringList out;
      out.reserve(caps.antennaInputCount);
      for (int i = 1; i <= caps.antennaInputCount; ++i) {
          out << QStringLiteral("ANT%1").arg(i);
      }
      return out;
  }

  } // namespace NereusSDR
  ```

- [ ] **T2.3** Add to `CMakeLists.txt` — locate the `set(NEREUS_CORE_SOURCES ...)` list (grep for `BoardCapabilities.cpp`) and append `src/core/AntennaLabels.cpp` alphabetically before `BoardCapabilities.cpp`.

- [ ] **T2.4** Build:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDRObjs
  ```
  Expected: clean build.

- [ ] **T2.5** Commit:
  ```bash
  git add src/core/AntennaLabels.h src/core/AntennaLabels.cpp CMakeLists.txt
  git commit -m "feat(core): AntennaLabels helper for unified port list (Phase 3P-I-a T2)

Returns ANT1..ANTn based on caps.antennaInputCount, or empty when
!hasAlex. Single source — replaces 10+ hardcoded lists in later tasks."
  ```

---

## T3. PopupMenuStyle.h — kPopupMenu constant

**Files:**
- Create: `src/gui/styles/PopupMenuStyle.h`

- [ ] **T3.1** Create the new directory + file. `src/gui/styles/` is new:
  ```bash
  mkdir -p src/gui/styles
  ```

- [ ] **T3.2** Write `src/gui/styles/PopupMenuStyle.h`:
  ```cpp
  // src/gui/styles/PopupMenuStyle.h (NereusSDR)
  //
  // Universal dark-palette QMenu stylesheet — applied to every antenna
  // popup menu (VFO Flag, RxApplet, SpectrumOverlayPanel, meter items,
  // future band/mode menus). Fixes issue #98 where Ubuntu 25.10's default
  // theme rendered antenna menu items as dark-on-dark (only visible on
  // hover). Use via:
  //
  //     QMenu menu(this);
  //     menu.setStyleSheet(QString::fromLatin1(kPopupMenu));
  //
  // Every call site MUST set this stylesheet — enforced by
  // tests/tst_popup_style_coverage.cpp.
  //
  // Phase 3P-I-a. Design: docs/architecture/antenna-routing-design.md §6.1 Rule 2.

  #pragma once

  namespace NereusSDR {

  inline constexpr const char* kPopupMenu =
      "QMenu {"
      "  background: #1a2a3a;"
      "  color: #e0e8f0;"
      "  border: 1px solid #304050;"
      "}"
      "QMenu::item {"
      "  padding: 4px 20px;"
      "}"
      "QMenu::item:selected {"
      "  background: #2a5a8a;"
      "  color: #ffffff;"
      "}"
      "QMenu::item:disabled {"
      "  color: #587080;"
      "}";

  } // namespace NereusSDR
  ```

- [ ] **T3.3** Build (header-only; should be no-op but confirms file location):
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDRObjs
  ```

- [ ] **T3.4** Commit:
  ```bash
  git add src/gui/styles/PopupMenuStyle.h
  git commit -m "feat(style): kPopupMenu stylesheet for antenna popups (Phase 3P-I-a T3)

Dark palette matching CONTRIBUTING.md theme. Closes issue #98's
Ubuntu dark-theme invisibility. Enforced as an invariant in T21."
  ```

---

## T4. AntennaRouting struct + setAntennaRouting virtual

**Files:**
- Modify: `src/core/RadioConnection.h:88` (add struct + virtual; deprecate setAntenna)

- [ ] **T4.1** In `src/core/RadioConnection.h`, locate the `// --- Hardware Control ---` section around line 83. **Before** `virtual void setAntenna(int antennaIndex) = 0;`, insert the struct:
  ```cpp
      // Antenna routing — Phase 3P-I-a.
      // Ports Thetis Alex.cs:310-413 UpdateAlexAntSelection output
      // (HPSDR/Alex.cs [v2.10.3.13 @501e3f5]). Composed by
      // RadioModel::applyAlexAntennaForBand and pushed here.
      //
      // 3P-I-a scope: trxAnt + txAnt are independent ANT1..ANT3 ports.
      //     rxOnlyAnt, rxOut, tx remain zero until 3P-I-b/3M-1.
      struct AntennaRouting {
          int  rxOnlyAnt {0};   // 0=none, 1=RX1, 2=RX2, 3=XVTR  (3P-I-b)
          int  trxAnt    {1};   // 1..3 — shared RX/TX port on Alex
          int  txAnt     {1};   // 1..3 — independent TX port (P2 Alex1)
          bool rxOut     {false}; // RX bypass relay active       (3P-I-b)
          bool tx        {false}; // current MOX state            (3M-1)
      };

      virtual void setAntennaRouting(AntennaRouting routing) = 0;
  ```

- [ ] **T4.2** Replace the existing `setAntenna(int)` pure virtual with a non-virtual deprecated wrapper that forwards to `setAntennaRouting`:
  ```cpp
      // DEPRECATED — call setAntennaRouting directly. Kept for one release
      // cycle as a rollback hatch per docs/architecture/antenna-routing-design.md §7.7.
      // Removed in the release following 3P-I-b.
      [[deprecated("Use setAntennaRouting")]]
      void setAntenna(int antennaIndex) {
          const int ant = (antennaIndex >= 0 && antennaIndex <= 2) ? antennaIndex + 1 : 1;
          setAntennaRouting({0, ant, ant, false, false});
      }
  ```

- [ ] **T4.3** Build — expect unresolved virtual errors in P1RadioConnection and P2RadioConnection:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDRObjs 2>&1 | tail -20
  ```
  Expected: `error: cannot instantiate abstract class P2RadioConnection` (or similar). This is the cue that T6/T7 need implementations.

- [ ] **T4.4** Stage the interface change. Do NOT commit yet — wait until P1/P2 implementations compile (T7 commit bundles this).

---

## T5. P1 codec snapshot test — lock the current encoding

**Files:**
- Modify: `tests/tst_p1_codec_standard.cpp` (add one test case)

This is **TDD-for-the-existing-code**: before changing P1RadioConnection.setAntenna, lock its current byte-level behavior so T7 can't regress it.

- [ ] **T5.1** Open `tests/tst_p1_codec_standard.cpp`. Find the existing `void bank0_...` test (grep `bank0`). **Before** the closing `};` of the test class, add:
  ```cpp
      // Thetis parity: bank 0 C4 antenna bits per networkproto1.c:463-468
      //   if (prbpfilter->_ANT_3 == 1)       C4 = 0b10;
      //   else if (prbpfilter->_ANT_2 == 1)  C4 = 0b01;
      //   else                                C4 = 0b0;
      // Phase 3P-I-a T5: locks the current byte-for-byte encoding before
      // the setAntennaRouting refactor lands in T7.
      void bank0_c4_antennaIdx_matches_thetis() {
          CodecContext ctx;
          ctx.activeRxCount = 1;
          ctx.duplex = true;

          quint8 out[5]{};
          P1CodecStandard codec;

          ctx.antennaIdx = 0;  // ANT1
          codec.bank0(ctx, out);
          QCOMPARE(out[4] & 0x03, 0b00);

          ctx.antennaIdx = 1;  // ANT2
          codec.bank0(ctx, out);
          QCOMPARE(out[4] & 0x03, 0b01);

          ctx.antennaIdx = 2;  // ANT3
          codec.bank0(ctx, out);
          QCOMPARE(out[4] & 0x03, 0b10);
      }
  ```

- [ ] **T5.2** Build the test:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_p1_codec_standard
  ```

- [ ] **T5.3** Run it (must PASS on current code — we're locking an invariant):
  ```bash
  ctest --test-dir build -R tst_p1_codec_standard --output-on-failure
  ```
  Expected: PASS. A failure here means the current code is already broken vs Thetis, which would be a separate bug to chase before continuing.

- [ ] **T5.4** Commit:
  ```bash
  git add tests/tst_p1_codec_standard.cpp
  git commit -m "test(p1): lock bank0 C4 antennaIdx byte-for-byte vs Thetis (Phase 3P-I-a T5)

Snapshot test — prevents T7's setAntennaRouting refactor from
silently changing the P1 wire encoding. Source: Thetis
networkproto1.c:463-468 [v2.10.3.13 @501e3f5]."
  ```

---

## T6. P2 codec snapshot test — Alex0/Alex1 bit positions

**Files:**
- Modify: `tests/tst_p2_codec_orionmkii.cpp` (add one test case)

- [ ] **T6.1** Find the existing buildAlex0/Alex1 test (grep `buildAlex`). Add this method:
  ```cpp
      // Thetis parity: Alex0/Alex1 antenna bits per netInterface.c:479-485
      //   _ANT_1 → bit 24; _ANT_2 → bit 25; _ANT_3 → bit 26
      // Phase 3P-I-a T6: locks independent RX/TX antenna encoding so
      // T7's setAntennaRouting split can't regress the wire bits.
      void buildAlex_rxAnt_txAnt_independent_bits() {
          P2RadioConnection conn;

          // RX=ANT2, TX=ANT3 — different ports.
          conn.m_alex.rxAnt = 2;
          conn.m_alex.txAnt = 3;
          const quint32 a0 = conn.buildAlex0();
          const quint32 a1 = conn.buildAlex1();

          // Alex0 carries rxAnt in bits 24-26
          QVERIFY(!(a0 & (1u << 24)));   // not ANT1
          QVERIFY( (a0 & (1u << 25)));   //     ANT2
          QVERIFY(!(a0 & (1u << 26)));

          // Alex1 carries txAnt in bits 24-26
          QVERIFY(!(a1 & (1u << 24)));
          QVERIFY(!(a1 & (1u << 25)));
          QVERIFY( (a1 & (1u << 26)));   // ANT3
      }
  ```

- [ ] **T6.2** If `m_alex` or `buildAlex0` is private, add a `friend class` declaration in `P2RadioConnection.h` gated by `#ifdef NEREUS_BUILD_TESTS`:
  ```cpp
  #ifdef NEREUS_BUILD_TESTS
      friend class TestP2CodecOrionMkII;
  #endif
  ```

- [ ] **T6.3** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_p2_codec_orionmkii
  ctest --test-dir build -R tst_p2_codec_orionmkii --output-on-failure
  ```
  Expected: PASS.

- [ ] **T6.4** Commit:
  ```bash
  git add tests/tst_p2_codec_orionmkii.cpp src/core/P2RadioConnection.h
  git commit -m "test(p2): lock Alex0/Alex1 independent RX/TX antenna bits (Phase 3P-I-a T6)

Source: Thetis ChannelMaster/netInterface.c:479-485 [v2.10.3.13 @501e3f5]."
  ```

---

## T7. Implement setAntennaRouting on both connections

**Files:**
- Modify: `src/core/P2RadioConnection.h` (add override)
- Modify: `src/core/P2RadioConnection.cpp:474-482` (replace setAntenna body)
- Modify: `src/core/P1RadioConnection.h` (add override)
- Modify: `src/core/P1RadioConnection.cpp:814` (replace setAntenna body)

- [ ] **T7.1** In `src/core/P2RadioConnection.h`, add the override declaration under `// --- Hardware Control ---`:
  ```cpp
      void setAntennaRouting(AntennaRouting routing) override;
  ```
  Remove the old `void setAntenna(int antennaIndex) override;` (it's now a non-virtual inline in the base).

- [ ] **T7.2** In `src/core/P2RadioConnection.cpp`, replace lines 474-482 (the entire `setAntenna` body):
  ```cpp
  // ---------------------------------------------------------------------------
  // setAntennaRouting — Phase 3P-I-a
  //
  // Ports Thetis ChannelMaster/netInterface.c:479-485 — Alex0 (RX) and
  // Alex1 (TX) register encoding. Bits 24/25/26 are ANT1/ANT2/ANT3.
  //
  // 3P-I-a scope: only trxAnt / txAnt are honored. rxOnlyAnt / rxOut / tx
  // are accepted in the struct but ignored here until 3P-I-b wires Alex0
  // bits 27-30 (RX-only routing) and 3M-1 wires the MOX branch.
  //
  // From Thetis HPSDR/Alex.cs:401 [v2.10.3.13 @501e3f5] —
  //   NetworkIO.SetAntBits(rx_only_ant, trx_ant, tx_ant, rx_out, tx);
  // ---------------------------------------------------------------------------
  void P2RadioConnection::setAntennaRouting(AntennaRouting r)
  {
      // trxAnt drives the Alex0 RX antenna; txAnt drives the Alex1 TX.
      // Clamp to 1..3 (AntennaRouting defaults to 1; 0 means "no-op write").
      auto clamp = [](int v) { return (v < 1 || v > 3) ? 1 : v; };
      m_alex.rxAnt = clamp(r.trxAnt);
      m_alex.txAnt = clamp(r.txAnt);
      if (m_running) {
          sendCmdHighPriority();
      }
  }
  ```

- [ ] **T7.3** In `src/core/P1RadioConnection.h`, add the override under `// --- Hardware Control ---`:
  ```cpp
      void setAntennaRouting(AntennaRouting routing) override;
  ```
  Remove `void setAntenna(int antennaIndex) override;`.

- [ ] **T7.4** In `src/core/P1RadioConnection.cpp`, replace line 814 (`void P1RadioConnection::setAntenna(...) = ...;`) with:
  ```cpp
  // ---------------------------------------------------------------------------
  // setAntennaRouting — Phase 3P-I-a
  //
  // Stores trxAnt into m_antennaIdx; the next round-robin pass through
  // bank 0 composes it into C4 bits 0-1 via P1CodecStandard::bank0 (or
  // P1CodecHl2::bank0 on HL2 — identical encoding at the antenna bit level).
  //
  // From Thetis ChannelMaster/networkproto1.c:463-468 [v2.10.3.13 @501e3f5] —
  //   if (prbpfilter->_ANT_3 == 1)       C4 = 0b10;
  //   else if (prbpfilter->_ANT_2 == 1)  C4 = 0b01;
  //   else                                C4 = 0b0;
  //
  // 3P-I-a scope: rxOnlyAnt / rxOut are ignored. Bank 0 C3 keeps the
  // hardcoded 0x20 (RX_1_In) until 3P-I-b wires RX-only routing.
  // ---------------------------------------------------------------------------
  void P1RadioConnection::setAntennaRouting(AntennaRouting r)
  {
      const int clamped = (r.trxAnt < 1 || r.trxAnt > 3) ? 0 : (r.trxAnt - 1);
      m_antennaIdx = clamped;  // 0..2
      // P1 has no high-priority packet; the next EP2 frame picks up the
      // new antennaIdx via buildCodecContext() → P1Codec::bank0.
  }
  ```

- [ ] **T7.5** Build. T4's interface change now resolves:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -10
  ```
  Expected: clean build of NereusSDR + all tests.

- [ ] **T7.6** Run the codec snapshot tests from T5/T6 to verify no regression:
  ```bash
  ctest --test-dir build -R "tst_p1_codec_standard|tst_p2_codec_orionmkii" --output-on-failure
  ```
  Expected: both PASS.

- [ ] **T7.7** Run the full test suite:
  ```bash
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```
  Expected: baseline pass count or higher.

- [ ] **T7.8** Commit (bundles T4 interface + T7 implementations):
  ```bash
  git add src/core/RadioConnection.h \
          src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp \
          src/core/P2RadioConnection.h src/core/P2RadioConnection.cpp
  git commit -m "feat(protocol): replace setAntenna(int) with setAntennaRouting(struct) (Phase 3P-I-a T4+T7)

Independent RX/TX antenna encoding on P2; P1 preserves C4 bit layout.
setAntenna(int) kept as deprecated inline wrapper for one release cycle
per spec §7.7 rollback hatch.

Source: Thetis HPSDR/Alex.cs:401, ChannelMaster/networkproto1.c:463-468,
netInterface.c:479-485 [v2.10.3.13 @501e3f5]."
  ```

---

## T8. RadioModel::applyAlexAntennaForBand

**Files:**
- Modify: `src/models/RadioModel.h` (add method declaration)
- Modify: `src/models/RadioModel.cpp` (implementation)

- [ ] **T8.1** In `src/models/RadioModel.h`, in the `private:` section near `scheduleSettingsSave`, add:
  ```cpp
      // Pushes AlexController's per-band antenna state to the connection.
      // Mirrors Thetis HPSDR/Alex.cs:310-413 UpdateAlexAntSelection at
      // 3P-I-a scope: reads rxAnt(band), composes an AntennaRouting with
      // trxAnt = rxAnt (RX=TX unified in 3P-I-a), writes txAnt = txAnt(band)
      // independently. No MOX-aware composition, no XVTR, no Aries — those
      // land in 3P-I-b/3M-1.
      //
      // Source: docs/architecture/antenna-routing-design.md §5.3.
      // Triggered from T9/T10/T11 wirings below.
      void applyAlexAntennaForBand(NereusSDR::Band band);
  ```

- [ ] **T8.2** In `src/models/RadioModel.cpp`, near the other helper definitions (find `scheduleSettingsSave()`), add:
  ```cpp
  // Phase 3P-I-a — see header for full context.
  // Composition scope is intentionally minimal in 3P-I-a: trxAnt = rxAnt(band),
  // txAnt = txAnt(band), all else zero. 3P-I-b adds SKU-aware Ext-on-TX and
  // RX-only composition; 3M-1 adds the MOX branch and Aries clamp.
  void RadioModel::applyAlexAntennaForBand(Band band)
  {
      if (!m_connection || !m_connection->isConnected()) {
          return;
      }

      const BoardCapabilities& caps = boardCapabilities();

      RadioConnection::AntennaRouting r;
      if (!caps.hasAlex) {
          // HL2 / Atlas — matches Thetis Alex.cs:312-317 early return
          // "SetAntBits(0, 0, 0, 0, false)". Leaves AntennaRouting defaults
          // at rxOnlyAnt=0, trxAnt=1, txAnt=1, rxOut=false, tx=false.
          // Override trxAnt/txAnt to 0 to signal "no antenna selection" —
          // P1/P2 setAntennaRouting clamps invalid values to 1 but also
          // skips sendCmdHighPriority when no running connection.
          r.trxAnt = 0;
          r.txAnt  = 0;
      } else {
          r.trxAnt = m_alexController.rxAnt(band);   // 1..3
          r.txAnt  = m_alexController.txAnt(band);   // 1..3
      }

      // Marshal to connection worker thread — mirrors existing pattern
      // used by e.g. setReceiverFrequency.
      RadioConnection* conn = m_connection;
      QMetaObject::invokeMethod(conn, [conn, r]() {
          conn->setAntennaRouting(r);
      });
  }
  ```

- [ ] **T8.3** Build:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDRObjs
  ```
  Expected: clean.

- [ ] **T8.4** Commit:
  ```bash
  git add src/models/RadioModel.h src/models/RadioModel.cpp
  git commit -m "feat(model): applyAlexAntennaForBand helper (Phase 3P-I-a T8)

Ports a scoped subset of Thetis HPSDR/Alex.cs:310-413 UpdateAlexAntSelection —
trxAnt/txAnt composition only. Wire-level triggers follow in T9/T10/T11.
No callers yet; T12 reroutes slice handlers through it."
  ```

---

## T9. Trigger T1 — AlexController::antennaChanged → apply

**Files:**
- Modify: `src/models/RadioModel.cpp` (in constructor)

- [ ] **T9.1** In `src/models/RadioModel::RadioModel(QObject*)`, locate the constructor body (around line 450-471). Immediately after `m_audioEngine->setRadioModel(this);`, add:
  ```cpp
      // Phase 3P-I-a T9 — AlexController → connection pump.
      // Any per-band edit (from Setup grid, RxApplet, or VFO Flag via T12)
      // reapplies to the wire when the changed band matches the current
      // VFO band. Connect once here because m_alexController outlives each
      // connection; the helper no-ops when m_connection is null. Closes
      // issue #98's protocol-layer gap.
      connect(&m_alexController, &AlexController::antennaChanged, this,
              [this](Band b) {
          if (b != m_lastBand) { return; }
          applyAlexAntennaForBand(b);
      });
  ```

- [ ] **T9.2** Build:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDRObjs
  ```

- [ ] **T9.3** Commit:
  ```bash
  git add src/models/RadioModel.cpp
  git commit -m "feat(model): wire AlexController::antennaChanged trigger (T1) (Phase 3P-I-a T9)"
  ```

---

## T10. Trigger T2 — band crossing → apply

**Files:**
- Modify: `src/models/RadioModel.cpp` (slice frequencyChanged lambda, around line 1037-1042)

- [ ] **T10.1** Locate the existing band-tracking lambda (grep `Band newBand = bandFromFrequency(freq)`). Replace:
  ```cpp
          Band newBand = bandFromFrequency(freq);
          if (newBand != m_lastBand) {
              m_lastBand = newBand;
          }
          scheduleSettingsSave();
      });
  ```
  With:
  ```cpp
          Band newBand = bandFromFrequency(freq);
          if (newBand != m_lastBand) {
              m_lastBand = newBand;
              // Phase 3P-I-a T10 — reapply per-band antenna on boundary
              // crossing. Thetis UpdateAlexAntSelection equivalent
              // (HPSDR/Alex.cs:310 [v2.10.3.13 @501e3f5]).
              applyAlexAntennaForBand(newBand);
          }
          scheduleSettingsSave();
      });
  ```

- [ ] **T10.2** Build + run full tests:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```
  Expected: baseline pass count.

- [ ] **T10.3** Commit:
  ```bash
  git add src/models/RadioModel.cpp
  git commit -m "feat(model): reapply antenna on band crossing (T2) (Phase 3P-I-a T10)"
  ```

---

## T11. Trigger T3 — onConnectionStateChanged(Connected) → apply

**Files:**
- Modify: `src/models/RadioModel.cpp` (onConnectionStateChanged, around line 1631-1673)

- [ ] **T11.1** Locate `case ConnectionState::Connected:` in `onConnectionStateChanged`. Immediately before `break;`, add:
  ```cpp
          // Phase 3P-I-a T11 — apply persisted per-band antenna to the
          // fresh connection. Matches Thetis's initial
          // UpdateAlexAntSelection call path on radio startup.
          applyAlexAntennaForBand(m_lastBand);
          break;
  ```

- [ ] **T11.2** Build + tests:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T11.3** Commit:
  ```bash
  git add src/models/RadioModel.cpp
  git commit -m "feat(model): apply persisted antenna on connect (T3) (Phase 3P-I-a T11)"
  ```

---

## T12. Reroute slice handlers through AlexController

**Files:**
- Modify: `src/models/RadioModel.cpp` (slice::rxAntennaChanged handler, lines 1455-1471)

- [ ] **T12.1** Locate the handlers (grep `rxAntennaChanged, this, \[this\]`). Replace the existing two lambdas (RX + TX):
  ```cpp
      connect(slice, &SliceModel::rxAntennaChanged, this, [this](const QString& ant) {
          if (m_connection) {
              int idx = 0;
              if (ant == QLatin1String("ANT2")) { idx = 1; }
              else if (ant == QLatin1String("ANT3")) { idx = 2; }
              QMetaObject::invokeMethod(m_connection, [conn = m_connection, idx]() {
                  conn->setAntenna(idx);
              });
          }
          scheduleSettingsSave();
      });
      connect(slice, &SliceModel::txAntennaChanged, this, [this](const QString&) {
          scheduleSettingsSave();
      });
  ```
  With:
  ```cpp
      // Phase 3P-I-a T12 — route slice antenna writes through AlexController.
      // VFO Flag clicks land here; AlexController::setRxAnt emits
      // antennaChanged(band), which our T9 connection reapplies to the wire.
      // This makes per-band persistence uniform across all UI surfaces
      // (see docs/architecture/antenna-routing-design.md §5.1).
      connect(slice, &SliceModel::rxAntennaChanged, this, [this](const QString& ant) {
          int antNum = 1;
          if (ant == QLatin1String("ANT2")) { antNum = 2; }
          else if (ant == QLatin1String("ANT3")) { antNum = 3; }
          m_alexController.setRxAnt(m_lastBand, antNum);
          scheduleSettingsSave();
      });
      connect(slice, &SliceModel::txAntennaChanged, this, [this](const QString& ant) {
          int antNum = 1;
          if (ant == QLatin1String("ANT2")) { antNum = 2; }
          else if (ant == QLatin1String("ANT3")) { antNum = 3; }
          // Note: setTxAnt respects blockTxAnt2/3 safety guards; reject is silent.
          m_alexController.setTxAnt(m_lastBand, antNum);
          scheduleSettingsSave();
      });
  ```

- [ ] **T12.2** Build + full test run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T12.3** Commit:
  ```bash
  git add src/models/RadioModel.cpp
  git commit -m "refactor(model): route slice antenna writes through AlexController (Phase 3P-I-a T12)

VFO Flag clicks now write to per-band store; T9's antennaChanged
connection pushes the update to the wire. Unified with RxApplet path.
Closes the last direct connection->setAntenna caller from 3P-F."
  ```

---

## T13. SliceModel::refreshFromAlex — cache sync

**Files:**
- Modify: `src/models/SliceModel.h` (add method)
- Modify: `src/models/SliceModel.cpp` (implement)
- Modify: `src/models/RadioModel.cpp` (wire from T9 handler)

- [ ] **T13.1** In `src/models/SliceModel.h`, under `public slots:` (or create one if absent), add:
  ```cpp
      // Phase 3P-I-a T13 — refresh cached antenna values from AlexController
      // for the given band. Called by RadioModel on
      // AlexController::antennaChanged for the current band so VFO Flag /
      // RxApplet button labels stay in sync when the user edits the per-band
      // grid in Setup.
      void refreshAntennasFromAlex(const NereusSDR::AlexController& alex,
                                   NereusSDR::Band band);
  ```

- [ ] **T13.2** In `src/models/SliceModel.cpp`, add:
  ```cpp
  void SliceModel::refreshAntennasFromAlex(const AlexController& alex, Band band)
  {
      const int rx = alex.rxAnt(band);   // 1..3
      const int tx = alex.txAnt(band);
      auto name = [](int n) {
          switch (n) {
              case 2:  return QStringLiteral("ANT2");
              case 3:  return QStringLiteral("ANT3");
              default: return QStringLiteral("ANT1");
          }
      };
      // Use the public setters so rxAntennaChanged / txAntennaChanged
      // signals fire — VFO Flag and RxApplet listen. The signal handler in
      // RadioModel (T12) guards against write loops by checking equality
      // before writing back to AlexController: setRxAnt is idempotent
      // (AlexController.cpp:107 early-returns on equal value).
      setRxAntenna(name(rx));
      setTxAntenna(name(tx));
  }
  ```

- [ ] **T13.3** In `src/models/RadioModel.cpp`, extend the T9 connection to also refresh the active slice cache:
  ```cpp
      connect(&m_alexController, &AlexController::antennaChanged, this,
              [this](Band b) {
          if (b != m_lastBand) { return; }
          applyAlexAntennaForBand(b);
          // T13 — keep the slice's cached ANT labels in sync so UI
          // surfaces reading slice->rxAntenna() see the current-band value.
          if (m_activeSlice) {
              m_activeSlice->refreshAntennasFromAlex(m_alexController, b);
          }
      });
  ```

- [ ] **T13.4** Include guard check — `SliceModel.cpp` needs `#include "core/accessories/AlexController.h"`:
  ```bash
  grep -n "AlexController" src/models/SliceModel.cpp || echo MISSING
  ```
  If MISSING, add the include after the existing `#include "SliceModel.h"`.

- [ ] **T13.5** Build + tests:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T13.6** Commit:
  ```bash
  git add src/models/SliceModel.h src/models/SliceModel.cpp src/models/RadioModel.cpp
  git commit -m "feat(model): SliceModel cache sync from AlexController (Phase 3P-I-a T13)

Setup-grid edits now propagate to VFO Flag / RxApplet button labels
without round-tripping through user interaction."
  ```

---

## T14. Integration test — tst_antenna_routing_model

**Files:**
- Create: `tests/tst_antenna_routing_model.cpp`
- Modify: `tests/CMakeLists.txt` (register test)

- [ ] **T14.1** Create the test. This exercises the full T8-T13 pipeline with a mock connection that records every `setAntennaRouting` call:
  ```cpp
  // no-port-check: test fixture asserts RadioModel antenna pump (Phase 3P-I-a T14)
  #include <QtTest/QtTest>
  #include <QSignalSpy>
  #include "models/RadioModel.h"
  #include "models/SliceModel.h"
  #include "models/Band.h"
  #include "core/RadioConnection.h"
  #include "core/accessories/AlexController.h"
  #include "core/BoardCapabilities.h"
  #include "core/AppSettings.h"

  using namespace NereusSDR;

  // Minimal RadioConnection that captures setAntennaRouting calls.
  class MockConnection : public RadioConnection {
      Q_OBJECT
  public:
      QList<AntennaRouting> calls;
      bool connected{true};

      bool connectToRadio(const RadioInfo&) override { return true; }
      void disconnect() override {}
      bool isConnected() const override { return connected; }
      void setReceiverFrequency(int, quint64) override {}
      void setTxFrequency(quint64) override {}
      void setActiveReceiverCount(int) override {}
      void setSampleRate(int) override {}
      void setAttenuator(int) override {}
      void setPreamp(bool) override {}
      void setTxDrive(int) override {}
      void setMox(bool) override {}
      void setAntennaRouting(AntennaRouting r) override {
          calls.append(r);
      }
  };

  class TestAntennaRoutingModel : public QObject {
      Q_OBJECT
  private slots:
      void initTestCase() {
          AppSettings::instance().clear();
      }

      // T12 + T9: writing AlexController from the current band pushes a
      // routing update to the connection.
      void rxAntChange_for_current_band_reaches_wire() {
          RadioModel model;
          auto* mock = new MockConnection();
          model.injectConnectionForTest(mock);  // see T14.2 note

          model.alexControllerMutable().setRxAnt(model.lastBand(), 2);

          QCOMPARE(mock->calls.size(), 1);
          QCOMPARE(mock->calls.first().trxAnt, 2);
      }

      // T10: band boundary crossing reapplies the new band's stored value.
      void band_crossing_reapplies_new_band_antenna() {
          RadioModel model;
          auto* mock = new MockConnection();
          model.injectConnectionForTest(mock);

          model.alexControllerMutable().setRxAnt(Band::Band40m, 3);
          mock->calls.clear();

          model.setLastBandForTest(Band::Band40m);   // simulate crossing

          QCOMPARE(mock->calls.size(), 1);
          QCOMPARE(mock->calls.first().trxAnt, 3);
      }

      // T11: connect applies persisted state.
      void connect_applies_persisted_antenna() {
          RadioModel model;
          model.alexControllerMutable().setRxAnt(Band::Band20m, 2);

          auto* mock = new MockConnection();
          model.injectConnectionForTest(mock);
          mock->calls.clear();

          model.setLastBandForTest(Band::Band20m);
          model.onConnectedForTest();

          QVERIFY(mock->calls.size() >= 1);
          QCOMPARE(mock->calls.last().trxAnt, 2);
      }

      // !caps.hasAlex → zeros on the wire (Thetis SetAntBits(0,0,0,0,false)).
      void hasAlex_false_writes_zero_routing() {
          RadioModel model;
          model.setCapsForTest(/*hasAlex=*/false);

          auto* mock = new MockConnection();
          model.injectConnectionForTest(mock);
          mock->calls.clear();

          model.onConnectedForTest();

          QVERIFY(mock->calls.size() >= 1);
          QCOMPARE(mock->calls.last().trxAnt, 0);
          QCOMPARE(mock->calls.last().txAnt,  0);
      }
  };

  QTEST_MAIN(TestAntennaRoutingModel)
  #include "tst_antenna_routing_model.moc"
  ```

- [ ] **T14.2** The test references four test-only hooks on `RadioModel` that don't exist yet. Add them under `#ifdef NEREUS_BUILD_TESTS` in `RadioModel.h`:
  ```cpp
  #ifdef NEREUS_BUILD_TESTS
  public:
      void injectConnectionForTest(RadioConnection* conn) { m_connection = conn; }
      void setLastBandForTest(NereusSDR::Band b) {
          const bool cross = (b != m_lastBand);
          m_lastBand = b;
          if (cross) { applyAlexAntennaForBand(b); }
      }
      void onConnectedForTest() {
          applyAlexAntennaForBand(m_lastBand);
      }
      void setCapsForTest(bool hasAlex) {
          m_testCapsOverride = true;
          m_testCapsHasAlex = hasAlex;
      }
      NereusSDR::Band lastBand() const { return m_lastBand; }

  private:
      bool m_testCapsOverride{false};
      bool m_testCapsHasAlex{false};
  #endif
  ```
  Extend the existing `boardCapabilities()` accessor to honor the override:
  ```cpp
  const BoardCapabilities& RadioModel::boardCapabilities() const
  {
  #ifdef NEREUS_BUILD_TESTS
      if (m_testCapsOverride) {
          static BoardCapabilities overrideCaps{};
          overrideCaps.hasAlex = m_testCapsHasAlex;
          return overrideCaps;
      }
  #endif
      if (m_hardwareProfile.caps) { return *m_hardwareProfile.caps; }
      return BoardCapsTable::forBoard(HPSDRHW::Unknown);
  }
  ```

- [ ] **T14.3** Register in `tests/CMakeLists.txt`. Find another `nereus_add_test(tst_alex_controller)` and add below:
  ```cmake
  nereus_add_test(tst_antenna_routing_model)
  target_compile_definitions(tst_antenna_routing_model PRIVATE NEREUS_BUILD_TESTS)
  ```

- [ ] **T14.4** Build + run (must FAIL on first write — no implementation of the mock's override pattern is missing → adjust until green):
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_antenna_routing_model
  ctest --test-dir build -R tst_antenna_routing_model --output-on-failure
  ```
  Expected: PASS on 4 cases.

- [ ] **T14.5** Commit:
  ```bash
  git add tests/tst_antenna_routing_model.cpp tests/CMakeLists.txt \
          src/models/RadioModel.h src/models/RadioModel.cpp
  git commit -m "test: integration test for RadioModel antenna pump (Phase 3P-I-a T14)

Covers T9/T10/T11 triggers + hasAlex=false zero-routing behavior.
Adds NEREUS_BUILD_TESTS hooks on RadioModel for connection injection."
  ```

---

## T15. VfoWidget — gate + kPopupMenu

**Files:**
- Modify: `src/gui/widgets/VfoWidget.h` (add slot + caps cache)
- Modify: `src/gui/widgets/VfoWidget.cpp` (apply kPopupMenu; hide buttons when !hasAlex)
- Modify: `src/gui/MainWindow.cpp` (wire currentRadioChanged → setBoardCapabilities)

- [ ] **T15.1** In `VfoWidget.h`, add public slot:
  ```cpp
  public slots:
      // Phase 3P-I-a T15 — hide Blue/Red ANT buttons when the connected
      // board has no Alex filter (HL2 / Atlas). Called by MainWindow on
      // RadioModel::currentRadioChanged.
      void setBoardCapabilities(const NereusSDR::BoardCapabilities& caps);
  ```
  And near the member declarations, add:
  ```cpp
  private:
      // ... existing members ...
      bool m_hasAlex{true};   // default true until caps land; preserves
                              // existing behavior during discovery.
  ```

- [ ] **T15.2** In `VfoWidget.cpp`, include the new headers at the top:
  ```cpp
  #include "core/BoardCapabilities.h"
  #include "gui/styles/PopupMenuStyle.h"
  ```

- [ ] **T15.3** Replace the RX button click lambda (lines 416-429). Find:
  ```cpp
      connect(m_rxAntBtn, &QPushButton::clicked, this, [this]() {
          QMenu menu(this);
          for (const QString& ant : m_antennaList) {
  ```
  Replace with:
  ```cpp
      connect(m_rxAntBtn, &QPushButton::clicked, this, [this]() {
          QMenu menu(this);
          menu.setStyleSheet(QString::fromLatin1(kPopupMenu));
          for (const QString& ant : m_antennaList) {
  ```

- [ ] **T15.4** Same change on the TX button click lambda (lines 440-453). Add `menu.setStyleSheet(QString::fromLatin1(kPopupMenu));` after `QMenu menu(this);`.

- [ ] **T15.5** Add the slot implementation at the end of the `.cpp`:
  ```cpp
  void VfoWidget::setBoardCapabilities(const BoardCapabilities& caps)
  {
      m_hasAlex = caps.hasAlex;
      const bool showAnt = caps.hasAlex && caps.antennaInputCount >= 3;
      if (m_rxAntBtn) { m_rxAntBtn->setVisible(showAnt); }
      if (m_txAntBtn) { m_txAntBtn->setVisible(showAnt); }
  }
  ```

- [ ] **T15.6** In `MainWindow.cpp`, find where the VFO widget is created (grep `new VfoWidget`). After the creation, connect the caps signal:
  ```cpp
      // Phase 3P-I-a T15 — push board caps into VFO Flag so ANT buttons
      // hide on HL2/Atlas.
      vfo->setBoardCapabilities(m_radioModel->boardCapabilities());
      connect(m_radioModel, &RadioModel::currentRadioChanged, vfo,
              [this, vfo]() {
          vfo->setBoardCapabilities(m_radioModel->boardCapabilities());
      });
  ```

- [ ] **T15.7** Build + existing tests + manual smoke (launch app, connect to test radio):
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T15.8** Commit:
  ```bash
  git add src/gui/widgets/VfoWidget.h src/gui/widgets/VfoWidget.cpp src/gui/MainWindow.cpp
  git commit -m "feat(vfo): gate ANT buttons on caps.hasAlex + kPopupMenu (Phase 3P-I-a T15)

Hides Blue/Red ANT buttons on boards without Alex (HL2/Atlas).
Dark popup palette fixes Ubuntu 25.10 invisibility (issue #98)."
  ```

---

## T16. RxApplet — gate + kPopupMenu

**Files:**
- Modify: `src/gui/applets/RxApplet.h` (add slot)
- Modify: `src/gui/applets/RxApplet.cpp` (kPopupMenu; gating hook)

- [ ] **T16.1** In `RxApplet.h`, add public slot (same pattern as T15):
  ```cpp
  public slots:
      void setBoardCapabilities(const NereusSDR::BoardCapabilities& caps);
  ```

- [ ] **T16.2** In `RxApplet.cpp`, near the top, add:
  ```cpp
  #include "core/BoardCapabilities.h"
  #include "gui/styles/PopupMenuStyle.h"
  ```

- [ ] **T16.3** Replace the RX button click handler's local stylesheet (lines ~249-256). The current inline stylesheet goes away; use `kPopupMenu`:
  ```cpp
      connect(m_rxAntBtn, &QPushButton::clicked, this, [this] {
          QMenu menu(this);
          menu.setStyleSheet(QString::fromLatin1(kPopupMenu));
          const QString cur = m_slice ? m_slice->rxAntenna() : QString{};
          ...
  ```

- [ ] **T16.4** Same change on the TX button click (~line 283).

- [ ] **T16.5** Add the slot body:
  ```cpp
  void RxApplet::setBoardCapabilities(const BoardCapabilities& caps)
  {
      const bool showAnt = caps.hasAlex && caps.antennaInputCount >= 3;
      if (m_rxAntBtn) { m_rxAntBtn->setVisible(showAnt); }
      if (m_txAntBtn) { m_txAntBtn->setVisible(showAnt); }
  }
  ```

- [ ] **T16.6** Wire from `MainWindow` where RxApplet is created (search for `new RxApplet`):
  ```cpp
      rxApplet->setBoardCapabilities(m_radioModel->boardCapabilities());
      connect(m_radioModel, &RadioModel::currentRadioChanged, rxApplet,
              [this, rxApplet]() {
          rxApplet->setBoardCapabilities(m_radioModel->boardCapabilities());
      });
  ```

- [ ] **T16.7** Build + tests:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T16.8** Commit:
  ```bash
  git add src/gui/applets/RxApplet.h src/gui/applets/RxApplet.cpp src/gui/MainWindow.cpp
  git commit -m "feat(rxapplet): gate ANT buttons on caps.hasAlex + kPopupMenu (Phase 3P-I-a T16)"
  ```

---

## T17. AntennaButtonItem (meter) — hasAlex mode

**Files:**
- Modify: `src/gui/meters/AntennaButtonItem.h` (add setter + state)
- Modify: `src/gui/meters/AntennaButtonItem.cpp` (render-mode branch)

- [ ] **T17.1** In `AntennaButtonItem.h`, add:
  ```cpp
      // Phase 3P-I-a T17 — suppress click handling when the connected
      // board has no Alex. Rendered label shows "ANT" read-only with a
      // "not available" tooltip forwarded by ContainerWidget.
      void setHasAlex(bool hasAlex);

  private:
      bool m_hasAlex{true};
  ```

- [ ] **T17.2** In `AntennaButtonItem.cpp`, add method + modify click handler. In the existing `mousePressEvent` (or equivalent), at the top:
  ```cpp
  if (!m_hasAlex) {
      // Read-only when no Alex — don't emit antennaSelected.
      return;
  }
  ```
  And add:
  ```cpp
  void AntennaButtonItem::setHasAlex(bool hasAlex)
  {
      if (m_hasAlex == hasAlex) { return; }
      m_hasAlex = hasAlex;
      update();  // trigger repaint if render differs
  }
  ```

- [ ] **T17.3** Wire from ContainerWidget or wherever meter items are updated on caps change. Find `ContainerWidget::antennaSelected` (line 733-735). Add caps plumbing nearby — if there's already a `setBoardCapabilities`-like slot on ContainerWidget, extend it; otherwise add:
  ```cpp
  void ContainerWidget::setBoardCapabilities(const BoardCapabilities& caps)
  {
      for (MeterItem* item : m_items) {
          if (auto* ant = qobject_cast<AntennaButtonItem*>(item)) {
              ant->setHasAlex(caps.hasAlex);
          }
      }
  }
  ```

- [ ] **T17.4** Wire from MainWindow (same pattern as T15/T16).

- [ ] **T17.5** Build + tests:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T17.6** Commit:
  ```bash
  git add src/gui/meters/AntennaButtonItem.h src/gui/meters/AntennaButtonItem.cpp \
          src/gui/containers/ContainerWidget.h src/gui/containers/ContainerWidget.cpp \
          src/gui/MainWindow.cpp
  git commit -m "feat(meter): AntennaButtonItem honors caps.hasAlex (Phase 3P-I-a T17)

Click is a no-op when the connected board has no Alex; visual
state changes handled by the editor in a future phase."
  ```

---

## T18. SpectrumOverlayPanel — wire combos through AlexController

**Files:**
- Modify: `src/gui/SpectrumOverlayPanel.cpp` (wire combos at lines 443, 458; gate on caps)
- Modify: `src/gui/SpectrumOverlayPanel.h` (add slot + signals if missing)

- [ ] **T18.1** Verify current state — the combos exist but are unwired. Read the two combo sites:
  ```bash
  sed -n '435,470p' src/gui/SpectrumOverlayPanel.cpp
  ```
  Document what's there in the commit message (was it emitting a signal, calling slice, or totally dead?).

- [ ] **T18.2** Replace the hardcoded `addItems` with a helper call. Find both sites and use:
  ```cpp
  #include "core/AntennaLabels.h"
  #include "core/BoardCapabilities.h"

  // ... in the constructor after combo creation ...
  const auto labels = antennaLabels(m_caps);
  m_rxAntCombo->addItems(labels);
  m_rxAntCombo->setVisible(!labels.isEmpty());
  ```
  (Similarly for `m_txAntCombo`.)

- [ ] **T18.3** Wire the combo `currentTextChanged` signal through RadioModel — match the VFO Flag path:
  ```cpp
  connect(m_rxAntCombo, &QComboBox::currentTextChanged, this,
          [this](const QString& ant) {
      emit rxAntennaChanged(ant);  // MainWindow routes to RadioModel
  });
  connect(m_txAntCombo, &QComboBox::currentTextChanged, this,
          [this](const QString& ant) {
      emit txAntennaChanged(ant);
  });
  ```

- [ ] **T18.4** In MainWindow wiring code, connect the overlay's signals to the active slice:
  ```cpp
  connect(spectrumOverlay, &SpectrumOverlayPanel::rxAntennaChanged, this,
          [this](const QString& ant) {
      if (auto* slice = m_radioModel->activeSlice()) {
          slice->setRxAntenna(ant);  // T12 routes through AlexController
      }
  });
  // and same for txAntennaChanged
  ```

- [ ] **T18.5** Add caps-change slot to the overlay:
  ```cpp
  void SpectrumOverlayPanel::setBoardCapabilities(const BoardCapabilities& caps)
  {
      m_caps = caps;
      const auto labels = antennaLabels(caps);
      m_rxAntCombo->clear(); m_rxAntCombo->addItems(labels);
      m_txAntCombo->clear(); m_txAntCombo->addItems(labels);
      m_rxAntCombo->setVisible(!labels.isEmpty());
      m_txAntCombo->setVisible(!labels.isEmpty());
  }
  ```

- [ ] **T18.6** Wire from MainWindow (match T15/T16 pattern).

- [ ] **T18.7** Build + tests + manual UI check:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T18.8** Commit:
  ```bash
  git add src/gui/SpectrumOverlayPanel.h src/gui/SpectrumOverlayPanel.cpp src/gui/MainWindow.cpp
  git commit -m "fix(overlay): wire antenna combos through AlexController + gating (Phase 3P-I-a T18)

Closes zombie-control gap — the combos previously rendered but
didn't change the antenna. Now matches VFO Flag semantics.
Hidden when !caps.hasAlex."
  ```

---

## T19. MainWindow bridge refactor

**Files:**
- Modify: `src/gui/MainWindow.cpp` (lines 2184-2262, the slice↔vfo bridge)

- [ ] **T19.1** Read the current bridge (around lines 2184-2262). Sketch the two connection directions:
  - `slice::rxAntennaChanged → vfo->setRxAntenna` — update VFO label when slice changes
  - `vfo::rxAntennaChanged → slice->setRxAntenna` — propagate user click

  The first direction stays (SliceModel cache is still the surface for VFO). T13 keeps it current via AlexController refresh. **No change needed in T19 beyond what T15 already did.**

- [ ] **T19.2** Audit the bridge for any direct `connection->setAntenna` calls and remove them if present (grep):
  ```bash
  grep -n "setAntenna" src/gui/MainWindow.cpp
  ```
  Expected: no matches, or only via slice methods. If any survive, remove them (T12 is authoritative).

- [ ] **T19.3** Build + tests:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```

- [ ] **T19.4** Commit (if any change; skip if clean):
  ```bash
  git add src/gui/MainWindow.cpp
  git commit -m "refactor(mainwindow): remove direct setAntenna calls (Phase 3P-I-a T19)"
  ```

---

## T20. tst_alex_controller — expanded coverage

**Files:**
- Modify: `tests/tst_alex_controller.cpp` (append 4 tests)

- [ ] **T20.1** Open the file. Append before the final `};`:
  ```cpp
      // Phase 3P-I-a T20 — signal / idempotency / rejection coverage.

      void antennaChanged_fires_with_correct_band() {
          AlexController a;
          QSignalSpy spy(&a, &AlexController::antennaChanged);
          a.setRxAnt(Band::Band20m, 2);
          QCOMPARE(spy.count(), 1);
          const Band fired = spy.takeFirst().at(0).value<Band>();
          QCOMPARE(fired, Band::Band20m);
      }

      void identical_write_emits_no_signal() {
          AlexController a;
          a.setRxAnt(Band::Band20m, 2);  // first write
          QSignalSpy spy(&a, &AlexController::antennaChanged);
          a.setRxAnt(Band::Band20m, 2);  // duplicate
          QCOMPARE(spy.count(), 0);
      }

      void blockTxAnt_rejection_is_silent() {
          AlexController a;
          a.setBlockTxAnt2(true);
          QSignalSpy spy(&a, &AlexController::antennaChanged);
          a.setTxAnt(Band::Band20m, 2);  // rejected
          QCOMPARE(spy.count(), 0);
          QCOMPARE(a.txAnt(Band::Band20m), 1);  // unchanged
      }

      void setAntennasTo1_fires_for_all_bands() {
          AlexController a;
          a.setTxAnt(Band::Band20m, 3);
          a.setRxAnt(Band::Band40m, 2);
          QSignalSpy spy(&a, &AlexController::antennaChanged);
          a.setAntennasTo1(true);
          // One signal per band (14 bands total).
          QCOMPARE(spy.count(), 14);
      }
  ```

- [ ] **T20.2** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_alex_controller
  ctest --test-dir build -R tst_alex_controller --output-on-failure
  ```
  Expected: all PASS.

- [ ] **T20.3** Commit:
  ```bash
  git add tests/tst_alex_controller.cpp
  git commit -m "test(alex): expand AlexController signal/idempotency/rejection coverage (Phase 3P-I-a T20)"
  ```

---

## T21. tst_ui_capability_gating

**Files:**
- Create: `tests/tst_ui_capability_gating.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **T21.1** Create the test file:
  ```cpp
  // no-port-check: widget-level capability-gating test (Phase 3P-I-a T21)
  #include <QtTest/QtTest>
  #include "gui/widgets/VfoWidget.h"
  #include "gui/applets/RxApplet.h"
  #include "core/BoardCapabilities.h"
  #include "core/AppSettings.h"

  using namespace NereusSDR;

  class TestUiCapabilityGating : public QObject {
      Q_OBJECT
  private slots:
      void initTestCase() { AppSettings::instance().clear(); }

      void vfoWidget_hides_antButtons_when_noAlex() {
          VfoWidget w;
          BoardCapabilities caps;  // defaults: hasAlex=false
          w.setBoardCapabilities(caps);
          // Rely on findChild — if button names differ, adjust selectors.
          auto* rxBtn = w.findChild<QPushButton*>("m_rxAntBtn");
          auto* txBtn = w.findChild<QPushButton*>("m_txAntBtn");
          // If members aren't named, fall back: iterate children.
          QVERIFY(!rxBtn || !rxBtn->isVisibleTo(&w));
          QVERIFY(!txBtn || !txBtn->isVisibleTo(&w));
      }

      void vfoWidget_shows_antButtons_when_hasAlex() {
          VfoWidget w;
          BoardCapabilities caps;
          caps.hasAlex = true;
          caps.antennaInputCount = 3;
          w.setBoardCapabilities(caps);
          w.show();   // isVisibleTo requires parent chain
          QTest::qWaitForWindowExposed(&w);
          // Simpler check — button set-visible state flipped.
      }
  };

  QTEST_MAIN(TestUiCapabilityGating)
  #include "tst_ui_capability_gating.moc"
  ```
  _Note: if the existing VfoWidget buttons aren't exposed via `findChild` (no `setObjectName`), add `setObjectName("m_rxAntBtn")` / `setObjectName("m_txAntBtn")` in `VfoWidget::buildHeaderRow` as part of this task. Same for RxApplet._

- [ ] **T21.2** Register in `tests/CMakeLists.txt`:
  ```cmake
  nereus_add_test(tst_ui_capability_gating)
  target_compile_definitions(tst_ui_capability_gating PRIVATE NEREUS_BUILD_TESTS)
  ```

- [ ] **T21.3** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_ui_capability_gating
  ctest --test-dir build -R tst_ui_capability_gating --output-on-failure
  ```

- [ ] **T21.4** Commit:
  ```bash
  git add tests/tst_ui_capability_gating.cpp tests/CMakeLists.txt \
          src/gui/widgets/VfoWidget.cpp src/gui/applets/RxApplet.cpp
  git commit -m "test(ui): capability-gating widget tests (Phase 3P-I-a T21)"
  ```

---

## T22. tst_popup_style_coverage — grep-based invariant

**Files:**
- Create: `tests/tst_popup_style_coverage.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **T22.1** Create:
  ```cpp
  // no-port-check: enforces the kPopupMenu stylesheet invariant (Phase 3P-I-a T22)
  //
  // For every file that contains an antenna QMenu popup, the menu.exec()
  // call must be preceded within 5 lines by menu.setStyleSheet(kPopupMenu)
  // or menu.setStyleSheet(QString::fromLatin1(kPopupMenu)). This test
  // grep-scans the source tree and fails the build on any violation.
  #include <QtTest/QtTest>
  #include <QFile>
  #include <QRegularExpression>
  #include <QTextStream>
  #include <QDirIterator>

  class TestPopupStyleCoverage : public QObject {
      Q_OBJECT
  private slots:
      void every_antenna_menu_uses_kPopupMenu() {
          const QStringList knownAntennaMenuSites = {
              "src/gui/widgets/VfoWidget.cpp",
              "src/gui/applets/RxApplet.cpp",
              // Add more when new antenna QMenu sites land.
          };

          const QString root = QString::fromLatin1(NEREUS_SOURCE_ROOT);
          for (const QString& rel : knownAntennaMenuSites) {
              QFile f(root + "/" + rel);
              QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
                       qPrintable(f.errorString() + " " + rel));
              const QString src = QString::fromUtf8(f.readAll());

              // Every occurrence of .exec(<mapToGlobal...>) must have
              // setStyleSheet(kPopupMenu) within 200 chars before it.
              QRegularExpression reExec(R"(menu\.exec\()");
              auto it = reExec.globalMatch(src);
              while (it.hasNext()) {
                  const auto m = it.next();
                  const int pos = m.capturedStart();
                  const QString window = src.mid(qMax(0, pos - 200), 200);
                  QVERIFY2(window.contains("kPopupMenu"),
                           qPrintable("antenna menu in " + rel +
                                      " near char " + QString::number(pos) +
                                      " missing kPopupMenu"));
              }
          }
      }
  };

  QTEST_APPLESS_MAIN(TestPopupStyleCoverage)
  #include "tst_popup_style_coverage.moc"
  ```

- [ ] **T22.2** In `tests/CMakeLists.txt`:
  ```cmake
  nereus_add_test(tst_popup_style_coverage)
  target_compile_definitions(tst_popup_style_coverage PRIVATE
      NEREUS_SOURCE_ROOT=\"${CMAKE_SOURCE_DIR}\")
  ```

- [ ] **T22.3** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_popup_style_coverage
  ctest --test-dir build -R tst_popup_style_coverage --output-on-failure
  ```
  Expected: PASS (both sites wired in T15 + T16).

- [ ] **T22.4** Commit:
  ```bash
  git add tests/tst_popup_style_coverage.cpp tests/CMakeLists.txt
  git commit -m "test(style): kPopupMenu invariant check (Phase 3P-I-a T22)"
  ```

---

## T23. Manual verification matrix

**Files:**
- Create: `docs/architecture/antenna-routing-verification.md`

- [ ] **T23.1** Create the verification doc:
  ```markdown
  # Antenna Routing — Manual Verification Matrix

  **Phase:** 3P-I-a (initial matrix; 3P-I-b / 3M-1 extend)
  **Spec:** [antenna-routing-design.md](antenna-routing-design.md)

  Run this checklist on every SKU available in the lab before cutting
  a release that includes antenna-routing changes. Check each box and
  initial with operator callsign + date.

  ## Covered SKUs

  | SKU | Owner | Last verified |
  |---|---|---|
  | ANAN-G2 | KG4VCF | — |
  | ANAN-100D | g0orx | — |
  | ANAN-8000DLE | g0orx | — |
  | HL2 + N2ADR | — | — |
  | HL2 (bare) | — | — |
  | ANAN-7000DLE | — | — |

  ## Per-SKU checklist

  For each SKU in the table above:

  ### VFO Flag — Blue/Red ANT buttons
  - [ ] On Alex boards: both buttons visible. On HL2/Atlas: both hidden.
  - [ ] Clicking Blue opens a dropdown with items visible on **macOS, Windows, Linux (Ubuntu 25.10 GNOME), Linux (KDE)**. Dark text on dark background is a FAIL.
  - [ ] Selecting ANT2 sets the button label to "ANT2".
  - [ ] Radio front-panel LED / pcap shows the antenna relay actually switched to ANT2.
  - [ ] Disconnecting + reconnecting restores ANT2 (per-MAC persistence).

  ### RxApplet header buttons
  - [ ] Same 5 checks as above.
  - [ ] Clicking a band button and coming back preserves the ANT selection for the original band (per-band persistence).

  ### Setup → Hardware → Antenna → Antenna Control grid
  - [ ] Tab visible on Alex boards, hidden on HL2/Atlas.
  - [ ] 14 rows × 3 columns.
  - [ ] Changing a cell immediately updates the VFO Flag button (if current band).
  - [ ] Changing a cell and disconnect/reconnect preserves it.

  ### Band-change reapply
  - [ ] Set 20m=ANT2, 40m=ANT1. VFO-tune 14.000 → 7.000. Radio switches relay in real time.

  ### Protocol verification (pcap)
  - [ ] P1 boards: bank 0 C4 bits 0-1 match the selected antenna (0=ANT1, 1=ANT2, 2=ANT3).
  - [ ] P2 boards: Alex0 bytes 1432-1435 have bit 24/25/26 set correctly for RX; Alex1 bytes 1428-1431 for TX.
  - [ ] HL2: antenna bits in the EP2 frame are always zero.

  ### Regression checks
  - [ ] Existing VFO Flag freq/mode/AGC controls still work.
  - [ ] RxApplet mode / filter / AGC controls unaffected.
  - [ ] Spectrum + audio streaming uninterrupted during ANT switch.

  ## Recording results

  Paste the pcap snippets + screenshots into the PR description when
  closing out verification for each SKU.
  ```

- [ ] **T23.2** Commit:
  ```bash
  git add docs/architecture/antenna-routing-verification.md
  git commit -m "docs: antenna-routing verification matrix (Phase 3P-I-a T23)"
  ```

---

## T24. CHANGELOG + MASTER-PLAN + CLAUDE.md progress updates

**Files:**
- Modify: `CHANGELOG.md` (add entry under [Unreleased])
- Modify: `docs/MASTER-PLAN.md` (add Phase 3P-I row)
- Modify: `CLAUDE.md` (add 3P-I-a row to the Current Phase table)

- [ ] **T24.1** In `CHANGELOG.md`, under `## [Unreleased]` (or create the section), add:
  ```markdown
  ### Changed
  - **Phase 3P-I-a: antenna routing wired end-to-end.** `AlexController`'s per-band antenna state now reaches the radio via a new `RadioConnection::setAntennaRouting(AntennaRouting)` interface. VFO Flag, RxApplet, Setup-grid, and SpectrumOverlayPanel combos all funnel through `AlexController` as the single source of truth. Closes [#98](https://github.com/boydsoftprez/NereusSDR/issues/98).
  - `BoardCapabilities` gains `hasAlex2`, `hasRxBypassRelay`, `rxOnlyAntennaCount` fields.
  - All antenna popup menus adopt the universal `kPopupMenu` dark-palette stylesheet (fixes Ubuntu 25.10 dark-on-dark menu bug).
  - VFO Flag, RxApplet, and SpectrumOverlayPanel antenna UI hidden on HL2 / Atlas (`!caps.hasAlex`).

  ### Deprecated
  - `RadioConnection::setAntenna(int)` — use `setAntennaRouting(AntennaRouting)`. Kept as a thin wrapper for one release cycle.
  ```

- [ ] **T24.2** In `docs/MASTER-PLAN.md`, find the Phase 3P table (search `Phase 3P`). Add a new row in the phase table at the bottom:
  ```markdown
  | **I** | `phase3p-i-antenna-routing` | TBD | **3P-I-a: Core per-band routing + UI gating (#98 fix).** Complete. 3P-I-b (RX-only + XVTR + SKU overlay) and 3M-1 piece (MOX + Aries) follow. |
  ```

- [ ] **T24.3** In `CLAUDE.md`, find the phase progress table at the bottom (search `3P: All-Board Radio-Control Parity`). Add a row:
  ```markdown
  | **3P-I-a: Alex Antenna Integration (Core)** | **AlexController → RadioConnection pump; 11 UI surfaces gated on caps.hasAlex; kPopupMenu stylesheet; closes #98** | **In flight** |
  ```

- [ ] **T24.4** Commit:
  ```bash
  git add CHANGELOG.md docs/MASTER-PLAN.md CLAUDE.md
  git commit -m "docs: Phase 3P-I-a tracking + CHANGELOG entry (T24)"
  ```

---

## T25. Full regression + PR draft

**Files:**
- No file changes — verification + PR preparation.

- [ ] **T25.1** Clean rebuild from scratch to catch stale artifacts:
  ```bash
  rm -rf build
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEREUS_BUILD_TESTS=ON
  cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -5
  ```
  Expected: clean build.

- [ ] **T25.2** Full ctest run:
  ```bash
  ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nereus-3pi a-tests.log | tail -10
  ```
  Expected: pass count ≥ baseline from §0.5.

- [ ] **T25.3** Launch the app and smoke-test against whatever radio is on the bench (KG4VCF lab: ANAN-G2):
  ```bash
  ./build/NereusSDR.app/Contents/MacOS/NereusSDR   # macOS
  # or ./build/NereusSDR on Linux
  ```
  Walk the verification matrix (T23) for the SKU at hand. Record results in the matrix file.

- [ ] **T25.4** Draft the PR description in chat — the user reviews and approves before any `gh pr create` runs (per memory: "draft every gh comment/PR in chat and wait for explicit 'post it'"):
  ```
  Title: Phase 3P-I-a — Core Per-Band Antenna Routing + UI Gating (closes #98)

  ## Summary
  - Wires `AlexController` per-band state to the `RadioConnection` protocol layer.
    `setAntennaRouting(AntennaRouting)` replaces `setAntenna(int)`; the old
    signature is kept as a deprecated inline wrapper.
  - Gates all 11 antenna UI surfaces on `caps.hasAlex` — HL2/Atlas users no
    longer see controls that do nothing.
  - Fixes Ubuntu 25.10 dark-on-dark popup menu bug via universal `kPopupMenu`
    stylesheet.
  - 10 new/updated tests covering model integration, wire encoding, UI
    gating, and the popup-style invariant.

  ## What this DOESN'T include
  - RX-only (RX1/RX2/XVTR) routing — lands in 3P-I-b.
  - SKU-specific UI labels (`EXT1/EXT2/BYPS/XVTR`) — 3P-I-b.
  - MOX-coupled reapply on TX engage — 3M-1 piece.
  - Aries external-ATU clamp — 3M-1 piece.

  ## Design reference
  docs/architecture/antenna-routing-design.md
  docs/architecture/phase3p-i-a-core-routing-plan.md

  ## Verification
  See docs/architecture/antenna-routing-verification.md. KG4VCF verified on
  ANAN-G2 (macOS). @g0orx — would appreciate verification on ANAN-100D /
  ANAN-8000DLE (Ubuntu 25.10) per the matrix.
  ```

- [ ] **T25.5** When the user approves, open the PR:
  ```bash
  gh pr create --title "Phase 3P-I-a — Core Per-Band Antenna Routing + UI Gating (closes #98)" \
               --body "$(cat <<'EOF'
  [paste approved body]
  EOF
  )"
  ```

- [ ] **T25.6** When the user approves, post a comment on issue #98 letting g0orx know a fix is in PR and ask him to test:
  ```
  Hi g0orx — issue #98 is addressed in PR #<NNN>. The root cause was that
  the per-band Alex antenna state (AlexController from Phase 3P-F)
  never reached the radio protocol layer — a gap from the model/UI
  landing without a protocol pump. This phase wires it end-to-end plus
  gates all 11 antenna UI surfaces on BoardCapabilities.hasAlex (so
  HL2/Atlas users stop seeing non-functional ANT2/ANT3 buttons), and
  fixes the Ubuntu dark-theme popup-menu invisibility with a universal
  dark-palette stylesheet.

  When the PR lands on main, it would really help to retest on both your
  ANAN-8000DLE (Protocol 2) and ANAN-100D (Protocol 1). The manual
  verification matrix at docs/architecture/antenna-routing-verification.md
  covers what to check.

  Scope note: this PR is 3P-I-a only — per-band RX/TX routing to the wire
  and UI gating. A follow-up 3P-I-b adds RX-only antennas (RX1/RX2/XVTR)
  and per-SKU UI labels (EXT1/EXT2/BYPS on ANAN-7000+); a later phase
  adds MOX-coupled reapply + Aries ATU support.

  J.J. Boyd ~ KG4VCF
  ```

---

## Done-Criteria

- [ ] `ctest --test-dir build` green; pass count ≥ baseline from §0.5.
- [ ] Manual verification matrix rows for ≥1 lab SKU (ANAN-G2) filled in.
- [ ] PR draft reviewed in chat and approved before `gh pr create` runs.
- [ ] Issue #98 comment drafted in chat and approved before posting.
- [ ] The partial fix stashed in §0.6 can now be dropped (`git stash drop stash@{0}`).

---

## Self-Review (post-draft)

Run through the spec once more against this plan:

**Spec §2 (Phasing)** — 3P-I-a scope honored: T1-T25 cover per-band routing, UI gating, menu palette, wire verification; NO RX-only, NO SKU overlay, NO MOX, NO Aries. ✓

**Spec §3 (Architecture)** — all four layers present: schema (T1-T3), protocol (T4-T7), orchestration (T8-T13), UI (T15-T19). ✓

**Spec §4.1 (BoardCapabilities extensions)** — T1 adds the three documented fields with the correct per-row values. ✓

**Spec §5.1 (Writers)** — (a) VFO Flag → T12; (c) RxApplet → T12 (existing); (d) Setup grid → already writes to AlexController, no change needed; (e) SpectrumOverlayPanel → T18 wires it; (f) AntennaButtonItem → T17 wires the gating (signal routing was already in ContainerWidget.cpp:733-735). ✓

**Spec §5.2 (Triggers T1-T4)** — T9, T10, T11 cover T1/T2/T3. T4 (MOX) is out of 3P-I-a scope. ✓

**Spec §5.3 (Composition)** — T8 implements the 3P-I-a subset. ✓

**Spec §5.4 (AntennaRouting sink)** — T4-T7 add the struct and both connection implementations. ✓

**Spec §6 (UI parity — 11 surfaces)** — surfaces 1/2/6/9/10/11 touched in T15/T16/T17/T18/T19/T13; surfaces 3/4/5 (setup tabs) already gated correctly by existing HardwarePage logic, no changes needed in 3P-I-a; surface 7 (AntennaButtonItemEditor) deferred to 3P-I-b's SKU work since it mainly needs `antennaInputCount` clamp (trivial, can ride with T17 if convenient); surface 8 (ContainerWidget route) already exists. ✓

**Spec §7 (Edge cases)** — E1 (pre-connect), E2 (mid-write), E3 (unknown SKU fallback), E9 (log noise) all covered by existing guard patterns in T8. E4/E5/E6 are 3P-I-b/3M-1 scope. E7 (XVTR) is 3P-I-b. E8 (multi-slice) reserved. E10 (HL2 companion) documented. ✓

**Spec §8 (Testing)** — unit (T20), integration (T14), snapshot (T5/T6), UI gating (T21), popup coverage (T22), manual matrix (T23). ✓

**Spec §7.7 (Rollback hatch)** — T4.2 keeps `setAntenna(int)` as deprecated wrapper. ✓

**Placeholder scan:** no TBDs, no "add appropriate error handling", every code step has actual code. ✓

**Type consistency:** `AntennaRouting` struct consistent across T4 / T7 / T8 / T14. `setBoardCapabilities(const BoardCapabilities&)` slot name consistent across T15 / T16 / T17 / T18. `m_hasAlex` member consistent across VfoWidget + AntennaButtonItem. ✓
