# Phase 3I-RP: Radio Model Selector + P1 C&C Completion

**Date:** 2026-04-16
**Status:** Approved
**Triggered by:** GitHub Issue #38 (RedPitaya user gets noise + periodic stream drops)

---

## Problem Statement

NereusSDR has two related defects that together make RedPitaya (and potentially
other non-standard Hermes-identifying devices) unusable:

### A) No model override

Discovery returns a board byte (e.g. 0x01 = Hermes), but that byte does not
distinguish between a real Hermes, an ANAN-10, an ANAN-100, or a RedPitaya.
The HPSDR ecosystem has only 6 board bytes for 16+ distinct radio models.
Thetis solves this with a manual model selector (`comboRadioModel` in Setup).
NereusSDR has none -- it takes the board byte at face value, which leads to
wrong ADC count, wrong capabilities, and wrong filter/attenuator configuration.

Thetis `clsHardwareSpecific.cs:178-183` shows the RedPitaya overrides:
- `SetRxADC(2)` -- 2 ADCs, not 1
- `SetMKIIBPF(0)` -- disable MKII BPF for OpenHPSDR compatibility
- `SetADCSupply(0, 50)` -- 50V supply
- `LRAudioSwap(0)` -- no audio swap
- `Hardware = HPSDRHW.OrionMKII` -- force OrionMKII capability profile

### B) Incomplete P1 C&C round-robin

Thetis cycles through 17 C&C banks at ~381 fps via the audio-driven EP2 path
(`networkproto1.c` WriteMainLoop cases 0-17). NereusSDR sends only 3 banks
(general settings, TX freq, RX1 freq) at 40 fps (25ms timer). The remaining
14 banks -- including dither/random, preamp, Alex filters, PA enable,
attenuator, CW keyer, and ADC assignments -- are never sent.

Specific issues in the current `composeCcBank0()`:
- C3 = 0x00: dither OFF, random OFF, preamp OFF, no attenuator bits
- C4: antenna bits zero, duplex not set, diversity not set
- Banks 4-16: never composed, never sent

### Confirmed by support bundle (Issue #38)

Patrick's RedPitaya (Pavel Demin firmware) connects as **P1 Hermes** (not P2):

```
"protocol": 1
"model": "Hermes (ANAN-10/100)"
```

Log pattern shows a repeating cycle:
1. Connect, receive I/Q for ~7 seconds
2. Watchdog triggers: "ep6 silent for 2020 ms"
3. Transition to Error state, 5-second reconnect timer
4. Reconnect succeeds, I/Q resumes for ~7 seconds
5. Repeat

Additionally, "Reconnected -- ep6 stream restored" log line spams 30-80 times
per reconnect cycle (fires per queued ep6 frame instead of once).

---

## Architecture: Two Layers

### Layer 1: HardwareProfile -- Model-driven configuration

A new struct that takes an `HPSDRModel` and produces the complete set of
hardware overrides. Ported from Thetis `clsHardwareSpecific.cs`.

```cpp
struct HardwareProfile {
    HPSDRModel           model;
    HPSDRHW              effectiveBoard;   // overridden board identity
    const BoardCapabilities* caps;         // looked up from effectiveBoard
    int                  adcCount;         // 1 or 2
    bool                 mkiiBpf;          // MKII bandpass filter enable
    int                  adcSupplyVoltage; // 33 or 50
    bool                 lrAudioSwap;      // L/R audio channel swap
};
```

Source mapping from `clsHardwareSpecific.cs:85-184`:

| HPSDRModel | adcCount | mkiiBpf | adcSupply | audioSwap | effectiveBoard |
|------------|----------|---------|-----------|-----------|----------------|
| HERMES | 1 | off | 33 | yes | Hermes |
| ANAN10 | 1 | off | 33 | yes | Hermes |
| ANAN10E | 1 | off | 33 | yes | HermesII |
| ANAN100 | 1 | off | 33 | yes | Hermes |
| ANAN100B | 1 | off | 33 | yes | HermesII |
| ANAN100D | 2 | off | 33 | no | Angelia |
| ANAN200D | 2 | off | 50 | no | Orion |
| ORIONMKII | 2 | on | 50 | no | OrionMKII |
| ANAN7000D | 2 | on | 50 | no | OrionMKII |
| ANAN8000D | 2 | on | 50 | no | OrionMKII |
| ANAN_G2 | 2 | on | 50 | no | Saturn |
| ANAN_G2_1K | 2 | on | 50 | no | Saturn |
| ANVELINAPRO3 | 2 | on | 50 | no | OrionMKII |
| HERMESLITE | 1 | off | 33 | yes | HermesLite |
| REDPITAYA | 2 | off | 50 | no | OrionMKII |

