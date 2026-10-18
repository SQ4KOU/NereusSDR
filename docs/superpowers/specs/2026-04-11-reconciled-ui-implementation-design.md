# NereusSDR — Reconciled UI Implementation Design

**Date:** 2026-04-11
**Status:** Approved
**Sources reconciled:**
- `docs/architecture/ui-feature-mapping.md` — control-level feature inventory
- `docs/architecture/comprehensive-ui-port-design.md` — architectural spec (classes, signals, files)
- `docs/MASTER-PLAN.md` — phased roadmap with progress tracking

**Design decisions made in interactive session.** This document is the single
source of truth for UI architecture going forward. Where it conflicts with the
three source documents, this document wins.

---

## 1. Architecture Model

Three-layer UI with shared RadioModel/SliceModel as single source of truth:

| Layer | Location | Purpose | Interaction |
|---|---|---|---|
| Left Overlay | `SpectrumOverlayPanel` per SpectrumWidget | Quick-access operating controls | Every QSO |
| Right Containers | Hybrid `ContainerManager` (MeterItems + AppletWidgets) | Meters, gauges, operating applets | Frequent |
| Setup Dialog | `SetupDialog` (modal, sidebar tree + search) | Deep configuration (~255 controls, ~45 pages) | Occasional |

### Data Flow

```
Left Overlay    --+
Right Containers --+--> signals --> RadioModel/SliceModel --> RadioConnection --> Radio
Setup Dialog    --+                        ^                         |
                                           +-- status updates ------+
                                           |
                                   AppSettings (persistence)
```

All three layers stay in sync via auto-queued signals. `m_updatingFromModel`
guards prevent echo loops. Follow AetherSDR's proven pattern exactly.

---

## 2. Hybrid Container System

### Decision

Keep the existing MeterItem-based container system for meters/gauges. Add
`AppletWidget` as a new container content type alongside MeterItems. Containers
can hold MeterItems OR AppletWidgets (or both). This preserves the working
3G-1/2/3 infrastructure while enabling functional control panels.

### Existing Infrastructure (unchanged)

- `ContainerWidget` — dock/float/resize/axis-lock shell (8 positions: TL, T, TR, R, BR, B, BL, L)
- `FloatingContainer` — top-level window wrapper (Qt::Window | Qt::Tool), pin-on-top, multi-monitor DPI
- `ContainerManager` — singleton lifecycle, float/dock transitions, axis-lock reposition, AppSettings persistence
- `MeterWidget` — QRhiWidget GPU renderer (3 pipelines: textured quad, vertex geometry, QPainter overlay)
- `MeterItem` hierarchy — BarItem, TextItem, ScaleItem, NeedleItem, SolidColourItem, ImageItem
- `ItemGroup` — preset factories (S-Meter, Power/SWR, ALC, Mic, Comp)
- `MeterPoller` — QTimer-based WDSP meter polling at 100ms/10fps

### New: AppletWidget as Container Content

ContainerWidget's content area uses a QVBoxLayout. Each content item is either
a MeterWidget or an AppletWidget, stacked vertically in user-defined order.
Drag-reorder changes the stacking order. The container scrolls if content
exceeds available height. Persistence serializes the ordered list of content
items (type + config) to AppSettings via ContainerManager.

```
ContainerWidget (existing)
  +-- content area (QVBoxLayout, scrollable) EXTENDED to hold:
      +-- MeterWidget (existing -- GPU-rendered meters/gauges)
      +-- AppletWidget subclasses (NEW -- functional control panels):
          +-- RxApplet
          +-- TxApplet
          +-- PhoneCwApplet (stacked: Phone/CW pages)
          +-- EqApplet
          +-- FmApplet
          +-- DigitalApplet
          +-- PureSignalApplet
          +-- DiversityApplet
          +-- CwxApplet
          +-- DvkApplet
          +-- CatApplet
          +-- TunerApplet
```

**AppletWidget base class:**

File: `src/gui/applets/AppletWidget.h/.cpp`

