# Phase 3G-4 / 3G-5 / 3G-6 — Advanced Meters, Interactive Items & Container Settings

Design spec for the three phases completing the NereusSDR meter/container system.
**Goal: 1:1 feature parity with Thetis MeterManager** (34 item types, 41 meter presets).

---

## Thetis Feature Matrix

Complete mapping of all 34 Thetis `MeterItemType` values to NereusSDR implementations.
Items marked **existing** are already implemented. Items marked **this spec** are new.

| Thetis MeterItemType | NereusSDR Class | Phase | Status |
|---|---|---|---|
| H_BAR / V_BAR | BarItem | 3G-3 | **Existing** |
| H_SCALE / V_SCALE | ScaleItem | 3G-3 | **Existing** |
| NEEDLE | NeedleItem | 3G-3 | **Existing** |
| TEXT | TextItem | 3G-3 | **Existing** |
| IMAGE | ImageItem | 3G-3 | **Existing** |
| SOLID_COLOUR | SolidColourItem | 3G-3 | **Existing** |
| ITEM_GROUP | ItemGroup | 3G-3 | **Existing** |
| HISTORY | HistoryGraphItem | 3G-4 | This spec |
| MAGIC_EYE | MagicEyeItem | 3G-4 | This spec |
| DIAL_DISPLAY | DialItem | 3G-4 | This spec |
| LED | LEDItem | 3G-4 | This spec |
| NEEDLE_SCALE_PWR | NeedleScalePwrItem | 3G-4 | This spec |
| SIGNAL_TEXT_DISPLAY | SignalTextItem | 3G-4 | This spec |
| ROTATOR | RotatorItem | 3G-4 | This spec |
| TEXT_OVERLAY | TextOverlayItem | 3G-4 | This spec |
| SPACER | SpacerItem | 3G-4 | This spec |
| FILTER_DISPLAY | FilterDisplayItem | 3G-4 | This spec |
| FADE_COVER | FadeCoverItem | 3G-4 | This spec |
| CLICKBOX | ClickBoxItem | 3G-5 | This spec |
| BAND_BUTTONS | BandButtonItem | 3G-5 | This spec |
| MODE_BUTTONS | ModeButtonItem | 3G-5 | This spec |
| FILTER_BUTTONS | FilterButtonItem | 3G-5 | This spec |
| ANTENNA_BUTTONS | AntennaButtonItem | 3G-5 | This spec |
| TUNESTEP_BUTTONS | TuneStepButtonItem | 3G-5 | This spec |
| OTHER_BUTTONS | OtherButtonItem | 3G-5 | This spec |
| VOICE_RECORD_PLAY_BUTTONS | VoiceRecordPlayItem | 3G-5 | This spec |
| VFO_DISPLAY | VfoDisplayItem | 3G-5 | This spec |
| CLOCK | ClockItem | 3G-5 | This spec |
| DATA_OUT | DataOutItem | 3G-5 | This spec |
| WEB_IMAGE | WebImageItem | 3G-4 | This spec |
| DISCORD_BUTTONS | DiscordButtonItem | 3G-5 | This spec |

### Thetis Meter Presets (AddMeter factory)

| Thetis MeterType | NereusSDR ItemGroup Preset | Notes |
|---|---|---|
| SIGNAL_STRENGTH | createSignalBarPreset() | Instantaneous signal bar |
| AVG_SIGNAL_STRENGTH | createAvgSignalBarPreset() | Averaged signal bar |
| SIGNAL_MAX_BIN | createMaxBinBarPreset() | Peak bin signal bar |
| SIGNAL_TEXT | createSignalTextPreset() | Large text signal readout |
| ADC | createAdcBarPreset() | ADC level bar |
| ACG_MAX_MAG | createAdcMaxMagPreset() | ADC peak detection |
| AGC | createAgcBarPreset() | AGC level bar |
| AGC_GAIN | createAgcGainBarPreset() | AGC gain bar |
| ESTIMATED_PBSNR | createPbsnrBarPreset() | Peak-to-baseline SNR |
| MIC | createMicPreset() | **Existing** |
| EQ | createEqBarPreset() | EQ level bar |
| LEVELER | createLevelerBarPreset() | Leveler bar |
| LEVELER_GAIN | createLevelerGainBarPreset() | Leveler gain bar |
| ALC | createAlcPreset() | **Existing** |
| ALC_GAIN | createAlcGainBarPreset() | ALC gain bar |
| ALC_GROUP | createAlcGroupBarPreset() | ALC group bar |
| CFC | createCfcBarPreset() | CFC bar |
| CFC_GAIN | createCfcGainBarPreset() | CFC gain bar |
| COMP | createCompPreset() | **Existing** |
| PWR | createPowerSwrPreset() | **Existing** |
| REVERSE_PWR | (included in PowerSwr) | **Existing** |
| SWR | (included in PowerSwr) | **Existing** |
| MAGIC_EYE | createMagicEyePreset() | This spec |
| ANANMM | createAnanMMPreset() | This spec — 7-needle composite |
| CROSS | createCrossNeedlePreset() | This spec — dual crossing needles |
| VFO_DISPLAY | createVfoDisplayPreset() | This spec |
| CLOCK | createClockPreset() | This spec |
| SPACER | createSpacerPreset() | This spec |
| TEXT_OVERLAY | createTextOverlayPreset() | This spec |
| DATA_OUT | createDataOutPreset() | This spec |
| ROTATOR | createRotatorPreset() | This spec |
| LED | createLedPreset() | This spec |
| WEB_IMAGE | createWebImagePreset() | This spec |
| BAND_BUTTONS | createBandButtonPreset() | This spec |
| MODE_BUTTONS | createModeButtonPreset() | This spec |
| FILTER_BUTTONS | createFilterButtonPreset() | This spec |
| ANTENNA_BUTTONS | createAntennaButtonPreset() | This spec |
| HISTORY | createHistoryPreset() | This spec |
| TUNESTEP_BUTTONS | createTuneStepButtonPreset() | This spec |
| DISCORD_BUTTONS | createDiscordButtonPreset() | This spec |
| FILTER_DISPLAY | createFilterDisplayPreset() | This spec |
| DIAL_DISPLAY | createDialPreset() | This spec |
| CUSTOM_METER_BAR | createCustomBarPreset() | This spec |
| OTHER_BUTTONS | createOtherButtonPreset() | This spec |
| VOICE_RECORD_PLAY_BUTTONS | createVoiceRecordPlayPreset() | This spec |

---

## Phase 3G-4: Advanced Meter Items (Passive/Display)

All data-bound, no mouse interaction. **11 new item types** + enhancements to existing NeedleItem.

### HistoryGraphItem — Scrolling Time-Series Graph

**Ported from:** Thetis `clsHistoryItem` (MeterManager.cs:16149+)