The profile is computed once at connect time and stored on RadioModel. Both
P1RadioConnection and P2RadioConnection read from it instead of doing raw
`BoardCapsTable::forBoard(info.boardType)` lookups.

### Layer 2: P1 C&C full round-robin

Complete `sendCommandFrame()` to cycle through all 17 Thetis C&C banks.
Values for each bank come from HardwareProfile + runtime state.

Current state (3 banks only):
- Subframe 0: always bank 0 (general settings, C3/C4 = 0x00)
- Subframe 1: alternates TX freq (bank 1) / RX1 freq (bank 2)

New design: both subframes advance through the full 17-bank cycle. Each 25ms
tick sends 2 consecutive banks. Full cycle in 9 ticks (~225ms).

### How the layers connect

```
User picks "Red Pitaya" in ConnectionPanel
  -> persisted as radios/<MAC>/modelOverride = 15
  -> on Connect: HardwareProfile computed from HPSDRModel::REDPITAYA
  -> P1RadioConnection reads profile.adcCount, .dither, etc.
  -> composeCcBank0()  uses profile for dither/random/preamp (C3), antenna/duplex/NDDCs (C4)
  -> composeCcBank4()  uses profile for ADC assignments
  -> composeCcBank10() uses profile for Alex filters, PA enable
  -> composeCcBank12() uses profile for RedPitaya-specific attenuation
```

---

## UI: ConnectionPanel Detail Panel

### Layout

When a row is selected in the discovered radios table, a detail panel appears
below the table and above the button strip:

```
+-- Discovered Radios -------------------------------------------------------+
| *  Name       Board    Proto  IP             MAC        In-Use             |
| * Hermes     Hermes    P1    192.168.1.175  AA:BB:..   free               |
+----------------------------------------------------------------------------+

+-- Selected Radio -----------------------------------------------------------+
|  Board: Hermes (0x01)    Protocol: P1    Firmware: 32                      |
|  IP: 192.168.1.175       MAC: AA:BB:CC:DD:EE:F1                            |
|                                                                             |
|  Radio Model: [Red Pitaya       v]                                          |
|               Board reports "Hermes" -- model override applied              |
+-----------------------------------------------------------------------------+

[Start Discovery] [Stop Discovery]   [Add Manually] [Forget]
                                     [Connect] [Disconnect] [Close]
```

### Behavior

- Panel hidden when no row selected. Appears on row click.
- "Radio Model" dropdown populated with models compatible with the discovered
  board byte + protocol. For a P1 Hermes, the list: Hermes, ANAN-10, ANAN-100,
  Red Pitaya, Hermes Lite 2. Compatibility determined by: (a) `boardForModel(m)`
  matches the discovered board byte, OR (b) the model is in a special-case
  list for that board byte. From `NetworkIO.cs:164-171`:
  - Hermes (0x01): HERMES, ANAN10, ANAN100, REDPITAYA, HERMESLITE
  - HermesII (0x02): ANAN10E, ANAN100B, REDPITAYA
  - OrionMKII (0x05): ORIONMKII, ANAN7000D, ANAN8000D, ANVELINAPRO3, REDPITAYA
  - Other board bytes: only the models whose boardForModel() matches.
- Default: auto-guess from board byte. If per-MAC override exists in
  AppSettings, use that instead.
- Dropdown change saves immediately to `radios/<MAC>/modelOverride`.
- Hint line only shown when selected model differs from auto-detection.
- Double-click on table row still connects immediately using persisted model.

### "Radio Setup..." menu item

The existing disabled `Radio > Radio Setup...` menu item is wired up to open
a small dialog showing the same detail panel for the currently-connected radio.
Allows model change post-connect. Reconnect required for changes to take effect
(dialog states this).

---

## Persistence

### Per-MAC storage

One new key added to the existing per-MAC radio storage:

```
radios/<MAC>/modelOverride = "15"   (HPSDRModel int value as string)
```

All other profile values are derived from the model enum at connect time.
Not stored.

### Auto-guess when no override exists

