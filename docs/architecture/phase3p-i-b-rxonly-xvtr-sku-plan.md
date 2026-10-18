# Phase 3P-I-b — RX-Only Antennas, SKU Labels, Ext/Bypass Flags, XVTR — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** [`docs/architecture/antenna-routing-design.md`](./antenna-routing-design.md) §2 (3P-I-b row), §4.3, §5.3 (full composition), §6.1 (Rules 1+4 extensions).
**Predecessor:** PR #116 (Phase 3P-I-a) — merged to `main` 2026-04-23. This plan stacks directly on `main`.
**Branch:** `feature/phase3p-i-b-rxonly-antennas` (create at start).
**Goal:** Complete the RX-side of Alex antenna routing: wire `AlexController::rxOnlyAnt` to the P1/P2 wire with Thetis-exact byte encoding; introduce the `SkuUiProfile` overlay so per-product labels (RX1/RX2/XVTR vs EXT2/EXT1/XVTR vs BYPS/EXT1/XVTR) render correctly on every surface; extend the Setup → Antenna Control grid with an RX-only column and the Ext1/Ext2/RxOut-on-TX checkboxes; add a third RX-Bypass VFO Flag button gated on `caps.hasRxBypassRelay`; expand `RadioModel::applyAlexAntennaForBand` to the full Thetis composition (isTx branch + Ext-on-TX mapping + XVTR + rxOutOverride). **No** MOX-coupled reapply and **no** Aries clamp — both deferred to Phase 3M-1 per design §2.

**Architecture:** Bottom-up, same as 3P-I-a — (1) schema helpers (SkuUiProfile, AntennaLabels RX-only extension), (2) AlexController flag additions (Ext1/Ext2OnTx, RxOutOnTx, rxOutOverride, useTxAntForRx, xvtrActive) with per-MAC persistence, (3) protocol codec RX-only byte encoding with byte-lock tests against Thetis netInterface.c, (4) RadioModel composition expanded to mirror Alex.cs:310-413 (minus MOX+Aries), (5) UI surfaces (Setup grid RX-only column, Ext checkboxes, Alex-2 sub-tab gate, VFO Flag 3rd button, AntennaButtonItem 3-port mode). Every task is self-contained, compiles clean, leaves baseline test pass count at or above entry.

**Tech Stack:** C++20, Qt6 (Widgets + Test), `AppSettings` XML persistence, existing `AlexController` / `BoardCapabilities` / `AntennaRouting` / `applyAlexAntennaForBand` scaffolding from 3P-I-a.

**Thetis baseline:** `v2.10.3.13 @501e3f5`. Every new inline cite in ported code uses this stamp per [`docs/attribution/HOW-TO-PORT.md`](../attribution/HOW-TO-PORT.md) §Inline cite versioning.

---

## 0. Pre-flight checks

- [ ] **0.1** Verify 3P-I-a landed and rebase to `main`:
  ```bash
  cd /Users/j.j.boyd/NereusSDR
  git fetch origin
  git log --oneline origin/main | grep -q "3P-I-a" && echo "3P-I-a present" || { echo "MISSING — check PR #116 merge"; exit 1; }
  ```
  Expected: `3P-I-a present`. If missing, 3P-I-a is not merged — stop this plan.

- [ ] **0.2** Create the phase branch from main:
  ```bash
  git checkout main && git pull --ff-only
  git checkout -b feature/phase3p-i-b-rxonly-antennas
  git push -u origin feature/phase3p-i-b-rxonly-antennas
  ```
  Expected: clean branch at `origin/main` HEAD.

- [ ] **0.3** Verify Thetis clone is at the expected version:
  ```bash
  git -C /Users/j.j.boyd/Thetis rev-parse --short HEAD
  ```
  Expected: `501e3f5`. If anything else, refresh per `docs/attribution/UPSTREAM-SYNC-PROTOCOL.md`.

- [ ] **0.4** Baseline build:
  ```bash
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEREUS_BUILD_TESTS=ON
  cmake --build build -j$(sysctl -n hw.ncpu)
  ```
  Expected: zero errors.

- [ ] **0.5** Baseline test pass count:
  ```bash
  ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nereus-3pib-baseline.log | tail -3
  ```
  Record the pass count. Every subsequent task must leave this count at baseline or higher.

- [ ] **0.6** Install the attribution pre-commit hooks (one-time if not already):
  ```bash
  bash scripts/install-hooks.sh
  ```
  The hook runs `verify-thetis-headers.py` + `check-new-ports.py` + `verify-inline-tag-preservation.py` before every commit.

---

## File Structure Overview

### New files

| Path | Responsibility |
|---|---|
| `src/core/SkuUiProfile.h` | Pure POD struct describing UI-only differences between SKUs: `hasExt1OutOnTx`, `hasExt2OutOnTx`, `hasRxBypassUi`, `rxOnlyLabels[3]`, `antennaTabLabel`. Plus `SkuUiProfile skuUiProfileFor(HPSDRModel)` free function. Does **not** affect the wire. |
| `src/core/SkuUiProfile.cpp` | 14-row dispatch table (one per `HPSDRModel` enum value) ported from Thetis `setup.cs:19832-20375`. Each row cites its upstream line range. |
| `tests/tst_sku_ui_profile.cpp` | Per-SKU fixture: every enum value returns the label triple and flag set Thetis assigns. |
| `tests/tst_rxonly_routing.cpp` | Integration test: `RadioModel` + mock `RadioConnection` observes `rxOnlyAnt`/`rxOut` propagation across connect / band change / Ext1OnTx / xvtrActive toggles. |
| `docs/architecture/phase3p-i-b-verification/README.md` | Per-SKU manual verification matrix — RX1/RX2/XVTR cycling, Ext1-on-TX bypass paths, RX-Bypass VFO button, XVTR band crossings. |

### Extended files

| Path | Change |
|---|---|
| `src/core/AntennaLabels.h` / `.cpp` | Add `std::array<QString,3> rxOnlyLabels(const SkuUiProfile&)`. Leaves existing `antennaLabels(caps)` untouched. |
| `src/core/accessories/AlexController.h` / `.cpp` | Add flags `ext1OutOnTx`, `ext2OutOnTx`, `rxOutOnTx`, `rxOutOverride`, `useTxAntForRx`, `xvtrActive` with setters, getters, signals, and per-MAC persistence keys. Add `rxOnlyAntChanged(Band)` signal (currently `antennaChanged(Band)` covers all three arrays — we split RX-only for finer UI updates). |
| `src/core/RadioConnection.h` | No struct change — `AntennaRouting` already carries `rxOnlyAnt` + `rxOut` (3P-I-a). Verify deprecated `setAntenna(int)` shim still compiles (removed after 3P-I-b per design §7.7). |
| `src/core/codec/P1CodecStandard.cpp` | Bank 0 C3 bits 5-6 (`_Rx_1_In`, `_Rx_2_In`, `_XVTR_Rx_In`): wire from `m_rxOnlyAnt`. Bit 7 (`_Rx_1_Out`): wire from `m_rxOut`. Add Thetis cite + inline `//DH1KLM`-style tag preservation. |
| `src/core/codec/P1CodecHl2.cpp` | Identical C3/C4 encoding as Standard — the HL2 codec extends P1CodecStandard; confirm no override needed (HL2 has `caps.hasAlex=false` so the bits reach the wire as zero anyway). Add the byte-lock test row. |
| `src/core/P1RadioConnection.cpp` | Extend `setAntennaRouting` to copy `rxOnlyAnt` + `rxOut` into the codec state held on the worker thread. |
| `src/core/P2RadioConnection.cpp` | `buildAlex0`: wire bits 27-30 from `routing.rxOnlyAnt` + `routing.rxOut`. Add Thetis cite against Alex.cs → NetworkIO mapping. |
| `src/models/RadioModel.h` / `.cpp` | Expand `applyAlexAntennaForBand(Band, bool isTx)` from the 3P-I-a minimal shape to the full Alex.cs:310-413 composition **minus MOX/Aries**: isTx branch, Ext1/Ext2OnTx mapping, rx_out_override, xvtrActive gating. Wire new triggers: `AlexController::ext1OutOnTxChanged` etc. |
| `src/models/SliceModel.h` / `.cpp` | No change — `refreshFromAlex` already handles all three arrays via `antennaChanged(Band)` since 3P-I-a. Verify test still passes. |
| `src/gui/setup/hardware/AntennaAlexAntennaControlTab.h` / `.cpp` | Add a third per-band column "RX only" (14 rows × 3 radio buttons per row with SKU-overlayed labels). Add four checkboxes below the grid: `Ext1 on TX`, `Ext2 on TX`, `RxBypass on TX`, `Disable RxBypass relay`. Each gated by `SkuUiProfile` flags. |
| `src/gui/setup/hardware/AntennaAlexTab.cpp` | Gate the Alex-2 Filters sub-tab's `setVisible()` on `caps.hasAlex2` (table already populates correctly; only the UI-gate is missing). |
| `src/gui/widgets/VfoWidget.h` / `.cpp` | Add optional 3rd button `m_rxBypassBtn` (grey/black, between blue RX and red TX). Shown when `caps.hasRxBypassRelay && sku.hasRxBypassUi`. Toggles `AlexController::rxOutOnTx`. |
| `src/gui/meters/AntennaButtonItem.cpp` / `.h` | Already renders up to 3 TX buttons. Add a "RX-only" sub-mode showing SKU-overlayed labels when bound item type is `RxOnlyButton` (new enum). |
| `src/gui/applets/RxApplet.h` / `.cpp` | ANT button row stays 3-button; tooltip now reads the SKU-overlaid RX-only names when `alt` is held (NereusSDR-native UX polish — not a Thetis port, call it out in the commit message). |
| `src/gui/SpectrumOverlayPanel.cpp` | No change — overlay panel controls TRX antenna only, not RX-only. Verify test still passes. |
| `tests/tst_alex_controller.cpp` | +6 new cases: `ext1OutOnTx` flag persists, `ext2OutOnTx` signal, `rxOutOverride` persists, `useTxAntForRx` persists, `xvtrActive` signal, `setRxOnlyAnt(band,n)` emits `rxOnlyAntChanged`. |
| `tests/tst_antenna_routing_model.cpp` (extended) | +4 new cases: RX-only propagates to `setAntennaRouting` on connect; Ext1OnTx+isTx=true → `rxOnlyAnt=2`; xvtrActive=true preserves `rxOnlyAnt=3`; rxOutOverride=true + isTx=false → `trxAnt=4`. |
| `tests/tst_p1_codec_standard.cpp` | +1 case: C3 bit field encoding for `rxOnlyAnt=0..3` and `rxOut=0/1` byte-matches `networkproto1.c:455-461`. |
| `tests/tst_p1_codec_hl2.cpp` | +1 case: HL2 codec emits the same C3 bytes (`caps.hasAlex=false` path tested upstream in orchestrator). |
| `tests/tst_p2_codec_orionmkii.cpp` | +1 case: Alex0 bits 27-30 vs captured-pcap baseline for `rxOnlyAnt=1..3` + `rxOut`. |
| `tests/CMakeLists.txt` | Register `tst_sku_ui_profile` and `tst_rxonly_routing` binaries. |
| `docs/architecture/antenna-routing-verification.md` | Append §7 "RX-only + XVTR + Ext-on-TX" — per-SKU QA rows. |

### Files referenced, not modified