- Fixed-size ring buffer (`std::vector<float>`) — not Thetis's List+time-cleanup
- Capacity options: 100 / 300 / 600 / 3000 samples (at 100ms poll = 10s / 30s / 60s / 5m)
- Default: 300 samples (30s window)
- **Dual-axis support** (from Thetis: `_history_data_list_0` + `_history_data_list_1`)
  — two independent data series with separate colors and auto-scaling
- QPainter line graph in `OverlayDynamic` layer — connects consecutive points left-to-right
- Auto-scaling Y-axis from running min/max (from Thetis `addReading()` lines 16468-16499)
- Subtle grid lines + Y-axis labels in `OverlayStatic` layer
- Time labels on X-axis at 5-second boundaries (from Thetis lines 34967-35003)
- Colors: cyan line `#00b4d8`, dark grid `#203040`, label text `#8090a0`
- Serialization tag: `HISTORY`

**Properties (from Thetis clsHistoryItem lines 16388-16730):**
- `capacity` (int) — ring buffer size per axis
- `lineColor0`, `lineColor1` (QColor) — per-axis line colors
- `showGrid` (bool, default true)
- `autoScale0`, `autoScale1` (bool, default true) — per-axis auto-ranging
- `showScale0`, `showScale1` (bool, default true) — per-axis visibility
- `keepFor` (int, seconds, default 60) — display window duration
- `ignoreHistoryDuration` (int, ms, default 2000) — startup ignore period

### MagicEyeItem — Vacuum Tube Magic Eye

**Ported from:** Thetis `clsMagicEyeItem` (MeterManager.cs:15855+)
**Background image:** `eye-bezel` (from Thetis image list, line 2426)

- Green phosphor arc that opens/closes with signal strength
- `OverlayDynamic` layer — QPainter with radial gradient + arc geometry
- Closed (narrow shadow) = strong signal (S9+), fully open = no signal (S0)
- Reuses NeedleItem's `dbmToFraction()` mapping for S-meter scale
- Green glow: `#00ff88` center fading to `#004400` edge, dark center shadow
- Bezel overlay: QPainter-drawn circular bezel (vector equivalent of eye-bezel.png)
  — also supports user-supplied PNG via ImageItem overlay for skin compatibility
- Smoothed value (same alpha as NeedleItem: `kSmoothAlpha = 0.3f`)
- Serialization tag: `MAGICEYE`

**Properties:**
- `glowColor` (QColor, default `#00ff88`)
- `bezelImagePath` (QString, optional — override procedural bezel with PNG)

### DialItem — Circular Dial Meter

**Ported from:** Thetis `clsDialDisplay` (MeterManager.cs:15399+)

- Circular dial with quadrant buttons (VFOA, VFOB, ACCEL, LOCK)
- Main circle rendered with `fillEllipse()`, ring with dash stroke style
- Corner quadrant buttons for VFO selection and acceleration toggle
- Speed indicators: Slow/Hold/Fast color states
- `OverlayStatic` for circle/ring/labels, `Geometry` for indicator,
  `OverlayDynamic` for quadrant state highlights
- Serialization tag: `DIAL`

**Properties (from Thetis clsDialDisplay lines 15399-15600):**
- `textColour` (QColor)
- `circleColour` (QColor)
- `padColour` (QColor)
- `ringColour` (QColor)
- `buttonOnColour`, `buttonOffColour`, `buttonHighlightColour` (QColor)
- `slowColour`, `holdColour`, `fastColour` (QColor) — speed indicator states

**Mouse interaction:** Quadrant click detection for VFO/ACCEL/LOCK toggles
(from Thetis MouseUp, line 15814-15836). Mouse wheel for VFO adjustment (line 15837).

### LEDItem — Colored LED Indicator

**Ported from:** Thetis `clsLed` (MeterManager.cs:19448+)

- Three shapes: `SQUARE`, `ROUND`, `TRIANGLE` (from Thetis LedShape enum)
- Two styles: `FLAT`, `THREE_D` (from Thetis LedStyle enum)
- Blink and pulsate animations (from Thetis properties)
- Color fade transitions between RX/TX states
- Panel background support (two colors for MOX/RX state fading)
- Conditional evaluation with delay timer
- `OverlayDynamic` layer
- Serialization tag: `LED`

**Properties (from Thetis clsLed lines 19448-19650):**
- `trueColour`, `falseColour` (QColor) — on/off state colors
- `panelBackColour1`, `panelBackColour2` (QColor) — RX/TX panel fade
- `ledShape` (enum: Square, Round, Triangle)
- `ledStyle` (enum: Flat, ThreeD)
- `blink` (bool) — enable blink animation
- `pulsate` (bool) — enable pulse animation
- `showBackPanel` (bool)
- `padding` (float)
- `condition` (QString) — conditional evaluation string
- `greenThreshold`, `amberThreshold`, `redThreshold` (double) — simple threshold mode

### NeedleScalePwrItem — Power-Calibrated Needle Scale Labels

**Ported from:** Thetis `clsNeedleScalePwrItem` (MeterManager.cs:14888+)

Companion to NeedleItem — renders non-linear power scale text labels around needle arcs.
Used by ANANMM and CrossNeedle presets.

- Positions text at scale calibration points along arc geometry
- Non-linear power scaling (0/5/10/25/50/100/150W for ANANMM, fine 0-20W for CrossNeedle reverse)
- Font sizing scales with meter size: `fontSizeEmScaled = (FontSize / 16f) * (rect.Width / 52f)`
- Unit auto-selection: mW when MaxPower <= 1W, else W (from Thetis line 31822)
- Low/High color zones for text (from Thetis LowColour/HighColour)
- `OverlayStatic` layer
- Serialization tag: `NEEDLESCALEPWR`

**Properties (from Thetis lines 14888-15040):**
- `lowColour`, `highColour` (QColor) — text color gradient by value
- `fontFamily` (QString, default "Trebuchet MS")
- `fontStyle` (enum: Regular, Bold)
- `fontSize` (float, default 20)
- `marks` (int) — number of labels to display
- `showMarkers` (bool, default true)
- `showType` (bool, default false) — show meter type label
- `darkMode` (bool, default false)
- `maxPower` (float) — power rating for scale computation
- `scaleCalibration` (QMap<float, QPointF>) — value → normalized (x,y) position

### SignalTextItem — Text-Based Signal Display

**Ported from:** Thetis `clsSignalText` (MeterManager.cs:20286+)

Large text display of signal strength with unit format switching.

- Three display modes (from Thetis Units enum, line 20288):
  - `DBM` — dBm format (e.g., "-73.2 dBm")
  - `S_UNITS` — S-meter format (e.g., "S9+10")
  - `UV` — microvolts format (e.g., "50.12 uV")
