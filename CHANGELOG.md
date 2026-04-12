# Changelog

## [Unreleased]

### In Progress — Phase 3G-6 (One-Shot): Container Settings Dialog + Full Thetis Parity + MMIO

**Status:** Plan frozen, execution pending. See [`docs/architecture/phase3g6a-plan.md`](docs/architecture/phase3g6a-plan.md) for the authoritative plan and [`docs/architecture/phase3g6-oneshot-handoff.md`](docs/architecture/phase3g6-oneshot-handoff.md) for the execution handoff.

Phase 3G-6 was originally landed as WIP after a prior session built the dialog infrastructure but left it with multiple rendering bugs and a minimal property editor. A debug pass (see `docs/architecture/phase3g6-debug-handoff.md`) identified:

- ANANMM/CrossNeedle backgrounds referenced missing `resources/meters/*.png` paths not registered as Qt resources.
- `NeedleScalePwrItem` did not honor `onlyWhenRx`/`onlyWhenTx`/`displayGroup`, causing scale label collisions on multi-needle presets.
- Live preview panel rendered as solid black when editing Container #0 (nested QRhiWidget in modal dialog, edit-path-specific).
- Per-item property editors existed for only 6 of ~30 item types, each minimal.
- No menu-based access to floating containers other than Container #0.

An interview pass decided the fix is a single one-shot phase covering the entire Thetis meter-configuration surface rather than incremental 3G-6a/6b/6c sub-phases. Scope includes:

- Rendering fixes: Qt-resource-registered meter backgrounds (ananMM, cross-needle), `displayGroup` filtering ported from Thetis `clsMeterItem`, scale label clean-up.
- Dialog rewrite to Thetis's 3-column layout (container dropdown, Available / In-use / Properties columns).
- Replace live preview with in-place editing + snapshot/revert on Cancel.
- Thetis-parity container-level controls: Lock, Notes, Highlight for setup, Title-bar toggle, Minimises, Auto height, Hides-when-RX-not-used, Duplicate.
- Per-item property editors for every implemented MeterItem subclass (~30 types), 100% Thetis parity on exposed fields.
- MMIO (Multi-Meter I/O) variable system: TCP/UDP/serial transports, variable registry, picker UI, parse rules, thread-safe value cache, persistence. New `src/core/mmio/` subsystem.
- Containers menu submenu listing all open containers for menu-based editing.
- Copy/Paste item settings, Reset Default Layout verification.