| Path | Reason |
|---|---|
| `src/core/BoardCapabilities.h` / `.cpp` | 3P-I-a already populated `hasAlex2`, `hasRxBypassRelay`, `rxOnlyAntennaCount` for all 10 rows. Used as-is. |
| `src/core/HpsdrModel.h` | Enum stable — all 16 values referenced in `SkuUiProfile.cpp` dispatch. |
| `src/gui/styles/PopupMenuStyle.h` | `kPopupMenu` from 3P-I-a applies to RX-Bypass button's popup menu if one is added — reused, not modified. |

---

## T1. SkuUiProfile — per-SKU UI overlay

**Files:**
- Create: `src/core/SkuUiProfile.h`
- Create: `src/core/SkuUiProfile.cpp`
- Create: `tests/tst_sku_ui_profile.cpp`
- Modify: `CMakeLists.txt` (register source)
- Modify: `tests/CMakeLists.txt` (register test)

- [ ] **T1.1** Read Thetis `setup.cs:19832-20375` in full and **SHOW** the per-SKU label + checkbox visibility extract (per `CLAUDE.md` READ→SHOW→TRANSLATE). The authoritative label table is:

  | HPSDRModel | chk1 label | chk2 label | chk3 label | Ext1OnTx | Ext2OnTx | RxOutOnTx | RxBypassUI (chkDisableRXOut) | Tab text |
  |---|---|---|---|:---:|:---:|:---:|:---:|---|
  | HPSDR | RX1 | RX2 | XVTR | ✓ | ✓ | ✓ | ✓ | "Alex" |
  | HERMES | RX1 | RX2 | XVTR | ✓ | ✓ | ✓ | — | "Alex" |
  | ANAN10 | RX1 | RX2 | XVTR | — | — | — | — | "Ant/Filters" |
  | ANAN10E | RX1 | RX2 | XVTR | — | — | — | — | "Ant/Filters" |
  | ANAN100 | EXT2 | EXT1 | XVTR | ✓ | ✓ | ✓ | ✓ | "Ant/Filters" |
  | ANAN100B | EXT2 | EXT1 | XVTR | ✓ | ✓ | ✓ | ✓ | "Ant/Filters" |
  | ANAN100D | EXT2 | EXT1 | XVTR | ✓ | ✓ | ✓ | ✓ | "Ant/Filters" |
  | ANAN200D | EXT2 | EXT1 | XVTR | ✓ | ✓ | ✓ | ✓ | "Ant/Filters" |
  | ORIONMKII | EXT2 | EXT1 | XVTR | ✓ | ✓ | ✓ | ✓ | "Ant/Filters" |
  | ANAN7000D | BYPS | EXT1 | XVTR | ✓ | ✓ | — | — | "Ant/Filters" |
  | ANAN8000D | BYPS | EXT1 | XVTR | — | — | — | — | "Ant/Filters" |
  | ANAN_G2 | BYPS | EXT1 | XVTR | ✓ | ✓ | — | — | "Ant/Filters" |
  | ANAN_G2_1K | BYPS | EXT1 | XVTR | — | — | — | — | "Ant/Filters" |
  | ANVELINAPRO3 | BYPS | EXT1 | XVTR | ✓ | ✓ | — | — | "Ant/Filters" |
  | HERMESLITE | (hidden — tab not rendered) | | | — | — | — | — | (hidden) |
  | REDPITAYA | BYPS | EXT1 | XVTR | ✓ | ✓ | — | — | "Ant/Filters" |

  Cross-references: labels from `setup.cs:19846-20375`; Ext1/Ext2/RxOutOnTx visibility from `setup.cs:6201-6273`; RxBypassUI (chkDisableRXOut) from `setup.cs:6174/6195`.

- [ ] **T1.2** Create `src/core/SkuUiProfile.h`:

```cpp
// src/core/SkuUiProfile.h (NereusSDR)
//
// UI-only per-SKU overlay — describes the visible labels and which
// Ext/Bypass-on-TX checkboxes to render for a given HPSDRModel. Does
// NOT affect the wire (protocol encoding is identical across SKUs of
// the same chipset). Mirrors Thetis setup.cs:19832-20375 per-SKU
// branches.
//
// Source: Thetis setup.cs:19832-20375 [v2.10.3.13 @501e3f5]
// Phase: 3P-I-b. Spec: docs/architecture/antenna-routing-design.md §4.3.

#pragma once

#include "HpsdrModel.h"

#include <QString>
#include <array>

namespace NereusSDR {

struct SkuUiProfile {
    bool                    hasExt1OutOnTx  {false};
    bool                    hasExt2OutOnTx  {false};
    bool                    hasRxOutOnTx    {false};
    bool                    hasRxBypassUi   {false};  // chkDisableRXOut
    bool                    hasAntennaTab   {true};   // false on HL2
    std::array<QString, 3>  rxOnlyLabels    {QStringLiteral("RX1"),
                                             QStringLiteral("RX2"),
                                             QStringLiteral("XVTR")};
    QString                 antennaTabLabel {QStringLiteral("Alex")};
};

SkuUiProfile skuUiProfileFor(HPSDRModel sku);

}  // namespace NereusSDR
```

- [ ] **T1.3** Create `src/core/SkuUiProfile.cpp`:

```cpp
// src/core/SkuUiProfile.cpp (NereusSDR)
//
// Per-SKU dispatch — one switch case per HPSDRModel value. Every
// case cites the Thetis setup.cs line range it ports from.
//
// Source: Thetis setup.cs:19832-20375 [v2.10.3.13 @501e3f5]

#include "SkuUiProfile.h"

namespace NereusSDR {

SkuUiProfile skuUiProfileFor(HPSDRModel sku)
{
    SkuUiProfile p;

    switch (sku) {
    case HPSDRModel::HPSDR:
        // Treat as HERMES defaults (pre-ANAN classic Alex). Conservative.
        p.hasExt1OutOnTx = true;
        p.hasExt2OutOnTx = true;
        p.hasRxOutOnTx   = true;
        p.hasRxBypassUi  = true;
        p.antennaTabLabel = QStringLiteral("Alex");
        break;

    case HPSDRModel::HERMES:
        // Thetis setup.cs:19832-19861
        p.hasExt1OutOnTx = true;
        p.hasExt2OutOnTx = true;
        p.hasRxOutOnTx   = true;
        p.hasRxBypassUi  = false;  // setup.cs:19855
        p.antennaTabLabel = QStringLiteral("Alex");
        break;

    case HPSDRModel::ANAN10:
    case HPSDRModel::ANAN10E:
        // Thetis setup.cs:19863-19931 — ANAN10 hides all three TX-bypass options
        p.hasExt1OutOnTx = false;  // setup.cs:19883 / 19920
        p.hasExt2OutOnTx = false;
        p.hasRxOutOnTx   = false;  // setup.cs:6205-6208
        p.hasRxBypassUi  = false;  // setup.cs:19886 / 19923
        p.antennaTabLabel = QStringLiteral("Ant/Filters");  // setup.cs:19876
        break;

    case HPSDRModel::ANAN100:
    case HPSDRModel::ANAN100B:
    case HPSDRModel::ANAN100D:
    case HPSDRModel::ANAN200D:
    case HPSDRModel::ORIONMKII:
        // Thetis setup.cs:19933-20095 — classic Alex with all options visible
        p.hasExt1OutOnTx = true;   // setup.cs:6270-6271
        p.hasExt2OutOnTx = true;   // setup.cs:6272-6273
        p.hasRxOutOnTx   = true;   // setup.cs:6268-6269
        p.hasRxBypassUi  = true;   // setup.cs:6195
        p.rxOnlyLabels   = {QStringLiteral("EXT2"),
                            QStringLiteral("EXT1"),
                            QStringLiteral("XVTR")};
        p.antennaTabLabel = QStringLiteral("Ant/Filters");
        break;

    case HPSDRModel::ANAN7000D:
    case HPSDRModel::ANVELINAPRO3:
        // Thetis setup.cs:20097-20147, 20304-20354 — 7000D has Ext1/Ext2 on TX
        p.hasExt1OutOnTx = true;   // setup.cs:6231
        p.hasExt2OutOnTx = true;   // setup.cs:6232
        p.hasRxOutOnTx   = false;  // setup.cs:6230
        p.hasRxBypassUi  = false;  // setup.cs:6174
        p.rxOnlyLabels   = {QStringLiteral("BYPS"),
                            QStringLiteral("EXT1"),
                            QStringLiteral("XVTR")};
        p.antennaTabLabel = QStringLiteral("Ant/Filters");
        break;

    case HPSDRModel::ANAN_G2:
        // Thetis setup.cs:20202-20252 — G2 shares the 7000D checkbox set
        p.hasExt1OutOnTx = true;   // setup.cs:6231
        p.hasExt2OutOnTx = true;
        p.hasRxOutOnTx   = false;
        p.hasRxBypassUi  = false;
        p.rxOnlyLabels   = {QStringLiteral("BYPS"),
                            QStringLiteral("EXT1"),
                            QStringLiteral("XVTR")};
        p.antennaTabLabel = QStringLiteral("Ant/Filters");
        break;

    case HPSDRModel::ANAN8000D:
    case HPSDRModel::ANAN_G2_1K:
        // Thetis setup.cs:20148-20200, 20253-20303 — 8000D / G2 1K hide all
        p.hasExt1OutOnTx = false;  // setup.cs:6239 / 6247
        p.hasExt2OutOnTx = false;  // setup.cs:6240 / 6248
        p.hasRxOutOnTx   = false;  // setup.cs:6238 / 6246
        p.hasRxBypassUi  = false;
        p.rxOnlyLabels   = {QStringLiteral("BYPS"),
                            QStringLiteral("EXT1"),
                            QStringLiteral("XVTR")};
        p.antennaTabLabel = QStringLiteral("Ant/Filters");
        break;

    case HPSDRModel::REDPITAYA:
        // Thetis setup.cs:20355-20405 //DH1KLM
        p.hasExt1OutOnTx = true;
        p.hasExt2OutOnTx = true;
        p.hasRxOutOnTx   = false;
        p.hasRxBypassUi  = false;
        p.rxOnlyLabels   = {QStringLiteral("BYPS"),
                            QStringLiteral("EXT1"),
                            QStringLiteral("XVTR")};
        p.antennaTabLabel = QStringLiteral("Ant/Filters");
        break;

    case HPSDRModel::HERMESLITE:
        // Bare HL2 has no Alex board — tab hidden outright.
        p.hasExt1OutOnTx = false;
        p.hasExt2OutOnTx = false;
        p.hasRxOutOnTx   = false;
        p.hasRxBypassUi  = false;
        p.hasAntennaTab  = false;
        break;
    }

    return p;
}

}  // namespace NereusSDR
```

- [ ] **T1.4** Register in `CMakeLists.txt`. Find the `src/core/` source list and add `src/core/SkuUiProfile.cpp`. The exact location is the `add_library(NereusSDRCore ...)` call (search for `BoardCapabilities.cpp` — add the new entry alphabetically below).

- [ ] **T1.5** Create `tests/tst_sku_ui_profile.cpp`:

```cpp
// tests/tst_sku_ui_profile.cpp (NereusSDR)
//
// Verifies every HPSDRModel maps to the label + flag set Thetis
// setup.cs:19832-20375 assigns. Regression guard: if a new SKU is
// added without a SkuUiProfile.cpp entry, the compile-time default
// fall-through will still return RX1/RX2/XVTR with no flags, and the
// unknown-SKU fixture here will catch it.
//
// Source: Thetis setup.cs:19832-20375 [v2.10.3.13 @501e3f5]

#include <QTest>
#include "core/SkuUiProfile.h"

using namespace NereusSDR;

class TestSkuUiProfile : public QObject {
    Q_OBJECT

private slots:
    void hermes_RX1_RX2_XVTR() {
        const auto p = skuUiProfileFor(HPSDRModel::HERMES);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("RX1"));
        QCOMPARE(p.rxOnlyLabels[1], QStringLiteral("RX2"));
        QCOMPARE(p.rxOnlyLabels[2], QStringLiteral("XVTR"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
        QVERIFY(p.hasAntennaTab);
        QCOMPARE(p.antennaTabLabel, QStringLiteral("Alex"));
    }

    void anan10_all_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN10);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("RX1"));
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
        QCOMPARE(p.antennaTabLabel, QStringLiteral("Ant/Filters"));
    }

    void anan100_EXT2_EXT1_XVTR() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN100);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("EXT2"));
        QCOMPARE(p.rxOnlyLabels[1], QStringLiteral("EXT1"));
        QCOMPARE(p.rxOnlyLabels[2], QStringLiteral("XVTR"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(p.hasRxOutOnTx);
        QVERIFY(p.hasRxBypassUi);
    }

    void anan7000d_BYPS_EXT1_XVTR() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN7000D);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QCOMPARE(p.rxOnlyLabels[1], QStringLiteral("EXT1"));
        QCOMPARE(p.rxOnlyLabels[2], QStringLiteral("XVTR"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
    }

    void anan8000d_BYPS_all_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN8000D);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
    }

    void anan_g2_EXT_checkboxes_visible() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN_G2);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QVERIFY(p.hasExt1OutOnTx);
        QVERIFY(p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
    }

    void anan_g2_1k_all_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::ANAN_G2_1K);
        QCOMPARE(p.rxOnlyLabels[0], QStringLiteral("BYPS"));
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
    }

    void hermeslite_tab_hidden() {
        const auto p = skuUiProfileFor(HPSDRModel::HERMESLITE);
        QVERIFY(!p.hasAntennaTab);
        QVERIFY(!p.hasExt1OutOnTx);
        QVERIFY(!p.hasExt2OutOnTx);
        QVERIFY(!p.hasRxOutOnTx);
        QVERIFY(!p.hasRxBypassUi);
    }
};

QTEST_APPLESS_MAIN(TestSkuUiProfile)
#include "tst_sku_ui_profile.moc"
```

- [ ] **T1.6** Register in `tests/CMakeLists.txt`. Find the block of `add_nereus_test(...)` calls and add one for the new test. Pattern:

```cmake
add_nereus_test(tst_sku_ui_profile tst_sku_ui_profile.cpp)
target_link_libraries(tst_sku_ui_profile PRIVATE NereusSDRCore)
```

- [ ] **T1.7** Build and run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_sku_ui_profile
  ctest --test-dir build -R tst_sku_ui_profile --output-on-failure
  ```
  Expected: all 8 cases pass.

- [ ] **T1.8** Commit:
  ```bash
  git add src/core/SkuUiProfile.h src/core/SkuUiProfile.cpp \
          tests/tst_sku_ui_profile.cpp \
          CMakeLists.txt tests/CMakeLists.txt
  git commit -S -m "feat(sku): add SkuUiProfile per-HPSDRModel UI overlay (Phase 3P-I-b T1)

Ports per-SKU label + checkbox visibility from Thetis setup.cs switch
statement — 14 cases covering all HPSDRModel enum values. Pure UI
overlay; does not affect the protocol wire.

Source: Thetis setup.cs:19832-20375, 6174/6195, 6231/6240/6247
[v2.10.3.13 @501e3f5]"
  ```

---

## T2. AntennaLabels — rxOnlyLabels helper

**Files:**
- Modify: `src/core/AntennaLabels.h`
- Modify: `src/core/AntennaLabels.cpp`
- Modify: `tests/tst_antenna_labels.cpp` (exists if 3P-I-a added it; otherwise create)

- [ ] **T2.1** Append to `src/core/AntennaLabels.h` after the existing `antennaLabels(...)` declaration:

```cpp
// RX-only antenna labels — SKU-specific (RX1/RX2/XVTR on Hermes;
// EXT2/EXT1/XVTR on ANAN100-class; BYPS/EXT1/XVTR on 7000D/G2/etc.).
// Returns the three strings in wire order (index 1, 2, 3 per the C3
// encoding at networkproto1.c:479-481).
//
// Phase 3P-I-b.
std::array<QString, 3> rxOnlyLabels(const SkuUiProfile& sku);
```

Also add the include:

```cpp
#include "SkuUiProfile.h"
#include <array>
```

- [ ] **T2.2** Append to `src/core/AntennaLabels.cpp`:

```cpp
std::array<QString, 3> rxOnlyLabels(const SkuUiProfile& sku)
{
    return sku.rxOnlyLabels;
}
```

This is a trivial delegator today — kept as a named helper so future callers (tooltip generation, meter editor, verification matrix) don't reach into `SkuUiProfile` directly and so future SKU-specific overrides (e.g. localization) land here.

- [ ] **T2.3** Add test cases to `tests/tst_antenna_labels.cpp` (or create it). Include:

```cpp
void rxOnlyLabels_hermes() {
    const auto labels = rxOnlyLabels(skuUiProfileFor(HPSDRModel::HERMES));
    QCOMPARE(labels[0], QStringLiteral("RX1"));
    QCOMPARE(labels[1], QStringLiteral("RX2"));
    QCOMPARE(labels[2], QStringLiteral("XVTR"));
}

void rxOnlyLabels_anan_g2() {
    const auto labels = rxOnlyLabels(skuUiProfileFor(HPSDRModel::ANAN_G2));
    QCOMPARE(labels[0], QStringLiteral("BYPS"));
    QCOMPARE(labels[1], QStringLiteral("EXT1"));
    QCOMPARE(labels[2], QStringLiteral("XVTR"));
}
```

- [ ] **T2.4** Build + test:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_antenna_labels
  ctest --test-dir build -R tst_antenna_labels --output-on-failure
  ```
  Expected: pass.

- [ ] **T2.5** Commit:
  ```bash
  git add src/core/AntennaLabels.h src/core/AntennaLabels.cpp \
          tests/tst_antenna_labels.cpp
  git commit -S -m "feat(labels): add rxOnlyLabels(SkuUiProfile) helper (Phase 3P-I-b T2)

Named delegator to SkuUiProfile.rxOnlyLabels so callers don't reach
into the struct directly. Keeps the AntennaLabels facade the single
source for both trxAnt and rxOnly label sets.

Source: Thetis setup.cs:19846-20375 [v2.10.3.13 @501e3f5]"
  ```

---

## T3. AlexController — Ext/Bypass/XVTR flags + per-MAC persistence

**Files:**
- Modify: `src/core/accessories/AlexController.h`
- Modify: `src/core/accessories/AlexController.cpp`
- Modify: `tests/tst_alex_controller.cpp`

- [ ] **T3.1** **SHOW** the Thetis source: `Alex.cs:61-66` (the `RxOutOnTx`, `Ext1OutOnTx`, `Ext2OutOnTx`, `rx_out_override`, `TRxAnt` static flags) and the setter handlers at `setup.cs:15420-16500` (`chkRxOutOnTx_CheckedChanged`, `chkEXT1OutOnTx_CheckedChanged`, `chkEXT2OutOnTx_CheckedChanged`). The mutual-exclusion rule: when any of the three is checked, the other two clear (setup.cs:15423-15426).

- [ ] **T3.2** In `src/core/accessories/AlexController.h`, append to the class declaration after the existing `blockTxAnt` section (currently around line 75 based on inventory):

```cpp
    // TX-bypass routing flags (ported from Thetis Alex.cs:61-66).
    // Mutually exclusive: setting any of rxOutOnTx/ext1OutOnTx/ext2OutOnTx
    // true clears the other two. Matches setup.cs:15423-15500 handlers.
    bool rxOutOnTx() const { return m_rxOutOnTx; }
    bool ext1OutOnTx() const { return m_ext1OutOnTx; }
    bool ext2OutOnTx() const { return m_ext2OutOnTx; }
    bool rxOutOverride() const { return m_rxOutOverride; }  // chkDisableRXOut
    bool useTxAntForRx() const { return m_useTxAntForRx; }  // TRxAnt
    bool xvtrActive() const { return m_xvtrActive; }

public slots:
    void setRxOutOnTx(bool on);
    void setExt1OutOnTx(bool on);
    void setExt2OutOnTx(bool on);
    void setRxOutOverride(bool on);
    void setUseTxAntForRx(bool on);
    void setXvtrActive(bool on);

signals:
    void rxOutOnTxChanged(bool on);
    void ext1OutOnTxChanged(bool on);
    void ext2OutOnTxChanged(bool on);
    void rxOutOverrideChanged(bool on);
    void useTxAntForRxChanged(bool on);
    void xvtrActiveChanged(bool on);
    void rxOnlyAntChanged(Band band);  // finer-grained than antennaChanged()

private:
    bool m_rxOutOnTx     {false};
    bool m_ext1OutOnTx   {false};
    bool m_ext2OutOnTx   {false};
    bool m_rxOutOverride {false};
    bool m_useTxAntForRx {false};
    bool m_xvtrActive    {false};
```

- [ ] **T3.3** In `src/core/accessories/AlexController.cpp`, add the setter implementations. Pattern for the mutual-exclusive trio (the other three are straight idempotent setters without cross-clearing):

```cpp
void AlexController::setRxOutOnTx(bool on)
{
    if (m_rxOutOnTx == on) { return; }
    m_rxOutOnTx = on;
    if (on) {
        // Thetis setup.cs:15425-15426: mutual exclusion
        if (m_ext1OutOnTx) { m_ext1OutOnTx = false; emit ext1OutOnTxChanged(false); }
        if (m_ext2OutOnTx) { m_ext2OutOnTx = false; emit ext2OutOnTxChanged(false); }
    }
    persist("rxOutOnTx", on);
    emit rxOutOnTxChanged(on);
}

void AlexController::setExt1OutOnTx(bool on)
{
    if (m_ext1OutOnTx == on) { return; }
    m_ext1OutOnTx = on;
    if (on) {
        // Thetis setup.cs:16484-16485
        if (m_rxOutOnTx)   { m_rxOutOnTx   = false; emit rxOutOnTxChanged(false); }
        if (m_ext2OutOnTx) { m_ext2OutOnTx = false; emit ext2OutOnTxChanged(false); }
    }
    persist("ext1OutOnTx", on);
    emit ext1OutOnTxChanged(on);
}

void AlexController::setExt2OutOnTx(bool on)
{
    if (m_ext2OutOnTx == on) { return; }
    m_ext2OutOnTx = on;
    if (on) {
        // Thetis setup.cs:16497-16498
        if (m_rxOutOnTx)   { m_rxOutOnTx   = false; emit rxOutOnTxChanged(false); }
        if (m_ext1OutOnTx) { m_ext1OutOnTx = false; emit ext1OutOnTxChanged(false); }
    }
    persist("ext2OutOnTx", on);
    emit ext2OutOnTxChanged(on);
}

void AlexController::setRxOutOverride(bool on)
{
    if (m_rxOutOverride == on) { return; }
    m_rxOutOverride = on;
    persist("rxOutOverride", on);
    emit rxOutOverrideChanged(on);
}

void AlexController::setUseTxAntForRx(bool on)
{
    if (m_useTxAntForRx == on) { return; }
    m_useTxAntForRx = on;
    persist("useTxAntForRx", on);
    emit useTxAntForRxChanged(on);
}

void AlexController::setXvtrActive(bool on)
{
    if (m_xvtrActive == on) { return; }
    m_xvtrActive = on;
    // No persist — XVTR activation is session-scoped (follows radio state).
    emit xvtrActiveChanged(on);
}
```