- Peak hold display (optional secondary readout)
- Bar style rendering (None/Line/SolidFilled/GradientFilled/Segments)
- Smoothed with attack/decay (0.8/0.2 from Thetis line 20353-20354)
- History duration 2000ms default
- Unit cycling via mouse wheel increment/decrement (from Thetis lines 20516-20539)
- `OverlayDynamic` layer
- Serialization tag: `SIGNALTEXT`

**Properties (from Thetis clsSignalText lines 20286-20540):**
- `units` (enum: Dbm, SUnits, Uv)
- `showValue` (bool) — display numeric reading
- `showPeakValue` (bool) — display peak hold value
- `showType` (bool) — display units label
- `showMarker` (bool) — display reading marker
- `peakHold` (bool) — enable peak hold
- `colour` (QColor, default red)
- `markerColour` (QColor, default yellow)
- `peakValueColour` (QColor, default red)
- `fontFamily` (QString, default "Trebuchet MS")
- `fontSize` (float, default 20)
- `barStyle` (enum: None, Line, SolidFilled, GradientFilled, Segments)
- `historyDuration` (int, ms, default 2000)

### RotatorItem — Antenna Rotator Compass Dial

**Ported from:** Thetis `clsRotatorItem` (MeterManager.cs:15042+)

Circular compass dial for antenna azimuth/elevation display and control.

- Three modes (from Thetis RotatorMode enum, line 15044):
  - `AZ` — azimuth only (0-360 compass)
  - `ELE` — elevation only (0-90 scale)
  - `BOTH` — dual needle: outer=AZ, inner=ELE
- Background images: `rotator_az-bg`, `rotator_ele-bg`, `rotator_both-bg`, `rotator_map-bg`
  — QPainter vector-drawn equivalents with optional PNG skin override
- Current heading shown as colored dot + arrow
- Beam width overlay arc (configurable width angle + transparency)
- Cardinal labels (N/S/E/W) and degree scale
- Smoothed value adjustment: 0.2x speed (from Thetis lines 15290-15312)
- Multi-layer: `OverlayStatic` for compass face, `OverlayDynamic` for needle/heading
- Serialization tag: `ROTATOR`

**Properties (from Thetis clsRotatorItem lines 15042-15340):**
- `mode` (enum: Az, Ele, Both)
- `allowControl` (bool) — enable user manipulation via drag
- `showValue` (bool) — display degree readout
- `showCardinals` (bool) — show N/S/E/W labels
- `showBeamWidth` (bool) — display antenna pattern width
- `beamWidth` (float, default 30 degrees)
- `beamWidthAlpha` (float, default 0.6)
- `darkMode` (bool)
- `padding` (float, default 0.5)
- `bigBlobColour` (QColor, red) — current heading dot
- `smallBlobColour` (QColor, white) — reference marker
- `outerTextColour` (QColor, grey) — cardinal/scale labels
- `arrowColour` (QColor, white)
- `beamWidthColour` (QColor)
- `backgroundColour` (QColor)
- `controlColour` (QColor, lime green)
- `fontFamily`, `fontSize` (QString, float)

**Mouse interaction:** Drag to set heading when `allowControl` is true.
Emits `rotatorCommandRequested(float azimuth, float elevation)` signal.
Control strings: `<PST><AZIMUTH>%AZ%</AZIMUTH></PST>` pattern for MMIO output.

### TextOverlayItem — Dynamic Text with Variable Substitution

**Ported from:** Thetis `clsTextOverlay` (MeterManager.cs:18746+)

Two-line text display with runtime variable substitution parser.

- Two independent text lines (`text1`, `text2`)
- Variable substitution via `%VARIABLE_NAME%` placeholders:
  - All Reading enum values (SIGNAL_STRENGTH, AVG_SIGNAL_STRENGTH, PWR, SWR, etc.)
  - Custom string variables (frequency, mode name, date/time)
  - `%PRECIS=n%` — set decimal precision for subsequent values
  - `%NL%` — newline character
  - MMIO variables (external data sources)
  - CAT variables (script output)
- Scrolling text support (`scrollX` speed, from Thetis line 18834)
- Per-line styling (font, color, background)
- `OverlayDynamic` layer
- Serialization tag: `TEXTOVERLAY`

**Properties (from Thetis clsTextOverlay lines 18746-19400):**
- `text1`, `text2` (QString) — template text with %placeholders%
- `showTextBackColour1`, `showTextBackColour2` (bool) — per-line background
- `showBackPanel` (bool, default true) — container panel background
- `xOffset1`, `yOffset1`, `xOffset2`, `yOffset2` (float) — positioning
- `scrollX` (float, default -0.15) — horizontal scroll speed (0 = disabled)
- `textColour1`, `textColour2` (QColor, white)
- `textBackColour1`, `textBackColour2` (QColor, dark grey)
- `panelBackColour1`, `panelBackColour2` (QColor, darker grey)
- `fontFamily1`, `fontFamily2` (QString, "Trebuchet MS")
- `fontSize1`, `fontSize2` (float, 18)
- `fontStyle1`, `fontStyle2` (enum: Regular, Bold, Italic)
- `padding` (float, 0.1)

### SpacerItem — Layout Spacer

**Ported from:** Thetis `clsSpacerItem` (MeterManager.cs:16116+)

Fixed vertical spacing element.

- Simple solid/gradient rectangle
- No data binding, no interaction
- `Background` layer
- Serialization tag: `SPACER`

**Properties (from Thetis lines 16116-16148):**
- `padding` (float, default 0.1) — height allocation
- `colour1`, `colour2` (QColor, dark grey) — gradient fill

### FilterDisplayItem — Mini Passband/Spectrum Display

**Ported from:** Thetis `clsFilterItem` (MeterManager.cs:16852+)

Mini passband display with spectrum, waterfall, and filter edge visualization.

- Three display modes (from Thetis FIDisplayMode enum, line 16865):
  - `PANADAPTER` — spectrum line only
  - `WATERFALL` — waterfall only
  - `PANAFALL` — both stacked (default)
  - `NONE` — disabled
- Spectrum data: 512-pixel mini spectrum (from Thetis MiniSpec.PIXELS, line 17006)
- Filter edges: RX (yellow), TX (red) vertical lines with highlights
- Notch filter overlays: orange lines for manual notch filters
- TX profile indicator when in TX mode
- Auto-zoom and snap-to-grid support
- `OverlayDynamic` layer (spectrum updates per frame)
- Serialization tag: `FILTERDISPLAY`

