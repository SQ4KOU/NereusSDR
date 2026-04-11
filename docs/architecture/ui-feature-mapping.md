# NereusSDR UI Feature Mapping

**Thetis + AetherSDR → NereusSDR Complete Feature Map**

Date: 2026-04-11
Status: Design Decisions Finalized (interactive session)

---

## 1. Target Layout Overview

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ Menu Bar: File │ Radio │ Settings │ View │ DSP │ Tools │ Help               │
├──────┬───────────────────────────────────────────────────┬───────────────────┤
│      │                                                   │  Toggle Row       │
│ Left │         Spectrum + Waterfall (Panafall)           │  [Rx][Tx][Ph][Eq] │
│ Over │                                                   │  [Cat][Tun][Met]  │
│ lay  │  ┌─VFO Flag A──────┐                             ├───────────────────┤
│      │  │ ANT1  USB  2.9K │                             │  ┌─SMeterWidget─┐ │
│[◀]   │  │ 14.225.000 MHz  │                             │  │  S-Meter     │ │
│[+RX] │  │ [🔊][DSP][Mode] │                             │  └──────────────┘ │
│[+TNF]│  └─────────────────┘                             │  ┌─RxApplet────┐  │
│[Band]│                                                   │  │ AGC/SQL/Pan │  │
│[ANT] │  ┌─VFO Flag B──────┐                             │  │ RIT/XIT/Step│  │
│[DSP] │  │ ANT2  CWU  500  │                             │  └─────────────┘  │
│[Disp]│  │  7.035.000 MHz  │                             │  ┌─TxApplet────┐  │
│[DAX] │  └─────────────────┘                             │  │ PWR/SWR/MOX │  │
│[ATT] │                                                   │  │ TUNE/PS-A   │  │
│[MNF] │                                                   │  └─────────────┘  │
│      │                                                   │  ┌─PhoneCwApp──┐  │
│      │  ┌─BandStackPanel (vertical bookmark strip)──┐   │  │ MIC/COMP/CW │  │
│      │  │ 14.074 FT8  ►                             │   │  └─────────────┘  │
│      │  │ 14.230 Rag                                │   │  ┌─PhoneApplet─┐  │
│      │  │ 14.035 CW                                 │   │  │ VOX/DEXP/FL │  │
│      │  └───────────────────────────────────────────┘   │  └─────────────┘  │
│      │                                                   │  ┌─EqApplet────┐  │
│      │  [S] [B] [-] [+]  ← waterfall zoom buttons      │  │ 8-band EQ   │  │
│      │                                                   │  └─────────────┘  │
├──────┴───────────────────────────────────────────────────┼───────────────────┤
│ Status Bar (double-height, 46px)                                            │
│ Left: [BS][+PAN][☰][TNF][CWX][DVK][FDX] Radio Info                        │
│ Center: STATION: VK3XYZ                                                     │
│ Right: [CAT][TCI][N1MM] PA:13.8V CPU:12% [TX] 12:34 UTC  (configurable)   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Design Decisions Summary

All 17 feature areas decided in interactive design session:

| # | Feature Area | Decision | Source Pattern |
|---|-------------|----------|----------------|
| 1 | Left-Side Overlay | AetherSDR 7-button + ATT + MNF | AetherSDR + Thetis |
| 2 | Right-Side AppletPanel | Applets inside ContainerManager | AetherSDR + NereusSDR |
| 3 | VFO/Slice Controls | AetherSDR floating VFO flag | AetherSDR (already done) |
| 4 | Audio Routing (DAX) | Native virtual audio devices per platform | Reimagined |
| 5 | DSP Toggles | WDSP core + client neural NR with engine callouts | Both + new |
| 6 | TX Controls | 3-applet split + Thetis extras | AetherSDR + Thetis |
| 7 | CW | PhoneCwApplet + APF in VFO + CWX panel | Both |
| 8 | FM | VFO quick controls + PhoneCwApplet FM sub-panel | Both |
| 9 | Band Selection | Overlay grid + SWL + stacking + BandStackPanel | Both |
| 10 | Setup Dialog | Sidebar + search + inline sync | Reimagined |
| 11 | Status Bar | AetherSDR double-height + configurable slots | AetherSDR + Thetis |
| 12 | Meters | SMeterWidget + HGauge in applets + MeterWidget custom | Both + NereusSDR |
| 13 | Display Modes | Panafall default + diagnostic floating windows | Both |
| 14 | Equalizer | EqApplet + presets + response curve | AetherSDR + new |
| 15 | PureSignal | TxApplet toggle + PSApplet + AmpView float | Thetis |
| 16 | CAT/TCI/Integration | Layered CommandRouter + protocol frontends | Reimagined |
| 17 | Memory/Band Stacking | BandStackPanel + cycle-on-click + expandable named | Both |