```cpp
class AppletWidget : public QWidget {
    Q_OBJECT
public:
    explicit AppletWidget(RadioModel* model, QWidget* parent = nullptr);
    virtual QString appletId() const = 0;     // e.g., "RX", "TX", "EQ"
    virtual QString appletTitle() const = 0;   // e.g., "RX Controls"
    virtual QIcon appletIcon() const;
    virtual void syncFromModel() = 0;          // refresh all controls from RadioModel
protected:
    RadioModel* m_model = nullptr;
    bool m_updatingFromModel = false;          // feedback loop guard
};
```

AppletWidgets are standard QWidgets with QPainter/stylesheets (NOT QRhiWidgets).

### Default Container #0 (out-of-box)

Single scrollable container, right-docked:

1. **SMeterWidget** (dedicated AetherSDR port, pixel-identical)
2. **RxApplet** (Tier 1 controls wired)
3. **Power/SWR + ALC meters** (MeterItem presets)
4. **TxApplet** (NYI shell until Phase 3I-1)

### User Customization

- Drag applets/meters between containers
- Create new containers (docked anywhere or floating on any monitor)
- Add/remove any content type from any container
- Import/export containers as Base64 strings
- Macro visibility control (programmable buttons show/hide containers)
- Per-container settings: border, background color, title, RX source, show on RX/TX, lock

---

## 3. Left Overlay (SpectrumOverlayPanel)

New class: `SpectrumOverlayPanel` in `src/gui/SpectrumOverlayPanel.h/.cpp`

Ported from AetherSDR's `SpectrumOverlayMenu`. One instance per SpectrumWidget,
vertical button strip anchored top-left.

### Buttons (top to bottom)

| Button | Flyout | Controls | Tier |
|---|---|---|---|
| **Collapse** | -- | Toggle collapse/expand | 1 |
| **+RX** | -- (direct action) | Add receive slice on current pan | 2 (Phase 3F) |
| **+TNF** | -- (direct action) | Add tracking notch filter at cursor freq | 2 |
| **BAND** | 6x3 grid | HF: 160-6m. SWL page: L/MW, 120m-11m. WWV, GEN, XVTR | 1 |
| **ANT** | 180px panel | RX/TX antenna dropdowns, RF gain slider (-8 to +32 dB), WNB toggle + level | 1 (partial) |
| **DSP** | 200px panel | WDSP: NR, NB, SNB, ANF, BIN, MNF (with level sliders). Client: NR2, RNN, DFNR, BNR (conditional, greyed if unavailable) | 1 |
| **Display** | Per-slice tabbed | RX1/RX2/TX tabs. FFT avg/FPS, fill alpha/color, WF gain/black level, auto-black, color scheme, grid, cursor freq, heat map, noise floor, weighted avg. Footer: "More Display Options -->" opens Setup | 1 |
| **DAX** | 140px panel | DAX Ch combo (Off, 1-4), IQ Ch combo (Off, 1-4) | 2 (Phase 3-DAX) |
| **ATT** | Inline | Step attenuator value / preamp selector | 1 |
| **MNF** | -- | Manual notch filter enable + add | 2 |

### Waterfall Zoom Buttons (bottom-left of waterfall)

`[S] [B] [-] [+]` — Segment zoom, Band zoom, zoom out, zoom in.

### Styling

- Button size: 68px x 22px
- Normal: `rgba(20, 30, 45, 240)` background, `#304050` border
- Hover/Active: `rgba(0, 112, 192, 180)` (blue highlight)
- Sub-panel background: `rgba(15, 15, 26, 220)`, border `#304050`
- Event filter blocks mouse/wheel from falling through to spectrum

### Signals

```cpp
// Direct actions
void addRxClicked(const QString& panId);
void addTnfClicked();

// Band
void bandSelected(const QString& bandName, double freqHz, const QString& mode);

// Antenna
void rxAntennaChanged(int index);
void txAntennaChanged(int index);
void rfGainChanged(int gain);
void wnbToggled(bool enabled);
void wnbLevelChanged(int level);

// DSP
void nbToggled(bool enabled);
void nrToggled(bool enabled);
void anfToggled(bool enabled);
void snbToggled(bool enabled);

// Display (per-slice via panId)
void fftAverageChanged(int frames);
void wfColorGainChanged(int gain);
void wfBlackLevelChanged(int level);
void colorSchemeChanged(int index);
void fftFpsChanged(int fps);
void fillAlphaChanged(int percent);
void fillColorChanged(const QColor& color);
void autoBlackToggled(bool enabled);
void showGridToggled(bool enabled);
void cursorFreqToggled(bool enabled);
void heatMapToggled(bool enabled);
void noiseFloorToggled(bool enabled);
void noiseFloorPositionChanged(int position);
void weightedAverageToggled(bool enabled);

// DAX
void daxAudioChannelChanged(int channel);
void daxIqChannelChanged(int channel);

// Shortcut to Setup
void openDisplaySettings(const QString& sliceName);
```