**Properties (from Thetis clsFilterItem lines 16852-17500):**
- `displayMode` (enum: Panadapter, Waterfall, Panafall, None)
- `dataLineColour` (QColor, lime green) — spectrum line
- `dataFillColour` (QColor, lime green) — fill under spectrum
- `edgesColourRX` (QColor, yellow) — RX filter edges
- `edgesColourTX` (QColor, red) — TX filter edges
- `edgeHighlightColour` (QColor, white) — active edge highlight
- `notchColour` (QColor, orange) — notch filter lines
- `notchHighlightColour` (QColor, lime green) — active notch highlight
- `settingOnColour` (QColor, cornflower blue)
- `meterBackColour` (QColor, black)
- `textColour` (QColor, white)
- `fillSpectrum` (bool) — fill area under curve
- `showLimits` (bool) — display min/max on scale
- `autoZoom` (bool) — automatic zoom tracking
- `snapLines` (bool) — snap-to-grid
- `fontScale` (float, 1.0)
- `padding` (float, 0.2)
- `rxZoom`, `txZoom` (float) — bandwidth zoom multipliers
- `waterfallPalette` (enum: Enhanced, Spectran, BlackWhite, LinLog, LinRad, LinAuto, Custom)
- `waterfallLowColour` (QColor, black)
- `waterfallFrameInterval` (int, 4) — update every Nth frame
- `rxSpecGridMin`, `rxSpecGridMax` (float) — spectrum dB range
- `txSpecGridMin`, `txSpecGridMax` (float)
- `rxWaterfallMin`, `rxWaterfallMax` (float)
- `txWaterfallMin`, `txWaterfallMax` (float)

**Mouse interaction:**
- Right-click on notch line → emits `notchPopupRequested(float freqHz)`
  (Thetis ShowNotchPopup, line 17610)
- Ctrl+Right-click → emits `addNotchRequested(float freqHz)` (line 17629)
- Drag filter edges (low/high/shift) → emits `filterEdgeChanged(int low, int high)`
- Click edge markers to select for adjustment

### FadeCoverItem — RX/TX Transition Overlay

**Ported from:** Thetis `clsFadeCover` (MeterManager.cs:7665+)

Semi-transparent overlay that fades items during RX/TX transitions.

- Renders a colored rectangle with configurable alpha
- Used to dim/fade meter items that aren't relevant in current RX/TX state
- `OverlayDynamic` layer (alpha changes on MOX transitions)
- Serialization tag: `FADECOVER`

**Properties:**
- `colour1`, `colour2` (QColor) — RX/TX state colors
- `alpha` (float, 0-1) — current transparency
- `fadeOnRx`, `fadeOnTx` (bool) — which state triggers fade

### WebImageItem — Web-Fetched Image Display

**Ported from:** Thetis `clsWebImage` (MeterManager.cs:14165+)

Displays an image fetched from a URL (e.g., remote webcam, QRZ photo).

- Async HTTP fetch with periodic refresh
- Cached locally to avoid repeated downloads
- Falls back to placeholder on fetch failure
- `Background` layer
- Serialization tag: `WEBIMAGE`

**Properties:**
- `url` (QString)
- `refreshInterval` (int, seconds, default 300)
- `fallbackColor` (QColor)

---

## Complex Composite Presets (ItemGroup factories)

### ANANMM Preset — createAnanMMPreset()

**Ported from:** Thetis `AddAnanMM()` (MeterManager.cs:22461-22815)

The signature ANAN Multi-Meter: 7 simultaneous needle readings on a single gauge face.

**Background layers:**
- Main meter face: QPainter vector-drawn arc gauge (or user PNG via skin system)
- RX background: shown only in RX mode
- TX background: shown only in TX mode

**Display Groups (user-selectable, from Thetis line 22578-22605):**
- Group 0: ALL (all needles visible)
- Group 1: PWR/SWR (default)
- Group 2: Comp (compression)
- Group 3: ALC
- Group 4: Volts/Amps

**Seven needle configurations:**

1. **Signal Strength (RX only)** — AVG_SIGNAL_STRENGTH
   - Red needle (233,51,50), history 4000ms (dim red)
   - Attack 0.8, Decay 0.2, LengthFactor 1.65
   - 16-point calibration: -127 to -13 dBm (from Thetis lines 22491-22506)

2. **Volts (all modes)** — VOLTS reading
   - Black needle, no history, Attack/Decay 0.2/0.2, LengthFactor 0.75
   - 3-point calibration: 10V, 12.5V, 15V

3. **Amps (TX only, DisplayGroup 4)** — AMPS reading
   - Black needle, no history, LengthFactor 1.15
   - 11-point calibration: 0-20A

4. **Power (TX only, DisplayGroup 1, Primary)** — PWR reading
   - Red needle, history 4000ms, Attack 0.2, Decay 0.1, LengthFactor 1.55
   - NormaliseTo100W: true
   - 10-point calibration: 0/5/10/25/30/40/50/60/100/150W

5. **Power Scale Labels** — NeedleScalePwrItem companion
   - 7 marks: 0, 5, 10, 25, 50, 100, 150W
   - Trebuchet MS Bold 22pt, Low=Gray, High=Red

6. **SWR (TX only, DisplayGroup 1)** — SWR reading
   - Black needle, history 4000ms (cornflower blue), LengthFactor 1.36
   - 6-point calibration: 1/1.5/2/2.5/3/10

7. **Compression (TX only, DisplayGroup 2)** — ALC_G reading
   - Black needle, no history, LengthFactor 0.96
   - 7-point calibration: 0-30dB

8. **ALC (TX only, DisplayGroup 3)** — ALC_GROUP reading
   - Black needle, no history, LengthFactor 0.75
   - 3-point calibration: -30/0/25dB

All calibration points are exact values from Thetis source (lines 22461-22815).
Needle offset (0.004, 0.736), RadiusRatio (1.0, 0.58) shared across all needles.

### CrossNeedle Preset — createCrossNeedlePreset()

**Ported from:** Thetis `AddCrossNeedle()` (MeterManager.cs:22817-23002)

Dual crossing needles showing forward and reflected power simultaneously.

**Background:** QPainter vector-drawn cross-needle gauge face (or user PNG skin)

**Two crossing needles:**

1. **Forward Power (Primary)** — PWR reading
   - Black needle, red history (4000ms), StrokeWidth 2.5
   - Clockwise direction, NormaliseTo100W: true
   - Needle offset (0.322, 0.611), LengthFactor 1.62
   - 15-point calibration: 0/5/10/15/20/25/30/35/40/50/60/70/80/90/100W

2. **Forward Power Scale Labels** — NeedleScalePwrItem, 8 marks
   - Trebuchet MS Bold 16pt, Low=Gray, High=Red

3. **Reverse Power** — REVERSE_PWR reading
   - Black needle, cornflower blue history (4000ms), StrokeWidth 2.5
   - **CounterClockwise direction** — creates mirror effect
   - Needle offset (-0.322, 0.611) — **negative X for mirroring**
   - NormaliseTo100W: true
   - 19-point fine calibration: 0/0.25/0.5/0.75/1/2/3/4/5/6/7/8/9/10/12/14/16/18/20W