When `modelOverride` is absent for a MAC, the system picks the first
`HPSDRModel` whose `boardForModel()` matches the discovered `HPSDRHW` board
byte. For most radios this is correct. For ambiguous cases (Hermes byte with
multiple possible models), the user sets it once and it sticks.

### Multi-radio support

The existing per-MAC settings infrastructure (`AppSettings::setHardwareValue`,
`hardwareValues`) already provides per-radio profiles. The model override
integrates naturally -- each radio's MAC gets its own model, and the
HardwareProfile computed from that model drives capability lookups,
HardwarePage tab visibility, and C&C configuration for that specific radio.

---

## P1 C&C Round-Robin: Complete Bank Specification

All 17 banks ported from Thetis `networkproto1.c` WriteMainLoop cases 0-17.

### Bank 0 (C0=0x00): General settings
Source: `networkproto1.c:450-471`

| Byte | Content | Current NereusSDR | Fix |
|------|---------|-------------------|-----|
| C0 | XmitBit (MOX) | Correct | -- |
| C1 | SampleRateIn2Bits & 3 | Correct | -- |
| C2 | OC output mask, EER bit | 0x00 | Populate from m_ocOutput |
| C3 | 10dB ATT, 20dB ATT, preamp, dither, random, RX input select | 0x00 | Populate from HardwareProfile + state |
| C4 | Antenna, duplex, NDDCs, diversity | Only NDDCs set | Add duplex=1, antenna bits, diversity |

### Bank 1 (C0=0x02): TX VFO frequency
Source: `networkproto1.c:476-482`. Already implemented correctly.

### Bank 2 (C0=0x04): RX1 VFO (DDC0) frequency
Source: `networkproto1.c:484-494`. Already implemented correctly.

### Bank 3 (C0=0x06): RX2 VFO (DDC1) frequency
Source: `networkproto1.c:497-511`. New. Sends RX2 freq (or TX freq for
PureSignal, or RX1 freq for Orion DDC config).

### Bank 4 (C0=0x1C): ADC assignments + TX step attenuator
Source: `networkproto1.c:517-523`. New.
- C1 = `P1_adc_cntrl & 0xFF` (ADC-to-DDC mapping low byte)
- C2 = `(P1_adc_cntrl >> 8) & 0x3F` (ADC-to-DDC mapping high bits)
- C3 = `adc[0].tx_step_attn & 0x1F` (TX step attenuator)

### Banks 5-9 (C0=0x08..0x10): RX3-RX7 VFO frequencies
Source: `networkproto1.c:525-575`. New. Most set to TX freq or RX1 freq
depending on DDC configuration.

### Bank 10 (C0=0x12): TX drive, mic, Alex HPF/LPF, PA enable
Source: `networkproto1.c:578-591`. New. Critical for reception:
- C1 = TX drive level
- C2 = mic boost/line-in, Apollo filter bits
- C3 = Alex HPF select (13MHz, 20MHz, 9.5MHz, 6.5MHz, 1.5MHz, bypass, 6M preamp) + PA enable bit 7
- C4 = Alex LPF select (30/20, 60/40, 80, 160, 6, 12/10, 17/15)

Alex HPF/LPF values computed from current RX frequency using the Thetis
frequency-to-filter lookup (already partially ported for P2 in
`P2RadioConnection`).

### Bank 11 (C0=0x14): Preamp control, mic settings, step ATT
Source: `networkproto1.c:593-601`. New.
- C1 = per-RX preamp bits, mic TRS/bias/PTT
- C2 = line-in gain, PureSignal run flag
- C3 = user digital outputs
- C4 = ADC0 step attenuator | 0x20 (enable bit)

### Bank 12 (C0=0x16): Step ATT ADC1/2, CW keyer
Source: `networkproto1.c:604-628`. New. **Contains RedPitaya-specific logic:**
```
if (XmitBit && model != REDPITAYA)
    C1 = 0x1F;  // force 31dB on ADC1 during TX
else if (model == REDPITAYA)
    C1 = adc[1].rx_step_attn & 0x1F;  // use actual value
else
    C1 = adc[1].rx_step_attn;
```

### Banks 13-14 (C0=0x1E, 0x20): CW settings
Source: `networkproto1.c:633-646`. New. CW enable, sidetone, RF delay, hang
delay, sidetone frequency.

### Bank 15 (C0=0x22): EER PWM
Source: `networkproto1.c:649-654`. New. EER PWM min/max.

