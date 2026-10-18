# Phase 3I — Verification Matrix

**Status:** Phase 3I complete, pending hardware smoke tests.
**Branch:** `feature/phase3i-radio-connector-port`
**Commits:** 25 (from b475d5b to this commit)

---

## Automated tests

All 9 test executables pass:

| # | Test | Category | Duration |
|---|---|---|---|
| 1 | `tst_hpsdr_enums` | Enum integer value fidelity | ~0.05 s |
| 2 | `tst_board_capabilities` | Capability registry invariants | ~0.04 s |
| 3 | `tst_radio_discovery_parse` | P1+P2 reply parsers | ~0.04 s |
| 4 | `tst_p1_wire_format` | ep2 compose + ep6 parse + board quirks | ~0.04 s |
| 5 | `tst_p1_loopback_connection` | Live RX path via P1FakeRadio | ~0.25 s |
| 6 | `tst_reconnect_on_silence` | Watchdog + bounded retries | ~49 s |
| 7 | `tst_connection_panel_saved_radios` | AppSettings radios/* schema | ~0.14 s |
| 8 | `tst_hardware_page_capability_gating` | Per-board tab visibility | ~0.10 s |
| 9 | `tst_hardware_page_persistence` | Per-MAC settings round-trip | ~0.10 s |

Full suite: ~50 seconds (dominated by the reconnect contract test).

---

## Hardware smoke checklist

Run each check against real radios on hand. Mark `[ ]` → `[x]` when confirmed.

### Hermes Lite 2 (HermesLite, P1)
- [ ] Appears in discovery list within 2 seconds of clicking Start Discovery
- [ ] Connects, state flips to Connected
- [ ] Firmware version displayed in Radio Info tab ≥ 70
- [ ] Tune RX1 to 14.200 MHz LSB → hear live audio
- [ ] Spectrum + waterfall update in real time
- [ ] Atten slider 0 → 30 dB drops noise floor ~30 dB
- [ ] Hardware Config tabs visible: Radio Info, HL2 I/O Board, Bandwidth Monitor
- [ ] Hardware Config tabs hidden: Antenna/ALEX, OC Outputs, XVTR, PureSignal, Diversity, PA Calibration
- [ ] Disconnect, close app, relaunch → auto-reconnect succeeds

### ANAN-100D / Angelia (P1)
- [ ] Discovers, connects, tunes, hears SSB
- [ ] Both RX1 and RX2 spectra update (2-ADC board)
- [ ] Diversity tab visible with phase/gain sliders
- [ ] PureSignal tab visible (cold, toggle persists)
- [ ] ALEX RX filter per-band grid populates

### ANAN-200D / Orion (P1)
- [ ] Discovers, connects, RX works
- [ ] PA Calibration tab visible with per-band tables
- [ ] Step attenuator cal button shown (hasStepAttenuatorCal = true)

### ANAN-7000DLE or 8000DLE / OrionMKII (P2)
- [ ] Discovers as HPSDRHW::OrionMKII, RX works through existing P2 path
- [ ] Hardware tabs match Saturn's set (ALEX/OC/XVTR/PS/Diversity/PA)

### ANAN-G2 / Saturn (P2) — **regression check**
- [ ] Everything works identically to main branch before Phase 3I
- [ ] No watchdog false-positives from reconnection state machine

### Hermes / ANAN-10 / ANAN-100 (P1)
- [ ] Discovers, connects, RX works
- [ ] Max sample rate shown is 192 kHz (Setup.cs:850-853)
- [ ] ALEX tab present and populated

### Atlas / Metis kit (P1)
- [ ] Discovers (if anyone still has one)
- [ ] Only Radio Info tab shown — no ALEX, OC, XVTR, PS, Diversity, PA, HL2, or BW Monitor

---

## Radios not available for smoke test

Record which boards you couldn't verify on hardware. Known issues found
on unverified boards are logged as follow-ups, not Phase 3I blockers.

- ANAN-10E / 100B / HermesII — unverified
- ANAN-8000DLE — unverified
- AnvelinaPro3 — unverified
- ANAN-G2-1K — unverified
- SaturnMKII — unverified

---

## Known issues from smoke testing

*To be filled in as issues are found during real-hardware verification.*

---

## Phase 3I deferred items (from design §9)

Landing in future phases:

| Item | Target phase |
|---|---|
| TX IQ producer + SSB modulator | TX phase |
| PureSignal feedback DSP + linearization | TX phase |
| HL2 IoBoardHl2 I2C-over-ep2 wire encoding | Phase 3L |
| Bandwidth monitor full port | Phase 3L |
| TCI protocol (logger integration) | Dedicated phase |
| RedPitaya board | Post-ANAN family |
| Sidetone generator (for CW TX) | TX phase |
| Firmware flashing in-app | Dedicated phase |
| Multi-radio simultaneous connection | Not planned |

---

## Migration notes (users upgrading from Phase 3G-8)

- **Settings compatibility:** Additive only. New keys under `radios/*` and
  `hardware/<MAC>/*`. Existing Saturn users keep their current settings
  unchanged.
- **API break:** The legacy `BoardType` enum was replaced by `HPSDRHW`.
  Third-party embedders (if any) need to update their callers. One docs-era
  mistake fixed: `Griffin=2` → `HermesII=2`.
- **errorOccurred signal:** Now carries a `RadioConnectionError` code
  alongside the message. Subscribers need to update their slot signature.