4. **Reverse Power Scale Labels** — NeedleScalePwrItem, 8 marks, CounterClockwise

### Edge Meter Display Mode

**Ported from:** Thetis `picMultiMeterDigital_Paint()` Edge mode (console.cs:23587-23663)

An alternative rendering style for signal meters — thin line indicator instead of filled bar.

- Three vertical lines at meter position: shadow-center-shadow
- Shadow line color: arithmetic mean of indicator color and background color
- Scale markers at bottom: low half (white) + high half (red)
- High-quality antialiased rendering

**Implementation:** Render mode flag on BarItem (or new EdgeMeterItem) rather than a
separate class. BarItem gains a `barStyle` property (Filled, Edge) to switch rendering.

**Edge meter colors (configurable in Setup, from Thetis console.cs:12612-12678):**
- `edgeMeterBackgroundColor` (default black)
- `edgeLowColor` (default white) — scale low half
- `edgeHighColor` (default red) — scale high half
- `edgeAvgColor` (default yellow) — indicator line
- `edgeBackgroundAlpha` (int 0-255, default 255) — transparency

**Measurement display mode switching (from Thetis MultiMeterMeasureMode enum):**
- SMeter format: `Common.SMeterFromDBM()`
- dBm format: value + " dBm"
- uV format: `Common.UVfromDBM()` + " uV"

---

## Phase 3G-5: Interactive Meter Items

**14 new interactive item types** plus MeterWidget mouse forwarding infrastructure.

### MeterWidget Mouse Event Forwarding

MeterWidget gains `mousePressEvent`, `mouseReleaseEvent`, `mouseMoveEvent`,
`wheelEvent` overrides:

1. Iterate items in reverse z-order (top-first)
2. Call `item->hitTest(localPos, widgetW, widgetH)` — base impl checks `pixelRect().contains()`
3. Dispatch to virtual methods on the hit item:
   - `handleMousePress(QMouseEvent*, int widgetW, int widgetH)`
   - `handleMouseRelease(QMouseEvent*, int widgetW, int widgetH)`
   - `handleWheel(QWheelEvent*, int widgetW, int widgetH)`
4. Base MeterItem provides default no-op implementations
5. Return value: `true` if handled (stops propagation), `false` to continue

### Shared ButtonBoxItem Base Class

**Ported from:** Thetis `clsButtonBox` (MeterManager.cs:12307+)

Common base for all button grid items (Band, Mode, Filter, Antenna, TuneStep, Other,
VoiceRecordPlay, Discord). Handles:

- Configurable grid layout (rows/columns)
- Rounded rectangle buttons with `fillRoundedRect()`
- Three states per button: fill, hover, click colors
- Indicator system with multiple types (from Thetis lines 12309-12327):
  - RING, BAR_LEFT, BAR_RIGHT, BAR_BOTTOM, BAR_TOP
  - DOT_LEFT, DOT_RIGHT, DOT_BOTTOM, DOT_TOP
  - DOT_TOP_LEFT, DOT_TOP_RIGHT, DOT_BOTTOM_LEFT, DOT_BOTTOM_RIGHT
  - TEXT_ICON_COLOUR
- Icon support (on/off state icons)
- Per-button font customization
- Click highlight timer (100ms visual feedback)
- `OverlayDynamic` layer

**Common Properties (from Thetis clsButtonBox):**
- `columns` (int) — grid column count
- `border` (float) — button border width
- `margin` (float) — spacing between buttons
- `radius` (float) — corner radius
- `heightRatio` (float) — button height/width ratio
- `visibleBits` (uint32) — bitmask for visible buttons
- Per-button: `fillColour`, `hoverColour`, `clickColour`, `borderColour`
- Per-button: `onColour`, `offColour`, `textColour`, `iconPath`
- Per-button: `indicatorType`, `indicatorWidth`
- `fadeOnRx`, `fadeOnTx` (bool) — disable in certain TX/RX states

### BandButtonItem — Band Selection Grid

**Ported from:** Thetis `clsBandButtonBox` (MeterManager.cs:11482+)

- Grid of band buttons: 160m, 80m, 60m, 40m, 30m, 20m, 17m, 15m, 12m, 10m, 6m, GEN
- Active band highlighted via value binding
- Colors: base `#1a2a3a`, active blue `#0070c0`/`#ffffff`, hover `#204060`, border `#205070`

**Mouse interaction:**
- Left-click → emits `bandClicked(int bandIndex)` signal
- Right-click → emits `bandStackRequested(int bandIndex)` signal
  (Thetis `PopupBandstack`, MeterManager.cs:11896)
- Alt+Click → drag mode for button reorder (from Thetis line 11175)

**Serialization tag:** `BANDBTNS`

### ModeButtonItem — Mode Selection Buttons

**Ported from:** Thetis `clsModeButtonBox` (MeterManager.cs:9951+)

- Buttons: LSB, USB, DSB, CWL, CWU, FM, AM, SAM, DIGL, DIGU
- Active mode highlighted via value binding
- Left-click → emits `modeClicked(int modeIndex)` signal
- No right-click action (matches Thetis)

**Serialization tag:** `MODEBTNS`

### FilterButtonItem — Filter Preset Buttons

**Ported from:** Thetis `clsFilterButtonBox` (MeterManager.cs:7674+)

- Buttons: Filter 1-10 + Var1, Var2
- Active filter highlighted via value binding
- Filter labels mode-dependent (set externally when mode changes)
- Left-click → emits `filterClicked(int filterIndex)` signal
- Right-click → emits `filterContextRequested(int filterIndex)` signal
  (Thetis `PopupFilterContextMenu`, MeterManager.cs:7917)

**Serialization tag:** `FILTERBTNS`

### AntennaButtonItem — Antenna Selection Buttons

**Ported from:** Thetis `clsAntennaButtonBox` (MeterManager.cs:9502+)

- 10 buttons: Rx Ant 1-3, Rx Aux 1-2 (Bypass/XVTR), Tx Ant 1-3, Rx/Tx toggle
- Color-coded by function: Rx=LimeGreen, Aux=Orange, Tx=Red/Gray, Toggle=Yellow
- Band-aware: responds to RX1/TX band changes via `antennasChanged()` callback
- Transverter support: detects XVTR bands (VHF0-VHF13)
- Button enable/disable with FadeOnRx/FadeOnTx
- Left-click → emits `antennaSelected(int index)` signal

**Properties (from Thetis lines 9502-9950):**
- `visibleBits` (int, default 1023) — 10-bit visibility mask

**Serialization tag:** `ANTENNABTNS`

### TuneStepButtonItem — Tuning Step Buttons

**Ported from:** Thetis `clsTunestepButtons` (MeterManager.cs:7999+)