---

## 4. Applet Inventory

All applets inherit from `AppletWidget`. Mode-dependent applets auto-show/hide
on mode change. NYI controls are `setEnabled(false)` with "NYI" badge and
phase tooltip.

### Always-Visible Applets

#### RxApplet

Per-slice RX controls beyond what VfoWidget provides.

| Control | Type | Thetis Source | Tier |
|---|---|---|---|
| Slice badge (A/B/C/D) | Label | grpVFOA/B | 1 |
| Lock button | Toggle | chkVFOLock | 1 (VfoWidget) |
| RX/TX ant buttons | Dropdown | toolStripStatusLabelRXAnt/TXAnt | 1 (VfoWidget) |
| Filter width label | Label | lblFilterWidth | 1 (VfoWidget) |
| Mode combo | Combo | panelMode radButtons | 1 (VfoWidget) |
| Filter preset buttons | Grid | panelFilter radFilter1-10 | 1 (VfoWidget) |
| FilterPassbandWidget | Visual | udFilterHigh/Low + ptbFilterWidth | 2 |
| AGC combo + threshold slider | Combo+Slider | comboAGC + ptbRF | 1 (partial) |
| AF gain slider + Mute | Slider+Toggle | ptbRX1AF + chkMUT | 1 (partial) |
| Audio pan slider | Slider | ptbPanMainRX | 2 |
| Squelch toggle + slider | Toggle+Slider | chkSquelch + ptbSquelch | 2 |
| RIT toggle + offset + zero | Toggle+Spin+Btn | chkRIT + udRIT + btnRITReset | 2 |
| XIT toggle + offset + zero | Toggle+Spin+Btn | chkXIT + udXIT + btnXITReset | 2 |
| Step size display + up/down | Label+Btn | txtWheelTune + btnTuneStep* | 2 |
| DIV (diversity) toggle | Toggle | -- | 3 |

#### TxApplet

TX power and keying controls. All Tier 2 -- NYI shell until Phase 3I-1.

| Control | Type | Thetis Source |
|---|---|---|
| Forward Power gauge | HGauge | picMultiMeterDigital (TX) |
| SWR gauge | HGauge | picMultiMeterDigital (TX) |
| RF Power slider (0-100%) | Slider | ptbPWR |
| Tune Power slider (0-100%) | Slider | ptbTune |
| MOX button | Toggle | chkMOX |
| TUNE button | Toggle | chkTUN |
| ATU button | Toggle | chkFWCATU |
| MEM button | Toggle | -- |
| TX Profile dropdown | Combo | comboTXProfile |
| 2-Tone test | Toggle | chk2TONE |
| PS-A toggle + indicators | Toggle+LED | PS |
| DUP (full duplex) | Toggle | chkFullDuplex |
| xPA indicator | Toggle | chkExternalPA |
| SWR protection indicator | LED | -- |
| Tune mode combo | Combo | comboTuneMode |

#### EqApplet

10-band graphic EQ. Tier 2 -- NYI until Phase 3I-3.

| Control | Type | Thetis Source |
|---|---|---|
| ON button | Toggle | chkRXEQ / chkTXEQ |
| RX / TX selector | Toggle pair | -- |
| 10-band sliders (32Hz-16kHz) | 10 Sliders | Equalizer form |
| Frequency response curve | QPainter | -- (new) |
| Preset dropdown | Combo | -- (new) |
| Reset button | Button | -- |

### Mode-Dependent Applets (auto-show on mode change)

#### PhoneCwApplet (stacked widget: Phone / CW pages)

**Phone page:**