---

## 3. Left-Side Spectrum Overlay

**Pattern:** AetherSDR SpectrumOverlayMenu — collapsible vertical button column, top-left of each SpectrumWidget. One instance per panadapter.

| Button | Sub-panel | Controls | Source |
|--------|-----------|----------|--------|
| **◀** | — | Toggle collapse/expand | AetherSDR |
| **+RX** | — | Add receive slice on this pan | AetherSDR |
| **+TNF** | — | Add tracking notch filter at cursor | AetherSDR |
| **Band** | 6×3 grid | HF: 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6. SWL page: L/MW, 120m-11m. WWV, GEN, XVTR | AetherSDR + Thetis SWL |
| **ANT** | 180px panel | RX antenna combo, RF gain slider (-8 to +32 dB), WNB toggle + level slider | AetherSDR |
| **DSP** | 200px panel | **WDSP:** NR, NB, SNB, ANF, BIN, MNF (with level sliders where applicable). **Client:** NR2, RNN, DFNR, BNR (conditional, greyed if unavailable). Engine callout labels. | Both |
| **Display** | panel | Most-used: color scheme, color gain, black level, ref level, dyn range, fill + alpha, CTUN toggle | AetherSDR |
| **DAX** | 140px panel | DAX Ch combo (Off, 1-4), IQ Ch combo (None, 1-2) | AetherSDR |
| **ATT** | inline | Step attenuator value / preamp selector | Thetis |
| **MNF** | — | Manual notch filter enable + add | Thetis |

**Waterfall zoom buttons** (bottom-left of waterfall): `[S] [B] [-] [+]`
- S = Segment zoom (zoom to visible signals)
- B = Band zoom (zoom to full band)
- `-` = zoom out, `+` = zoom in

**Less-used display settings** (menu bar → Settings → Display):
FFT size, window function, FPS, averaging mode, grid options, waterfall line duration, background image, noise floor position, auto-black.

---

## 4. Right-Side AppletPanel

**Pattern:** AetherSDR AppletPanel as content inside NereusSDR ContainerManager. Scrollable, drag-reorderable, individually floatable via FloatingContainer.

**Default layout (top to bottom):**

### 4.1 Toggle Row
Row of toggle buttons controlling applet visibility + slice selectors:
`[Rx] [Tx] [PhCw] [Ph] [Eq] [Cat] [Tun] [Met] [PS]` + TX/RX selector combo + Lock button

### 4.2 SMeterWidget (always visible)
Dedicated AetherSDR port — analog S-meter gauge.
- RX modes: S-Meter, S-Meter Peak, Sig Avg, ADC L, ADC R (Thetis selector)
- TX modes: Power, SWR, Level, Compression
- Peak hold with configurable decay
- Dynamic power scale (barefoot / amplifier)

### 4.3 RxApplet
Per-slice RX controls beyond what VfoWidget provides:

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| Slice badge (A/B/C/D) | Label | grpVFOA/B | ❌ |
| Lock button | Toggle | chkVFOLock | ✅ (VfoWidget) |
| RX/TX ant buttons | Dropdown | toolStripStatusLabelRXAnt/TXAnt | ✅ (VfoWidget) |
| Filter width label | Label | lblFilterWidth | ✅ (VfoWidget) |
| Mode combo | Combo | panelMode radButtons | ✅ (VfoWidget) |
| Filter preset buttons | Grid | panelFilter radFilter1-10 | ✅ (VfoWidget) |
| FilterPassbandWidget | Visual | udFilterHigh/Low + ptbFilterWidth | ❌ |
| AGC combo + threshold slider | Combo+Slider | comboAGC + ptbRF | ✅ (VfoWidget partial) |
| AF gain slider + Mute | Slider+Toggle | ptbRX1AF + chkMUT | ✅ (VfoWidget partial) |
| Audio pan slider | Slider | ptbPanMainRX | ❌ |
| Squelch toggle + slider | Toggle+Slider | chkSquelch + ptbSquelch | ❌ |
| RIT toggle + offset + zero | Toggle+Spin+Btn | chkRIT + udRIT + btnRITReset | 🔲 (VfoWidget stub) |
| XIT toggle + offset + zero | Toggle+Spin+Btn | chkXIT + udXIT + btnXITReset | 🔲 (VfoWidget stub) |
| Step size display + up/down | Label+Btn | txtWheelTune + btnTuneStep* | ❌ |
| DIV (diversity) toggle | Toggle | — | ❌ |