- Dynamic button count from TuneStepList (1Hz, 10Hz, 100Hz, 1kHz, 10kHz, 100kHz, 1MHz)
- Button text = step name with "Hz" stripped
- Active step highlighted
- Left-click → emits `tuneStepSelected(int stepIndex)` signal
- Responds to `tuneStepIndexChanged(old, new)` callback

**Serialization tag:** `TUNESTEPBTNS`

### OtherButtonItem — Miscellaneous Control Buttons

**Ported from:** Thetis `clsOtherButtons` (MeterManager.cs:8225+)

Full-featured control button panel with macro support.

**Core buttons (from Thetis OtherButtonId enum):**
POWER, RX_2, MON, TUN, MOX, TWOTON, DUP, PS_A, PLAY, REC,
ANF, SNB, MNF, AVG, PEAK_HOLD, CTUN, VAC1, VAC2, MUTE, BIN,
SUBRX, PAN_SWAP, XPA, SPECTRUM, PANADAPTER, SCOPE, SCOPE2, PHASE,
WATERFALL, HISTOGRAM, PANAFALL, PANASCOPE, SPECTRASCOPE, DISPLAY_OFF

**Macro buttons (31 slots):** _MACRO_0 through _MACRO_30
- Per-macro: ButtonStateType (TOGGLE, CAT, CONT_VIS)
- Per-macro: OnState/OffState, OnText/OffText
- Per-macro: ContainerVisibleID (toggle container visibility)
- Per-macro: CatMacro (execute CAT command string)

**Mouse interaction:**
- Left-click → emits `otherButtonClicked(int buttonId)` signal
- Alt+Click → drag mode (reorder buttons, from Thetis lines 9211-9270):
  - Shift+Drag: swap two buttons
  - Plain drag: shift buttons between positions
- Macro buttons → emits `macroTriggered(int macroIndex)` signal

**Properties:**
- `visibleBits` (int[] groups) — multi-group visibility bitmask
- `dragable` (bool, default true)
- Per-button state properties: Power, RX2Enabled, MON, Tune, MOX, TwoTone, etc.
- `macroSettings` (array of per-macro config)

**Serialization tag:** `OTHERBTNS`

### VoiceRecordPlayItem — Voice Message Record/Play Buttons

**Ported from:** Thetis `clsVoiceRecordPlay` (MeterManager.cs:10222+)

DVK (Digital Voice Keyer) controls embedded in meter panel.

- Record/Play/Stop buttons for voice messages
- Left-click → emits `voiceAction(int action)` signal
- Action codes: Record, Play, Stop, Next, Previous

**Serialization tag:** `VOICERECPLAY`

### VfoDisplayItem — Frequency Display with Wheel-to-Tune

**Ported from:** Thetis `clsVfoDisplay` (MeterManager.cs:12881+)

- Three display modes (from Thetis VfoDisplayMode): VFO_A, VFO_B, VFO_BOTH
- Render states: VFO, BAND, MODE, FILTER, TUNE_STEP
- Displays frequency in `XX.XXX.XXX` MHz.kHz.Hz format with digit grouping
- Below frequency: mode label, filter label, band label
- Each digit is a hot zone for mouse wheel tuning
- Split indicator with dedicated color
- RX/TX state color coding

**Mouse interaction:**
- Mouse wheel over digit → emits `frequencyChangeRequested(int64_t deltaHz)`
- Right-click on band text → emits `bandStackRequested(int bandIndex)`
  (Thetis MeterManager.cs:13273)
- Right-click on filter text → emits `filterContextRequested(int filterIndex)`
  (Thetis MeterManager.cs:13287)
- Long-press detection (1s) for special actions (from Thetis line 13293)

**Properties (from Thetis clsVfoDisplay lines 12881-13500):**
- `displayMode` (enum: VfoA, VfoB, VfoBoth)
- `frequencyColour`, `frequencyColourSmall` (QColor)
- `modeColour`, `filterColour`, `bandColour` (QColor)
- `splitBackColour`, `splitColour` (QColor)
- `rxColour`, `txColour` (QColor)
- `digitHighlightColour` (QColor)
- `fontFamily`, `fontStyle`, `fontSize`

**Serialization tag:** `VFO`

### ClockItem — UTC/Local Time Display

**Ported from:** Thetis `clsClock` (MeterManager.cs:14075+)

- Dual display: Local time (left, 48% width) + UTC (right, 48% width)
- Time format: `HH:mm:ss` (24h) or `hh:mm:ss AP` (12h)
- Date line: `ddd d MMM yyyy` format
- `OverlayDynamic` layer with internal `QTimer` (1s interval)
- No mouse interaction (matches Thetis)

**Properties (from Thetis clsClock lines 14075-14165):**
- `show24Hour` (bool, default true)
- `showType` (bool) — show "Local" / "UTC" labels
- `timeColour` (QColor, `#c8d8e8`)
- `dateColour` (QColor, `#8090a0`)
- `typeTitleColour` (QColor, `#708090`)
- `fontFamily`, `fontStyle`, `fontSize`

**Serialization tag:** `CLOCK`

### ClickBoxItem — Interactive Click Region

**Ported from:** Thetis `clsClickBox` (MeterManager.cs:7571+)

Invisible hit target for mouse interaction overlay.

- No visual rendering (transparent)
- Provides hit test region for mouse wheel/click events
- Used as overlay on bars/text for unit cycling (S-units/dBm/uV)
- Emits `clicked()`, `wheelIncrement()`, `wheelDecrement()` signals

**Serialization tag:** `CLICKBOX`

### DataOutItem — External Data Output

**Ported from:** Thetis `clsDataOut` (MeterManager.cs:16047+)

Bridges meter readings to external systems via MMIO (Multi-Meter I/O).

- Supports UDP, TCP/IP, Serial Port, TCP Client transport modes
- Output formats: JSON, XML, RAW
- Publishes bound meter readings to external consumers
- No visual rendering (invisible item)
- Serialization tag: `DATAOUT`

**Properties:**
- `mmioGuid` (QString) — target MMIO device identifier
- `mmioVariable` (QString) — variable name to publish
- `outputFormat` (enum: Json, Xml, Raw)
- `transportMode` (enum: Udp, Tcp, Serial, TcpClient)

### DiscordButtonItem — Discord Integration Buttons

**Ported from:** Thetis `clsDiscordButtonBox` (MeterManager.cs:11983+)

Discord Rich Presence integration controls.

- Connect/Disconnect toggle, status display
- Left-click → emits `discordAction(int action)` signal

**Serialization tag:** `DISCORDBTNS`

### Signal Wiring Pattern

All interactive item signals propagate:
```
MeterItem (emits signal)
  → MeterWidget (re-emits or forwards)
    → ContainerWidget (re-emits)
      → ContainerManager (routes to MainWindow)
        → MainWindow (calls model setters)
```

Items never reference RadioModel or SliceModel directly.

---

