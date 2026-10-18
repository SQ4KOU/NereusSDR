# Antenna Routing — Manual Verification Matrix

**Phase:** 3P-I-a (initial matrix; 3P-I-b / 3M-1 extend)
**Spec:** [antenna-routing-design.md](antenna-routing-design.md)
**Plan:** [phase3p-i-a-core-routing-plan.md](phase3p-i-a-core-routing-plan.md)

Run this checklist on every SKU available in the lab before cutting a
release that includes antenna-routing changes. Check each box and
initial with operator callsign + date. Paste pcap snippets and
screenshots into the PR description when closing out verification for
each SKU.

---

## Covered SKUs

| SKU | Owner | Last verified |
|---|---|---|
| ANAN-G2 | KG4VCF | — |
| ANAN-100D | g0orx | — |
| ANAN-8000DLE | g0orx | — |
| HL2 + N2ADR | — | — |
| HL2 (bare) | — | — |
| ANAN-7000DLE | — | — |

---

## Per-SKU checklist

For each SKU in the table above:

### VFO Flag — Blue/Red ANT buttons
- [ ] On Alex boards: both buttons visible. On HL2/Atlas: both hidden.
- [ ] Clicking Blue opens a dropdown with items visible on **macOS,
      Windows, Linux (Ubuntu 25.10 GNOME), Linux (KDE)**. Dark text on
      dark background is a FAIL (this is issue #98 — do not regress).
- [ ] Selecting ANT2 sets the button label to "ANT2".
- [ ] Radio front-panel LED (or pcap) shows the antenna relay actually
      switched to ANT2.
- [ ] Disconnecting + reconnecting restores ANT2 (per-MAC persistence).

### RxApplet header buttons
- [ ] Same 5 checks as above.
- [ ] Clicking a band button and coming back preserves the ANT selection
      for the original band (per-band persistence).

### Setup → Hardware → Antenna → Antenna Control grid
- [ ] Tab visible on Alex boards, hidden on HL2/Atlas.
- [ ] 14 rows × 3 columns (per Thetis grpAntennaBands).
- [ ] Changing a cell immediately updates the VFO Flag button
      (if current band).
- [ ] Changing a cell and disconnect/reconnect preserves it.

### Spectrum overlay (ANT flyout)
- [ ] ANT flyout RX Ant / TX Ant rows visible on Alex boards, hidden
      on HL2/Atlas.
- [ ] Combo choices populate from BoardCapabilities
      (antennaInputCount).
- [ ] Picking ANT2 in the combo makes the VFO Flag button show "ANT2"
      and pcap shows the relay switch.
- [ ] Crossing a band boundary updates the combo label to match the
      new band's persisted antenna.

### AntennaButtonItem (meter)
- [ ] On Alex boards: clicking Rx1..Tx3 toggles the button `on` state
      and fires `antennaSelected`.
- [ ] On HL2/Atlas: clicking the item has no visible effect and emits
      no `antennaSelected` (verify via a debug log hook or by watching
      the radio-side pcap — no wire traffic).

### Band-change reapply
- [ ] Set 20m=ANT2, 40m=ANT1 via the Setup grid.
- [ ] VFO-tune 14.200 → 7.150. Radio switches relay in real time.
- [ ] Tune back 7.150 → 14.200. Relay switches back.

### Protocol verification (pcap)
- [ ] P1 boards: bank 0 C4 bits 0-1 match the selected antenna
      (0=ANT1, 1=ANT2, 2=ANT3).
- [ ] P2 boards: Alex0 bytes 1432-1435 have bit 24/25/26 set correctly
      for RX; Alex1 bytes 1428-1431 for TX.
- [ ] HL2: antenna bits in the EP2 frame are always zero (HL2 has no
      Alex).

### Regression checks
- [ ] Existing VFO Flag freq/mode/AGC controls still work.
- [ ] RxApplet mode / filter / AGC controls unaffected.
- [ ] Spectrum + audio streaming uninterrupted during ANT switch.
- [ ] VAX combo on the spectrum overlay still works (not broken by the
      new rxAntConn / txAntConn in bindToSliceZero).

---

## Known out-of-scope items (3P-I-b / 3M-1)

These are **expected to be non-functional** in 3P-I-a and are covered by
later phases:

- RX-only inputs (RX1 / RX2 / XVTR) — the SpectrumOverlayPanel combos
  only write through to `rxAntenna` / `txAntenna`, not `rxOnlyAntenna`.
  3P-I-b adds the RX-only path.
- SKU-specific port labels (`EXT1` / `EXT2` / `BYPS` on ANAN-7000+) —
  3P-I-b overlays the model-specific label map on top of
  `AntennaLabels`.
- MOX-coupled reapply on TX engage — 3M-1 piece.
- Aries external-ATU clamp (forces trx_ant=1) — 3M-1 piece.

Do not mark items above as FAIL for this phase.

---

## Recording results

When verifying, record in the PR comment:

```
### <SKU> — <date> — <operator callsign>

VFO Flag:            PASS / FAIL (detail)
RxApplet:            PASS / FAIL
Setup grid:          PASS / FAIL
Spectrum overlay:    PASS / FAIL
AntennaButtonItem:   PASS / FAIL
Band-change reapply: PASS / FAIL
Pcap:                PASS / FAIL (attach pcap snippet or annotated screenshot)
Regression:          PASS / FAIL
```

Attach pcap files or Wireshark screenshots for the protocol
verification block. PR reviewers can skip protocol-detail review if a
clean pcap is attached; without one they will ask for one before merge.

---

## §7 — RX-Only + XVTR + Ext-on-TX (Phase 3P-I-b)

Per-SKU matrix. Mark each cell ✓ / ✗ / N/A on the test bench.

| # | Step | ANAN-G2 | ANAN-100D | ANAN-8000DLE | Hermes | HL2 bare |
|---|------|:---:|:---:|:---:|:---:|:---:|
| 1 | Setup → Antenna Control RX-only column header matches SKU: "BYPS/EXT1/XVTR" (G2 / 7000D / 8000DLE / G2-1K / ANVELINAPRO3 / REDPITAYA), "EXT2/EXT1/XVTR" (100/100B/100D/200D/ORIONMKII), "RX1/RX2/XVTR" (HERMES / ANAN10) | | | | | N/A (tab hidden) |
| 2 | Selecting RX-only = RX1 / EXT1 / BYPS on 20m and tuning to 20m produces visible bit on captured pcap (P1 C3 bit 5; P2 Alex0 bit 10) | | | | | N/A |
| 3 | Setup → Antenna Control TX-bypass checkbox visibility matches SKU (see T1 table in plan) — 8000DLE / G2-1K / ANAN10 show zero TX-bypass checkboxes; HERMES shows all three Ext/RxOut trio + Use TX-for-RX | | | | | N/A |
| 4 | Setup → Antenna → Alex-2 Filters sub-tab hidden on HERMES/ANAN100/ANAN10 (`caps.hasAlex2=false`); visible on ORIONMKII/Saturn/SaturnMKII | | | | | N/A |
| 5 | VFO Flag BYPS (grey 3rd) button visible per SKU: HERMES/ANAN100-class yes; ANAN10/ANAN8000D/G2/G2-1K/ANVELINAPRO3/REDPITAYA no | | | | | N/A |
| 6 | VFO Flag BYPS toggle syncs bidirectionally with Setup → Antenna Control "RX Bypass on TX" checkbox | | | N/A | | N/A |
| 7 | Ext1-on-TX checkbox toggle with MOX off → no change on wire (isTx=false path); with MOX on → `rxOnlyAnt=2` bit on wire (P1 C3 bit 6 / P2 Alex0 bit 9). MOX path is 3M-1 scope — record N/A if MOX not yet wired. | N/A | N/A | N/A | N/A | N/A |
| 8 | xvtrActive = ON via Setup option, rxOnlyAnt=3 on 20m: pcap shows bit 5+6 (P1) / bit 8 (P2); xvtrActive = OFF: pcap shows all-zero for bits 5-7 (P1) / bits 8-11 (P2) | | | | | N/A |
| 9 | rxOutOverride toggle with `rxOnlyAnt != 0`: `trxAnt=4` on RX (P2 Alex0 encoding deferred; verify via qCDebug log), rxOut relay off | | | | | N/A |
| 10 | Rapid band change 20m → 40m → 20m with different RX-only per band produces correct bits on wire at each band, no flicker, no spurious intermediate writes | | | | | N/A |
| 11 | Disconnect + reconnect preserves per-band RX-only state + all 5 TX-bypass flag states | | | | | N/A |
| 12 | AntennaButtonItem meter shows SKU-labeled RX-only buttons (indices 3-5) when placed in a container | | | | | N/A |

**Bench notes:**

- **HL2 bare:** verify the Alex tab is outright hidden (not just grayed). Confirm no spurious RX-only bits on the P1 wire — capture an HL2 pcap and grep: `prbpfilter->_Rx_1_In / _Rx_2_In / _XVTR_Rx_In / _Rx_1_Out` should all remain zero. The orchestration emits `{0,0,0,false,false}` via `caps.hasAlex=false` early-return.
- **ANAN8000DLE:** verify BYPS button doesn't appear on VFO Flag despite `caps.hasRxBypassRelay=true` — the `SkuUiProfile.hasRxBypassUi=false` must override. Setup → Antenna Control also shows zero TX-bypass checkboxes (all 3 Ext/RxOut hidden + Disable RX Bypass hidden).
- **ANAN-G2:** `SkuUiProfile` shows Ext1-on-TX + Ext2-on-TX but NOT RxOutOnTx + DisableRXOut. Setup grid shows "BYPS/EXT1/XVTR" column headers. VFO Flag BYPS button hidden. This is deliberate — the G2 uses bits 8-11 on Alex0 directly; the bypass-out relay is controlled via the EXT2-on-TX tooltip variant "RX Bypass during transmit".
- **xvtrActive** is a session-scoped NereusSDR-native flag (not persisted across restart); there's no Thetis equivalent as a state flag (Thetis passes `xvtr` as a method argument). Setup option surface for `xvtrActive` is deferred — today it's toggleable only via test / API.
- **MOX-coupled reapply** (Thetis Alex.cs:339-347 isTx branch) is wired in RadioModel T6 but the `isTx=true` trigger is not fired until Phase 3M-1 adds the `TransmitModel::moxChanged` hook. Integration tests via `tst_antenna_routing_model::ext*_on_tx_maps_rxOnly_*` exercise the composition with `applyAlexAntennaForBand(band, /*isTx=*/true)` called directly.

## §8 — Protocol bit-layout reference (post-3P-I-b)

P1 bank 0 C3 bits — from `networkproto1.c:453-468`:

| Bit | Field | Wire meaning |
|:---:|---|---|
| 0 | `_10_dB_Atten` | 10 dB attenuator |
| 1 | `_20_dB_Atten` | 20 dB attenuator |
| 2 | `rx[0].preamp` | preamp |
| 3 | `adc[0].dither` | dither |
| 4 | `adc[0].random` | random |
| 5 | `_Rx_1_In` | rxOnlyAnt = 1 → RX1 |
| 6 | `_Rx_2_In` | rxOnlyAnt = 2 → RX2 |
| 5+6 | `_XVTR_Rx_In` | rxOnlyAnt = 3 → XVTR |
| 7 | `_Rx_1_Out` | rxOut → RX-Bypass-Out relay |

P1 bank 0 C4 bits 0-1 encode `trxAnt` (1 → `0b00`, 2 → `0b01`, 3 → `0b10`) per `networkproto1.c:463-468`.

P2 Alex0 32-bit register — from `network.h:263-307`:

| Bit | Field | Wire meaning |
|:---:|---|---|
| 8 | `_XVTR_Rx_In` | rxOnlyAnt = 3 → XVTR |
| 9 | `_Rx_2_In` | rxOnlyAnt = 2 → RX2 (labeled EXT1) |
| 10 | `_Rx_1_In` | rxOnlyAnt = 1 → RX1 (labeled EXT2) |
| 11 | `_Rx_1_Out` | rxOut → K36 RL17 bypass relay |
| 24-26 | `_ANT_1`/`_ANT_2`/`_ANT_3` | trxAnt |
| 27 | `_TR_Relay` | T/R relay (NOT an RX-only bit — do not set from antenna path) |

P2 Alex1 bits 24-26 encode `txAnt` via `_TXANT_1`/`_TXANT_2`/`_TXANT_3`.

**Design-doc correction (2026-04-23):** `antenna-routing-design.md` §5.4 originally said "P2 Alex0 bits 27-30" for RX-only — that was wrong (bit 27 is `_TR_Relay`). Corrected in the T5 implementation to use bits 8-11. The design doc will be amended in a follow-up when the next edit pass lands; this verification doc is authoritative until then.