### 4.4 TxApplet
TX power and keying controls:

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| Forward Power gauge | HGauge | picMultiMeterDigital (TX mode) | ❌ |
| SWR gauge | HGauge | picMultiMeterDigital (TX mode) | ❌ |
| RF Power slider (0-100%) | Slider | ptbPWR | ❌ |
| Tune Power slider (0-100%) | Slider | ptbTune | ❌ |
| MOX button | Toggle | chkMOX | ❌ |
| TUNE button | Toggle | chkTUN | ❌ |
| ATU button | Toggle | chkFWCATU | ❌ |
| MEM button | Toggle | — | ❌ |
| TX Profile dropdown | Combo | comboTXProfile | ❌ |
| 2-Tone test | Toggle | chk2TONE | ❌ |
| PS-A toggle + indicators | Toggle+LED | chkFWCATUBypass (PS) | ❌ |
| DUP (full duplex) | Toggle | chkFullDuplex | ❌ |
| xPA indicator | Toggle | chkExternalPA | ❌ |
| SWR protection indicator | LED | — | ❌ |
| Tune mode combo | Combo | comboTuneMode | ❌ |

### 4.5 PhoneCwApplet (3-mode switcher: Phone / CW / FM)

**Phone sub-panel:**

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| Mic level gauge | HGauge | picMultiMeterDigital (MIC mode) | ❌ |
| Compression gauge | HGauge | picMultiMeterDigital (COMP mode) | ❌ |
| Mic profile combo | Combo | — (new) | ❌ |
| Mic source combo | Combo | — (front/rear) | ❌ |
| Mic level slider | Slider | ptbMic | ❌ |
| ACC button | Toggle | — | ❌ |
| PROC button + slider | Toggle+Slider | chkCPDR + ptbCPDR | ❌ |
| DAX button | Toggle | — | ❌ |
| MON button + slider | Toggle+Slider | chkMON | ❌ |

**CW sub-panel:**

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| ALC gauge | HGauge | picMultiMeterDigital (ALC mode) | ❌ |
| CW speed slider (1-60 WPM) | Slider | ptbCWSpeed | ❌ |
| CW pitch + up/down | Label+Btn | udCWPitch | ❌ |
| Delay slider | Slider | udCWBreakInDelay | ❌ |
| Sidetone toggle + slider | Toggle+Slider | chkCWSidetone | ❌ |
| Break-in button (QSK) | Toggle | chkQSK | ❌ |
| Iambic button | Toggle | chkCWIambic | ❌ |
| Firmware keyer | Toggle | chkCWFWKeyer | ❌ |
| CW pan slider | Slider | — | ❌ |

**FM sub-panel:**

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| FM MIC slider | Slider | ptbFMMic | ❌ |
| Deviation (5.0k / 2.5k) | Radio | radFMDeviation5kHz/2kHz | ❌ |
| CTCSS enable + tone combo | Toggle+Combo | chkFMCTCSS + comboFMCTCSS | ❌ |
| Simplex | Toggle | chkFMTXSimplex | ❌ |
| Repeater offset (MHz) | Spinner | udFMOffset | ❌ |
| Offset direction (-/+/Rev) | Toggle | chkFMTXLow/High/Rev | ❌ |
| FM TX Profile | Combo | comboFMTXProfile | ❌ |
| FM Memory | Combo+Nav | comboFMMemory + btnFMMemory* | ❌ |

### 4.6 PhoneApplet (TX audio processing)

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| AM Carrier level slider | Slider | Setup → Transmit | ❌ |
| VOX toggle + level slider | Toggle+Slider | chkVOX + ptbVOX | ❌ |
| VOX delay slider | Slider | — | ❌ |
| DEXP toggle + level slider | Toggle+Slider | chkNoiseGate + ptbNoiseGate | ❌ |
| TX filter Low Cut + up/down | Slider+Btn | udTXFilterLow | ❌ |
| TX filter High Cut + up/down | Slider+Btn | udTXFilterHigh | ❌ |

### 4.7 EqApplet

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| ON button | Toggle | chkRXEQ / chkTXEQ | ❌ |
| RX / TX selector | Toggle pair | — | ❌ |
| 8-band sliders (63-8k Hz) | 8 Sliders | Equalizer form | ❌ |
| 10-band mode switch | Toggle | — (extension) | ❌ |
| Frequency response curve | QPainter | — (new) | ❌ |
| Preset dropdown | Combo | — (new) | ❌ |
| Reset button | Button | — | ❌ |