## Phase 3G-6: Container Settings Dialog

User-facing UI for composing container contents and editing item properties.

### ContainerSettingsDialog

Modal `QDialog`, opened from container title bar settings button
(`ContainerWidget::settingsRequested()` signal, already wired).

**Three-panel layout:**

```
+------------------+---------------------------+------------------+
| Content List     | Property Editor           | Live Preview     |
| (200px fixed)    | (stretch)                 | (200px fixed)    |
|                  |                           |                  |
| [icon] S-Meter   | Range: [-140] to [0]     | +--------------+ |
| [icon] BarItem   | Color: [  #00b4d8  ]     | | Live meter   | |
| [icon] TextItem  | Red threshold: [___]     | | rendering    | |
|                  | Attack: [0.8]             | |              | |
|  [+ Add] [- Del] | Decay: [0.2]             | +--------------+ |
+------------------+---------------------------+------------------+
```

**Left panel — Content list:**
- Ordered list of current items (icon + type name + binding label)
- Drag handles for reorder (changes z-order and vertical stacking)
- Add/Remove buttons at bottom
- "Add" opens categorized picker popup

**Add Item picker categories:**
- **Meters:** BarItem, NeedleItem, DialItem, HistoryGraphItem, MagicEyeItem,
  SignalTextItem, NeedleScalePwrItem
- **Indicators:** LEDItem, TextItem, ClockItem, TextOverlayItem
- **Controls:** BandButtonItem, ModeButtonItem, FilterButtonItem, AntennaButtonItem,
  TuneStepButtonItem, OtherButtonItem, VoiceRecordPlayItem, VfoDisplayItem,
  DiscordButtonItem
- **Display:** FilterDisplayItem, RotatorItem
- **Layout:** SolidColourItem, ImageItem, WebImageItem, SpacerItem, FadeCoverItem,
  ClickBoxItem
- **Data:** DataOutItem

Each entry: small icon + name + one-line description. New item inserted after
current selection with sensible defaults.

**Center panel — Property editor:**
Switches form based on selected item type. All items share common properties
(position x/y/w/h sliders, z-order spinner, binding ID dropdown). Type-specific
properties as listed in Phase 3G-4 and 3G-5 sections above.

**Right panel — Live preview:**
A `MeterWidget` instance rendering the current item configuration in real-time.
Updates as properties change in the editor.

**Container-level properties** (tab or section above item list):
- Background color picker
- Border toggle
- Title text
- RX source dropdown (RX1, RX2, etc.)
- Show on RX / Show on TX toggles

### Import/Export

- **Export:** Serializes container item list + container properties via existing
  `MeterWidget::serializeItems()` + `ContainerWidget::serialize()`.
  Base64-encoded for clipboard safety. "Copy to Clipboard" button.
- **Import:** "Paste from Clipboard" button → Base64 decode → validate →
  confirmation dialog → replace current content.
- Format: existing pipe-delimited text, Base64-wrapped.

### Preset Browser

Built-in named presets via `ItemGroup` factory methods (extending existing pattern).
All Thetis MeterType presets available:

| Preset | Contents |
|--------|----------|
| S-Meter Only | NeedleItem S-meter, full container |
| S-Meter Bar (Signal) | BarItem + ScaleItem, instantaneous signal |
| S-Meter Bar (Avg) | BarItem + ScaleItem, averaged signal |
| S-Meter Bar (MaxBin) | BarItem + ScaleItem, spectral peak |
| S-Meter Text | SignalTextItem, large text readout |
| S-Meter + Bar | NeedleItem (top) + BarItem (bottom) |
| ANAN Multi Meter | 7-needle ANANMM composite |
| Cross Needle | Dual fwd/rev power crossing needles |
| Full Panel | S-Meter + Power/SWR + ALC + Mic + Comp (stacked) |
| TX Monitor | Power/SWR + ALC + Comp + LEDs (clip, SWR alarm) |
| Power/SWR | Power + SWR horizontal bars |
| ALC | ALC horizontal bar |
| ALC Gain | ALC gain horizontal bar |
| ALC Group | ALC group horizontal bar |
| Mic Level | Mic input horizontal bar |
| Compressor | Compressor horizontal bar |
| EQ Level | EQ level horizontal bar |
| Leveler | Leveler horizontal bar |
| Leveler Gain | Leveler gain horizontal bar |
| CFC | CFC horizontal bar |
| CFC Gain | CFC gain horizontal bar |
| ADC | ADC level horizontal bar |
| ADC MaxMag | ADC peak detection bar |
| AGC | AGC level horizontal bar |
| AGC Gain | AGC gain horizontal bar |
| PBSNR | Peak-to-baseline SNR bar |
| Magic Eye | MagicEyeItem tuning eye |
| VFO Display | VfoDisplayItem frequency display |
| Clock | ClockItem UTC/Local time |
| Contest | VfoDisplay + BandButtons + ModeButtons + Clock |
| Retro | MagicEyeItem + DialItem |
| History | HistoryGraphItem + TextItem readout |
| Rotator | RotatorItem compass dial |
| Filter Display | FilterDisplayItem mini passband |
| Custom Bar | User-defined bar with custom range |

Preset picker: dropdown at top of dialog + "Load Preset" button.
Loading replaces all current items (confirmation prompt).

### Persistence

All configuration persists through existing `AppSettings` XML system.
`ContainerWidget::serialize()` / `deserialize()` already handle container + item
state. This dialog provides the UI to edit what's serialized.

---

## Serialization Registry

The `ItemGroup::deserialize()` method needs a type tag registry for all types.

**Existing tags:** `BAR`, `SOLID`, `IMAGE`, `SCALE`, `TEXT`, `NEEDLE`

**New tags:**

| Tag | Class |
|-----|-------|
| `HISTORY` | HistoryGraphItem |
| `MAGICEYE` | MagicEyeItem |
| `DIAL` | DialItem |
| `LED` | LEDItem |
| `NEEDLESCALEPWR` | NeedleScalePwrItem |
| `SIGNALTEXT` | SignalTextItem |
| `ROTATOR` | RotatorItem |
| `TEXTOVERLAY` | TextOverlayItem |
| `SPACER` | SpacerItem |
| `FILTERDISPLAY` | FilterDisplayItem |
| `FADECOVER` | FadeCoverItem |
| `WEBIMAGE` | WebImageItem |
| `CLICKBOX` | ClickBoxItem |
| `BANDBTNS` | BandButtonItem |
| `MODEBTNS` | ModeButtonItem |
| `FILTERBTNS` | FilterButtonItem |
| `ANTENNABTNS` | AntennaButtonItem |
| `TUNESTEPBTNS` | TuneStepButtonItem |
| `OTHERBTNS` | OtherButtonItem |
| `VOICERECPLAY` | VoiceRecordPlayItem |
| `VFO` | VfoDisplayItem |
| `CLOCK` | ClockItem |
| `DATAOUT` | DataOutItem |
| `DISCORDBTNS` | DiscordButtonItem |

