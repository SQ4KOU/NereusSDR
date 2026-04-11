# Next Session Prompt: Phase 3G-4 through 3G-6 (Advanced Meters + Container Polish)

> Copy-paste this as the opening message for the next Claude Code session.

---

I'm working on NereusSDR, an SDR console app (Qt6/C++20). We just completed Phase 3-UI (full UI skeleton with 12 applets, overlay panel, menus, setup dialog, status bar) and all documentation is updated.

**Current state:** Branch `main`, commit c1d6113. The app runs, connects to an ANAN-G2 radio via Protocol 2, receives/demodulates audio, displays live spectrum+waterfall, has a full UI skeleton with 150+ NYI control widgets, and persists container state between restarts.

**What's next:** Phase 3G-4 → 3G-5 → 3G-6 (Advanced Meters + Container Polish)

## Phase 3G-4: Advanced Meter Items

Add new MeterItem types to the existing MeterWidget GPU rendering pipeline:

1. **HistoryGraphItem** — scrolling time-series graph (signal strength over time)
   - Ring buffer of values, QPainter line graph in OverlayDynamic layer
   - Configurable time window (10s, 30s, 60s, 5m)
   - Grid lines + dB scale labels
   - From Thetis: MeterManager.cs history display mode

2. **MagicEyeItem** — vacuum tube magic eye meter (decorative/retro)
   - Green phosphor arc that opens/closes with signal strength
   - QPainter gradient fill in OverlayDynamic layer
   - From Thetis: not present, new NereusSDR feature inspired by vintage receivers

3. **DialItem** — circular dial meter (alternative to NeedleItem arc)
   - Full 270° arc with needle pointer
   - Tick marks + labels around circumference
   - From AetherSDR: alternative meter style option

4. **LEDItem** — colored LED indicator dot
   - States: off (grey), green, amber, red
   - Used for TX status, clipping, SWR alarm, PS status
   - From Thetis: various status indicator LEDs

**Key files:**
- `src/gui/meters/MeterItem.h` — add new item subclasses (each is a MeterItem with renderLayer(), paint(), optionally emitVertices())
- `src/gui/meters/MeterItem.cpp` — implementations
- `src/gui/meters/ItemGroup.cpp` — new preset factories

**Existing infrastructure to use:**
- MeterItem base class with normalized 0-1 positioning, 4 render layers
- MeterWidget GPU pipeline (Background texture, Geometry vertices, OverlayStatic, OverlayDynamic)
- ItemGroup::installInto() for compositing
- MeterPoller for live data binding (100ms/10fps)

## Phase 3G-5: Interactive Meter Items

Add meter items that respond to user interaction (clicks, drags):

1. **BandButtonItem** — clickable band selection buttons inside meter widget
   - Row of HF band buttons (160m-6m), click to tune
   - From Thetis: band button bar in console front panel

2. **ModeButtonItem** — clickable mode selection buttons
   - LSB/USB/CW/AM/FM/DIGL/DIGU modes
   - Highlight current mode, click to switch

3. **FilterButtonItem** — filter preset buttons (1-10 + Var1/Var2)
   - Click to select filter width preset

4. **VfoDisplayItem** — frequency display inside meter widget
   - Large digit display with MHz/kHz/Hz grouping
   - Wheel-to-tune, click digit to edit

5. **ClockItem** — UTC/Local time display
   - Updated every second, configurable format

**Key pattern:** These items need mouse event handling. MeterWidget will need to forward mouse events to items via `hitTest()` → `handleClick()`/`handleWheel()`.

## Phase 3G-6: Container Settings Dialog

Full container composability UI:

1. **ContainerSettingsDialog** — per-container settings
   - Content list with drag-to-reorder
   - Add/remove MeterItem or AppletWidget content
   - Background color picker
   - Border toggle
   - Title/notes editor
   - RX source selector
   - Show on RX/TX toggles

2. **Import/Export** — Base64 container configs
   - Copy container layout to clipboard
   - Paste to create duplicate container
   - Share meter layouts between users

3. **Preset browser** — built-in container presets
   - "S-Meter Only", "Full Panel", "TX Monitor", "Contest", etc.

**Reference:**
- Read CLAUDE.md for project conventions and source-first porting protocol
- Read STYLEGUIDE.md for visual design constants
- Read src/gui/meters/MeterItem.h for the existing MeterItem API
- Read src/gui/meters/MeterWidget.h for the GPU rendering pipeline
- Read src/gui/meters/ItemGroup.h for preset composition pattern
- Read src/gui/containers/ContainerWidget.h for container API
- AetherSDR source at ../AetherSDR/ for visual reference
- Thetis source at ../Thetis/ for feature/endpoint reference

**Build:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j$(nproc)`

**Run:** `open build/NereusSDR.app` (macOS) or `./build/NereusSDR` (Linux)

Start by reading CLAUDE.md and the meter system files, then create an implementation plan in docs/architecture/ before writing code.