### 4.8 CatApplet

| Control | Type | Source | Status |
|---------|------|--------|--------|
| CAT TCP enable + per-channel status (A-D) | Toggle+LEDs | AetherSDR CatApplet | ❌ |
| CAT PTY enable + per-channel paths | Toggle+Labels | AetherSDR CatApplet | ❌ |
| TCI enable + port + status | Toggle+Edit+LED | AetherSDR + Thetis | ❌ |
| DAX enable + per-channel meters/gains | Toggle+MeterSliders | AetherSDR CatApplet | ❌ |
| DAX IQ per-channel enable + rate | Toggle+Combo | AetherSDR CatApplet | ❌ |

### 4.9 TunerApplet

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| Fwd Power gauge | HGauge | Aries ATU | ❌ |
| SWR gauge | HGauge | Aries ATU | ❌ |
| Relay position bars (C1/L/C2) | RelayBar | Aries ATU | ❌ |
| TUNE button | Button | Aries ZZTU | ❌ |
| OPERATE/BYPASS/STANDBY | Toggle | Aries ZZOV | ❌ |
| Antenna switch (1/2/3) | Buttons | Aries ZZOA/ZZOC | ❌ |

### 4.10 MeterApplet

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| PA Temperature gauge | HGauge | nudPwrTemp | ❌ |
| Supply Voltage gauge | HGauge | toolStripStatusLabel_Volts | ❌ |
| Fan Speed gauge | HGauge | — | ❌ |

### 4.11 PSApplet (PureSignal Calibration)

| Control | Type | Thetis Source | Status |
|---------|------|---------------|--------|
| Calibrate button | Button | PSForm | ❌ |
| Auto-cal toggle | Toggle | PSForm | ❌ |
| Feedback level gauge | HGauge (color-coded) | PSForm | ❌ |
| Coefficient save/restore | Buttons | PSForm | ❌ |
| Two-tone test button | Button | PSForm | ❌ |
| Status indicators | LEDs | PSForm | ❌ |

---

## 5. Audio Routing — DAX Architecture

**Decision:** Native virtual audio devices on all platforms. DAX-style UI replaces Thetis VAC.

### Platform Backends

| Platform | Technology | Virtual Devices Created |
|----------|-----------|----------------------|
| **Linux** | PipeWire API (~200 LOC) | NereusSDR DAX 1-4 RX/TX + IQ 1-2 |
| **macOS** | Core Audio HAL plugin | NereusSDR DAX 1-4 RX/TX + IQ 1-2 |
| **Windows** | SYSVAD-based kernel driver | NereusSDR DAX 1-4 RX/TX + IQ 1-2 |
| **Windows (interim)** | Bundle VB-Cable + auto-detect | External virtual cable devices |

### Routing Architecture

```
DaxRouter → VirtualAudioBackend (interface)
              ├── PipeWireBackend (Linux)
              ├── CoreAudioHalBackend (macOS)
              ├── SysvadBackend (Windows native)
              └── ExternalDeviceBackend (VB-Cable fallback)
```

### Data Flow

```
RX: Radio → DDC → RxChannel → WDSP → DaxRouter → DAX N RX (virtual device) → WSJT-X
TX: WSJT-X → DAX N TX (virtual device) → DaxRouter → TxChannel → WDSP → Radio
IQ: Radio → DDC → (raw I/Q, before WDSP) → DaxRouter → DAX IQ N → SDR#/HDSDR
```

### Channel Layout

| Endpoint | Type | Purpose |
|----------|------|---------|
| DAX 1-4 RX | Render (speaker) | Decoded audio from assigned slice → external app |
| DAX 1-4 TX | Capture (mic) | TX audio from external app → TxChannel |
| DAX IQ 1-2 | Render (stereo I/Q) | Raw DDC I/Q from assigned slice → SDR panadapter |

### UI Surfaces

- **Overlay DAX sub-panel** (per-pan): DAX Ch combo, IQ Ch combo — quick slice assignment
- **CatApplet** (applet panel): global DAX enable, per-channel meters/gains, IQ channel status
- **Settings → DAX Configuration**: full routing matrix (channel↔device mapping, rates, buffer, latency, advanced options)

---

## 6. CAT/TCI/Integration — CommandRouter Architecture

**Decision:** Layered architecture with protocol-agnostic central API.

```
┌──────────────────────────────────────────────────┐
│                  CommandRouter                    │
│         (~200 typed internal functions)           │
│    setFrequency(), setMode(), setTx(), ...       │
└──┬──────┬──────┬──────┬──────┬───────────────────┘
   │      │      │      │      │
   ▼      ▼      ▼      ▼      ▼
rigctld  TCI   ZZ CAT  MIDI  Kenwood
(4-ch)   v2.0  (serial) map  TS-2000
TCP+PTY  WS+IQ  +TCP         compat
```