| Control | Type | Thetis Source |
|---|---|---|
| Mic level gauge | HGauge | picMultiMeterDigital (MIC) |
| Compression gauge | HGauge | picMultiMeterDigital (COMP) |
| Mic profile combo | Combo | -- (new) |
| Mic source combo | Combo | -- (front/rear) |
| Mic level slider | Slider | ptbMic |
| ACC button | Toggle | -- |
| PROC button + slider | Toggle+Slider | chkCPDR + ptbCPDR |
| DAX button | Toggle | -- |
| MON button + slider | Toggle+Slider | chkMON |
| VOX toggle + level + delay | Toggle+Slider+Slider | chkVOX + ptbVOX |
| DEXP toggle + level | Toggle+Slider | chkNoiseGate + ptbNoiseGate |
| TX filter Low/High Cut | Slider+Btn | udTXFilterLow/High |
| AM Carrier level slider | Slider | Setup -> Transmit |

**CW page:**

| Control | Type | Thetis Source |
|---|---|---|
| ALC gauge | HGauge | picMultiMeterDigital (ALC) |
| CW speed slider (1-60 WPM) | Slider | ptbCWSpeed |
| CW pitch + up/down | Label+Btn | udCWPitch |
| Delay slider | Slider | udCWBreakInDelay |
| Sidetone toggle + slider | Toggle+Slider | chkCWSidetone |
| Break-in button (QSK) | Toggle | chkQSK |
| Iambic button | Toggle | chkCWIambic |
| Firmware keyer | Toggle | chkCWFWKeyer |
| CW pan slider | Slider | -- |

#### FmApplet (separate applet)

| Control | Type | Thetis Source |
|---|---|---|
| FM MIC slider | Slider | ptbFMMic |
| Deviation (5.0k / 2.5k) | Radio | radFMDeviation5kHz/2kHz |
| CTCSS enable + tone combo | Toggle+Combo | chkFMCTCSS + comboFMCTCSS |
| Simplex | Toggle | chkFMTXSimplex |
| Repeater offset (MHz) | Spinner | udFMOffset |
| Offset direction (-/+/Rev) | Toggle | chkFMTXLow/High/Rev |
| FM TX Profile | Combo | comboFMTXProfile |
| FM Memory | Combo+Nav | comboFMMemory + btnFMMemory* |

#### DigitalApplet (VAC/DAX routing)

| Control | Type | Thetis Source |
|---|---|---|
| VAC 1 enable + device combo | Toggle+Combo | -- |
| VAC 2 enable + device combo | Toggle+Combo | -- |
| VAC sample rate combo | Combo | -- |
| VAC stereo/mono toggle | Toggle | -- |
| VAC buffer size | Combo | -- |
| VAC TX/RX gain | Sliders | -- |
| rigctld channel assignment | Combo | -- |

### On-Demand Applets (toggle via menu or button)

#### PureSignalApplet

| Control | Type | Thetis Source |
|---|---|---|
| Calibrate button | Button | PSForm |
| Auto-cal toggle | Toggle | PSForm |
| Feedback level gauge (color-coded) | HGauge | PSForm |
| Correction magnitude bar | HGauge | PSForm |
| Coefficient save/restore | Buttons | PSForm |
| Two-tone test button | Button | PSForm |
| Status indicators | LEDs | PSForm |
| Info readout (iterations, feedback dB, correction dB) | Labels | PSForm |

#### DiversityApplet

| Control | Type | Thetis Source |
|---|---|---|
| Diversity enable toggle | Toggle | -- |
| Source selector (RX1, RX2) | Combo | -- |
| Gain slider (sub-RX relative) | Slider | -- |
| Phase slider (0-360 deg) | Slider | -- |
| ESC mode selector | Combo | -- |
| R2 gain + phase for ADC2 alignment | Sliders | -- |

#### CwxApplet

| Control | Type | Thetis Source |
|---|---|---|
| Text input field | TextEdit | CWX form |
| Send button | Button | CWX form |
| Speed override slider (WPM) | Slider | CWX form |
| 6-12 memory buttons | Buttons | CWX form |
| Repeat toggle + interval | Toggle+Spin | CWX form |
| Keyboard-to-CW mode toggle | Toggle | CWX form |