**Branch:** `feature/phase3g6-container-settings-dialog` (PR #1, will be force-updated as execution proceeds).
**Commit plan:** ≈48 atomic GPG-signed commits in 7 review blocks.
**User-supplied assets required before execution:** `resources/meters/ananMM.png` (in place), `resources/meters/cross-needle.png` (pending), `resources/meters/cross-needle-bg.png` (pending).

### Added — Phase 3G-5: Interactive Meter Items

**Mouse event infrastructure:**
- MeterItem base class gains virtual `hitTest()`, `handleMousePress()`, `handleMouseRelease()`, `handleMouseMove()`, `handleWheel()` with default no-op implementations
- MeterWidget forwards mouse events to items in reverse z-order (top-first hit dispatch)

**ButtonBoxItem shared base class** (from Thetis clsButtonBox, MeterManager.cs:12307+):
- Configurable grid layout (rows/columns), rounded rectangle buttons
- 13 indicator types: Ring, Bar (L/R/B/T), Dot (L/R/B/T/TL/TR/BL/BR), TextIconColour
- Three-state colors per button (fill/hover/click), on/off state
- 100ms click highlight timer (from Thetis `setupClick` pattern)
- FadeOnRx/FadeOnTx visibility guards, visibility bitmask

**8 interactive button grid items:**
- `BandButtonItem` — 12 band buttons (160m-6m, GEN), left-click select + right-click bandstack popup (from Thetis clsBandButtonBox)
- `ModeButtonItem` — 10 mode buttons (LSB-DIGU), left-click select (from Thetis clsModeButtonBox)
- `FilterButtonItem` — 12 filter buttons (F1-F10, Var1/2), mode-dependent labels, right-click context menu (from Thetis clsFilterButtonBox)
- `AntennaButtonItem` — 10 color-coded buttons: Rx (green), Aux (orange), Tx (red), Toggle (yellow) (from Thetis clsAntennaButtonBox)
- `TuneStepButtonItem` — 7 step buttons (1Hz-1MHz) with active highlighting (from Thetis clsTunestepButtons)
- `OtherButtonItem` — 34 core control buttons + 31 macro slots with Toggle/CAT/ContainerVis modes (from Thetis clsOtherButtons)
- `VoiceRecordPlayItem` — 5 DVK buttons (REC/PLAY/STOP/NEXT/PREV) (from Thetis clsVoiceRecordPlay)
- `DiscordButtonItem` — 12 Discord Rich Presence buttons (from Thetis clsDiscordButtonBox)

**3 standalone interactive items:**
- `VfoDisplayItem` — frequency display in XX.XXX.XXX format with per-digit mouse wheel tuning, mode/filter/band labels, RX/TX indicator, split indicator (from Thetis clsVfoDisplay)
- `ClockItem` — dual Local + UTC time display with 1s auto-refresh, 24h/12h toggle (from Thetis clsClock)
- `ClickBoxItem` — invisible hit region for unit cycling overlays (from Thetis clsClickBox)

**1 data item:**
- `DataOutItem` — MMIO external data bridge with JSON/XML/RAW output formats and UDP/TCP/Serial transport (from Thetis clsDataOut)

**Signal-based interaction:** all interactive items emit Qt signals, never touch RadioModel directly. Signal chain: MeterItem → MeterWidget → ContainerWidget → MainWindow.

**ContainerWidget signal forwarding:** `wireInteractiveItem()` connects band/mode/filter/antenna/tuneStep/other/macro/voice/discord/VFO signals from MeterItems to container-level signals.

**ItemGroup extensions:** 13 new type tags in deserialize registry. 3 new presets: VFO Display, Clock, Contest (VFO + band buttons + mode buttons + clock).

### Added — Phase 3G-4: Advanced Meter Items

**12 new MeterItem types (passive/display):**
- `SpacerItem` — layout spacer with gradient fill (from Thetis clsSpacerItem)
- `FadeCoverItem` — RX/TX transition overlay (from Thetis clsFadeCover)
- `LEDItem` — LED indicator: 3 shapes (Square/Round/Triangle), 2 styles (Flat/ThreeD), blink/pulsate (from Thetis clsLed)
- `HistoryGraphItem` — scrolling time-series with dual-axis ring buffer, auto-scaling (from Thetis clsHistoryItem)
- `MagicEyeItem` — vacuum tube magic eye with green phosphor arc (from Thetis clsMagicEyeItem)
- `NeedleScalePwrItem` — non-linear power scale labels for needle arcs (from Thetis clsNeedleScalePwrItem)
- `SignalTextItem` — S-units/dBm/uV signal display with peak hold and bar styles (from Thetis clsSignalText)
- `DialItem` — circular dial with VFO/ACCEL/LOCK quadrant buttons (from Thetis clsDialDisplay)
- `TextOverlayItem` — two-line text with %VARIABLE% substitution parser (from Thetis clsTextOverlay)
- `WebImageItem` — async web-fetched image with periodic refresh (from Thetis clsWebImage)
- `FilterDisplayItem` — mini passband spectrum/waterfall with filter edges and notch overlays (from Thetis clsFilterItem)
- `RotatorItem` — antenna rotator compass dial with AZ/ELE/BOTH modes (from Thetis clsRotatorItem)

**Complex composite presets:**
- ANANMM 7-needle multi-meter (signal, volts, amps, power, SWR, compression, ALC) with exact Thetis calibration points
- CrossNeedle dual fwd/rev power meter with mirrored CounterClockwise geometry

**NeedleItem extensions:** scaleCalibration, needleOffset, radiusRatio, lengthFactor, direction (CW/CCW), onlyWhenRx/Tx, displayGroup, historyEnabled/Color, normaliseTo100W

**Edge meter display mode:** BarItem gains Filled/Edge style with 3-line indicator (from Thetis Edge meter)

**15+ bar preset factories:** Signal, AvgSignal, MaxBin, ADC, AGC, PBSNR, EQ, Leveler, ALC variants, CFC variants

**MeterBinding extensions:** SignalMaxBin(7), PbSnr(8), TxEq-TxCfcGain(106-112), HwVolts/Amps/Temp(200-202), RotatorAz/Ele(300-301)

### Fixed — Applet Panel Layout
- RxApplet: fix STEP row overflow (138px content in 99px column), change filter grid
  from 2×5 to 3-column layout matching AetherSDR, remove fixedWidth from RIT/XIT
  labels so they flex with panel width, add Expanding sizePolicy to RIT/XIT toggles
- TxApplet: add outer→body wrapping pattern, merge MOX/TUNE/ATU/MEM into single
  4-button row matching AetherSDR, fix value label styles and slider label colors
- PhoneCwApplet: add missing applyComboStyle() to mic combos, set Ignored sizePolicy
  on CW QSK/Iambic/FW Keyer buttons for even spacing, flex CW pitch label
- EqApplet: add outer→body wrapping, reduce scale/spacer widths for 10-band fit,
  reduce band label font to 9px, add applyComboStyle() to preset combo

### Added — Phase 3-UI: Full UI Skeleton

**Applets (12 total, 150+ control widgets):**
- `src/gui/applets/RxApplet.h/.cpp` — RX controls: mode, AGC, AF/RF gain, filter presets; Tier 1 wired to SliceModel
- `src/gui/applets/TxApplet.h/.cpp` — TX controls: mic gain, drive, tune power, compression, DEX
- `src/gui/applets/PhoneCwApplet.h/.cpp` — Voice/CW macro keys, VOX, CW speed/weight
- `src/gui/applets/EqApplet.h/.cpp` — 10-band RX/TX graphic equalizer
- `src/gui/applets/FmApplet.h/.cpp` — FM deviation, sub-tone, repeater offset
- `src/gui/applets/DigitalApplet.h/.cpp` — digital mode settings (baud, shift, encoding)
- `src/gui/applets/PureSignalApplet.h/.cpp` — PureSignal calibrate, feedback level indicator
- `src/gui/applets/DiversityApplet.h/.cpp` — diversity RX phase/gain controls
- `src/gui/applets/CwxApplet.h/.cpp` — CW message memory keyer
- `src/gui/applets/DvkApplet.h/.cpp` — digital voice keyer playback controls
- `src/gui/applets/CatApplet.h/.cpp` — CAT/rigctld port configuration
- `src/gui/applets/TunerApplet.h/.cpp` — antenna tuner control (tune, bypass, memory)

**Spectrum Overlay Panel:**
- `src/gui/SpectrumOverlayPanel.h/.cpp` — 10-button overlay panel with 5 flyout sub-panels (display, filter, noise, spots, tools); auto-close on outside click

**Menu Bar (9 menus, ~60 items):**
- File, Radio, View, DSP, Band, Mode, Containers, Tools, Help menus wired to MainWindow

**Status Bar:**
- Double-height status bar (46px): UTC clock, radio info, TX/RX indicators, signal level

**Setup Dialog:**
- `src/gui/SetupDialog.h/.cpp` — 47 pages across 10 categories (Radio, Audio, DSP, Display, CW, Digital, TX, Logging, Network, Misc); all pages have real controls

**Applet Panel:**
- `src/gui/AppletPanelWidget.h/.cpp` — fixed header (S-Meter) + scrollable applet body for Container #0

**Infrastructure:**
- `src/gui/StyleConstants.h` — shared color palette, font sizes, widget style constants
- `src/gui/widgets/HGauge.h/.cpp` — horizontal bar gauge widget
- `src/gui/widgets/ComboStyle.h/.cpp` — styled combo box shared across applets

**Container / Persistence fixes:**
- Content restored on restart (applet panel state persisted)
- Pin-on-top state persists across sessions
- Floating container position persists correctly
- Container minimum width 260px enforced in all dock modes

**S-Meter enhancements:**
- Dynamic resize with aspect-ratio scaling and full-width arc
- SMeterWidget ported directly from AetherSDR for pixel-identical fidelity

### Added — Phase 3G-3: Core Meter Groups (in progress)
- NeedleItem: arc-style S-meter needle participating in all 4 GPU render pipelines
  - P1 Background: QPainter arc segments (white S0-S9, red S9+, blue inner TX arc)
  - P2 Geometry: uniform-width needle quad + shadow + amber peak marker triangle
  - P3 OverlayStatic: tick marks and labels (1,3,5,7,9,+20,+40) via needleDir vector
  - P3 OverlayDynamic: S-unit readout (cyan) + dBm readout (light steel)
- Multi-layer MeterItem infrastructure: participatesIn() + paintForLayer() virtuals
- Exponential smoothing (SMOOTH_ALPHA=0.3 from AetherSDR) + peak hold (Medium preset)
- S-unit scaling from Thetis: S0=-127dBm, S9=-73dBm, 6dB/S-unit
- Aspect-ratio-locked meterRect() helper (2:1 matching AetherSDR sizeHint 280x140)
- TX MeterBinding constants 100-105 (Power, ReversePower, SWR, Mic, Comp, ALC)
- Preset factories: createSMeterPreset, createPowerSwrPreset, createAlcPreset, createMicPreset, createCompPreset
- ItemGroup::installInto() for positioning groups within MeterWidget sub-regions
- Default Container #0 layout: S-Meter (55%) + Power/SWR (30%) + ALC (15%)
- NEEDLE serialization format + deserializer registration
- NOTE: S-meter will be re-implemented as dedicated SMeterWidget (direct AetherSDR port)
  for pixel-identical fidelity — the MeterItem/NeedleItem approach introduces rendering
  artifacts from GPU pipeline splitting and texture-based text

### Added — Phase 3G-2: MeterWidget GPU Renderer
- MeterWidget (QRhiWidget): 3-pipeline GPU rendering engine for meters inside ContainerWidget
  - Pipeline 1 (textured quad): cached background textures and images
  - Pipeline 2 (vertex-colored geometry): animated bar fills with per-frame vertex updates
  - Pipeline 3 (QPainter overlay, split static/dynamic): tick marks, labels, value readouts
- MeterItem base class with normalized 0-1 positioning and data binding
- Concrete item types: BarItem (H/V), TextItem, ScaleItem (H/V), SolidColourItem, ImageItem
- BarItem exponential smoothing (attack 0.8 / decay 0.2, from Thetis MeterManager.cs)
- ItemGroup composite with createHBarPreset() factory (AetherSDR visual style)
- MeterPoller: QTimer-based WDSP meter polling at 100ms via RxChannel::getMeter()
- MeterBinding constants mapping to RxMeterType (SignalPeak through AgcAvg)
- Pipe-delimited item serialization compatible with ContainerWidget persistence
- Container #0 pre-populated with live horizontal signal strength bar meter
- Shaders: meter_textured.vert/.frag (Pipelines 1 & 3), meter_geometry.vert/.frag (Pipeline 2)
- lcMeter logging category

### Added — Phase 3G-1: Container Infrastructure
- ContainerWidget: dock/float/resize container shell with title bar, axis-lock (8 positions), content slot
- FloatingContainer: top-level window wrapper with pin-on-top, minimize-with-console, geometry persist
- ContainerManager: singleton lifecycle — 3 dock modes (PanelDocked/OverlayDocked/Floating)
- QSplitter-based MainWindow layout: spectrum left, Container #0 right panel
- Axis-lock repositioning: docked containers track main window resize per anchor position
- 24-field pipe-delimited serialization (Thetis-compatible + DockMode extension)
- Container + splitter state persistence to AppSettings
- Hover-reveal title bar with dock-mode-aware buttons (float/dock, axis cycle, pin, settings)
- Wider hover detection zone (40px) for reliable title bar reveal on high-DPI displays
- Frameless floating containers on macOS (no native title bar, container provides its own)
- Ctrl-snap to 10px grid during drag and resize
- Phase roadmap: AetherSDR-style AppletPanel planned for Container #0 in Phase 3G-AP

### Added — Phase 3E: VFO & Controls + CTUN Panadapter
- Floating VFO flag widget (AetherSDR pattern): frequency display, mode/filter/AGC tabs, antenna buttons
- SliceModel: DSPMode enum, AGCMode, per-mode filter defaults from Thetis, tuning step, AF/RF gain
- SmartSDR-style CTUN panadapter: independent pan center and VFO, WDSP shift offsets
- Off-screen VFO indicator with double-click to recenter
- I/Q pipeline rewired through ReceiverManager with DDC-aware routing
- Alex HPF/LPF/BPF filter registers ported from Thetis (frequency-based selection)
- Full-spectrum FFT output (all N bins, FFT-shift + mirror for correct sideband orientation)
- VFO settings persistence (frequency, mode, filter, AGC, step, gains, antennas)
- AetherSDR-style VFO control buttons and green toggle filter presets
- CTUN zoom: frequency scale bar drag and Ctrl+scroll zoom into FFT bin subsets
- Hybrid zoom: smooth bin subsetting during drag, FFT replan on mouse release
- visibleBinRange() maps display window to DDC bin indices (GPU + CPU + waterfall)
- DDC center frequency tracking (m_ddcCenterHz) for correct bin-to-frequency mapping
- Waterfall preserves existing rows across zoom changes (new rows at current scale)

### Added — Phase 3D: GPU Spectrum & Waterfall
- FFTEngine: FFTW3 float-precision on dedicated spectrum worker thread
- SpectrumWidget: QRhiWidget GPU rendering via Metal (macOS) / Vulkan / D3D12
- 3 GPU pipelines: waterfall (ring-buffer texture), spectrum (vertex-colored), overlay (QPainter→texture)
- 6 GLSL 4.40 shaders compiled to QSB via qt_add_shaders()
- 4096-point FFT, Blackman-Harris 4-term window, 30 FPS rate limiting
- VFO marker, filter passband overlay, cursor frequency readout
- Mouse interaction: scroll zoom, drag ref level, click-to-tune
- Right-click SpectrumOverlayMenu with display settings
- AetherSDR default + Thetis Enhanced/Spectran/BlackWhite color schemes

### Added — Phase 3C: macOS Build
- WDSP linux_port.h cross-platform compat (stdlib.h, string.h, fcntl.h, LPCRITICAL_SECTION)
- ARM64 flush-to-zero guard, AVRT stubs
- Fixed use-after-free crash: wisdom poll timer accessing deleted QThread

### Added — Phase 3B: WDSP Integration + Audio Pipeline
- WDSP v1.29 built as static library in third_party/wdsp/
- RxChannel wrapper: I/Q accumulation (238→1024), fexchange2(), NB1/NB2
- AudioEngine: QAudioSink 48kHz stereo Int16, 10ms timer drain
- FFTW wisdom generation with progress dialog, cached for subsequent launches
- Audio device selection and persistence via AppSettings

### Added — Phase 3A: Radio Connection (Protocol 2)
- P2RadioConnection faithfully ported from Thetis ChannelMaster/network.c
- P2 discovery (60-byte packet, byte[4]=0x02) on all network interfaces
- CmdGeneral/CmdRx/CmdTx/CmdHighPriority byte-for-byte from Thetis
- I/Q data streaming: DDC2, 1444 bytes/packet, 238 samples at 48kHz
- ConnectionPanel dialog with discovered radio list, connect/disconnect
- LogManager with runtime category toggles, AppSettings persistence
- SupportDialog with log viewer, category checkboxes, support bundle creation
- Auto-reconnect to last connected radio via saved MAC

## [0.1.0] - 2026-04-08

### Added
- Initial project scaffolding
- CMake build system with Qt6 and C++20
- Stub source files (AppSettings, RadioDiscovery, RadioConnection, WdspEngine, RadioModel, MainWindow)
- Documentation (README, CLAUDE.md, CONTRIBUTING, STYLEGUIDE)
- CI workflow (GitHub Actions)
- Phase 1 analysis document stubs