### Bank 16 (C0=0x24): BPF2 filters
Source: `networkproto1.c:657-665`. New. Second BPF filter bank, XVTR enable.

### Bank 17 (C0=0x26): AnvelinaPro3 extra OC pins
Source: `networkproto1.c:668-673`. New. Only sent when model is AnvelinaPro3.
Skipped for all other models (cycle is 0..16 instead of 0..17).

---

## Runtime State Members

New members on P1RadioConnection, set via setters, read by composeCcBank
functions:

| Member | Type | Source | Default |
|--------|------|--------|---------|
| `m_profile` | `HardwareProfile` | Computed at connect | From model |
| `m_preamp[3]` | `bool` | SliceModel / setup | From profile |
| `m_stepAttn[3]` | `int` | SliceModel / setup | 0 |
| `m_dither[3]` | `bool` | HardwareProfile | true (from profile) |
| `m_random[3]` | `bool` | HardwareProfile | true (from profile) |
| `m_txDrive` | `int` | TxApplet / setup | 0 |
| `m_paEnabled` | `bool` | HardwareProfile | From profile |
| `m_ocOutput` | `quint8` | Setup OC tab | 0 |
| `m_duplex` | `bool` | Always on | true |
| `m_diversity` | `bool` | Setup | false |
| `m_alexHpf` | `quint8` | Computed from freq | 0 |
| `m_alexLpf` | `quint8` | Computed from freq | 0 |
| `m_cwConfig` | `CwConfig` | CW applet / setup | Defaults |
| `m_micConfig` | `MicConfig` | Setup | Defaults |
| `m_adcCtrl` | `quint16` | HardwareProfile + DDC config | From profile |

---

## Bug Fixes Included

### Reconnect log spam

In the ep6 receive path, add `m_reconnectedLogged` guard:
- Set to `true` on first "Reconnected" log after a reconnect
- Cleared on disconnect or error transition
- Prevents 30-80 duplicate log lines per reconnect cycle

### P2 connection: board capability override

`P2RadioConnection::connectToRadio()` currently does
`m_caps = &BoardCapsTable::forBoard(info.boardType)`. Change to read from
HardwareProfile: `m_caps = profile.caps`. This ensures a RedPitaya connecting
via P2 (possible with different firmware) gets OrionMKII capabilities.

---

## Files Modified

| File | Changes |
|------|---------|
| `src/core/HardwareProfile.h` (new) | HardwareProfile struct + factory function |
| `src/core/HardwareProfile.cpp` (new) | `profileForModel()` -- port of clsHardwareSpecific.cs |
| `src/core/P1RadioConnection.h` | New runtime state members, composeCcBank declarations |
| `src/core/P1RadioConnection.cpp` | Full 17-bank round-robin, reconnect log spam fix |
| `src/core/P2RadioConnection.cpp` | Use HardwareProfile for caps lookup |
| `src/core/RadioDiscovery.h` | Add `HPSDRModel modelOverride` field to RadioInfo |
| `src/core/AppSettings.cpp/h` | Read/write `modelOverride` per-MAC |
| `src/gui/ConnectionPanel.h/cpp` | Detail panel with model dropdown |
| `src/gui/MainWindow.cpp` | Wire "Radio Setup..." menu item |
| `src/models/RadioModel.h/cpp` | Store HardwareProfile, pass to connections |
| `src/gui/setup/HardwarePage.cpp` | Use profile.effectiveBoard for tab visibility |

---

## Out of Scope

- TX I/Q streaming (Phase 3I-TX)
- PureSignal DDC feedback routing (Phase 3I-PS)
- HL2 I/O Board I2C commands (Phase 3L)
- Full Alex filter auto-compute for all bands (partial: only the lookup table,
  not the full Thetis `console.cs:6830-7234` filter state machine)
- CW keyer UI wiring (banks 13-14 will send defaults; CW applet wiring is
  future work)

---

## Verification

1. Connect to an ANAN-G2 (Saturn/P2) -- confirm no regression, same behavior
2. Connect to a Hermes Lite 2 (P1) -- confirm full C&C cycle, no stream drops
3. With a RedPitaya pcap from Patrick: compare NereusSDR's EP2 output against
   Thetis's EP2 output byte-for-byte for banks 0, 10, 11, 12
4. Model selector: set a radio to "Red Pitaya", close app, reopen -- confirm
   override persists and is applied on auto-reconnect
5. Multi-radio: save two radios with different models, switch between them,
   confirm each gets its own profile