#### DvkApplet

| Control | Type | Thetis Source |
|---|---|---|
| 4-8 voice keyer slots | Record/Play/Stop per slot | DVK |
| Record button + level meter | Button+Meter | DVK |
| Repeat toggle + interval | Toggle+Spin | DVK |
| Semi break-in | Toggle | DVK |
| WAV file import | Button | DVK |

#### CatApplet

| Control | Type | Source |
|---|---|---|
| rigctld enable + per-channel status (A-D) | Toggle+LEDs | AetherSDR |
| TCP CAT enable + port | Toggle+Edit | AetherSDR + Thetis |
| TCI enable + port + status | Toggle+Edit+LED | AetherSDR + Thetis |
| Active connections count | Label | -- |
| Serial port combo | Combo | Thetis |

#### TunerApplet (Aries ATU)

| Control | Type | Thetis Source |
|---|---|---|
| Fwd Power gauge | HGauge | Aries ATU |
| SWR gauge | HGauge | Aries ATU |
| Relay position bars (C1/L/C2) | RelayBar | Aries ATU |
| TUNE button | Button | Aries ZZTU |
| OPERATE/BYPASS/STANDBY | Toggle | Aries ZZOV |
| Antenna switch (1/2/3) | Buttons | Aries ZZOA/ZZOC |

### SMeterWidget (special case)

Not an applet. Dedicated QWidget port of AetherSDR's SMeterWidget for
pixel-identical fidelity. Lives inside Container #0 as a distinct widget type.

- RX modes: S-Meter, S-Meter Peak, Sig Avg, ADC L, ADC R
- TX modes: Power, SWR, Level, Compression
- Peak hold with configurable decay
- Dynamic power scale (barefoot / amplifier)

---

## 5. Setup Dialog

`SetupDialog` -- QDialog with QSplitter: tree navigation left (QTreeWidget),
content right (QStackedWidget). Access via File -> Settings.

File: `src/gui/SetupDialog.h/.cpp`

### NYI Treatment

- `setEnabled(false)` -- greyed out, not interactive
- Small "NYI" QLabel badge positioned top-right of control or group
- Phase label on NYI badge tooltip: "Available in Phase 3I"
- Section headers show progress fraction: "Hardware (3/28 wired)"

### Categories and Control Counts

| Category | Sub-pages | Approx Controls | Tier Mix |
|---|---|---|---|
| General | Startup & Preferences, UI Scale & Theme, Navigation | ~30 | 1+2+3 |
| Hardware | Radio Selection, ADC/DDC Config, Calibration, Penny/Hermes OC, Alex Filters, Firmware, Andromeda, Other H/W | ~400 | 1+2+3 |
| Audio | Device Selection, ASIO Config, VAC 1, VAC 2, NereusDAX, Recording | ~100 | 1+2+3 |
| DSP | AGC/ALC, NR/ANF, NB/SNB, CW, AM/SAM, FM, VOX/DEXP, CFC, EER, MNF | ~120/group | 1+2+3 |
| Display | Spectrum Defaults, Waterfall Defaults, Grid & Scales, RX2 Display, TX Display | ~350 | 1+2+3 |
| Transmit | Power & PA, TX Profiles, Speech Processor, PureSignal | ~200 | 2+3 |
| Appearance | Colors & Theme, Meter Styles, Gradients, Skins, Collapsible Display | ~400 | 2+3 |
| CAT & Network | Serial Ports (1-4), TCI Server, TCP/IP CAT, MIDI Control | ~200 | 3 |
| Keyboard | Shortcuts list, edit/reset/import/export | ~150 | 2+3 |
| Diagnostics | Signal Generator, Hardware Tests, Logging | ~200 | 1+3 |

**Totals:** ~45 Tier 1 (wire now), ~130 Tier 2 (wire with phases), ~80 Tier 3 (wire later). ~255 total.

### Inline Sync

Most-used settings also appear in applets/overlays (AGC threshold, NR level,
color scheme, etc.) with bidirectional binding through RadioModel/SliceModel
signals. Changing a setting in one place updates all others.

### File Structure

