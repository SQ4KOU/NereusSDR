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