### Protocol Frontends (build order)

| # | Protocol | Transport | Purpose | Priority |
|---|----------|-----------|---------|----------|
| 1 | **rigctld** (Hamlib v1) | TCP (4 ports, per-slice) + PTY | Logging/digital software (WSJT-X, N1MM, Log4OM) | P3 |
| 2 | **TCI v2.0** (ExpertSDR3) | WebSocket + binary audio/IQ | Modern integration, audio streaming, spots | P3 |
| 3 | **ZZ CAT** (Thetis extended) | Serial + TCP | Apache Labs accessories (Andromeda, Aries, Ganymede) | P4 |
| 4 | **MIDI mapper** | RtMidi | Hardware controllers (Behringer, DJ controllers) | P4 |
| 5 | **Kenwood TS-2000** compat | Serial + TCP | Legacy logging software | P4 |

### Spot Sources (built-in clients)

| Source | Protocol | Transport | Priority |
|--------|----------|-----------|----------|
| DX Cluster | Telnet | TCP | P3 |
| RBN | Telnet | TCP | P3 |
| WSJT-X | Binary UDP | UDP multicast | P3 |
| POTA | REST JSON | HTTPS | P4 |
| SpotCollector (DXLab) | UDP push | UDP | P4 |
| FreeDV Reporter | Socket.IO v4 | WebSocket | P4 |

All feed into SpotModel → panadapter overlay with DXCC color coding.

### Additional Integrations

| Integration | Source | Priority |
|-------------|--------|----------|
| N1MM+ spectrum sharing | UDP XML broadcast (Thetis port) | P4 |
| Serial PTT/CW | QSerialPort DTR/RTS/CTS/DSR | P3 |
| Andromeda front panel | ZZ CAT with JSON payloads | P4 |
| Aries ATU | ZZ CAT subset | P4 |
| Ganymede PA | ZZ CAT subset | P4 |

---

## 7. Settings Dialog — Sidebar + Search

**Pattern:** Single Settings window with sidebar tree navigation + search bar. All Thetis Setup tabs mapped.

### Sidebar Categories

| Category | Thetis Setup Tabs Mapped | Controls (approx) |
|----------|-------------------------|-------------------|
| **Radio** | General → H/W Select, F/W Set, RX2, Navigation | ~200 |
| **Radio → Hardware** | ADC, Penny/Hermes Ctrl, Alex Filters, Other H/W | ~400 |
| **Radio → Options** | Options 1-3, Startup Options | ~300 |
| **Radio → Calibration** | Calibration, Filters | ~150 |
| **Audio** | Audio Options, Advanced, cmASIO | ~100 |
| **Audio → DAX** | VAC 1 + VAC 2 → reimagined as DAX routing matrix | ~80 |
| **Audio → Recording** | Recording | ~60 |
| **Display** | Display General, RX 1, RX 2, TX | ~350 |
| **DSP → NR/ANF** | NR/ANF, MNF | ~120 |
| **DSP → NB/SNB** | NB/SNB | ~60 |
| **DSP → AGC/ALC** | AGC/ALC | ~80 |
| **DSP → CW** | CW Keyer | ~60 |
| **DSP → AM/SAM** | AM/SAM | ~50 |
| **DSP → FM** | FM | ~40 |
| **DSP → Audio** | DSP Audio | ~70 |
| **DSP → VOX/DEXP** | VOX/DE | ~50 |
| **DSP → CFC** | CFC (Continuous Frequency Compressor) | ~80 |
| **DSP → EER** | EER (Envelope Elimination & Restoration) | ~40 |
| **DSP → Options** | DSP Options | ~100 |
| **Transmit** | Transmit (single page) | ~200 |
| **PA / Watt Meter** | PA Gain, Watt Meter | ~300 |
| **Appearance** | General, RX Display, Gradients, Meter, Meters/Gadgets | ~400 |
| **Appearance → Skins** | Skin Servers, Legacy Items, Collapsible Display | ~100 |
| **Keyboard** | Keyboard shortcuts | ~150 |
| **CAT/TCI** | Serial, Network, Options | ~200 |
| **CAT → Accessories** | User Interface, Andromeda, Multi Meter I/O | ~150 |
| **Profiles** | TX profiles, mic profiles (new, from AetherSDR) | ~50 |
| **Tests** | Test generators, diagnostics | ~200 |