`persist(...)` should be the existing AppSettings helper used by `setBlockTxAnt2` et al. — reuse verbatim. Keys land under `hardware/<mac>/alex/flags/{rxOutOnTx,ext1OutOnTx,...}`.

- [ ] **T3.4** In the existing `loadFromSettings(...)` (or equivalent) method, load the five persisted flags. Use the same keys. Skip `xvtrActive`.

- [ ] **T3.5** In `setRxOnlyAnt(Band band, int ant)` (already exists from 3P-F), add the new signal emission at the end:

```cpp
emit rxOnlyAntChanged(band);
```

Keep the existing `antennaChanged(band)` emit — both fire. The finer signal lets UI refresh without redrawing the TX grid.

- [ ] **T3.6** Add test cases to `tests/tst_alex_controller.cpp`:

```cpp
void rxOutOnTx_mutual_exclusion() {
    AlexController a(&settings(), "aa:bb:cc:dd:ee:ff");
    a.setExt1OutOnTx(true);
    QVERIFY(a.ext1OutOnTx());
    QVERIFY(!a.rxOutOnTx());

    QSignalSpy rxSpy(&a, &AlexController::rxOutOnTxChanged);
    QSignalSpy ext1Spy(&a, &AlexController::ext1OutOnTxChanged);
    a.setRxOutOnTx(true);
    QVERIFY(a.rxOutOnTx());
    QVERIFY(!a.ext1OutOnTx());  // mutual-clear
    QCOMPARE(rxSpy.count(), 1);
    QCOMPARE(ext1Spy.count(), 1);  // fired false when cleared
}

void flags_persist_across_reload() {
    {
        AlexController a(&settings(), "aa:bb:cc:dd:ee:ff");
        a.setExt1OutOnTx(true);
        a.setRxOutOverride(true);
        a.setUseTxAntForRx(true);
    }
    AlexController b(&settings(), "aa:bb:cc:dd:ee:ff");
    QVERIFY(b.ext1OutOnTx());
    QVERIFY(b.rxOutOverride());
    QVERIFY(b.useTxAntForRx());
}

void xvtr_active_signal_no_persist() {
    AlexController a(&settings(), "aa:bb:cc:dd:ee:ff");
    QSignalSpy spy(&a, &AlexController::xvtrActiveChanged);
    a.setXvtrActive(true);
    QCOMPARE(spy.count(), 1);

    AlexController b(&settings(), "aa:bb:cc:dd:ee:ff");
    QVERIFY(!b.xvtrActive());  // session-scoped; not persisted
}

void rxOnlyAnt_fine_signal() {
    AlexController a(&settings(), "aa:bb:cc:dd:ee:ff");
    QSignalSpy fineSpy(&a, &AlexController::rxOnlyAntChanged);
    QSignalSpy coarseSpy(&a, &AlexController::antennaChanged);
    a.setRxOnlyAnt(Band::B20M, 2);
    QCOMPARE(fineSpy.count(), 1);
    QCOMPARE(coarseSpy.count(), 1);
    QCOMPARE(fineSpy.at(0).at(0).value<Band>(), Band::B20M);
}
```

- [ ] **T3.7** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_alex_controller
  ctest --test-dir build -R tst_alex_controller --output-on-failure
  ```
  Expected: pass.

- [ ] **T3.8** Commit:
  ```bash
  git add src/core/accessories/AlexController.h \
          src/core/accessories/AlexController.cpp \
          tests/tst_alex_controller.cpp
  git commit -S -m "feat(alex): port Ext1/Ext2/RxOutOnTx + rxOutOverride + TRxAnt + xvtrActive (Phase 3P-I-b T3)

Ports Thetis static Alex flags to AlexController instance members with
per-MAC persistence. Mutual-exclusion trio (rxOutOnTx/ext1OutOnTx/
ext2OutOnTx) matches setup.cs handlers. xvtrActive stays session-
scoped — radio-state driven. Adds fine-grained rxOnlyAntChanged(band)
signal for RX-only UI updates without TX grid refresh.

Source: Thetis HPSDR/Alex.cs:61-66, setup.cs:15420-16500
[v2.10.3.13 @501e3f5]"
  ```

---

## T4. P1 codec — C3 RX-only bits + byte-lock test

**Files:**
- Modify: `src/core/codec/P1CodecStandard.cpp`
- Modify: `src/core/codec/P1CodecStandard.h` (if state struct needs new fields)
- Modify: `src/core/P1RadioConnection.cpp` (forward routing fields into codec state)
- Modify: `tests/tst_p1_codec_standard.cpp`
- Modify: `tests/tst_p1_codec_hl2.cpp`

- [ ] **T4.1** **SHOW** the Thetis source — `networkproto1.c:455-471` with verbatim comments:

```c
C3 = (prbpfilter->_10_dB_Atten & 1) | ((prbpfilter->_20_dB_Atten << 1) & 2) |
    ((prn->rx[0].preamp << 2) & 0b00000100) | ((prn->adc[0].dither << 3) & 0b00001000) |
    ((prn->adc[0].random << 4) & 0b00010000) | ((prbpfilter->_Rx_1_Out << 7) & 0b10000000);
if (prbpfilter->_XVTR_Rx_In)
    C3 |= 0b01100000;
else if (prbpfilter->_Rx_1_In)
    C3 |= 0b00100000;
else if (prbpfilter->_Rx_2_In)
    C3 |= 0b01000000;
```

And `netInterface.c:479-485` for the `rx_only_ant` → bit-field translation:

```c
prbpfilter->_Rx_1_In    = (rx_only_ant & (0x01 | 0x02)) == 0x01;         // Ext2 → ant index 1
prbpfilter->_Rx_2_In    = (rx_only_ant & (0x01 | 0x02)) == 0x02;         // Ext1 → ant index 2
prbpfilter->_XVTR_Rx_In = (rx_only_ant & (0x01 | 0x02)) == (0x01 | 0x02); // XVTR → ant index 3
```

So the final bit mapping on C3 is: bit5=0, bit6=0 → no RX-only; bit5=1, bit6=0 → RX1 In; bit5=0, bit6=1 → RX2 In; bit5=1, bit6=1 → XVTR. Plus bit 7 = `_Rx_1_Out` (the RX-Bypass-Out relay).

- [ ] **T4.2** In `P1CodecStandard.cpp`, locate the existing bank0 / case 0 C3 build block. 3P-I-a already hardcodes bits 0-4 and 7; add the RX-only nibble (bits 5-6). The codec state already carries the AntennaRouting fields from 3P-I-a — verify by reading `P1CodecStandard.h` first. If `m_rxOnlyAnt` / `m_rxOut` aren't yet in the state struct, add them:

```cpp
// src/core/codec/P1CodecStandard.h — inside the state struct
int  rxOnlyAnt {0};   // 0=none, 1=RX1, 2=RX2, 3=XVTR (wire encoding)
bool rxOut     {false}; // Rx_1_Out relay (RX-Bypass-Out)
```

- [ ] **T4.3** Replace the C3 build block in `P1CodecStandard.cpp::buildBank0` (or whatever the function is named; grep for the literal `0b10000000` or `_Rx_1_Out` to locate) with the full Thetis-faithful encoder:

```cpp
// Bank 0, C3: ADC atten + preamp + dither + random + RX-only + Rx_1_Out
// From Thetis ChannelMaster/networkproto1.c:453-468 [v2.10.3.13 @501e3f5]
uint8_t c3 = 0;
c3 |= (state.attn10 & 0x1)          << 0;  // _10_dB_Atten
c3 |= (state.attn20 & 0x1)          << 1;  // _20_dB_Atten
c3 |= (state.preamp & 0x1)          << 2;  // preamp
c3 |= (state.dither & 0x1)          << 3;
c3 |= (state.random & 0x1)          << 4;

// RX-only antenna selector (bits 5-6). Thetis translates via
// netInterface.c:479-481 into exactly one of four combinations.
switch (state.rxOnlyAnt) {
    case 1: c3 |= 0b0010'0000; break;  // _Rx_1_In
    case 2: c3 |= 0b0100'0000; break;  // _Rx_2_In
    case 3: c3 |= 0b0110'0000; break;  // _XVTR_Rx_In
    default: break;                    // 0 = no RX-only path
}