```
src/gui/
+-- SetupDialog.h/.cpp
+-- SetupPage.h/.cpp              <- base class for all pages
+-- setup/
    +-- GeneralPage.h/.cpp
    +-- HardwarePage.h/.cpp
    +-- AudioPage.h/.cpp
    +-- DspPage.h/.cpp
    +-- DisplayPage.h/.cpp
    +-- TransmitPage.h/.cpp
    +-- AppearancePage.h/.cpp
    +-- CatNetworkPage.h/.cpp
    +-- KeyboardPage.h/.cpp
    +-- DiagnosticsPage.h/.cpp
```

---

## 6. Menu Bar

MASTER-PLAN expanded structure with 9 top-level menus:

```
File | Radio | View | DSP | Band | Mode | Containers | Tools | Help
```

| Menu | Items |
|---|---|
| **File** | Settings..., Profiles ->, Quit (Ctrl+Q) |
| **Radio** | Discover..., Connect (Ctrl+K), Disconnect, Radio Setup..., Antenna Setup..., Transverters..., Protocol Info |
| **View** | Pan Layout -> (1/2v/2h/2x2/12h), Add/Remove Pan, Band Plan -> (Off/S/M/L + region), Display Mode ->, UI Scale ->, Dark/Light Theme, Minimal Mode (Ctrl+M), Keyboard Shortcuts... |
| **DSP** | NR (check), NR2 (check), NB (check), NB2 (check), ANF (check), TNF (check), BIN (check), AGC -> (Off/Slow/Med/Fast/Custom), Equalizer..., PureSignal..., Diversity... |
| **Band** | HF -> (160m-6m), VHF ->, GEN ->, WWV, Band Stacking... |
| **Mode** | LSB, USB, DSB, CWL, CWU, AM, SAM, FM, DIGL, DIGU, DRM, SPEC |
| **Containers** | New Container..., Container Settings..., Reset Default Layout, [dynamic show/hide list] |
| **Tools** | CWX..., Memory Manager, CAT Control..., TCI Server..., DAX Audio..., MIDI Mapping..., Macro Buttons..., Network Diagnostics..., Support Bundle... |
| **Help** | Getting Started, NereusSDR Help, Understanding Data Modes, What's New, About NereusSDR |

---

## 7. Status Bar

AetherSDR double-height (46px) with configurable right-side slots.

### Left Section (action toggles)

| Control | Function | Source |
|---|---|---|
| Band Stack (3 dots) | Open BandStackPanel | AetherSDR |
| +PAN | Add panadapter | AetherSDR |
| Panel Toggle | Toggle container panel visibility | AetherSDR |
| TNF | Toggle global TNF enable | AetherSDR |
| CWX | Toggle CW keyer panel | Both |
| DVK | Toggle Digital Voice Keyer panel | AetherSDR |
| FDX | Toggle full duplex | Thetis |
| Radio Info | Model + firmware (stacked) | Both |

### Center

Station callsign -- double-click to connect/disconnect.

### Right Section (configurable indicators)

| Indicator | Default Visible |
|---|---|
| CAT Serial status | Yes |
| CAT TCP status | Yes |
| TCI status | Yes |
| N1MM status | No (opt-in) |
| PA Voltage | Yes |
| PA Current | Yes |
| TX Inhibit | Yes |
| CPU usage | Yes |
| TX indicator | Yes |
| Record/Play | No (opt-in) |
| Timer | No (opt-in) |
| UTC Time | Yes |
| Date | Yes |
| Local Time | Yes |

Configurable via Settings -> Appearance -> Status Bar.

---

## 8. Supporting Systems

### DAX Architecture

Native virtual audio devices per platform:

```
DaxRouter -> VirtualAudioBackend (interface)
               +-- PipeWireDAX (Linux)
               +-- CoreAudioDAX (macOS -- HAL plugin)
               +-- WindowsDAX (Phase A: VB-Cable, B: virtual ASIO, C: WDM miniport)
               +-- ExternalDeviceBackend (fallback)
```

4 audio channels + 2 IQ channels. DAX IQ taps raw I/Q pre-WDSP, DAX Audio
taps post-WDSP. UI surfaces: overlay DAX sub-panel, CatApplet, Settings ->
Audio -> NereusDAX.

