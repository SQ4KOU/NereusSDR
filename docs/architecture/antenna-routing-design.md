# Antenna Routing — Board-Aware Design

**Status:** Draft · 2026-04-22
**Catalyst:** [issue #98](https://github.com/boydsoftprez/NereusSDR/issues/98) — "ANT selection does not work" (reporter: g0orx, tested on ANAN-8000DLE + ANAN-100D, Ubuntu 25.10)
**Phase home:** 3P-I (Alex Antenna Integration) — closes the protocol-layer gap left by Phase 3P-F (which shipped the `AlexController` model and Antenna Control UI without wiring them to the radio).
**Thetis baseline:** `v2.10.3.13 @501e3f5` (all file:line citations reference this stamp).

---

## 1 · Context and Problem

Phase 3P-F (PR #96, merged) landed the `AlexController` model — per-band TxAnt / RxAnt / RxOnlyAnt arrays with per-MAC persistence and Block-TX safety — plus the **Setup → Hardware → Antenna → Antenna Control** per-band grid and the RxApplet antenna buttons that auto-populate from it. What it did **not** land: any path that pushes `AlexController` state to the wire.

The result: a user can configure per-band antennas in Setup, clicks save, connects to the radio, and nothing happens. The radio stays on ANT1. Issue #98 is the field report that made the gap visible.

The minimal fix ("wire `AlexController::antennaChanged` to `RadioConnection::setAntenna`") is incomplete because it assumes ANT1/2/3 works the same way on every board. It does not:

- **Hermes Lite 2** has no Alex filter board. Thetis `Alex.cs:312-317` explicitly short-circuits: `if (!alex_enabled) { NetworkIO.SetAntBits(0, 0, 0, 0, false); return; }`. HL2 gateware **does** repack the `ANT` bit into an I²C byte aimed at companion boards (N2ADR I/O board, address 0x20) per the HL2 Protocol wiki — but bare HL2 has no such board and the bits are effectively a no-op on the hardware side.
- **Atlas** (classic HPSDR / Penelope) has a single RF connector; ANT2/ANT3 have no physical meaning.
- **ANAN SKU families** differ from each other in UI-visible ways even within a single chipset (ANAN10 hides `Ext1/Ext2OutOnTx`; ANAN8000DLE suppresses `RxBypassOut`; ANAN_G2_1K hides all three). The "which labels show" matters for parity with Thetis's user-facing presentation.
- **Aries ATU** is an external tuner that clamps TX and RX to ANT1. Thetis models this as a software override (`LimitTXRXAntenna` in `Alex.cs:60`), not a hardware constraint.

The design below captures the full board-aware model, split into phases that can each ship independently.

---

## 2 · Scope and Phasing

| Phase | Name | Goal | Lands with |
|-------|------|------|------------|
| **3P-I-a** | Core Per-Band Routing + UI Gating | Close issue #98. Per-band RX/TX routing on `hasAlex` boards; HL2/Atlas surfaces hidden; `kPopupMenu` palette. | Next PR after PR #34 (3G-13) merges. |
| **3P-I-b** | RX-Only Paths + XVTR + Ext-on-TX + SKU Overlay | Pre-TX polish. `RxOnlyAnt` routing, XVTR handling, Ext1/Ext2-on-TX flags, `HPSDRModel` SKU overlay for per-ANAN UI differences, `hasAlex2` sub-tab gate. | Before 3M-1 (first TX bring-up). |
| **3M-1 piece** | MOX-Coupled Reapply + Aries | MOX-sensitive `trxAnt` recomposition on TX engage; Aries external-ATU toggle wires `setAntennasTo1`. | Inside Phase 3M-1 (TX basics). |

Each phase ships its own implementation plan under `docs/architecture/phase3p-i-*-plan.md` following the existing phase-plan convention.

---

## 3 · Architecture

### 3.1 Layered view

```
┌─────────────────────────────────────────────────────────────────┐
│ UI Layer · 11 surfaces (see §6)                                 │
│   All writes funnel through AlexController                      │
│   All reads consult BoardCapabilities + HPSDRModel SKU overlay  │
└─────────────────────────────────────────────────────────────────┘
                            │ setRxAnt/setTxAnt/setRxOnlyAnt(band, ant)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ State Layer · AlexController (single source of truth)           │
│   • Per-band arrays: TxAnt[14], RxAnt[14], RxOnlyAnt[14]        │
│   • Safety: blockTxAnt2, blockTxAnt3, LimitTXRXAntenna          │
│   • Per-MAC AppSettings persistence                             │
│   • Signal: antennaChanged(Band)                                │
└─────────────────────────────────────────────────────────────────┘
                            │ antennaChanged / bandChanged / connected / moxChanged
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ Orchestration · RadioModel::applyAlexAntennaForBand(band, isTx) │
│   Mirrors Thetis Alex.cs:310 UpdateAlexAntSelection             │
│   Composes an AntennaRouting struct, posts to connection        │
└─────────────────────────────────────────────────────────────────┘
                            │ setAntennaRouting(AntennaRouting)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ Protocol Layer · RadioConnection (P1 / P2 / HL2)                │
│   P1: bank 0 C4 (trxAnt) + bank 0 C3 bits (rxOnly + rxOut)      │
│   P2: Alex0/Alex1 32-bit regs at bytes 1428-1435                │
│   HL2 variant: same wire format; caps.hasAlex gates whether     │
│        non-zero values ever hit the wire (bare HL2 = zeros)     │
└─────────────────────────────────────────────────────────────────┘
                            ▼
                        Radio hardware
```

### 3.2 Invariants

1. **`AlexController` is authoritative.** No other state store for antennas. `SliceModel::m_rxAntenna/m_txAntenna` become caches synced from AlexController (Rule 3, §6).
2. **Every push is idempotent.** `AlexController::setRxAnt` returns early on equal value; `applyAlexAntennaForBand` cheap enough to run on every band crossing.
3. **Thread boundary preserved.** `AlexController` lives on the main thread. Composition runs on the main thread. `QMetaObject::invokeMethod` marshals the `setAntennaRouting` call onto the connection's worker thread. Wire-layer state is touched only on the worker thread.
4. **SKU overlay affects UI only.** `HPSDRModel` value never reaches `setAntennaRouting`. The wire sees the same encoding regardless of SKU; only the UI labels and which checkboxes render vary.

---

## 4 · Capability Schema

### 4.1 `BoardCapabilities` extensions (chipset grain)

Existing fields already in place (src/core/BoardCapabilities.h:249-281): `antennaInputCount`, `xvtrJackCount`, `hasAlex`, `hasAlexFilters`, `hasAlexTxRouting`. **Reuse these.** `antennaInputCount` is the TX/TRX port count (1 on HL2/Atlas; 3 on Alex boards) — the spec does NOT duplicate it under a new name.

New fields added by this spec:

```cpp
struct BoardCapabilities {
    // ... existing fields ...

    // Antenna extensions (3P-I)
    bool    hasAlex2            {false};  // P2 second BPF bank (ANAN7000D family)
    bool    hasRxBypassRelay    {false};  // RxOut relay physically present (false on HL2/Atlas)
    int     rxOnlyAntennaCount  {0};      // 0 on HL2/Atlas; 3 on Alex boards (RX1/RX2/XVTR)
};
```

Populated for all 10 rows in `BoardCapsTable::kTable` (src/core/BoardCapabilities.cpp). HL2/Atlas keep the default zeros; Hermes / HermesII / Angelia / Orion / OrionMkII / Saturn / SaturnMkII pick up `rxOnlyAntennaCount=3, hasRxBypassRelay=true`; P2 ANAN7000D-descended boards additionally get `hasAlex2=true`.

### 4.2 `HPSDRModel` SKU enum (product grain)

**Already exists** at src/core/HpsdrModel.h with the full Thetis enum — values match `enums.cs:109-131` one-to-one (HPSDR=0, HERMES=1, ANAN10=2, … HERMESLITE=14, REDPITAYA=15). Also ships with `boardForModel(HPSDRModel)` and `displayName(HPSDRModel)` helpers, plus `Q_DECLARE_METATYPE` for Qt.

Already exists on `HardwareProfile` at src/core/HardwareProfile.h:73 as field `HardwareProfile::model`. Existing helper `defaultModelForBoard(HPSDRHW)` at HardwareProfile.h:92 picks the auto-guess default SKU for a discovered board. `compatibleModels(HPSDRHW)` at HardwareProfile.h:98 returns the full list of user-selectable SKUs for a given board.

What this spec adds on top of that:

- A detection refinement pass at connect time: when the auto-guessed default is ambiguous (e.g. HermesLite chipset could be HL1 or HL2 — same `HPSDRHW::HermesLite` board byte), use firmware version to pick the right `HPSDRModel`. Lives in a new helper `refineSkuFromFirmware(HardwareProfile&, QString fwVersion)` in HardwareProfile.cpp. For 3P-I-a scope we refine only the HL1/HL2 split; other boards accept the default.
- A Settings → Diagnostics entry exposing `HardwareProfile::model` so field reports cross-reference cleanly.

Unknown boards (no `HPSDRHW` match) → `HPSDRModel::HERMES` fallback via `defaultModelForBoard(HPSDRHW::Unknown)`; conservative for generic Alex-like behavior. Logged warning on `lcConnection` carries the raw board byte + firmware version.

### 4.3 `SkuUiProfile` overlay (UI presentation only)

New file: src/core/SkuUiProfile.h. Does NOT affect the wire. Ports the per-SKU branches from Thetis `setup.cs:6157-6286`:

```cpp
struct SkuUiProfile {
    bool hasExt1OutOnTx  {false};
    bool hasExt2OutOnTx  {false};
    bool hasRxBypassUi   {true};
    std::array<QString,3> rxOnlyLabels {
        QStringLiteral("RX1"), QStringLiteral("RX2"), QStringLiteral("XVTR")
    };
    QString antennaTabLabel {QStringLiteral("Alex")};  // "Alex" vs "Ant/Filters"
};

SkuUiProfile skuUiProfileFor(HPSDRModel sku);
```

Per-SKU configuration table in src/core/SkuUiProfile.cpp:

| SKU | Ext1OnTx | Ext2OnTx | RxBypassUi | RxOnlyLabels | Tab label |
|-----|:---:|:---:|:---:|---|---|
| HPSDR | — | — | ✓ | RX1 / RX2 / XVTR | Alex |
| HERMES | ✓ | ✓ | ✓ | RX1 / RX2 / XVTR | Alex |
| HERMESLITE | — | — | — | (hidden) | (tab hidden) |
| ANAN10 / ANAN10E | — | — | ✓ | RX1 / RX2 / XVTR | Ant/Filters |
| ANAN100 / 100B | ✓ | ✓ | ✓ | EXT2 / EXT1 / XVTR | Ant/Filters |
| ANAN100D / 200D | ✓ | ✓ | ✓ | EXT2 / EXT1 / XVTR | Ant/Filters |
| ORIONMKII | ✓ | ✓ | ✓ | BYPS / EXT1 / XVTR | Ant/Filters |
| ANAN7000D | ✓ | ✓ | — (hidden) | BYPS / EXT1 / XVTR | Ant/Filters |
| ANAN8000D | — | — | — (hidden) | BYPS / EXT1 / XVTR | Ant/Filters |
| ANAN_G2 | ✓ | ✓ | — (hidden) | BYPS / EXT1 / XVTR | Ant/Filters |
| ANAN_G2_1K | — | — | — (hidden) | BYPS / EXT1 / XVTR | Ant/Filters |
| ANVELINAPRO3 | ✓ | ✓ | — (hidden) | BYPS / EXT1 / XVTR | Ant/Filters |
| REDPITAYA | ✓ | ✓ | — (hidden) | BYPS / EXT1 / XVTR | Ant/Filters |

Sources cited per row in code comments (`setup.cs:19832-20302` has all branches).

---

## 5 · Data Flow

### 5.1 Write paths (all → AlexController)

| # | Surface | Call chain |
|---|---------|------------|
| a | VFO Flag Blue button | `VfoWidget::rxAntennaChanged(text)` → `SliceModel::setRxAntenna(text)` → `RadioModel` handler → `m_alexController.setRxAnt(m_lastBand, 2)` |
| b | VFO Flag Red button | same path for TX |
| c | RxApplet ANT buttons | already dual-write `SliceModel` + `AlexController` (current behaviour); post-fix the `SliceModel` write is redundant-but-idempotent |
| d | Setup → Antenna Control grid | writes directly to `AlexController::setRxAnt(band, n)` / `setTxAnt(band, n)` / `setRxOnlyAnt(band, n)` |
| e | SpectrumOverlayPanel combos (currently unwired zombies) | wire through `RadioModel::setRxAntenna(text)` → same path as (a) |
| f | Container AntennaButtonItem (meter) | `antennaSelected` signal → `RadioModel::setRxAntenna(text)` |
| g | Aries toggle (3M-1) | `AlexController::setAntennasTo1(true)` |

### 5.2 Triggers that invoke `applyAlexAntennaForBand(band, isTx)`

| # | Trigger | Phase |
|---|---------|-------|
| T1 | `AlexController::antennaChanged(band)` where `band == m_lastBand` | 3P-I-a |
| T2 | `SliceModel::frequencyChanged` crosses band boundary (new ≠ `m_lastBand`) | 3P-I-a |
| T3 | `onConnectionStateChanged(Connected)` | 3P-I-a |
| T4 | `TransmitModel::moxChanged` | 3M-1 piece |

### 5.3 Composition (the Thetis port)

Mirrors `Alex.cs:310-413`. Lives in `RadioModel::applyAlexAntennaForBand`:

```cpp
void RadioModel::applyAlexAntennaForBand(Band band, bool isTx)
{
    if (!m_connection || !m_connection->isConnected()) { return; }

    const BoardCapabilities& caps = boardCapabilities();
    if (!caps.hasAlex) {
        // HL2/Atlas no-op — match Thetis Alex.cs:312-317
        // SetAntBits(0,0,0,0,false)
        postRouting({0, 0, 0, false, false});
        return;
    }

    int txAnt   = m_alexController.txAnt(band);                   // 1..3
    int rxOnly  = m_alexController.rxOnlyAnt(band);               // 0..3
    int trxAnt;
    bool rxOut;

    const SkuUiProfile& sku = m_skuProfile;
    const bool xvtrActive = m_xvtrActive;    // 3P-I-b; false until then

    if (isTx) {
        // Thetis Alex.cs:339-347
        if (sku.hasExt2OutOnTx)      { rxOnly = 1; }
        else if (sku.hasExt1OutOnTx) { rxOnly = 2; }
        else                          { rxOnly = 0; }
        rxOut  = (rxOnly != 0);
        trxAnt = txAnt;
    } else {
        // Thetis Alex.cs:349-366
        if (xvtrActive && rxOnly >= 3)       { rxOnly = 3; }
        else if (!xvtrActive && rxOnly >= 3) { rxOnly -= 3; }
        rxOut  = (rxOnly != 0);
        trxAnt = m_alexController.useTxAntForRx()
                    ? txAnt
                    : m_alexController.rxAnt(band);
    }

    // Aries clamp — Alex.cs:381-382
    if (m_alexController.ariesActive() && trxAnt != 4) { trxAnt = 1; }

    postRouting({rxOnly, trxAnt, txAnt, rxOut, isTx});
}
```

3P-I-a ships only the `trxAnt = m_alexController.rxAnt(band)` branch (no `isTx` handling, no `xvtrActive`, no `sku.hasExt*OnTx`, no Aries). 3P-I-b adds the SKU-aware RX-only composition and the `hasAlex=false` no-op now becomes meaningful for HL2 UI gating. 3M-1 adds the `isTx` branch and Aries.

### 5.4 `AntennaRouting` sink

New `RadioConnection` virtual (src/core/RadioConnection.h):

```cpp
struct AntennaRouting {
    int  rxOnlyAnt;  // 0=none, 1=RX1_IN, 2=RX2_IN, 3=XVTR
    int  trxAnt;     // 1..3 (4 = special RX override)
    int  txAnt;      // 1..3
    bool rxOut;      // RX bypass relay active
    bool tx;         // current MOX state
};

virtual void setAntennaRouting(AntennaRouting r) = 0;
```

Replaces `setAntenna(int)`. Old method kept as a deprecated thin wrapper for one release cycle (rollback hatch, §7.7).

**P1 encoding** (src/core/codec/P1CodecStandard.cpp `bank0`): port `networkproto1.c:463-468`. C4 bits 0-1 carry `trxAnt` as 0b00/0b01/0b10 for ANT1/2/3. C3 bit 5 (0x20) = `_Rx_1_In`, bit 6 (0x40) = `_Rx_2_In`, bits 5+6 (0x60) = `_XVTR_Rx_In`. Currently hardcoded to `0x20`; becomes driven by `rxOnlyAnt`.

**P2 encoding** (src/core/P2RadioConnection.cpp `buildAlex0` / `buildAlex1`): already correct for ANT1/2/3 bits 24-26 on both Alex0 (RX) and Alex1 (TX); split the single-index `setAntenna` into independent RX/TX updates.

**HL2 encoding** (src/core/codec/P1CodecHl2.cpp): identical wire to P1CodecStandard. Gating lives upstream — the orchestrator skips non-zero writes when `caps.hasAlex=false`.

---

## 6 · UI Parity — All 11 Surfaces

| # | Surface | File | Reads | Writes | Gate |
|---|---------|------|-------|--------|------|
| 1 | VFO Flag RX/TX buttons | src/gui/widgets/VfoWidget.cpp:410-468 | SliceModel → AlexController | SliceModel → AlexController | `caps.hasAlex && caps.antennaInputCount >= 3` |
| 2 | RxApplet header buttons | src/gui/applets/RxApplet.cpp:239-320, 1366-1393 | AlexController | AlexController | same |
| 3 | Antenna Control grid | src/gui/setup/hardware/AntennaAlexAntennaControlTab.cpp:148-265 | AlexController | AlexController | parent gates on `caps.hasAlexFilters` |
| 4 | Alex Antenna parent tab | src/gui/setup/hardware/AntennaAlexTab.cpp:141-150 | BoardCaps | n/a | gates Alex-2 on `caps.hasAlex2` |
| 5 | HardwarePage visibility | src/gui/setup/HardwarePage.cpp:202 | BoardCaps | n/a | ✓ already gates on `caps.hasAlexFilters` |
| 6 | AntennaButtonItem (meter) | src/gui/meters/AntennaButtonItem.cpp:60-104 | ContainerWidget | `antennaSelected` signal | render mode per `caps.hasAlex` |
| 7 | AntennaButtonItemEditor | src/gui/containers/meter_property_editors/AntennaButtonItemEditor.cpp | MeterItem | MeterItem | clamp options to `caps.antennaInputCount` |
| 8 | ContainerWidget route | src/gui/containers/ContainerWidget.cpp:733-735 | AntennaButtonItem | `antennaSelected` out | pure router |
| 9 | SpectrumOverlayPanel combos | src/gui/SpectrumOverlayPanel.cpp:443, 458 | hardcoded | currently unwired | **3P-I-a wires through AlexController**, gates on `caps.hasAlex` |
| 10 | MainWindow slice-VFO bridge | src/gui/MainWindow.cpp:2184-2262 | SliceModel | VfoWidget | refactor: bridge `AlexController::antennaChanged` → `vfo->setRxAntenna` |
| 11 | SliceModel state | src/models/SliceModel.h:250-259, 523-524 | internal | setters (read-only cache) | n/a (model) |

### 6.1 Four parity rules

**Rule 1 · Single label source.** Every hardcoded `{"ANT1","ANT2","ANT3"}` list (currently in 10+ locations, including VfoWidget.h:496, RxApplet.h:232, SpectrumOverlayPanel.cpp:443/458, SliceModel.h:523-524) is replaced by a helper in src/core/AntennaLabels.{h,cpp}:

```cpp
QStringList antennaLabels(const BoardCapabilities& caps);          // "ANT1".."ANT3" or {}
std::array<QString,3> rxOnlyLabels(const SkuUiProfile& sku);       // varies per SKU
```

AntennaButtonItem's `"Rx1/Rx2/Rx3/Aux1/Aux2/XVTR/Tx1/Tx2/Tx3/Rx/Tx"` label set is a distinct **meter composition** convention (it lists every button variant), not an antenna port list — stays as is but its **editor combo** uses the shared helper when selecting which port each button represents.

**Rule 2 · Single popup stylesheet.** `kPopupMenu` constant moves to src/gui/styles/PopupMenuStyle.h (new file). Every `QMenu menu(...); ...; menu.exec()` antenna dropdown includes the header and calls `menu.setStyleSheet(kPopupMenu)`. A grep-based test (`tests/tst_popup_style_coverage.cpp`) fails the build on a bare antenna QMenu without the stylesheet call.

**Rule 3 · Single state source.** `AlexController` is authoritative. `SliceModel::m_rxAntenna/m_txAntenna` become **caches synced from AlexController per-band** — the public setters still exist (callers don't change), but a call to `slice->setRxAntenna("ANT2")` now routes to `m_alexController.setRxAnt(m_lastBand, 2)` via the RadioModel handler; the slice's own storage is then refreshed by the reverse direction (`AlexController::antennaChanged(band) → slice->refreshFromAlex(band)`). Refresh triggers on the reverse direction:

- Band change → `SliceModel` refreshes from `AlexController.rxAnt(newBand)` / `.txAnt(newBand)`
- `AlexController::antennaChanged(band)` for the current band → refresh
- Connect/disconnect → refresh

Every surface that reads `slice->rxAntenna()` keeps working unchanged; the returned value is just guaranteed current-band-correct.

**Rule 4 · Universal capability gating.**

- **VFO Flag (1):** `setVisible(caps.hasAlex && caps.antennaInputCount >= 3)`.
- **RxApplet (2):** same; hooked in `setBoardCapabilities` slot fired on `RadioModel::currentRadioChanged`.
- **Grid (3):** RX-Only column hidden when `caps.rxOnlyAntennaCount == 0`. Column headers populated from `sku.rxOnlyLabels`. Ext1/Ext2-on-TX checkboxes gated on `sku.hasExt1OutOnTx` / `hasExt2OutOnTx`. Rx-Bypass-Out row gated on `sku.hasRxBypassUi`.
- **AntennaButtonItem (6):** three modes — full palette when `caps.hasAlex && antennaInputCount >= 3`; read-only "ANT" label when `!caps.hasAlex` (click no-op + tooltip); "ANT1 (Aries)" badge when Aries active (3M-1).
- **SpectrumOverlayPanel (9):** wire the combos through `AlexController` now (closes the zombie-control gap); gate on `caps.hasAlex`.

### 6.2 Tooltip policy

Every gated control gets a Thetis-first tooltip (per existing tooltip-parity feedback). Where Thetis has no text (Block-TX safety, "Aries locked" badge, "Not available on this radio"), the tooltip is NereusSDR-native and says so.

### 6.3 Future surfaces (out of scope but gating is free to extend)

- **CAT `AN` command** (Phase 3K) — reads/writes `RadioModel::setRxAntenna`, same path.
- **TCI antenna command** (Phase 3J) — same.
- **Status bar / title bar antenna badge** (future polish) — read-only from `AlexController.rxAnt(currentBand)`.

---

## 7 · Edge Cases and Error Handling

| # | Case | Behavior |
|---|------|----------|
| E1 | Pre-connect writes | `AlexController` accepts writes; `applyAlexAntennaForBand` no-ops (`m_connection==nullptr`). On connect, `Connected` state handler applies the persisted value. |
| E2 | Disconnect mid-write | `applyAlexAntennaForBand` guards on `m_connection && m_connection->isConnected()`. State persists; applies on next connect. |
| E3 | Unknown SKU | Discovery byte unrecognized → `defaultModelForBoard(HPSDRHW::Unknown)` falls back to `HPSDRModel::HERMES` with conservative `SkuUiProfile` (no Ext, RX1/RX2/XVTR labels). Logged warning on `lcConnection` with raw board-id + firmware. Core routing still works when `caps.hasAlex=true`. |
| E4 | Aries override + manual click | User clicks ANT2; `AlexController` stores the value but `applyAlexAntennaForBand` clamps to ANT1 before posting. VFO Flag label updates to "ANT1 (Aries)". Disabling Aries reapplies stored value. (3M-1 scope.) |
| E5 | Block-TX vs hardware | `AlexController::setTxAnt` rejects silently when `blockTxAnt2=true`. UI grays ANT2 menu item with tooltip "Blocked by TX safety". This is NereusSDR-native — not a Thetis behavior. |
| E6 | MOX engage during band change | 3P-I-a punts: `applyAlexAntennaForBand` runs with `isTx=false` regardless of MOX. 3M-1 adds MOX trigger T4 and correct `isTx` composition. |
| E7 | XVTR with `rxOnlyAnt=0` | `rxOnly=0, rxOut=0` — radio returns to mainline antenna. Matches Thetis `Alex.cs:361`. |
| E8 | Multi-slice (RX2 future) | Antenna routing keyed off the "hardware-dominant slice" band (RX1 VFO band). Matches Thetis behavior — single Alex board can't serve two antennas simultaneously. Out of scope for 3P-I; spec reserves the design space. |
| E9 | Log noise at rapid tuning | Band-crossing reapply cheap + idempotent. No throttling needed (Thetis doesn't throttle either). |
| E10 | HL2 companion board hot-plug | Session-scope gating — new caps picked up on next reconnect. Document in Setup → Diagnostics. Full hot-reconfigure is out of scope. |

### 7.7 Rollback hatch

`RadioConnection::setAntenna(int)` stays as a deprecated wrapper calling `setAntennaRouting({0, idx+1, idx+1, false, false})` for one release cycle after 3P-I-a. Removed in the release following 3P-I-b.

---

## 8 · Testing

### 8.1 Unit tests

Expand `tests/tst_alex_controller.cpp`:

- Per-band write emits `antennaChanged(band)` with correct band
- Idempotent write emits no signal
- `setTxAnt(band, 2)` rejects silently when `blockTxAnt2=true`; no signal
- `setAntennasTo1(true)` fires signal for all 14 bands; all clamped to 1

### 8.2 Integration tests

New `tests/tst_antenna_routing_model.cpp`:

- Fixture: `RadioModel` with mock `RadioConnection` capturing `setAntennaRouting` calls
- `setRxAnt(Band20m, 2)` on connected radio at 20m → one `{trxAnt=2}` call
- Band change 20m → 40m with `RxAnt[40m]=3` → one `{trxAnt=3}` call
- `Connected` state transition → one call with current band's value
- Connect with `caps.hasAlex=false` → one `{0,0,0,false,false}` call (matches Thetis `SetAntBits(0,0,0,0,false)`)

### 8.3 Wire-encoding snapshot tests

Extend `tests/tst_p1_codec_standard.cpp` and `tests/tst_p2_codec_orionmkii.cpp`:

- P1 bank 0 C4 encoding for `trxAnt=1/2/3` → bytes match `networkproto1.c:463-468` byte-for-byte
- P1 bank 0 C3 encoding for `rxOnly=0/1/2/3` → bits 5-6 match `networkproto1.c:456-461`
- P2 Alex0 register bits 24/25/26 for `rxAnt=1/2/3` — regression baseline vs captured pcap
- P2 Alex1 register for `txAnt` — same
- HL2 codec with `caps.hasAlex=false` → antenna bits are zero

Baseline fixtures in `tests/fixtures/antenna/` as checked-in JSON.

### 8.4 Capability gating tests

New `tests/tst_ui_capability_gating.cpp`:

- VfoWidget with `caps.hasAlex=false` → ANT buttons hidden
- RxApplet with `caps.hasAlex=false` → ANT buttons hidden
- SpectrumOverlayPanel with `caps.hasAlex=false` → combos hidden
- AntennaControlTab with `caps.rxOnlyAntennaCount=0` → RX-only column hidden
- `sku=ANAN10` → Ext1/Ext2 checkboxes hidden
- `sku=ANAN7000D` → labels read `"BYPS/EXT1/XVTR"`

### 8.5 Popup style coverage test

New `tests/tst_popup_style_coverage.cpp` (grep-based):

- Every antenna `QMenu ... menu.exec()` site preceded by `menu.setStyleSheet(kPopupMenu)`
- Fails the build if a new antenna menu slips past this invariant

### 8.6 Manual verification matrix

New `docs/architecture/antenna-routing-verification.md` — per-SKU QA checklist (one column per SKU NereusSDR testers have in hand: ANAN-G2, ANAN-100D, ANAN-8000DLE, HL2 + N2ADR, HL2 bare, ANAN-7000DLE if available):

- ANT1 → ANT2 → ANT3 cycles visible on VFO Flag + radio front-panel LED + pcap
- Per-band persistence: change ANT on 20m, switch to 40m, back → retained
- Menu readability on macOS / Windows / Linux (Ubuntu 25.10 GNOME, KDE)
- Aries mode clamps correctly (3M-1)
- HL2 bare: UI hidden, no protocol disturbance
- Disconnect/reconnect preserves state

---

## 9 · Open Questions

1. **`useTxAntForRx` flag.** Thetis `Alex.cs:363-364` has a `TRxAnt` bool that, when true, uses `TxAnt[idx]` for both TX and RX. We have this flag in `AlexController` but it's unwired to any UI. Scope for 3P-I-b to add a checkbox on the Antenna Control tab.

2. **XVTR activation.** Thetis `UpdateAlexAntSelection(band, tx, alex_enabled, xvtr)` takes an `xvtr` bool. NereusSDR has a `Band::XVTR` enum value but no "XVTR active" runtime flag. 3P-I-b introduces `m_xvtrActive` on `RadioModel`, settable from a Setup option (stub — real XVTR integration is its own future phase).

3. **AnvelinaPro3 `bank 17`.** Thetis `networkproto1.c:668-674` sends an extra bank (17) with OC pin extras for ANVELINAPRO3. NereusSDR's `P1RadioConnection::maxBank` already branches on this at line 1301. Verify the antenna bits in bank 0 still apply — no expected impact but the verification matrix must include ANVELINAPRO3.

4. **RX2 antenna (future RX2 phase).** Phase 3F adds a second receiver slice. Open: does RX2 get its own row in the Antenna Control grid, or share RX1's? Leaning toward **share** (single Alex board) with an explicit UI note — but this is a Phase 3F decision, not 3P-I.

5. **HL2 N2ADR companion board antenna routing.** Separate future phase. N2ADR adds TX antenna switching and a dedicated RX input via an on-board RP2040 over I²C address 0x1D — not Alex-bit encoded. Handled via a new `Hl2CompanionController` that subscribes to `AlexController::antennaChanged` when `IoBoardHl2` reports a companion present. Out of scope here.

---

## 10 · References

### Thetis sources (all `v2.10.3.13 @501e3f5`)
- `Project Files/Source/Console/HPSDR/Alex.cs:30-106` — AlexController state model
- `Project Files/Source/Console/HPSDR/Alex.cs:299-413` — UpdateAlexAntSelection logic
- `Project Files/Source/Console/HPSDR/NetworkIOImports.cs:336` — `SetAntBits` P/Invoke
- `Project Files/Source/Console/setup.cs:6157-6286` — per-SKU UI visibility branches
- `Project Files/Source/Console/setup.cs:19832-20404` — per-HPSDRModel setup branches
- `Project Files/Source/Console/console.cs:14928, 29062, 29113` — UpdateAlexAntSelection call sites
- `Project Files/Source/ChannelMaster/networkproto1.c:446-471` — P1 bank 0 C&C encoding
- `Project Files/Source/enums.cs:109-131` — HPSDRModel enum

### NereusSDR files touched
- **Added:** `src/core/SkuUiProfile.{h,cpp}`, `src/core/AntennaLabels.{h,cpp}`, `src/gui/styles/PopupMenuStyle.h`
- **Extended:** `src/core/BoardCapabilities.h` (+3 fields), `src/core/BoardCapabilities.cpp` (kTable rows), `src/core/HardwareProfile.{h,cpp}` (firmware-refined SKU), `src/core/RadioConnection.h` (setAntennaRouting), `src/models/RadioModel.{h,cpp}` (applyAlexAntennaForBand), `src/models/SliceModel.{h,cpp}` (refreshFromAlex)
- **Refactored:** all 11 UI surfaces in §6
- **Already present, referenced as-is:** `src/core/HpsdrModel.h` (HPSDRModel enum + helpers), `src/core/accessories/AlexController.{h,cpp}` (per-band storage)

### External docs
- [HL2 Protocol wiki](https://github.com/softerhardware/Hermes-Lite2/wiki/Protocol) — Alex bit repack into I²C
- [HL2 External Connections](https://github.com/softerhardware/Hermes-Lite2/wiki/External-Connections) — single-SMA topology
- [N2ADR I/O board](https://github.com/jimahlstrom/HL2IOBoard) — companion board interface
- [Aries ATU (F6ITU K_Aries)](https://github.com/F6ITU/K_Aries) — external ATU hardware

### Related NereusSDR docs
- `docs/architecture/radio-abstraction.md` — RadioConnection interface
- `docs/architecture/phase3i-radio-connector-port-design.md` — P1 family port
- `docs/attribution/HOW-TO-PORT.md` — header preservation + inline cite versioning rules
- `CLAUDE.md` — Source-first porting protocol

---

## 11 · Companion Implementation Plans

Each phase gets its own plan file following the phase-plan convention:

- `docs/architecture/phase3p-i-a-core-routing-plan.md` — 3P-I-a step breakdown
- `docs/architecture/phase3p-i-b-rxonly-xvtr-sku-plan.md` — 3P-I-b step breakdown
- `docs/architecture/phase3m-1-mox-aries-addendum.md` — 3M-1 piece (added alongside TX plan)

These are produced by the `superpowers:writing-plans` skill after this spec is approved.