**Inline sync:** Most-used settings also appear in applets/overlays (AGC threshold, NR level, color scheme, etc.) with bidirectional binding.

---

## 8. Status Bar

**Pattern:** AetherSDR double-height (46px) with configurable right-side slots.

### Left Section (action toggles)
| Control | Function | Source |
|---------|----------|--------|
| Band Stack (3 dots) | Open BandStackPanel | AetherSDR |
| +PAN | Add panadapter | AetherSDR |
| ☰ Panel Toggle | Toggle AppletPanel visibility | AetherSDR |
| TNF | Toggle global TNF enable | AetherSDR |
| CWX | Toggle CW keyer panel | Both |
| DVK | Toggle Digital Voice Keyer panel | AetherSDR |
| FDX | Toggle full duplex | Thetis |
| Radio Info | Model + firmware (stacked) | Both |

### Center
Station callsign — double-click to connect/disconnect.

### Right Section (configurable indicators)
| Indicator | Thetis Source | Default Visible |
|-----------|---------------|-----------------|
| CAT Serial status | toolStripStatusLabel_CatSerial | ✅ |
| CAT TCP status | toolStripStatusLabel_CatTCPip | ✅ |
| TCI status | toolStripStatusLabel_TCI | ✅ |
| N1MM status | toolStripStatusLabel_N1MM | ❌ (opt-in) |
| PA Voltage | toolStripStatusLabel_Volts | ✅ |
| PA Current | toolStripStatusLabel_Amps | ✅ |
| TX Inhibit | toolStripStatusLabel_TXInhibit | ✅ |
| CPU usage | toolStripDropDownButton_CPU | ✅ |
| TX indicator | — | ✅ |
| Record/Play | toolStripStatusLabel_play_record | ❌ (opt-in) |
| Timer | toolStripStatusLabel_timer | ❌ (opt-in) |
| UTC Time | toolStripStatusLabel_UTCTime | ✅ |
| Date | toolStripStatusLabel_Date | ✅ |
| Local Time | toolStripStatusLabel_LocalTime | ✅ |

Configurable via Settings → Appearance → Status Bar.

---

## 9. Menu Bar

Combined from both codebases:

| Menu | Items |
|------|-------|
| **File** | Quit (Ctrl+Q) |
| **Radio** | Connect... (Ctrl+K), Disconnect, Protocol Info |
| **Settings** | Radio Setup..., Display Settings..., DSP Settings..., Keyboard Shortcuts..., Profiles... |
| **View** | Applet Panel (toggle), Band Plan (submenu: Off/S/M/L), Pan Layout (1/2v/2h/2x2/12h), Single-Click Tune, UI Scale (75-200%), Minimal Mode (Ctrl+M) |
| **DSP** | PureSignal (opens PSApplet focus), AmpView (floating), Equalizer (opens EqApplet focus), CWX (toggle panel) |
| **Tools** | Database Manager, Memory Channels, N1MM+ Spectrum, Support/Diagnostics |
| **Help** | About NereusSDR, What's New, Keyboard Shortcuts (Ctrl+/) |

---

## 10. Thetis Console Controls — Complete Mapping

Every Thetis front-panel control mapped to its NereusSDR home:

### VFO Controls

| Thetis Control | NereusSDR Location | Status |
|----------------|-------------------|--------|
| txtVFOAFreq (frequency display) | VfoWidget frequency row | ✅ |
| txtVFOBFreq | VfoWidget (slice B) | ✅ |
| chkVFOLock (A/B lock) | VfoWidget lock button | ✅ |
| chkVFOSplit (split) | VfoWidget / status bar FDX | ❌ |
| btnVFOSwap (A↔B) | RxApplet or keyboard shortcut | ❌ |
| btnVFOAtoB / btnVFOBtoA | RxApplet or keyboard shortcut | ❌ |
| chkVFOSync | Settings → Radio → Options | ❌ |
| txtWheelTune (step display) | RxApplet step size | ❌ |
| btnTuneStepChange (±) | RxApplet step size up/down | ❌ |
| chkRIT / udRIT / btnRITReset | RxApplet RIT section | 🔲 (VfoWidget stub) |
| chkXIT / udXIT / btnXITReset | RxApplet XIT section | 🔲 (VfoWidget stub) |
| btnIFtoVFO | RxApplet or keyboard shortcut | ❌ |
| btnZeroBeat | RxApplet or keyboard shortcut | ❌ |

### Mode Buttons (12 modes)

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| radModeLSB..radModeDRM (12 buttons) | VfoWidget Mode tab combo + overlay | ✅ |
| RX2 mode buttons (12 duplicate) | VfoWidget (slice B) Mode tab | ✅ |