Files:
```
src/audio/
+-- NereusDAX.h/.cpp              <- DAX abstraction
+-- CoreAudioDAX.h/.cpp           <- macOS backend
+-- PipeWireDAX.h/.cpp            <- Linux backend
+-- WindowsDAX.h/.cpp             <- Windows backend
+-- AsioBackend.h/.cpp            <- ASIO audio backend
```

### CAT/TCI/Integration (CommandRouter)

Layered architecture with protocol-agnostic central API (~200 typed functions):

| # | Protocol | Transport | Priority |
|---|---|---|---|
| 1 | rigctld (Hamlib v1) | TCP (4 ports, per-slice) + PTY | P3 |
| 2 | TCI v2.0 | WebSocket + binary audio/IQ | P3 |
| 3 | ZZ CAT (Thetis extended) | Serial + TCP | P4 |
| 4 | MIDI mapper | RtMidi | P4 |
| 5 | Kenwood TS-2000 compat | Serial + TCP | P4 |

Spot sources: DX Cluster, RBN, WSJT-X (built-in clients, P3), POTA,
SpotCollector, FreeDV Reporter (P4). All feed into SpotModel -> panadapter
overlay with DXCC color coding.

### ASIO Backend

`AsioBackend` as alternative to QAudioSink for primary audio I/O. Separate
from NereusDAX. When ASIO is selected, replaces QAudioSink entirely. Same
atomic parameter pattern as current AudioEngine.

---

## 9. Phase Ordering

### Revised Phase Sequence

```
3G-3 (in progress -- finish SMeterWidget)
  |
  v
3-UI (NEW -- full UI skeleton)
  |
  v
3G-4 -> 3G-5 -> 3G-6 (advanced/interactive meter items, container settings)
  |
  v
3I-1 -> 3I-2 -> 3I-3 -> 3I-4 (TX: SSB -> CW -> chain -> PureSignal)
  |
  v
3F (multi-panadapter -- requires PureSignal for UpdateDDCs state machine)
  |
  v
3H (skins)

Independent (anytime after 3-UI): 3J, 3K, 3L, 3M, 3N, 3-DAX
```

### Phase 3-UI: Full UI Skeleton (NEW)

**Goal:** Build the complete UI surface with NYI shells. Wire all Tier 1
controls. Give users visibility into the full feature set from day one.

| Deliverable | Details |
|---|---|
| SpectrumOverlayPanel | 10-button strip + flyout sub-panels. Wire Tier 1: Band, DSP, Display, ANT, ATT. NYI: +RX, +TNF, DAX, MNF |
| AppletWidget base class | `src/gui/applets/AppletWidget.h/.cpp` |
| ContainerManager extension | Accept AppletWidget as content type alongside MeterWidget/MeterItems |
| RxApplet | Wire Tier 1: slice badge, lock, ant, filter, mode, AGC, AF gain, mute. NYI: passband, squelch, RIT/XIT, pan, step, DIV |
| TxApplet | Full NYI shell |
| PhoneCwApplet | NYI shell -- Phone + CW stacked pages |
| EqApplet | NYI shell -- 10-band layout visible but disabled |
| FmApplet, DigitalApplet | NYI shells |
| PureSignalApplet, DiversityApplet | NYI shells |
| CwxApplet, DvkApplet | NYI shells |
| CatApplet, TunerApplet | NYI shells |
| Default Container #0 | Single scrollable: SMeterWidget, RxApplet, Power/SWR+ALC, TxApplet (NYI) |
| SetupDialog | Tree navigation, ~45 pages as NYI skeletons, wire ~45 Tier 1 controls |
| Menu bar expansion | Full 9-menu layout |
| Status bar | Double-height with left toggles, center callsign, configurable right indicators |

**Verification:** App launches with full UI visible. Tier 1 controls function.
All NYI controls visibly greyed with phase badges. Containers dock/float/resize
with mixed MeterItem + AppletWidget content.

### Progressive Wiring Schedule