Each class implements `serialize()` / `deserialize()` following the existing
`TAG|x|y|w|h|bindingId|zOrder|...typeSpecificFields` pipe-delimited format.

---

## MeterBinding ID Extensions

Current bindings (MeterPoller.h): SignalPeak(0), SignalAvg(1), AdcPeak(2), AdcAvg(3),
AgcGain(4), AgcPeak(5), AgcAvg(6), TxPower(100), TxReversePower(101), TxSwr(102),
TxMic(103), TxComp(104), TxAlc(105).

**New bindings needed for full parity (from Thetis Reading enum):**

```cpp
// RX meters (0-49)
constexpr int SignalMaxBin   = 7;   // Spectral peak
constexpr int PbSnr          = 8;   // Peak-to-baseline SNR

// TX meters (100-149)
constexpr int TxEq           = 106;
constexpr int TxLeveler      = 107;
constexpr int TxLevelerGain  = 108;
constexpr int TxAlcGain      = 109;
constexpr int TxAlcGroup     = 110;
constexpr int TxCfc          = 111;
constexpr int TxCfcGain      = 112;

// Hardware readings (200+)
constexpr int HwVolts        = 200;
constexpr int HwAmps         = 201;
constexpr int HwTemperature  = 202;

// Rotator readings (300+)
constexpr int RotatorAz      = 300;
constexpr int RotatorEle     = 301;
```

---

## Files Modified / Created

### New files (Phase 3G-4 — passive items):
- `src/gui/meters/HistoryGraphItem.h/.cpp`
- `src/gui/meters/MagicEyeItem.h/.cpp`
- `src/gui/meters/DialItem.h/.cpp`
- `src/gui/meters/LEDItem.h/.cpp`
- `src/gui/meters/NeedleScalePwrItem.h/.cpp`
- `src/gui/meters/SignalTextItem.h/.cpp`
- `src/gui/meters/RotatorItem.h/.cpp`
- `src/gui/meters/TextOverlayItem.h/.cpp`
- `src/gui/meters/SpacerItem.h/.cpp`
- `src/gui/meters/FilterDisplayItem.h/.cpp`
- `src/gui/meters/FadeCoverItem.h/.cpp`
- `src/gui/meters/WebImageItem.h/.cpp`

### New files (Phase 3G-5 — interactive items):
- `src/gui/meters/ButtonBoxItem.h/.cpp` (shared base class)
- `src/gui/meters/BandButtonItem.h/.cpp`
- `src/gui/meters/ModeButtonItem.h/.cpp`
- `src/gui/meters/FilterButtonItem.h/.cpp`
- `src/gui/meters/AntennaButtonItem.h/.cpp`
- `src/gui/meters/TuneStepButtonItem.h/.cpp`
- `src/gui/meters/OtherButtonItem.h/.cpp`
- `src/gui/meters/VoiceRecordPlayItem.h/.cpp`
- `src/gui/meters/VfoDisplayItem.h/.cpp`
- `src/gui/meters/ClockItem.h/.cpp`
- `src/gui/meters/ClickBoxItem.h/.cpp`
- `src/gui/meters/DataOutItem.h/.cpp`
- `src/gui/meters/DiscordButtonItem.h/.cpp`

### New files (Phase 3G-6 — container dialog):
- `src/gui/containers/ContainerSettingsDialog.h/.cpp`

### Modified files:
- `src/gui/meters/MeterItem.h` — add virtual `hitTest()`, `handleMousePress()`,
  `handleMouseRelease()`, `handleWheel()` to base class
- `src/gui/meters/MeterWidget.h/.cpp` — add mouse event overrides + forwarding
- `src/gui/meters/ItemGroup.h/.cpp` — add all new preset factories, update deserialize registry
- `src/gui/meters/MeterPoller.h/.cpp` — add new MeterBinding IDs
- `src/gui/containers/ContainerWidget.h/.cpp` — wire settings button to dialog,
  forward interactive item signals
- `CMakeLists.txt` — add new source files

### Resource files:
- `resources/meters/` — Default gauge face PNG images (replaceable by user):
  - `ananMM.png` — ANANMM main meter face
  - `ananMM-bg.png` — ANANMM RX background
  - `ananMM-bg-tx.png` — ANANMM TX background
  - `cross-needle.png` — CrossNeedle main meter face
  - `cross-needle-bg.png` — CrossNeedle bottom band
  - `eye-bezel.png` — MagicEye bezel overlay
  - `rotator_az-bg.png` — Rotator azimuth background
  - `rotator_ele-bg.png` — Rotator elevation background
  - `rotator_both-bg.png` — Rotator dual-mode background
  - `rotator_map-bg.png` — Rotator map overlay
  (Names match Thetis `loadImages()` at MeterManager.cs:2426 for skin compatibility.
  Default images generated in NereusSDR dark theme style. Users can replace via
  ContainerSettingsDialog file picker or by dropping PNGs in the meters directory.)

---

## Design Decisions

1. **Ring buffer over List+cleanup** for HistoryGraphItem — O(1) insert vs Thetis's
   O(n) removeAt(0). Fixed capacity is simpler and more predictable.

2. **Separate files per item type** rather than adding to MeterItem.h/.cpp —
   MeterItem.cpp is already 12k+ tokens. Each new type gets its own .h/.cpp pair.

3. **Signal-based interaction** — interactive items emit Qt signals, never touch
   RadioModel directly. Wiring happens at ContainerManager/MainWindow level.

4. **File-based gauge face images** — ANANMM, CrossNeedle, Rotator, and MagicEye
   background images loaded from `resources/meters/` (PNG files shipped with the app).
   Users can replace images from the ContainerSettingsDialog (file picker in the
   property editor). This matches Thetis's `%AppData%\OpenHPSDR\Meters\` pattern and
   enables skin-based customization. Default images are QPainter-rendered PNGs
   generated at build time or first run, matching the NereusSDR dark theme. The image
   path is stored per-item in the serialization data, so each container can use
   different gauge faces.

5. **ButtonBoxItem base class** — shared grid layout, indicator system, and styling
   logic factored out of 8 concrete button types, matching Thetis clsButtonBox hierarchy.

6. **Edge meter as BarItem render mode** — not a separate class. BarItem gains a
   `barStyle` property (Filled/Edge) to switch rendering, avoiding class proliferation.

7. **1:1 calibration point preservation** — all ANANMM and CrossNeedle scale
   calibration points preserved exactly from Thetis source with origin comments.

8. **Full macro system** for OtherButtonItem — 31 macro slots with CAT command
   execution, container visibility toggles, and custom state management.
   Matching Thetis OtherButtonMacroSettings exactly.