### Band Buttons

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| radBand160..radBand6 (HF, 15 buttons) | Overlay Band sub-panel HF grid | ❌ |
| radBandVHF0..VHF13 (14 XVTR) | Overlay Band sub-panel XVTR page | ❌ |
| radBandGEN0..GEN13 (14 SWL) | Overlay Band sub-panel SWL page | ❌ |
| Band stacking (3 per band, cycling) | BandStackPanel + cycle-on-click | ❌ |

### Filter Buttons

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| radFilter1..10 (10 presets) | VfoWidget Mode tab filter buttons | ✅ |
| radFilterVar1/Var2 | VfoWidget Mode tab | ❌ |
| udFilterHigh/Low | VfoWidget (drag on spectrum) | ✅ |
| ptbFilterWidth/Shift | RxApplet FilterPassbandWidget | ❌ |

### DSP Controls

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| chkNR | VfoWidget DSP tab + overlay DSP | ✅ |
| chkNB | VfoWidget DSP tab + overlay DSP | ✅ |
| chkDSPNB2 (SNB) | Overlay DSP sub-panel | ❌ |
| chkANF | VfoWidget DSP tab + overlay DSP | ✅ |
| chkBIN | Overlay DSP sub-panel | ❌ |
| chkMUT | RxApplet mute button | ❌ |
| chkTNF (MNF enable) | Overlay MNF button | ❌ |
| btnTNFAdd (+MNF) | Overlay +TNF button | ❌ |

### TX Controls

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| chkMOX | TxApplet MOX | ❌ |
| chkTUN | TxApplet TUNE | ❌ |
| chk2TONE | TxApplet 2-Tone | ❌ |
| chkMON | PhoneCwApplet MON | ❌ |
| ptbPWR (drive) | TxApplet RF Power slider | ❌ |
| ptbTune (tune power) | TxApplet Tune Power slider | ❌ |
| ptbMic (mic gain) | PhoneCwApplet mic slider | ❌ |
| chkVOX + ptbVOX | PhoneApplet VOX | ❌ |
| chkNoiseGate + ptbNoiseGate (DEXP) | PhoneApplet DEXP | ❌ |
| chkCPDR + ptbCPDR (compressor) | PhoneCwApplet PROC | ❌ |
| comboTXProfile | TxApplet TX Profile | ❌ |
| udTXFilterHigh/Low | PhoneApplet TX filter | ❌ |
| chkRXEQ / chkTXEQ | EqApplet ON + RX/TX selector | ❌ |

### Audio Controls

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| ptbAF (master AF) | RxApplet or Settings | ❌ |
| ptbRX1AF / ptbRX2AF | VfoWidget Audio tab AF slider | ✅ |
| comboAGC | VfoWidget Audio tab AGC combo | ✅ |
| ptbRF (AGC-T) | RxApplet AGC threshold | ❌ |
| comboPreamp / udRX1StepAttData | Overlay ATT button | ❌ |
| chkSquelch + ptbSquelch | RxApplet squelch | ❌ |
| ptbPanMainRX / ptbPanSubRX | RxApplet audio pan slider | ❌ |

### CW Controls

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| ptbCWSpeed | PhoneCwApplet CW speed | ❌ |
| udCWPitch | PhoneCwApplet CW pitch | ❌ |
| chkCWSidetone | PhoneCwApplet CW sidetone | ❌ |
| chkCWIambic | PhoneCwApplet CW iambic | ❌ |
| chkCWFWKeyer | PhoneCwApplet firmware keyer | ❌ |
| chkQSK + udCWBreakInDelay | PhoneCwApplet break-in | ❌ |
| chkCWAPFEnabled + APF controls | VfoWidget DSP tab APF section | ❌ |
| CWX macro keyer | CWX collapsible left panel | ❌ |

### Meters

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| picMultiMeterDigital (RX) | SMeterWidget (AppletPanel top) | 🔲 (MeterWidget partial) |
| comboMeterRXMode | SMeterWidget mode selector | ❌ |
| comboMeterTXMode | TxApplet gauges (contextual) | ❌ |
| RX2 meter | SMeterWidget (per-slice) | ❌ |

### Display Controls

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| comboDisplayMode (12 modes) | Panafall default, diagnostic floats via View menu | ✅ (partial) |
| ptbDisplayPan / ptbDisplayZoom | Spectrum drag + Ctrl+scroll + zoom buttons | ✅ |
| chkDisplayAVG / chkDisplayPeak | Overlay Display sub-panel | 🔲 |
| chkFWCATU (CTUN) | Overlay Display sub-panel CTUN toggle | ✅ |