| Phase | Controls Lit Up |
|---|---|
| 3G-4..6 | Advanced meter items (history, magic eye, dial), interactive items (band/mode buttons), container settings dialog |
| 3I-1 | TxApplet (MOX, TUNE, drive, mic gain, TX profile), PhoneCwApplet Phone page, DvkApplet |
| 3I-2 | PhoneCwApplet CW page (speed, sidetone, QSK), CwxApplet |
| 3I-3 | EqApplet, VOX/DEXP, compressor, CESSB, leveler, full TX DSP chain |
| 3I-4 | PureSignalApplet, AmpView float |
| 3F | +RX button, multi-pan layout, DDC assignment, RX2 display tab |
| 3-DAX | DAX overlay button, DigitalApplet, NereusDAX setup page |
| 3J | TCI status bar indicator, spot overlay |
| 3K | CatApplet, rigctld setup page |

### Dependency Graph

```
3G-3 -> 3-UI -> 3G-4 -> 3G-5 -> 3G-6    (meters + UI, sequential)
                  \
           3-UI -> 3I-1 -> 3I-2 -> 3I-3 -> 3I-4 -> 3F -> 3H    (TX then multi-RX)
                                                     ^
                                          PureSignal before 3F
                                          (UpdateDDCs PS DDC states)

Independent after 3-UI: 3J, 3K, 3L, 3M, 3N, 3-DAX
```

---

## 10. File Structure Summary

New files created by this design:

```
src/gui/
+-- SpectrumOverlayPanel.h/.cpp          <- Layer 1: left overlay
+-- SetupDialog.h/.cpp                   <- Layer 3: setup dialog
+-- SetupPage.h/.cpp                     <- Base class for setup pages
+-- setup/                               <- Setup sub-pages
|   +-- GeneralPage.h/.cpp
|   +-- HardwarePage.h/.cpp
|   +-- AudioPage.h/.cpp
|   +-- DspPage.h/.cpp
|   +-- DisplayPage.h/.cpp
|   +-- TransmitPage.h/.cpp
|   +-- AppearancePage.h/.cpp
|   +-- CatNetworkPage.h/.cpp
|   +-- KeyboardPage.h/.cpp
|   +-- DiagnosticsPage.h/.cpp
+-- applets/                             <- Layer 2: container applets
    +-- AppletWidget.h/.cpp              <- Base class
    +-- RxApplet.h/.cpp
    +-- TxApplet.h/.cpp
    +-- EqApplet.h/.cpp
    +-- PhoneCwApplet.h/.cpp
    +-- PureSignalApplet.h/.cpp
    +-- DiversityApplet.h/.cpp
    +-- CwxApplet.h/.cpp
    +-- DvkApplet.h/.cpp
    +-- FmApplet.h/.cpp
    +-- DigitalApplet.h/.cpp
    +-- CatApplet.h/.cpp
    +-- TunerApplet.h/.cpp
src/audio/
+-- NereusDAX.h/.cpp                     <- DAX abstraction
+-- CoreAudioDAX.h/.cpp                  <- macOS backend
+-- PipeWireDAX.h/.cpp                   <- Linux backend
+-- WindowsDAX.h/.cpp                    <- Windows backend
+-- AsioBackend.h/.cpp                   <- ASIO audio backend
```

---

## 11. Conflict Resolution Record

Decisions made during reconciliation that override source documents:

| Conflict | Resolution | Rationale |
|---|---|---|
| Container architecture (unified vs separate) | Hybrid: MeterItems + AppletWidgets in same ContainerManager | Preserves working 3G-1/2/3 code while enabling composability |
| UI skeleton timing (upfront vs incremental) | Dedicated Phase 3-UI after 3G-3 | Users see full feature surface immediately; Tier 1 controls wire on day one |
| Menu bar structure | MASTER-PLAN expanded (9 menus with Band, Mode, Containers) | Most complete and discoverable |
| Phone/CW/FM applet split | PhoneCwApplet (stacked) + separate FmApplet + DigitalApplet | Clean mode separation without proliferating tiny applets |
| Default Container #0 layout | Single scrollable: SMeterWidget, RxApplet, Power/SWR+ALC, TxApplet | Mixed content demonstrates hybrid model; everything in one container for simplicity |
| comprehensive-ui-port internal contradiction (meters-first vs controls-first) | SMeterWidget first, then RxApplet, then meters, then TxApplet | Visual anchor (S-meter) at top, operating controls below, TX last (NYI initially) |