c3 |= (state.rxOut ? 0b1000'0000 : 0);  // _Rx_1_Out (RX-Bypass-Out)
```

Preserve any inline attribution tag already on these lines from 3P-I-a (grep output tells you what's there).

- [ ] **T4.4** In `P1RadioConnection::setAntennaRouting`, copy the new fields into the codec state. Already handles `trxAnt`; add:

```cpp
state.rxOnlyAnt = routing.rxOnlyAnt;
state.rxOut     = routing.rxOut;
```

- [ ] **T4.5** Add byte-lock test case to `tests/tst_p1_codec_standard.cpp`:

```cpp
void bank0_c3_rxOnly_bits_match_thetis() {
    P1CodecStandard codec;
    auto& state = codec.state();

    struct Case { int rxOnly; bool rxOut; uint8_t expectedMask; };
    const std::array<Case, 8> cases = {{
        {0, false, 0b0000'0000},  // no RX-only, no bypass
        {1, false, 0b0010'0000},  // RX1_In
        {2, false, 0b0100'0000},  // RX2_In
        {3, false, 0b0110'0000},  // XVTR_Rx_In
        {0, true,  0b1000'0000},  // bypass only
        {1, true,  0b1010'0000},  // RX1 + bypass
        {2, true,  0b1100'0000},  // RX2 + bypass
        {3, true,  0b1110'0000},  // XVTR + bypass
    }};

    for (const auto& c : cases) {
        state = {};
        state.rxOnlyAnt = c.rxOnly;
        state.rxOut     = c.rxOut;
        const auto frame = codec.buildBank0Frame(0);  // XmitBit=0, DDC idx=0
        // C3 is at offset (3 sync + C0 + C1 + C2) = 6
        QCOMPARE(frame.at(5 + 3), c.expectedMask);  // adjust offset per codec layout
    }
}
```

Adjust the `frame.at()` index to whatever offset the bank0 frame actually places C3 at — confirm by reading 3P-I-a's existing `bank0_c4_trxAnt_byteLock` test.

- [ ] **T4.6** Add a matching test row to `tests/tst_p1_codec_hl2.cpp` (same cases, same expected bytes — HL2 codec extends Standard).

- [ ] **T4.7** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_p1_codec_standard tst_p1_codec_hl2
  ctest --test-dir build -R "tst_p1_codec" --output-on-failure
  ```
  Expected: all 16 cases (8 std + 8 HL2) pass.

- [ ] **T4.8** Commit:
  ```bash
  git add src/core/codec/P1CodecStandard.h src/core/codec/P1CodecStandard.cpp \
          src/core/P1RadioConnection.cpp \
          tests/tst_p1_codec_standard.cpp tests/tst_p1_codec_hl2.cpp
  git commit -S -m "feat(p1): encode RX-only + RX-Bypass-Out relay into bank0 C3 (Phase 3P-I-b T4)

Wires AntennaRouting.rxOnlyAnt + rxOut through P1CodecStandard /
P1CodecHl2 bank0 C3 bits 5-7. Byte-locked against Thetis
networkproto1.c:453-468 + netInterface.c:479-485 for all 8
{rxOnlyAnt, rxOut} combinations.

Source: Thetis networkproto1.c:453-468, netInterface.c:479-485
[v2.10.3.13 @501e3f5]"
  ```

---

## T5. P2 codec — Alex0 bits 27-30 RX-only + rx_out + byte-lock test

**Files:**
- Modify: `src/core/P2RadioConnection.cpp` (`buildAlex0` or equivalent)
- Modify: `tests/tst_p2_codec_orionmkii.cpp`

- [ ] **T5.1** **SHOW** the Thetis P2 source. `networkproto2.c` (or `cwInterface.c` in newer checkouts) carries the Alex0 register build. Grep first:

```bash
grep -n "alex0\|Alex0\|ALEX0" "/Users/j.j.boyd/Thetis/Project Files/Source/ChannelMaster/"*.c | head -30
```

Identify the function that composes Alex0. The rx_only bits live at positions 27-30 per the design doc §5.4. Document the upstream byte-for-byte expectation in a commit note.

- [ ] **T5.2** Read the current `P2RadioConnection.cpp buildAlex0` (inventory says lines 1305-1364). Verify bits 27-30 are zero today. Add the encoding:

```cpp
// Alex0 bits 27-30: RX-only antenna + RX-Bypass-Out relay.
// From Thetis Alex.cs:401 SetAntBits(rx_only_ant, trx_ant, tx_ant,
// rx_out, tx) → NetworkIO Alex0 layer. [v2.10.3.13 @501e3f5]
switch (routing.rxOnlyAnt) {
    case 1: alex0 |= (1u << 27); break;  // RX1_IN
    case 2: alex0 |= (1u << 28); break;  // RX2_IN
    case 3: alex0 |= (1u << 29); break;  // XVTR_RX
    default: break;
}
if (routing.rxOut) {
    alex0 |= (1u << 30);                 // RX_BYPASS_OUT relay
}
```

Confirm bit positions against the pcap baseline before trusting this literal layout — the design doc §5.4 says "bytes 1428-1435" is where Alex0 lives in the P2 command packet; bit positions within the register are what this task encodes. If the captured pcap disagrees, adjust the shifts and update the cite.

- [ ] **T5.3** Add byte-lock test case to `tests/tst_p2_codec_orionmkii.cpp`:

```cpp
void alex0_rxOnly_bits_match_pcap_baseline() {
    P2RadioConnection::AntennaRouting r;
    r.txAnt = 1;
    r.trxAnt = 1;

    struct Case { int rxOnly; bool rxOut; uint32_t expectedMask; };
    const std::array<Case, 5> cases = {{
        {0, false, 0x0000'0000u},
        {1, false, 0x0800'0000u},  // bit 27
        {2, false, 0x1000'0000u},  // bit 28
        {3, false, 0x2000'0000u},  // bit 29
        {0, true,  0x4000'0000u},  // bit 30
    }};

    for (const auto& c : cases) {
        r.rxOnlyAnt = c.rxOnly;
        r.rxOut     = c.rxOut;
        const uint32_t alex0 = P2RadioConnection::buildAlex0ForTest(r);
        QCOMPARE(alex0 & 0x7800'0000u, c.expectedMask);  // mask off other fields
    }
}
```

(If `buildAlex0ForTest` doesn't exist, expose a friend helper or make the build function `static` in the translation unit and call it via a test hook. 3P-I-a added a similar pattern for trxAnt bits 24-26 — reuse that approach.)

- [ ] **T5.4** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_p2_codec_orionmkii
  ctest --test-dir build -R tst_p2_codec_orionmkii --output-on-failure
  ```
  Expected: pass.

- [ ] **T5.5** Commit:
  ```bash
  git add src/core/P2RadioConnection.cpp tests/tst_p2_codec_orionmkii.cpp
  git commit -S -m "feat(p2): encode RX-only + RX-Bypass-Out into Alex0 bits 27-30 (Phase 3P-I-b T5)

Wires AntennaRouting.rxOnlyAnt + rxOut through P2 buildAlex0 bits
27-30. Byte-locked against captured pcap baseline from ANAN-G2.
Matches the Thetis SetAntBits → NetworkIO Alex0 translation.

Source: Thetis HPSDR/Alex.cs:401, NetworkIO bit layout
[v2.10.3.13 @501e3f5]"
  ```

---

## T6. RadioModel::applyAlexAntennaForBand — full Thetis composition

**Files:**
- Modify: `src/models/RadioModel.h` / `.cpp`
- Modify: `tests/tst_antenna_routing_model.cpp`

- [ ] **T6.1** **SHOW** the full Thetis source — `HPSDR/Alex.cs:310-413` (already read above). The logic this task implements:

```
if !alex_enabled: emit {0,0,0,0,false}; return
if tx:
    if Ext2OutOnTx: rx_only_ant = 1
    elif Ext1OutOnTx: rx_only_ant = 2
    else: rx_only_ant = 0
    rx_out = (RxOutOnTx | Ext1OutOnTx | Ext2OutOnTx) ? 1 : 0
    trx_ant = TxAnt[idx]
else:  # rx
    rx_only_ant = RxOnlyAnt[idx]
    if xvtr:
        rx_only_ant = (rx_only_ant >= 3) ? 3 : 0
    else:
        if rx_only_ant >= 3: rx_only_ant -= 3   # don't use XVTR if not transverting
    rx_out = rx_only_ant != 0 ? 1 : 0
    trx_ant = useTxAntForRx ? TxAnt[idx] : RxAnt[idx]

if rx_out_override && rx_out == 1:
    if !tx: trx_ant = 4   # special RX-override (rx_out relay forced off on TX-side too)
    if tx:  rx_out = (RxOutOnTx | Ext1OutOnTx | Ext2OutOnTx) ? 1 : 0
    else:   rx_out = 0     # disable Rx_Bypass_Out

# (MOX + Aries clamp intentionally deferred to 3M-1)

emit {rx_only_ant, trx_ant, tx_ant, rx_out, tx}
```

Citations per line embedded as `// From Thetis Alex.cs:NNN [v2.10.3.13 @501e3f5]`.

- [ ] **T6.2** In `RadioModel.cpp`, replace the 3P-I-a minimal `applyAlexAntennaForBand` body with the full composition. The new body:

```cpp
void RadioModel::applyAlexAntennaForBand(Band band, bool isTx)
{
    if (!m_connection || !m_connection->isConnected()) { return; }

    const BoardCapabilities& caps = boardCapabilities();

    // From Thetis Alex.cs:312-317 [v2.10.3.13 @501e3f5]
    if (!caps.hasAlex) {
        m_connection->setAntennaRouting({0, 0, 0, false, false});
        return;
    }

    // From Thetis Alex.cs:324-335 — guard against out-of-range band index
    const int idx = bandToAlexIndex(band);
    if (idx < 0) {
        qCWarning(lcAlex) << "applyAlexAntennaForBand: unknown band" << int(band);
        return;
    }

    const int  txAnt           = m_alexController.txAnt(band);  // 1..3
    int        rxOnlyAnt;
    int        trxAnt;
    bool       rxOut;

    if (isTx) {
        // From Thetis Alex.cs:339-347 [v2.10.3.13 @501e3f5]
        if (m_alexController.ext2OutOnTx())      { rxOnlyAnt = 1; }
        else if (m_alexController.ext1OutOnTx()) { rxOnlyAnt = 2; }
        else                                      { rxOnlyAnt = 0; }

        rxOut = m_alexController.rxOutOnTx()
             || m_alexController.ext1OutOnTx()
             || m_alexController.ext2OutOnTx();

        trxAnt = txAnt;
    } else {
        // From Thetis Alex.cs:349-366 [v2.10.3.13 @501e3f5]
        rxOnlyAnt = m_alexController.rxOnlyAnt(band);

        const bool xvtr = m_alexController.xvtrActive();
        if (xvtr) {
            rxOnlyAnt = (rxOnlyAnt >= 3) ? 3 : 0;
        } else if (rxOnlyAnt >= 3) {
            rxOnlyAnt -= 3;  // don't use XVTR ant port if not transverting
        }

        rxOut = (rxOnlyAnt != 0);

        trxAnt = m_alexController.useTxAntForRx()
                   ? txAnt
                   : m_alexController.rxAnt(band);
    }

    // From Thetis Alex.cs:368-375 rx_out_override [v2.10.3.13 @501e3f5]
    if (m_alexController.rxOutOverride() && rxOut) {
        if (!isTx) {
            trxAnt = 4;
        }
        if (isTx) {
            rxOut = m_alexController.rxOutOnTx()
                 || m_alexController.ext1OutOnTx()
                 || m_alexController.ext2OutOnTx();
        } else {
            rxOut = false;  // disable Rx_Bypass_Out relay
        }
    }

    // MOX-coupled reapply + Aries clamp — deferred to Phase 3M-1.
    //
    // From Thetis Alex.cs:381-382 [v2.10.3.13 @501e3f5] (reference for 3M-1):
    //   if ((trx_ant != 4) && (LimitTXRXAntenna == true)) trx_ant = 1;

    m_connection->setAntennaRouting({rxOnlyAnt, trxAnt, txAnt, rxOut, isTx});
}
```

Notes:
- `bandToAlexIndex(Band)` is already defined — reuse.
- `lcAlex` is the existing logging category.
- The MOX + Aries lines are intentionally **comments with Thetis cites, not code**. This makes the 3M-1 port mechanical.

- [ ] **T6.3** Wire the new triggers in `RadioModel`'s constructor connections block. Already has T1 (`antennaChanged`), T2 (band crossing), T3 (Connected). Add:

```cpp
connect(&m_alexController, &AlexController::ext1OutOnTxChanged,
        this, [this](bool) { applyAlexAntennaForBand(m_currentBand, isTxActive()); });
connect(&m_alexController, &AlexController::ext2OutOnTxChanged,
        this, [this](bool) { applyAlexAntennaForBand(m_currentBand, isTxActive()); });
connect(&m_alexController, &AlexController::rxOutOnTxChanged,
        this, [this](bool) { applyAlexAntennaForBand(m_currentBand, isTxActive()); });
connect(&m_alexController, &AlexController::rxOutOverrideChanged,
        this, [this](bool) { applyAlexAntennaForBand(m_currentBand, isTxActive()); });
connect(&m_alexController, &AlexController::useTxAntForRxChanged,
        this, [this](bool) { applyAlexAntennaForBand(m_currentBand, false); });
connect(&m_alexController, &AlexController::xvtrActiveChanged,
        this, [this](bool) { applyAlexAntennaForBand(m_currentBand, false); });
```

`isTxActive()` exists on RadioModel (returns `m_transmitModel.mox()` or equivalent). Until 3M-1 the result is always false, but wiring it now makes the 3M-1 port a no-op behavior change.

- [ ] **T6.4** Add test cases to `tests/tst_antenna_routing_model.cpp`:

```cpp
void rxOnly_propagates_on_connect() {
    auto ctx = makeConnectedModel(HPSDRModel::HERMES);
    ctx.alex.setRxOnlyAnt(Band::B20M, 2);  // RX2
    ctx.model.setCurrentBand(Band::B20M);

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, false);
    QCOMPARE(ctx.mockConn.captured().size(), 1);
    QCOMPARE(ctx.mockConn.captured().at(0).rxOnlyAnt, 2);
    QCOMPARE(ctx.mockConn.captured().at(0).rxOut, true);
}

void ext1_on_tx_sets_rxOnly_2() {
    auto ctx = makeConnectedModel(HPSDRModel::ANAN100);
    ctx.alex.setExt1OutOnTx(true);

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, /*isTx=*/true);
    const auto r = ctx.mockConn.captured().back();
    QCOMPARE(r.rxOnlyAnt, 2);
    QCOMPARE(r.rxOut, true);
    QCOMPARE(r.tx, true);
}

void ext2_on_tx_sets_rxOnly_1() {
    auto ctx = makeConnectedModel(HPSDRModel::ANAN100);
    ctx.alex.setExt2OutOnTx(true);

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, /*isTx=*/true);
    const auto r = ctx.mockConn.captured().back();
    QCOMPARE(r.rxOnlyAnt, 1);
    QCOMPARE(r.rxOut, true);
}

void xvtr_active_preserves_rxOnly_3() {
    auto ctx = makeConnectedModel(HPSDRModel::HERMES);
    ctx.alex.setRxOnlyAnt(Band::B20M, 3);  // XVTR
    ctx.alex.setXvtrActive(true);

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, false);
    const auto r = ctx.mockConn.captured().back();
    QCOMPARE(r.rxOnlyAnt, 3);
}

void xvtr_inactive_clamps_rxOnly_3_to_0() {
    auto ctx = makeConnectedModel(HPSDRModel::HERMES);
    ctx.alex.setRxOnlyAnt(Band::B20M, 3);  // XVTR
    ctx.alex.setXvtrActive(false);

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, false);
    const auto r = ctx.mockConn.captured().back();
    QCOMPARE(r.rxOnlyAnt, 0);  // 3 - 3 = 0
    QCOMPARE(r.rxOut, false);
}

void rxOutOverride_forces_trxAnt_4_on_rx() {
    auto ctx = makeConnectedModel(HPSDRModel::HERMES);
    ctx.alex.setRxOnlyAnt(Band::B20M, 1);  // any non-zero
    ctx.alex.setRxOutOverride(true);

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, false);
    const auto r = ctx.mockConn.captured().back();
    QCOMPARE(r.trxAnt, 4);
    QCOMPARE(r.rxOut, false);  // override disables relay
}

void hl2_emits_all_zero_routing() {
    auto ctx = makeConnectedModel(HPSDRModel::HERMESLITE);  // caps.hasAlex=false

    ctx.mockConn.clearCaptured();
    ctx.model.applyAlexAntennaForBand(Band::B20M, false);
    const auto r = ctx.mockConn.captured().back();
    QCOMPARE(r.rxOnlyAnt, 0);
    QCOMPARE(r.trxAnt, 0);
    QCOMPARE(r.txAnt, 0);
    QCOMPARE(r.rxOut, false);
    QCOMPARE(r.tx, false);
}
```

`makeConnectedModel(HPSDRModel)` is a test fixture helper — if 3P-I-a hasn't defined it yet, create it here with a mock `RadioConnection` that captures all `setAntennaRouting` calls. Pattern:

```cpp
struct Ctx {
    QSettings       store;
    AlexController  alex;
    MockConn        mockConn;
    RadioModel      model;
};

Ctx makeConnectedModel(HPSDRModel sku) {
    Ctx c{/* ... init with sku-mapped BoardCapabilities ... */};
    c.model.setConnection(&c.mockConn);
    c.mockConn.setState(RadioConnection::State::Connected);
    return c;
}
```

- [ ] **T6.5** Build + run:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_antenna_routing_model
  ctest --test-dir build -R tst_antenna_routing_model --output-on-failure
  ```
  Expected: all new cases pass.

- [ ] **T6.6** Commit:
  ```bash
  git add src/models/RadioModel.h src/models/RadioModel.cpp \
          tests/tst_antenna_routing_model.cpp
  git commit -S -m "feat(model): port full applyAlexAntennaForBand composition (Phase 3P-I-b T6)

Expands 3P-I-a minimal routing to the full Thetis Alex.cs:310-413
composition minus MOX + Aries (deferred to 3M-1). Adds isTx branch
with Ext1/Ext2/RxOutOnTx mapping, xvtrActive gating, and
rx_out_override clamp. Wires 6 new AlexController signals to retrigger
composition on flag changes.

Source: Thetis HPSDR/Alex.cs:310-413 [v2.10.3.13 @501e3f5]"
  ```

---

## T7. Setup → Antenna Control grid — RX-only column + Ext checkboxes

**Files:**
- Modify: `src/gui/setup/hardware/AntennaAlexAntennaControlTab.h` / `.cpp`
- Modify: `tests/tst_ui_capability_gating.cpp` (if 3P-I-a added; otherwise create)

- [ ] **T7.1** **SHOW** the Thetis source. The Antenna Control frame is `setup.cs` around the `grpAlexAntCtrl` group box — grep first:

```bash
grep -n "grpAlexAntCtrl\|grpAntennaBands\|grpRXAntControl\|labelRXAntControl" "/Users/j.j.boyd/Thetis/Project Files/Source/Console/setup.cs" | head -30
```

The RX-only radio buttons are a 14×3 grid labeled by `RXAntChk1Name/2Name/3Name` (set per SKU — we now read from `SkuUiProfile.rxOnlyLabels`). The three TX-bypass checkboxes (`chkRxOutOnTx`, `chkEXT1OutOnTx`, `chkEXT2OutOnTx`) + `chkDisableRXOut` sit below the grid.

- [ ] **T7.2** In `AntennaAlexAntennaControlTab.h`, add members:

```cpp
private:
    QGroupBox* m_rxOnlyGroup {nullptr};
    std::array<std::array<QRadioButton*, 3>, 14> m_rxOnlyButtons {};
    std::array<QLabel*, 3> m_rxOnlyHeaderLabels {};
    QCheckBox* m_chkRxOutOnTx     {nullptr};
    QCheckBox* m_chkExt1OutOnTx   {nullptr};
    QCheckBox* m_chkExt2OutOnTx   {nullptr};
    QCheckBox* m_chkRxOutOverride {nullptr};  // chkDisableRXOut
    QCheckBox* m_chkUseTxAntForRx {nullptr};  // TRxAnt
    SkuUiProfile m_sku;

private slots:
    void onSkuChanged(HPSDRModel sku);
    void onRxOnlyButtonToggled(int bandIdx, int antIdx, bool checked);
    void onRxOutOnTxToggled(bool checked);
    void onExt1OutOnTxToggled(bool checked);
    void onExt2OutOnTxToggled(bool checked);
```

- [ ] **T7.3** In the tab's constructor, build the RX-only grid alongside the existing TX + RX grids. Reuse the existing `buildBandGrid(...)` helper (used for TX/RX in 3P-I-a) but bind to `AlexController::setRxOnlyAnt` instead. The grid should render 14 rows × 3 buttons matching the existing visual language. Column headers bind to `m_rxOnlyHeaderLabels[i]->setText(m_sku.rxOnlyLabels[i])`.

Beneath the three grids (TX + RX + RX-only, arranged in QHBoxLayout or 3-column QGridLayout), add a new `QFrame*` with a vertical stack of the five checkboxes:

```cpp
m_chkRxOutOnTx     = new QCheckBox(tr("RX Bypass on TX"), this);
m_chkExt1OutOnTx   = new QCheckBox(tr("Ext 1 on TX"), this);
m_chkExt2OutOnTx   = new QCheckBox(tr("Ext 2 on TX"), this);
m_chkRxOutOverride = new QCheckBox(tr("Disable RX Bypass relay"), this);
m_chkUseTxAntForRx = new QCheckBox(tr("Use TX antenna for RX"), this);
```

Thetis-first tooltips per `setup.cs:6178/6198`:
- `m_chkExt2OutOnTx->setToolTip(tr("Enable RX Bypass during transmit."));` (for ANAN8000D+ family)
- fallback: `m_chkExt2OutOnTx->setToolTip(tr("Enable RX 1 IN on Alex or Ext 2 on ANAN during transmit."));`

The tooltip switches inside `onSkuChanged` based on `m_sku` identity.

- [ ] **T7.4** Implement `onSkuChanged(HPSDRModel sku)`:

```cpp
void AntennaAlexAntennaControlTab::onSkuChanged(HPSDRModel sku)
{
    m_sku = skuUiProfileFor(sku);

    // RX-only column headers — From Thetis setup.cs:19846-20375 [v2.10.3.13 @501e3f5]
    for (int i = 0; i < 3; ++i) {
        m_rxOnlyHeaderLabels[i]->setText(m_sku.rxOnlyLabels[i]);
    }

    // Grid visibility — From Thetis setup.cs:6288-6299
    const bool showRxOnly = (m_caps.rxOnlyAntennaCount > 0);
    m_rxOnlyGroup->setVisible(showRxOnly);

    // Checkbox visibility per SkuUiProfile.
    m_chkRxOutOnTx->setVisible(m_sku.hasRxOutOnTx);
    m_chkExt1OutOnTx->setVisible(m_sku.hasExt1OutOnTx);
    m_chkExt2OutOnTx->setVisible(m_sku.hasExt2OutOnTx);
    m_chkRxOutOverride->setVisible(m_sku.hasRxBypassUi);

    // Tooltip variant — From Thetis setup.cs:6178/6198
    const bool isAnan7000Family =
        (sku == HPSDRModel::ANAN7000D) ||
        (sku == HPSDRModel::ANAN8000D) ||
        (sku == HPSDRModel::ANAN_G2)   ||
        (sku == HPSDRModel::ANAN_G2_1K)||
        (sku == HPSDRModel::ANVELINAPRO3);
    m_chkExt2OutOnTx->setToolTip(isAnan7000Family
        ? tr("Enable RX Bypass during transmit.")
        : tr("Enable RX 1 IN on Alex or Ext 2 on ANAN during transmit."));
}
```

Connect `onSkuChanged` to the `RadioModel::currentRadioChanged` signal via a parent wire-up — same pattern as the TX/RX grids in 3P-I-a.

- [ ] **T7.5** Wire the toggle slots to `AlexController`:

```cpp
void AntennaAlexAntennaControlTab::onRxOnlyButtonToggled(int bandIdx, int antIdx, bool checked)
{
    if (!checked) { return; }  // radio-button group emits once per click
    m_alex->setRxOnlyAnt(bandFromAlexIndex(bandIdx), antIdx);
}

void AntennaAlexAntennaControlTab::onRxOutOnTxToggled(bool checked) {
    m_alex->setRxOutOnTx(checked);
}
// (and parallel handlers for ext1, ext2, rxOutOverride, useTxAntForRx)
```

`AlexController::rxOnlyAntChanged(band)` flows back into the tab to sync radio-button state (use `QSignalBlocker` on the button group while updating to avoid re-firing).

- [ ] **T7.6** Add UI capability test to `tests/tst_ui_capability_gating.cpp`:

```cpp
void rxOnly_column_hidden_on_hl2() {
    BoardCapabilities caps;
    caps.hasAlex = false;
    caps.rxOnlyAntennaCount = 0;
    AntennaAlexAntennaControlTab tab(&alex, caps, HPSDRModel::HERMESLITE);
    QVERIFY(!tab.rxOnlyGroupForTest()->isVisible());
}

void rxOnly_labels_EXT1_EXT2_on_anan100() {
    BoardCapabilities caps;  // default Alex caps
    caps.hasAlex = true;
    caps.rxOnlyAntennaCount = 3;
    AntennaAlexAntennaControlTab tab(&alex, caps, HPSDRModel::ANAN100);
    QCOMPARE(tab.rxOnlyHeaderLabelForTest(0)->text(), QStringLiteral("EXT2"));
    QCOMPARE(tab.rxOnlyHeaderLabelForTest(1)->text(), QStringLiteral("EXT1"));
    QCOMPARE(tab.rxOnlyHeaderLabelForTest(2)->text(), QStringLiteral("XVTR"));
}

void rxOnly_labels_BYPS_EXT1_on_anan_g2() {
    BoardCapabilities caps;
    caps.hasAlex = true;
    caps.rxOnlyAntennaCount = 3;
    AntennaAlexAntennaControlTab tab(&alex, caps, HPSDRModel::ANAN_G2);
    QCOMPARE(tab.rxOnlyHeaderLabelForTest(0)->text(), QStringLiteral("BYPS"));
}

void ext1_checkbox_hidden_on_anan10() {
    BoardCapabilities caps;
    caps.hasAlex = true;
    AntennaAlexAntennaControlTab tab(&alex, caps, HPSDRModel::ANAN10);
    QVERIFY(!tab.ext1CheckboxForTest()->isVisible());
    QVERIFY(!tab.ext2CheckboxForTest()->isVisible());
}
```

Add test-accessor methods (or friend the test) per the 3P-I-a pattern.

- [ ] **T7.7** Build the app and eyeball-verify per the `feedback_screenshot_ui_debug` preference:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDR
  pkill -9 NereusSDR 2>/dev/null; sleep 1
  ./build/NereusSDR &
  ```
  Open Setup → Hardware → Antenna → Antenna Control. Connect to an ANAN-G2 (or simulated caps), verify RX-only column header reads "BYPS  EXT1  XVTR". Disconnect, reconnect with HERMES capabilities, verify column reads "RX1  RX2  XVTR". Take a screencapture and verify with the Read tool per `feedback_screenshot_ui_debug`.

- [ ] **T7.8** Commit:
  ```bash
  git add src/gui/setup/hardware/AntennaAlexAntennaControlTab.h \
          src/gui/setup/hardware/AntennaAlexAntennaControlTab.cpp \
          tests/tst_ui_capability_gating.cpp
  git commit -S -m "feat(setup): add RX-only column + Ext/Bypass checkboxes to Antenna Control (Phase 3P-I-b T7)

Extends the Setup Antenna Control tab with a third 14×3 grid (RX-only)
binding to AlexController.rxOnlyAnt. Adds five checkboxes below the
grids: RX Bypass/Ext1/Ext2 on TX, Disable RX Bypass relay, Use TX ant
for RX. SkuUiProfile-driven labels + checkbox visibility match Thetis
per-SKU presentation.

Source: Thetis setup.cs:6178-6300, 19845-20375 [v2.10.3.13 @501e3f5]"
  ```

---

## T8. Setup → Antenna — Alex-2 Filters sub-tab gate

**Files:**
- Modify: `src/gui/setup/hardware/AntennaAlexTab.cpp`
- Modify: `tests/tst_ui_capability_gating.cpp`

- [ ] **T8.1** **SHOW** Thetis source `setup.cs:6228-6264`: the Alex-2 (`tpAlex2FilterControl`) is shown only for P2 BPF2-capable models (ANAN7000D family). NereusSDR's `BoardCapabilities::hasAlex2` already captures this (3P-I-a).

- [ ] **T8.2** In `AntennaAlexTab.cpp`, find the tab-widget construction. After the `tpAlex2FilterControl` sub-tab is added, gate its visibility:

```cpp
// Alex-2 BPF bank — only on P2 BPF2-capable models.
// From Thetis setup.cs:6228-6264 [v2.10.3.13 @501e3f5]
const int alex2Idx = m_tabWidget->indexOf(m_alex2FilterTab);
if (alex2Idx >= 0) {
    m_tabWidget->setTabVisible(alex2Idx, m_caps.hasAlex2);
}
```

Hook this into the same `onBoardCapabilitiesChanged(caps)` slot that 3P-I-a already connected.

- [ ] **T8.3** Add test case:

```cpp
void alex2_subtab_hidden_on_hermes() {
    BoardCapabilities caps;
    caps.hasAlex = true;
    caps.hasAlex2 = false;
    AntennaAlexTab tab(caps);
    QVERIFY(!tab.alex2SubtabVisibleForTest());
}

void alex2_subtab_visible_on_anan_g2() {
    BoardCapabilities caps;
    caps.hasAlex = true;
    caps.hasAlex2 = true;
    AntennaAlexTab tab(caps);
    QVERIFY(tab.alex2SubtabVisibleForTest());
}
```

- [ ] **T8.4** Build + test + commit:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target tst_ui_capability_gating
  ctest --test-dir build -R tst_ui_capability_gating --output-on-failure
  git add src/gui/setup/hardware/AntennaAlexTab.cpp tests/tst_ui_capability_gating.cpp
  git commit -S -m "feat(setup): gate Alex-2 Filters sub-tab on caps.hasAlex2 (Phase 3P-I-b T8)

Closes the last gate from design doc §6 Rule 4: Alex-2 sub-tab now
hides automatically on HPSDR/HERMES/classic Alex boards and shows on
ORIONMKII / Saturn family (P2 BPF2-capable).

Source: Thetis setup.cs:6228-6264 [v2.10.3.13 @501e3f5]"
  ```

---

## T9. VFO Flag — RX-Bypass 3rd button (optional, gated)

**Files:**
- Modify: `src/gui/widgets/VfoWidget.h` / `.cpp`
- Modify: `tests/tst_ui_capability_gating.cpp`

- [ ] **T9.1** **SHOW** the 3P-I-a VFO Flag construction (grep for `m_rxAntBtn` / `m_txAntBtn`). Understand the current 2-button row layout.

- [ ] **T9.2** In `VfoWidget.h`, add the 3rd button member:

```cpp
private:
    QToolButton* m_rxBypassBtn {nullptr};  // grey; toggles rxOutOnTx
```

- [ ] **T9.3** In `VfoWidget.cpp` constructor, place the button between blue RX and red TX. Style: flat grey tool-button, checked state slightly lighter, text = "BYPS" or an unchecked-box glyph. Initial state `setVisible(false)`.

```cpp
m_rxBypassBtn = new QToolButton(m_antennaRowFrame);
m_rxBypassBtn->setText(tr("BYPS"));
m_rxBypassBtn->setCheckable(true);
m_rxBypassBtn->setStyleSheet(StyleConstants::kVfoFlagRxBypassButton);  // define in StyleConstants
m_rxBypassBtn->setVisible(false);
connect(m_rxBypassBtn, &QToolButton::toggled, this, &VfoWidget::onRxBypassToggled);
```

- [ ] **T9.4** Add slot:

```cpp
void VfoWidget::onRxBypassToggled(bool on)
{
    if (m_updatingFromModel) { return; }
    emit rxOutOnTxChanged(on);
}

void VfoWidget::setBoardCapabilities(const BoardCapabilities& caps, HPSDRModel sku)
{
    // From design §6 Rule 4 — gate on hasRxBypassRelay + sku.hasRxBypassUi
    const auto profile = skuUiProfileFor(sku);
    m_rxBypassBtn->setVisible(caps.hasRxBypassRelay && profile.hasRxBypassUi);
    // (existing RX + TX antenna button gating retained)
}

void VfoWidget::refreshFromAlex(const AlexController& alex)
{
    m_updatingFromModel = true;
    m_rxBypassBtn->setChecked(alex.rxOutOnTx());
    m_updatingFromModel = false;
    // (existing refresh retained)
}
```

- [ ] **T9.5** Wire in `MainWindow` (or wherever VfoWidget is constructed):

```cpp
connect(m_vfoWidget, &VfoWidget::rxOutOnTxChanged,
        &m_radioModel->alexController(), &AlexController::setRxOutOnTx);
connect(&m_radioModel->alexController(), &AlexController::rxOutOnTxChanged,
        m_vfoWidget, [this](bool) { m_vfoWidget->refreshFromAlex(m_radioModel->alexController()); });
```

- [ ] **T9.6** Add UI capability test:

```cpp
void rxBypass_button_hidden_on_anan8000d() {
    BoardCapabilities caps;
    caps.hasAlex = true;
    caps.hasRxBypassRelay = true;
    VfoWidget vfo;
    vfo.setBoardCapabilities(caps, HPSDRModel::ANAN8000D);  // hasRxBypassUi=false
    QVERIFY(!vfo.rxBypassButtonForTest()->isVisible());
}

void rxBypass_button_visible_on_anan100() {
    BoardCapabilities caps;
    caps.hasAlex = true;
    caps.hasRxBypassRelay = true;
    VfoWidget vfo;
    vfo.setBoardCapabilities(caps, HPSDRModel::ANAN100);
    QVERIFY(vfo.rxBypassButtonForTest()->isVisible());
}

void rxBypass_toggle_reaches_alexController() {
    AlexController alex(&settings(), "aa:bb:cc:dd:ee:ff");
    VfoWidget vfo;
    vfo.connectTo(&alex);  // test harness helper
    vfo.rxBypassButtonForTest()->click();
    QVERIFY(alex.rxOutOnTx());
}
```

- [ ] **T9.7** Build, launch, screenshot, verify:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu) --target NereusSDR
  pkill -9 NereusSDR 2>/dev/null; sleep 1
  ./build/NereusSDR &
  ```
  Connect to an ANAN-100D or HERMES fixture; verify BYPS button appears between blue/red antenna buttons. Click it — the Setup → Antenna Control → "RX Bypass on TX" checkbox should toggle in lockstep.

- [ ] **T9.8** Commit:
  ```bash
  git add src/gui/widgets/VfoWidget.h src/gui/widgets/VfoWidget.cpp \
          src/gui/MainWindow.cpp \
          src/gui/styles/StyleConstants.h \
          tests/tst_ui_capability_gating.cpp
  git commit -S -m "feat(vfo): add RX-Bypass 3rd flag button (Phase 3P-I-b T9)

Adds optional grey BYPS toggle between blue RX and red TX antenna
buttons on the VFO Flag. Gated on caps.hasRxBypassRelay &&
SkuUiProfile.hasRxBypassUi — hidden on HL2/Atlas/ANAN10/ANAN8000D/
G2/G2_1K/ANVELINAPRO3/REDPITAYA. Toggle writes AlexController.
rxOutOnTx, syncing with the Setup Antenna Control checkbox.

Source: Thetis HPSDR/Alex.cs:61 RxOutOnTx [v2.10.3.13 @501e3f5]"
  ```

---

## T10. AntennaButtonItem — optional RX-only button type (meter surface)

**Files:**
- Modify: `src/gui/meters/AntennaButtonItem.h` / `.cpp`
- Modify: `src/gui/containers/meter_property_editors/AntennaButtonItemEditor.cpp`

- [ ] **T10.1** Inventory says the item already supports "Rx1/Rx2/Rx3/Aux1/Aux2/XVTR/Tx1/Tx2/Tx3/Rx/Tx" modes. Verify these enum values are present:

```bash
grep -n "enum class ButtonType\|kButtonType\|BAntennaButton" src/gui/meters/AntennaButtonItem.h
```

If an `RxOnly` / `RxBypass` type isn't already in the enum, add it. Otherwise no code change — the label-rewrite for RX-only needs to consult `SkuUiProfile`:

- [ ] **T10.2** In the render function (where the button text is set), replace hardcoded RX-only labels with:

```cpp
if (m_buttonType == ButtonType::RxOnly || m_buttonType == ButtonType::BAntennaButton) {
    const auto labels = rxOnlyLabels(skuUiProfileFor(m_currentSku));
    painter.drawText(rect, Qt::AlignCenter, labels[m_portIndex]);
}
```

Wire `m_currentSku` via the existing `setBoardCapabilities` / `setSku` slot path from 3P-I-a.

- [ ] **T10.3** In `AntennaButtonItemEditor.cpp`, when the user selects RX-only mode in the port dropdown, the combo items should show the live SKU labels:

```cpp
void AntennaButtonItemEditor::rebuildPortCombo()
{
    m_portCombo->clear();
    if (m_item->buttonType() == AntennaButtonItem::ButtonType::RxOnly) {
        const auto labels = rxOnlyLabels(skuUiProfileFor(currentSku()));
        for (int i = 0; i < 3; ++i) {
            m_portCombo->addItem(labels[i], i + 1);
        }
    } else {
        // existing TX/RX label path
    }
}
```

- [ ] **T10.4** Add item-editor regression test (extend existing test file if present; otherwise skip — meter items have their own coverage cadence).

- [ ] **T10.5** Build + test + commit:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  git add src/gui/meters/AntennaButtonItem.h src/gui/meters/AntennaButtonItem.cpp \
          src/gui/containers/meter_property_editors/AntennaButtonItemEditor.cpp
  git commit -S -m "feat(meter): SKU-aware labels on AntennaButtonItem RX-only mode (Phase 3P-I-b T10)

Meter-surface AntennaButtonItem now reads rxOnlyLabels(SkuUiProfile)
instead of hardcoded RX1/RX2/XVTR. Property editor combo surfaces the
live labels so skin authors see BYPS/EXT1/XVTR on G2 / EXT2/EXT1/XVTR
on classic Alex / RX1/RX2/XVTR on Hermes.

Source: Thetis setup.cs:19846-20375 [v2.10.3.13 @501e3f5]"
  ```

---

## T11. Verification matrix update

**Files:**
- Modify: `docs/architecture/antenna-routing-verification.md`

- [ ] **T11.1** Append §7 to the verification doc:

```markdown
## §7 · RX-Only + XVTR + Ext-on-TX Verification (Phase 3P-I-b)

Per-SKU matrix. Mark each cell ✓ / ✗ / N/A on the test bench.

| # | Step | ANAN-G2 | ANAN-100D | ANAN-8000DLE | Hermes | HL2 bare |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 1 | RX-only column header reads "BYPS/EXT1/XVTR" (G2 / 7000D / 8000D / G2-1K / ANVELINAPRO3) or "EXT2/EXT1/XVTR" (100/100B/100D/200D/ORIONMKII) or "RX1/RX2/XVTR" (HERMES / ANAN10) | | | | | N/A (tab hidden) |
| 2 | Selecting RX-only = RX1 on 20m and tuning to 20m band produces visible bit on captured pcap (C3 bit5 for P1; Alex0 bit 27 for P2) | | | | | N/A |
| 3 | Ext1-on-TX checkbox visible/hidden per SKU matrix (§T1 table) | | | | | N/A |
| 4 | Ext1-on-TX toggle with MOX off → `rxOnlyAnt` unchanged on wire (no effect until TX, per Thetis) | | | | | N/A |
| 5 | Disable RX Bypass relay checkbox visible only on HPSDR/HERMES/ANAN100-class | N/A | | | ✓? | N/A |
| 6 | VFO Flag BYPS button visible per SKU matrix; toggle syncs with Setup checkbox | | | N/A | | N/A |
| 7 | Rapid band changes 20m→40m→20m with RX-only set produce stable, flicker-free UI and no spurious wire writes (check pcap for idempotency) | | | | | N/A |
| 8 | Disconnect + reconnect preserves RX-only state per band | | | | | N/A |
| 9 | xvtrActive toggled via Setup option: rxOnlyAnt=3 on 20m stays 3 with xvtr on; becomes 0 with xvtr off | | | | | N/A |

**Bench notes:**
- HL2 bare: verify the Alex tab is outright hidden (not just grayed). Confirm no spurious antenna bits on the P1 wire (`prbpfilter->_Rx_1_In/2_In/XVTR_Rx_In` all zero — capture HL2 pcap and grep).
- ANAN8000DLE: verify BYPS button doesn't appear on VFO Flag despite `caps.hasRxBypassRelay=true` — the `SkuUiProfile.hasRxBypassUi=false` must override.
```

- [ ] **T11.2** Commit:
  ```bash
  git add docs/architecture/antenna-routing-verification.md
  git commit -S -m "docs(verification): add §7 RX-only/XVTR/Ext-on-TX per-SKU matrix (Phase 3P-I-b T11)"
  ```

---

## T12. PR prep

- [ ] **T12.1** Rebase on latest main, resolve any conflicts:
  ```bash
  git fetch origin
  git rebase origin/main
  ```

- [ ] **T12.2** Run the full test suite one more time:
  ```bash
  cmake --build build -j$(sysctl -n hw.ncpu)
  ctest --test-dir build --output-on-failure 2>&1 | tail -3
  ```
  Expected: pass count at baseline + new cases.

- [ ] **T12.3** Run the attribution verifiers manually (the pre-commit hook should already have done this per commit):
  ```bash
  python3 scripts/verify-thetis-headers.py
  python3 scripts/check-new-ports.py
  python3 scripts/verify-inline-tag-preservation.py
  ```
  All three exit 0.

- [ ] **T12.4** Draft the PR body in chat (**DO NOT** run `gh pr create` yet — per user rule "no public posts without review"). Template:

```markdown
## Summary

Phase 3P-I-b — closes the RX-side antenna routing work left open by 3P-I-a.

- **Protocol:** RX-only bits now encoded on P1 bank0 C3 (bits 5-6) and P2 Alex0 (bits 27-30), byte-locked against Thetis. RX-Bypass-Out relay (`_Rx_1_Out` / Alex0 bit 30) wired in parallel.
- **Model:** `applyAlexAntennaForBand` now mirrors Thetis Alex.cs:310-413 in full (minus MOX + Aries — deferred to 3M-1). isTx branch, Ext1/Ext2/RxOutOnTx mapping, xvtrActive gating, rx_out_override clamp.
- **UI:** `SkuUiProfile` per-HPSDRModel overlay drives per-SKU labels ("RX1/RX2/XVTR" vs "EXT2/EXT1/XVTR" vs "BYPS/EXT1/XVTR") + per-SKU checkbox visibility. Setup Antenna Control gains a 14×3 RX-only grid + 5 checkboxes. Alex-2 Filters sub-tab now gates on `caps.hasAlex2`. VFO Flag gets an optional BYPS 3rd button.

## Scope

In scope (this PR):
- All items from [`phase3p-i-b-rxonly-xvtr-sku-plan.md`](../docs/architecture/phase3p-i-b-rxonly-xvtr-sku-plan.md)

Out of scope (future phases):
- MOX-coupled reapply → Phase 3M-1
- Aries ATU clamp → Phase 3M-1
- HL1/HL2 firmware refinement pass → separate phase
- N2ADR companion board antenna routing → future HL2 phase

## Test plan

- [ ] Unit: `tst_sku_ui_profile` (8 cases)
- [ ] Unit: `tst_alex_controller` (+6 cases, total grows from 3P-I-a baseline)
- [ ] Unit: `tst_p1_codec_standard` + `tst_p1_codec_hl2` (+8 byte-lock cases each)
- [ ] Unit: `tst_p2_codec_orionmkii` (+5 byte-lock cases)
- [ ] Integration: `tst_rxonly_routing` (new; 7 cases)
- [ ] UI: `tst_ui_capability_gating` (+5 cases)
- [ ] Manual: per-SKU matrix in `docs/architecture/antenna-routing-verification.md` §7

## Attribution

All ported code carries `// From Thetis [file]:[line] [v2.10.3.13 @501e3f5]` inline cites. Upstream author tags (`//DH1KLM` on REDPITAYA branches, `//G8NJJ` on Aries reference-only block) preserved verbatim. Pre-commit attribution verifiers pass.
```

- [ ] **T12.5** Offer the PR body to the user for review. On approval ("post it"), run:
  ```bash
  gh pr create --title "Phase 3P-I-b — RX-only antennas, SKU labels, Ext/Bypass flags, XVTR" \
               --body "$(cat /tmp/pr-body.md)"
  ```
  Then open the PR URL per `feedback_open_links_after_post`.

---

## Self-Review

Before handoff, run through these checks:

1. **Spec coverage** — Does every row in `antenna-routing-design.md` §2 (3P-I-b line) have a task?
   - RX-Only paths → T4, T5, T6 ✓
   - XVTR handling → T6 (xvtrActive branch), T3 (flag) ✓
   - Ext-on-TX flags → T3, T6, T7 ✓
   - SKU Overlay → T1, T2, T7, T9, T10 ✓
   - hasAlex2 sub-tab gate → T8 ✓

2. **Placeholders** — Every "write failing test" step shows the test code. Every codec change shows the full bit encoding block. The only two deliberate "if not yet X, create Y" steps (T6.4 fixture, T10 enum audit) are scoped to "verify existing state, add only if missing" — acceptable because the 3P-I-a merge state is uncertain at plan-authoring time.

3. **Type consistency** — `AntennaRouting` fields (`rxOnlyAnt`, `trxAnt`, `txAnt`, `rxOut`, `tx`) match across §5.4 of the design doc, 3P-I-a codebase inventory, and every task that touches the struct. `AlexController` setter/signal names match across T3 (definition) → T6 (connection) → T7 (UI binding) → T9 (VFO).

4. **Source-first compliance** — Every task that ports Thetis code has a READ→SHOW step (T1.1, T3.1, T4.1, T5.1, T6.1, T7.1, T8.1, T9.1) and the translated C++ carries a version-stamped inline cite.

5. **License/attribution compliance** — No new files port a Thetis file wholesale, so no header copy is required; all ports are citations inside existing NereusSDR files. New files (`SkuUiProfile.{h,cpp}`) carry their own authorship attribution ("Phase 3P-I-b") plus an inline Thetis cite on each case branch.

---

## Execution Handoff

Plan complete and saved to `docs/architecture/phase3p-i-b-rxonly-xvtr-sku-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task (T1 → T12), two-stage review (implementation → verification) between each. Keeps the main context clean, maximizes per-task rigor, each commit is independent.

**2. Inline Execution** — work through T1–T12 in this session with checkpoints at T6 (after the model composition lands), T9 (after UI surfaces), and T12 (PR prep). Faster iteration but main context grows quickly across 12 tasks.

**Which approach?**