### Multi-RX / SubRX

| Thetis | NereusSDR Location | Status |
|--------|-------------------|--------|
| chkEnableMultiRX (SubRX) | +RX overlay button (adds slice) | ❌ |
| chkRX2 (RX2 enable) | +RX or View → Add Panadapter | ❌ |
| ptbRX0Gain / ptbRX1Gain | RxApplet AF gain per slice | ❌ |
| chkPanSwap | RxApplet or keyboard shortcut | ❌ |

---

## 11. Port Priority Matrix

| Priority | Category | Features | Phase Target |
|----------|----------|----------|-------------|
| **P0 — Ship-blocking** | Core RX | VFO, mode, band, filter, AGC, AF/RF gain, S-meter, spectrum/waterfall | ✅ Done (3A-3E, 3G) |
| **P1 — Alpha** | Basic TX + RX polish | MOX, TUNE, drive, mic gain, CW speed/pitch/sidetone, FM basic, preamp/ATT, antenna select, squelch, RIT/XIT, step size | 3I-1, 3I-2 |
| **P2 — Beta** | Full TX + features | PureSignal, equalizer, VOX/DEXP, compressor, TX profiles, Alex filters, display settings, band stacking, multi-pan | 3I-3, 3I-4, 3F |
| **P3 — 1.0** | Integration | rigctld, TCI v2.0, DAX, serial PTT/CW, DX cluster/RBN/WSJT-X spots, recording, memory, keyboard shortcuts, profiles, N1MM spectrum | 3J, 3K, 3M |
| **P4 — Post-1.0** | Advanced | MIDI, Andromeda, Aries ATU, Ganymede PA, ZZ CAT full, skins, POTA/SpotCollector/FreeDV spots, client neural NR, diagnostic display modes, configurable status bar | 3H, future |

---

## 12. New Widgets Required

Summary of new Qt widgets needed beyond current codebase:

| Widget | Purpose | Based On |
|--------|---------|----------|
| SpectrumOverlayButtons | Left-side 9-button overlay with sub-panels | AetherSDR SpectrumOverlayMenu |
| AppletPanel | Scrollable applet stack with toggle row | AetherSDR AppletPanel |
| RxApplet | Per-slice RX controls | AetherSDR RxApplet |
| TxApplet | TX power/keying controls | AetherSDR TxApplet |
| PhoneCwApplet | 3-mode switcher (Phone/CW/FM) | AetherSDR PhoneCwApplet + Thetis FM |
| PhoneApplet | TX audio processing | AetherSDR PhoneApplet |
| EqApplet | 8/10-band EQ with response curve | AetherSDR EqApplet + new |
| CatApplet | CAT/TCI/DAX status and control | AetherSDR CatApplet |
| TunerApplet | ATU control | AetherSDR TunerApplet |
| MeterApplet | PA telemetry gauges | AetherSDR MeterApplet |
| PSApplet | PureSignal calibration | Thetis PSForm (new) |
| SMeterWidget | Dedicated analog S-meter | AetherSDR SMeterWidget |
| HGauge | Horizontal bar gauge with zones | AetherSDR HGauge |
| FilterPassbandWidget | Visual filter trapezoid with drag | AetherSDR FilterPassbandWidget |
| BandStackPanel | Vertical bookmark strip | AetherSDR BandStackPanel |
| CwxPanel | CW macro keyer (collapsible left) | AetherSDR CwxPanel |
| DvkPanel | Digital voice keyer (collapsible left) | AetherSDR DvkPanel |
| StatusBarWidget | Double-height configurable status bar | AetherSDR (new) |
| SettingsDialog | Sidebar + search settings window | New design |
| GuardedSlider | QSlider blocking wheel events | AetherSDR GuardedSlider |
| GuardedComboBox | QComboBox blocking wheel events | AetherSDR GuardedSlider.h |
| ScrollableLabel | QLabel with scroll-to-adjust | AetherSDR GuardedSlider.h |
| DaxRouter | Virtual audio routing engine | New design |
| CommandRouter | Protocol-agnostic radio command API | New design |

---

*Document generated from interactive design session analyzing:*
*Thetis setup.designer.cs (76,406 lines, 4,440 controls, 67 tabs),*
*Thetis console.Designer.cs (7,929 lines, 635 controls),*
*AetherSDR src/gui/ (13 widget classes, 8 applets, 12 dialog types),*
*NereusSDR src/gui/ (61 source files, 15,601 LOC current).*
