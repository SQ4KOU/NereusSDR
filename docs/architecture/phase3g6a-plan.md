# Phase 3G-6 (One-Shot) — Full Thetis-Parity Rewrite: Container Settings Dialog + All Meter Item Property Editors + MMIO

**Status:** Plan — pending approval
**Branch:** `feature/phase3g6-container-settings-dialog` (continues from 3G-6 WIP)
**Supersedes:** `phase3g6-container-settings-dialog-plan.md` and the earlier 3G-6a/6b/6c phased draft of this document
**Scope note:** This is a deliberately massive single-phase effort targeting 100% Thetis parity for the meter system's user-facing configuration surface, including the Multi-Meter I/O (MMIO) external-data subsystem. Expect 3000–6000 lines of code across 40+ files. Review strategy: many small, atomic commits on one branch, one big PR.

---

## Context

Phase 3G-6 landed infrastructure for a Container Settings Dialog with a 3-panel layout, 30+ preset factories, import/export, and 6 partial per-type property editors. Visual debugging in a separate pass (see `phase3g6-debug-handoff.md`) uncovered meter rendering bugs (missing PNG backgrounds, overlapping scale labels, a broken live preview) plus a major UX gap: the dialog exposes maybe 5% of Thetis's actual configuration surface.

After a decision interview, the direction is a **single big-bang phase** that:

1. Fixes every known rendering bug in the meter pipeline.
2. Restructures the dialog UI to Thetis's 3-column layout.
3. Replaces the broken live preview with in-place editing (snapshot + revert on Cancel).
4. Ports **every container-level control** Thetis exposes.
5. Builds **per-item property editors for every item type** currently implemented in NereusSDR — 30+ classes, ~200+ fields.
6. Ports Thetis's **MMIO (Multi-Meter I/O) variable system** — TCP/UDP/serial transports, variable registry, variable picker UI, parse rules, thread-safe value cache, persistence.
7. Adds a Containers menu submenu so any container is reachable from the menu bar.

There are no deferred sub-phases. The earlier 3G-6a/6b/6c split is discarded at the user's direction.

## Goals

1. Every ANANMM / CrossNeedle / core preset renders correctly (background images load via Qt resources, needles point at the right calibration points, scale labels respect mode filtering).
2. Dialog visually and functionally matches Thetis's `tpMultiMetersIO` 3-column layout.
3. The dialog's preview panel is gone; edits apply live to the actual container with snapshot+revert semantics.
4. Every `ContainerWidget` configuration field Thetis exposes is editable, persistent, and honored at runtime.
5. Every MeterItem subclass has a dedicated property editor that exposes all Thetis-parity fields for that type.
6. MMIO variables can be defined, bound to meter items, and displayed exactly as Thetis does.
7. The Containers menu lets the user edit any open container by name, not only Container #0.

## Non-goals

| Out of scope | Reason |
|---|---|
| Per-band meter configs | Thetis doesn't have this either. |
| Thetis's scripting engine (CAT scripts → meter readings) | Separate subsystem; tracked in 3K (CAT/rigctld phase). |
| Legacy Thetis skin import/export | Tracked in 3H (Skins). |
| Moving the dialog into NereusSDR's `SetupDialog` as an embedded tab | User decision: keep standalone. |

Everything else is in scope.

## Decisions (from interview, final)

| Question | Decision |
|---|---|
| Dialog purpose | Power-user workbench. |
| Sequencing | Big-bang one-shot — collapse 3G-6a/6b/6c into a single phase. |
| Live preview fate | Delete it. In-place editing with snapshot + revert. |
| Cancel semantics | Snapshot container state on open → live edits → revert on Cancel, keep on OK/Close. |
| Cross-switch via container dropdown | Auto-commit current edits, take new snapshot of newly-selected container. |
| New container + Cancel | Cancel destroys the newly-created container (keep current MainWindow wiring). |
| Menu-based container access | Submenu listing all open containers **and** gear icon on each container's title bar. |
| Container-level property parity | All 8 decided fields: Highlight, Notes, Duplicate, Title-bar toggle, Lock, Hides-when-RX-not-used, Minimises, Auto container height. |
| Per-item property scope | **100% Thetis parity for every implemented item type.** |
| onlyWhenRx / onlyWhenTx / displayGroup filter fix | Port Thetis's exact rule into NeedleScalePwrItem and any other filtered items. |
| ANANMM background | Ship as Qt resource (already in place at `resources/meters/ananMM.png`). |
| CrossNeedle preset | User supplies `cross-needle.png` and `cross-needle-bg.png` before the phase executes; wired via Qt resources. |
| Copy / Paste item settings | In scope. Clipboard keyed by item type. |
| Dialog layout | Mirror Thetis 3-column 1:1. |
| Dialog housing | Standalone dialog (not a SetupDialog tab). |
| MMIO | In scope, fully. Transports (TCP/UDP/serial), variable registry, variable picker UI, parse rules, thread-safe cache, persistence. |
| Reset Default Layout | Verify it works; fix if broken. |
| Per-container Recover button | Not in scope. |
| Branch strategy | Continue on `feature/phase3g6-container-settings-dialog`; many small GPG-signed commits; one big PR (force-update PR #1). |

## Architecture changes

### 1. Dialog re-layout (Thetis 3-column)

```
 ┌─ Container Settings ─────────────────────────────────────────┐
 │ Container: [Main Panel                          ▾] [Dup][Del]│
 │ Title: [_____] Notes: [________]  ☐Border ☐Lock ☐Hide title  │
 │ ☐Show RX ☐Show TX ☐Minimises ☐Auto height ☐Hide when not used│
 │ ☐Highlight for setup   RX source: ○RX1 ●RX2    Bg: [▇]       │
 ├──────────────────────────────────────────────────────────────┤
 │  Available          In-use items         Properties          │
 │  ┌────────────┐     ┌────────────┐       ┌─────────────────┐ │
 │  │ RX ────    │[Add>│ S-Meter    │ ↑     │ [dynamically    │ │
 │  │  Signal    │     │ Scale      │ ↓     │  swapped per    │ │
 │  │  Avg Sig.  │[<Rm]│ Text       │       │  selected item  │ │
 │  │  ...       │     │ Bar        │       │  type]          │ │
 │  │ TX ────    │     │ ...        │       │                 │ │
 │  │  MIC       │     │            │       │ [Copy Settings] │ │
 │  │  ALC       │     │            │       │ [Paste Settings]│ │
 │  │  ...       │     │            │       │                 │ │
 │  │ Special ── │     │            │       │                 │ │
 │  │  MagicEye  │     │            │       │                 │ │
 │  │  ANANMM    │     │            │       │                 │ │
 │  │  ...       │     │            │       │                 │ │
 │  └────────────┘     └────────────┘       └─────────────────┘ │
 ├──────────────────────────────────────────────────────────────┤
 │ [Save…][Load…][Presets…][MMIO Variables…]  [Apply][Cancel][OK]│
 └──────────────────────────────────────────────────────────────┘
```

Key points:
- Container dropdown selects which container is being edited. Switching auto-commits current edits and takes a new snapshot of the new selection.
- Available list categorizes items: **RX meters**, **TX meters**, **Special**. Alphabetical within each category. Matches Thetis `lstMetersAvailable` (setup.cs:24522-24566).
- In-use list has ↑/↓ reorder and Add/Remove buttons flowing between the two listboxes.
- Properties column is a `QStackedWidget` with a page per item type. Pages are built on demand on first selection.
- Copy/Paste Settings buttons inside the properties panel work against an in-memory clipboard keyed by item type. Paste is disabled when the clipboard's type doesn't match the selected item's type.
- **New footer button: `MMIO Variables…`** opens the MMIO variable manager dialog (see section 6).

### 2. New state on ContainerWidget / ContainerManager

All new fields below need: member + getter/setter + `Changed` signal + serialization + default in constructor + doc comment citing Thetis origin. Pipe-delimited `serialize()` format appends new fields at the end with a version marker to preserve backward compatibility with Phase 3G-6 saved state.

| Field | Type | Persist | Thetis reference |
|---|---|---|---|
| `m_locked` | `bool` | yes | `ContainerLocked` MeterManager.cs:3079 |
| `m_notes` | `QString` | yes | `GetContainerNotes` MeterManager.cs:3182 |
| `m_titleBarVisible` | `bool` | yes | `chkContainerNoTitle` setup.cs:24447 |
| `m_minimises` | `bool` | yes | `ContainerMinimises` MeterManager.cs:2720 |
| `m_minimised` | `bool` | runtime only | — |
| `m_autoHeight` | `bool` | yes | `ContainerAutoHeight` MeterManager.cs:3068 |
| `m_hidesWhenRxNotUsed` | `bool` | yes | `chkContainer_hidewhennotused` setup.cs:24469 |
| `m_highlighted` | `bool` | runtime only | `chkContainerHighlight` setup.cs:24443 |

### 3. ContainerManager additions

- `duplicateContainer(containerId)` — Base64 round-trip into a new floating container.
- `allContainers() -> QList<ContainerWidget*>` — for the Containers menu submenu and the dialog's container dropdown.
- `containerAdded(id)`, `containerRemoved(id)`, `containerTitleChanged(id, title)` signals — MainWindow rebuilds the submenu in response.

### 4. NeedleScalePwrItem and displayGroup filtering

Trace the exact rule in Thetis `clsNeedleScalePwrItem::Paint` and any related `clsMeterItem` base-class logic that honors `onlyWhenRx` / `onlyWhenTx` / `displayGroup`. Port 1:1 per the source-first protocol. Add the following to any item type that needs them: `onlyWhenRx`, `onlyWhenTx`, `displayGroup` (int). In `paint` / `paintForLayer`, early-return if the current container mode doesn't match the filter. Expose the fields in the per-type property editors.

Items that definitely need this treatment: `NeedleItem` (already has it), `NeedleScalePwrItem`, `BarItem`, `TextItem`, `ImageItem`, `SignalTextItem`, `MagicEyeItem`, `DialItem`. The broad rule in Thetis: **every meter item class has these fields on its base `clsMeterItem`**, so the port is a base-class addition rather than per-subclass plumbing.

### 5. Per-item property editors — 100% Thetis parity

Every implemented MeterItem subclass gets a dedicated property editor page in the dialog's properties `QStackedWidget`. Each page exposes all fields Thetis's corresponding per-type panel exposes (referenced by line numbers in Thetis `setup.cs` and `setup.Designer.cs` when implementing). Below is the full item-type list and the target property-coverage summary per item. The exact control list for each comes from reading Thetis's panels at implementation time — the summary here is what the earlier Thetis exploration established.

**Base-class common fields (every item):**
Position (X/Y), Size (W/H), Z-Order, Update Rate, Attack Ratio, Decay Ratio, Data Binding (built-in + MMIO), Background Color, Show Title, Title Color, Title Text, Fade on RX, Fade on TX, Shadow, `onlyWhenRx`, `onlyWhenTx`, displayGroup.

**Per-type coverage (in addition to base):**

| Item type | Additional properties (target parity) |
|---|---|
| `BarItem` | Low/High/Indicator/Sub-indicator colors, Bar Style (Segmented/Solid/Line), Segmented low/high colors, Peak Hold + marker color, Show Peak Value + peak value color, Show History + color + alpha + duration + ignore-duration, Max Power, Power Scale color, Orientation (H/V), Edge mode |
| `NeedleItem` | Needle color, Length factor, Needle offset X/Y, Radius ratio X/Y, Placement (Bottom/Top/Left/Right), Direction (CW/CCW), Style (Line/Arrow/Hollow), Peak needle shadow fade, Show peak, Source label, Max power, Scale calibration min/max/high (full 16-point curve remains code-only, matching Thetis) |
| `ScaleItem` | Orientation, Tick count, Major/minor tick colors, Label font + color, Min/Max range |
| `TextItem` | Text string, Font family, Font size, Font weight/italic, Alignment, Color |
| `ImageItem` | Image path (resource or file chooser), Aspect mode, Tint color |
| `SolidColourItem` | Fill color, Rounded corners radius |
| `SpacerItem` | Padding (RX / TX independent), Fade on RX, Fade on TX |
| `FadeCoverItem` | Color, Fade start/stop thresholds |
| `LEDItem` | True color, False color, Shape (Square/Round/Triangle), Style (Flat/3D), Blink, Pulsate, Show back panel, Panel colors (RX/TX), Padding, Condition string, Green/Amber/Red thresholds |
| `HistoryGraphItem` | Capacity, Line color 0/1, Show grid, Auto-scale 0/1, Show scale 0/1, Keep-for duration, Ignore history duration, Axis min/max per axis, Line/time colors, Fade on RX/TX |
| `MagicEyeItem` | Glow color, Bezel image path override, Dark mode, Eye scale, Eye bezel scale |
| `NeedleScalePwrItem` | Low/High colors, Font family, Font style, Font size, Marks count, Show markers, Show type, Dark mode, Max power, Calibration dictionary (code-only), onlyWhenRx/Tx, displayGroup |
| `SignalTextItem` | Units mode (dBm/S-Units/uV), Show value, Show peak value, Show type, Show marker, Peak hold, Text color, Marker color, Peak color, Font family/size, Bar style (None/Line/Solid/Gradient/Segments) |
| `DialItem` | Text/Circle/Pad/Ring colors, Button on/off/highlight colors, Slow/Hold/Fast speed colors, VFO click behavior |
| `TextOverlayItem` | Text (supports variable substitution), Font, Color, Size |
| `WebImageItem` | URL, Refresh interval, Aspect mode, Tint |
| `FilterDisplayItem` | Center, Bandwidth, Snap frequencies (CW / sidebands / other), Colors |
| `RotatorItem` | Update rate, Arrow color, Large/small dot colors, Show beam width, Beam width color, Text color, Show cardinals, Dark mode, Beam width alpha, Padding, Beam width degrees, Allow control, Control color |
| `ClockItem` | 12/24h format, Title text, Title font+color, Time font+color, Date font+color, Show date |
| `BandButtonItem` (and the other button-box types: Mode / Filter / Antenna / TuneStep / Other / VoiceRecordPlay / Discord) | Columns, Border, Margin, Corner radius, Height ratio, Use indicator, Indicator border, Indicator on/off color, Fill color, Hover color, Border color, Click color, Font color, Use inactive color, Indicator style, Font scale, Font X shift, Font Y shift, Fix text size, **plus per-type sub-panels** (antenna: RX1/2/3, TX1/2/3, Bypass, Ext1, Xvtr; tunestep: step bitmap; other: macro config per button; voice-record-play: per-slot config) |
| `VfoDisplayItem` | Font, Colors (digits / decimal / separator), Show MHz/kHz/Hz, Blink, Hold color |
| `ClickBoxItem` | Click action (enum), Action payload |
| `DataOutItem` | Output transport (serial / UDP / TCP), Format string, Variables, Update interval |

Every editor page is its own `QWidget` subclass in `src/gui/containers/meter_property_editors/` (new subdirectory). Each editor emits `propertyChanged()` on any edit; the dialog wires the signal to apply the change live to the currently-selected item and trigger a container redraw.

### 6. MMIO (Multi-Meter I/O) subsystem

Thetis's MMIO lets meter items bind to data sources outside WDSP. The NereusSDR port introduces a new namespace/subsystem under `src/core/mmio/`.

**New files (core):**
- `src/core/mmio/ExternalVariableEngine.h/.cpp` — singleton, owns variable registry, manages transport workers on its own thread.
- `src/core/mmio/VariableDefinition.h/.cpp` — value object: name, transport type, address, parse rule, update policy (poll interval / event-driven).
- `src/core/mmio/ITransportWorker.h` — abstract base for transport workers.
- `src/core/mmio/TcpTransportWorker.h/.cpp` — TCP client. Reconnect on drop. Emits `valueReceived(varName, bytes)`.
- `src/core/mmio/UdpTransportWorker.h/.cpp` — UDP listener. Source IP/port filter. Emits on packet.
- `src/core/mmio/SerialTransportWorker.h/.cpp` — QSerialPort-backed. Compile-gated behind `HAVE_SERIALPORT`.
- `src/core/mmio/ParseRule.h/.cpp` — converts raw bytes to float. Supported rule kinds (minimum viable set, matching Thetis's capability): `ascii` (parse printf-style), `scanf` (`%f`), `hex-le` / `hex-be` (fixed-width integer), `regex-group-1` (regex first capture → double), `last-line` (text protocol, take last newline-delimited field). Exact Thetis rule set is TBD during implementation.
- `src/core/mmio/VariableCache.h/.cpp` — thread-safe `QHash<QString, std::atomic<double>>`. Written by transport workers, read by MeterPoller.

**MeterPoller integration:**
- `MeterBinding` namespace gets a new reserved ID range for MMIO variables (e.g. `MmioBindingBase = 10000`).
- New API: `MeterPoller::registerMmioVariable(name) -> int bindingId`. Poller's `poll()` slot reads the current value from `VariableCache::value(name)` and broadcasts `updateMeterValue(bindingId, value)`.
- Items subscribe via existing `setBindingId()` plumbing. No MeterItem changes needed beyond the binding ID range.

**UI:**
- `src/gui/containers/MmioVariablesDialog.h/.cpp` — the variable manager dialog. Lists all defined variables, per-variable add/edit/remove, live status indicator (connected / waiting / error), last-received timestamp and value. Accessible from the main dialog's `MMIO Variables…` button.
- `src/gui/containers/MmioVariableEditDialog.h/.cpp` — add/edit one variable: name, transport type (TCP/UDP/serial), transport-specific fields, parse rule type, parse rule parameters, update policy, test button.
- **Variable picker button** in every per-item property editor: next to the Data Binding dropdown, a small `Variable…` button opens a popup listing user-defined MMIO variables. Picking one sets the item's binding ID to the MMIO variable's reserved ID. Matches Thetis `btnMMIO_variable` at setup.cs:25479.

**Persistence:**
- Variable definitions stored in `AppSettings` under key prefix `MmioVariables/`. Each variable serializes to a single XML attribute blob (name, transport, params, parse rule, interval). On app start, `ExternalVariableEngine::init()` reads all variables, starts workers.

**Threading:**
- `ExternalVariableEngine` owns a dedicated `QThread` for all transport workers. Transports live on that thread. Workers emit `valueReceived` signals, auto-queued to the engine on its own thread, which writes atomically into `VariableCache`.
- `MeterPoller::poll()` (on the main thread) reads from the cache via atomic load. No mutex in the poll hot path.
- No UI code runs on the MMIO thread.

**Error handling:**
- Transport errors are logged via `qCWarning(lcMmio)`. Connection state is surfaced in the MMIO Variables dialog as a per-variable status column (Connected / Reconnecting / Error). The meter item shows its last-known good value on transport error; after a configurable timeout with no updates, the item paints a dashed "stale" overlay.

**Thetis reference:**
- `btnMMIO_variable` / `btnMMIO_variable_2` (setup.cs:25479-25548) — picker buttons.
- `clsIGSettings.SetSetting<T>` (MeterManager.cs:1516) — Thetis's per-item settings mechanism we don't strictly need to mirror since we have AppSettings.
- The exact Thetis transport + parse classes are TBD during implementation — they live somewhere under `Project Files/Source/Console/` and will need their own Explore agent pass when we get to MMIO.

### 7. Dialog edit model

1. Dialog opens → calls `container->serialize()` and stores the result as `m_snapshot`.
2. All user edits apply **live** to the real container. Property editors connect their `propertyChanged` signal to a slot that writes the new value into the active item and triggers a redraw.
3. Cancel → `container->deserialize(m_snapshot)` fully reverts.
4. OK / Close / Apply → discard snapshot, current state persists.
5. Container dropdown switch → auto-commit (discard old snapshot), take new snapshot from newly-selected container.
6. New Container path: `MainWindow.cpp:838-855` wiring unchanged — Cancel destroys the newly-created container.
7. Highlight for setup: runtime-only `m_highlighted` flag on `ContainerWidget`. When true, paints a 2-pixel accent outline around the container's frame. Cleared on dialog close.

### 8. Resource registration

`resources.qrc` gets:

```xml
<qresource prefix="/meters">
    <file alias="ananMM.png">resources/meters/ananMM.png</file>
    <file alias="cross-needle.png">resources/meters/cross-needle.png</file>
    <file alias="cross-needle-bg.png">resources/meters/cross-needle-bg.png</file>
</qresource>
```

`src/gui/meters/ItemGroup.cpp:775, 1008, 1016` — update paths to `":/meters/ananMM.png"` etc.

**Dependency:** User-supplied `cross-needle.png` and `cross-needle-bg.png` must be in `resources/meters/` before the phase executes. `ananMM.png` is already in place.

### 9. Containers menu restructure

Replace the current single `"Container Settings..."` action (MainWindow.cpp:859-866) with a dynamically-populated submenu:

```
Containers
├─ New Container…
├─ Edit Container ▸
│   ├─ Main Panel
│   ├─ NereusSDR Meter [1]
│   ├─ NereusSDR Meter [2]
│   └─ …
├─ MMIO Variables…
├─ Reset Default Layout
├─ ─────────────
├─ Digital / VAC   ☐
├─ PureSignal      ☐
└─ …
```

The submenu rebuilds on `ContainerManager::containerAdded`, `containerRemoved`, `containerTitleChanged`. Gear icon on each container's title bar remains functional.

## File touch list

### New files

| File | Purpose |
|---|---|
| `src/gui/containers/meter_property_editors/BaseItemEditor.h/.cpp` | Shared base class for per-item editor pages (common fields) |
| `src/gui/containers/meter_property_editors/BarItemEditor.h/.cpp` | BarItem-specific editor |
| `src/gui/containers/meter_property_editors/NeedleItemEditor.h/.cpp` | NeedleItem-specific editor |
| `src/gui/containers/meter_property_editors/ScaleItemEditor.h/.cpp` | ScaleItem editor |
| `src/gui/containers/meter_property_editors/TextItemEditor.h/.cpp` | TextItem editor |
| `src/gui/containers/meter_property_editors/ImageItemEditor.h/.cpp` | ImageItem editor |
| `src/gui/containers/meter_property_editors/SolidColourItemEditor.h/.cpp` | SolidColourItem editor |
| `src/gui/containers/meter_property_editors/SpacerItemEditor.h/.cpp` | SpacerItem editor |
| `src/gui/containers/meter_property_editors/FadeCoverItemEditor.h/.cpp` | FadeCoverItem editor |
| `src/gui/containers/meter_property_editors/LedItemEditor.h/.cpp` | LEDItem editor |
| `src/gui/containers/meter_property_editors/HistoryGraphItemEditor.h/.cpp` | HistoryGraphItem editor |
| `src/gui/containers/meter_property_editors/MagicEyeItemEditor.h/.cpp` | MagicEyeItem editor |
| `src/gui/containers/meter_property_editors/NeedleScalePwrItemEditor.h/.cpp` | NeedleScalePwrItem editor (incl. displayGroup / onlyWhenRx/Tx) |
| `src/gui/containers/meter_property_editors/SignalTextItemEditor.h/.cpp` | SignalTextItem editor |
| `src/gui/containers/meter_property_editors/DialItemEditor.h/.cpp` | DialItem editor |
| `src/gui/containers/meter_property_editors/TextOverlayItemEditor.h/.cpp` | TextOverlayItem editor |
| `src/gui/containers/meter_property_editors/WebImageItemEditor.h/.cpp` | WebImageItem editor |
| `src/gui/containers/meter_property_editors/FilterDisplayItemEditor.h/.cpp` | FilterDisplayItem editor |
| `src/gui/containers/meter_property_editors/RotatorItemEditor.h/.cpp` | RotatorItem editor |
| `src/gui/containers/meter_property_editors/ClockItemEditor.h/.cpp` | ClockItem editor |
| `src/gui/containers/meter_property_editors/ButtonBoxItemEditor.h/.cpp` | Shared editor base for button-box items |
| `src/gui/containers/meter_property_editors/BandButtonItemEditor.h/.cpp` | BandButtonItem editor subclass |
| `src/gui/containers/meter_property_editors/ModeButtonItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/FilterButtonItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/AntennaButtonItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/TuneStepButtonItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/OtherButtonItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/VoiceRecordPlayItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/DiscordButtonItemEditor.h/.cpp` | … |
| `src/gui/containers/meter_property_editors/VfoDisplayItemEditor.h/.cpp` | VfoDisplayItem editor |
| `src/gui/containers/meter_property_editors/ClickBoxItemEditor.h/.cpp` | ClickBoxItem editor |
| `src/gui/containers/meter_property_editors/DataOutItemEditor.h/.cpp` | DataOutItem editor |
| `src/gui/containers/MmioVariablesDialog.h/.cpp` | MMIO variable manager dialog |
| `src/gui/containers/MmioVariableEditDialog.h/.cpp` | Add/edit a single MMIO variable |
| `src/gui/containers/MmioVariablePickerPopup.h/.cpp` | Popup for item-level Variable… button |
| `src/core/mmio/ExternalVariableEngine.h/.cpp` | MMIO engine singleton |
| `src/core/mmio/VariableDefinition.h/.cpp` | Variable value object |
| `src/core/mmio/ITransportWorker.h` | Transport base interface |
| `src/core/mmio/TcpTransportWorker.h/.cpp` | TCP transport |
| `src/core/mmio/UdpTransportWorker.h/.cpp` | UDP transport |
| `src/core/mmio/SerialTransportWorker.h/.cpp` | Serial transport (HAVE_SERIALPORT) |
| `src/core/mmio/ParseRule.h/.cpp` | Bytes → float parsing |
| `src/core/mmio/VariableCache.h/.cpp` | Thread-safe value cache |
| `resources/meters/cross-needle.png` | (user-supplied) |
| `resources/meters/cross-needle-bg.png` | (user-supplied) |

### Modified files

| File | Change |
|---|---|
| `resources.qrc` | Add `/meters` qresource block |
| `CMakeLists.txt` | Add new src files, `HAVE_SERIALPORT` gate for serial transport |
| `src/gui/containers/ContainerSettingsDialog.h/.cpp` | Major rewrite: 3-column layout, snapshot state, container dropdown, in-place edit model, per-type editor integration, copy/paste clipboard |
| `src/gui/containers/ContainerWidget.h/.cpp` | Add lock/notes/minimises/autoHeight/hidesWhenRxNotUsed/titleBarVisible/highlighted fields, getters/setters/signals, highlight paint, serialize/deserialize updates |
| `src/gui/containers/ContainerManager.h/.cpp` | duplicateContainer, allContainers, containerAdded/Removed/TitleChanged signals |
| `src/gui/containers/FloatingContainer.h/.cpp` | title-bar-visible toggle, minimised state |
| `src/gui/meters/MeterItem.h/.cpp` | Add base-class fields: onlyWhenRx, onlyWhenTx, displayGroup, sourceLabel (where missing), attack/decay as base-class, fade-on-RX/TX, shadow base field. Honor filters in `paint` / `paintForLayer`. |
| `src/gui/meters/NeedleScalePwrItem.h/.cpp` | Use inherited filter fields; wire into paint |
| `src/gui/meters/BarItem.cpp` | Add segmented/solid/line style, peak hold, power-scale fields (if missing) |
| `src/gui/meters/NeedleItem.cpp` | Confirm all Thetis-parity fields are present (placement, direction, style, radius ratio, etc. — likely most already exist from 3G-3/3G-4) |
| `src/gui/meters/ItemGroup.cpp` | Change PNG paths to `:/meters/…`; wire displayGroup/onlyWhenRx/Tx on ANANMM NeedleScalePwrItem instances; restore CrossNeedle preset to picker once assets arrive |
| `src/gui/meters/MeterPoller.h/.cpp` | Add `registerMmioVariable(name) -> int`, read from VariableCache in `poll()` |
| `src/gui/MainWindow.cpp:832-870` | Rebuild Containers menu: Edit Container submenu, MMIO Variables entry, verify Reset Default Layout |
| `src/core/AppSettings` (as needed) | Persistence for MMIO variable definitions under `MmioVariables/` prefix |
| `docs/architecture/phase3g6-debug-handoff.md` | Mark findings as addressed in this plan |
| `CHANGELOG.md` | Phase entry |
| `CLAUDE.md` | Update phase table to show 3G-6 complete |

## Acceptance criteria

### Rendering
- [ ] Fresh ANANMM floating container renders with the user-supplied `ananMM.png` background correctly scaled and positioned.
- [ ] Fresh CrossNeedle floating container renders with the user-supplied `cross-needle.png` and `cross-needle-bg.png`.
- [ ] Needles point at the right scale positions in both RX and TX states on both presets.
- [ ] In RX mode on ANANMM, only the S scale is visible; PO/SWR/Id/COMP/ALC/Vd scales are hidden. In TX mode those scales become visible and S hides.
- [ ] No overlapping scale labels on any preset regardless of mode.
- [ ] Container #0's S-Meter in the main window is unchanged and correct.

### Dialog
- [ ] Dialog opens from `Containers → Edit Container → [any container]`, from `Containers → New Container…`, and from each container's gear icon.
- [ ] Container dropdown lists all open containers by title; switching auto-commits current edits and takes a fresh snapshot for the new selection.
- [ ] 3-column Thetis layout renders without clipping at default size (≈1000×650).
- [ ] Available items categorized: RX / TX / Special, alphabetized within each category.
- [ ] Add / Remove / Up / Down operate on the correct selection; reorder changes rendering z-order.
- [ ] Properties panel swaps per selected item type. Every item type currently implemented in NereusSDR has a working editor page.
- [ ] Copy / Paste Settings buttons copy a whole item's configuration; Paste is greyed out when the clipboard type doesn't match.
- [ ] Save / Load round-trips a container correctly via Base64.
- [ ] Cancel reverts every edit made since the dialog opened or since the last container switch.
- [ ] OK / Close persists edits.

### Container-level controls
- [ ] Title, Notes, Border, Title bar visibility, Lock, Show-on-RX, Show-on-TX, Hides-when-RX-not-used, Minimises, Auto container height, Highlight for setup, Duplicate button, Delete button — all work and (where persistent) survive relaunch.
- [ ] Lock prevents drag and resize.
- [ ] Hides-when-RX-not-used honors the RX-active signal correctly.
- [ ] Minimised state collapses to title bar only and persists across relaunch.
- [ ] Auto container height resizes the container vertically to fit content.
- [ ] Highlight outline appears on the container being edited and clears when the dialog closes.
- [ ] Duplicate clones the current container as a new floating copy with identical items, colors, and properties.

### Per-item property editors
- [ ] Every item type listed in section 5 has a dedicated editor page.
- [ ] Every editable field on each page applies live to the target item and triggers an immediate redraw.
- [ ] Scale calibration min/max/high on NeedleItem updates needle rendering immediately.
- [ ] Color pickers display the item's current color before click.
- [ ] Button box items show per-type sub-panels (antenna toggles, tunestep bitmap, etc.).

### MMIO
- [ ] `Containers → MMIO Variables…` opens the variable manager dialog.
- [ ] Add a TCP variable, point it at a known-good echo service, verify status = Connected and last value updates.
- [ ] Add a UDP variable, send a test packet, verify value updates.
- [ ] Add a serial variable (if `HAVE_SERIALPORT`), verify port list enumerates and a loopback test works.
- [ ] Bind a meter item to a MMIO variable via the Variable… picker in its editor page; verify the item renders the external value.
- [ ] Transport drops (disconnect TCP mid-test, disconnect serial cable) result in the item showing last-known value and a stale overlay after timeout.
- [ ] Transport errors are logged to `lcMmio` category.
- [ ] Variable definitions persist across relaunch.
- [ ] Transports run on their own thread; no UI jank during network I/O.

### Menu
- [ ] Containers menu shows Edit Container submenu, live-populated from the container list.
- [ ] Submenu updates when containers are added, removed, or retitled.
- [ ] Reset Default Layout restores default state and destroys broken persisted floaters.

### Build & ops
- [ ] `cmake --build build` succeeds on macOS Metal with no new warnings on modified files.
- [ ] App launches, main window renders, Container #0 S-Meter renders correctly.
- [ ] Binary size increase is proportional and reasonable (≤ 15%).
- [ ] All commits GPG-signed.
- [ ] PR #1 updated as `Phase 3G-6 (one-shot) — Thetis-parity rewrite + MMIO`.

## Testing plan

Visual verification (screenshot + `Read` tool inspection) at each of these checkpoints:

1. **After ANANMM PNG wire**: Fresh ANANMM floating container renders correctly in both RX and TX modes.
2. **After CrossNeedle PNG wire**: Fresh CrossNeedle renders.
3. **After scale filter fix**: No scale overlap in either preset, either mode.
4. **After dialog 3-column layout**: Dialog opens, no clipping, all panels visible, dropdown works.
5. **After snapshot/revert**: Make edits, press Cancel, state reverts.
6. **After container-level control additions**: Each new control verified for UI state + persistence across relaunch.
7. **After each per-item editor is implemented**: Each editor page shows, edits apply live, no crashes on type switch.
8. **After Copy / Paste settings**: Copy an item, paste onto another of same type, verify fields match.
9. **After MMIO engine**: Variable manager dialog, add each transport type, bind to an item, verify live updates.
10. **After menu restructure**: Submenu populates correctly, each submenu entry opens the dialog on the right container.
11. **End-to-end**: Launch fresh, create ANANMM, edit, save, relaunch, verify persistence.

Screenshots archived under `/tmp/nereus_dbg/3g6-*.png`.

## Risks & unknowns

- **Scope**. This is the largest single phase in NereusSDR's history. Risk of mid-phase scope reconsideration is high. Mitigation: the plan is structured into independently-reviewable commits so partial progress is still shippable if the phase is interrupted.
- **Thetis MMIO reverse-engineering**. The exact transport classes, parse rules, and UI plumbing for MMIO live somewhere in Thetis I haven't read yet. Implementation starts with a targeted Explore agent on `setup.cs:25479-25548` and downstream files to get a complete picture before coding. If the reality diverges from my current understanding, the MMIO section of this plan will need a point-release update.
- **Serial port cross-platform**. `QSerialPort` behavior differs subtly between macOS, Linux, and Windows. All three platforms will need smoke testing.
- **Thread-safe VariableCache under MeterPoller's 100ms poll**. `std::atomic<double>` load is fine, but the value-age tracking (for stale overlay) needs careful thought so the UI thread doesn't drift.
- **ContainerWidget serialization backward compatibility**. Phase 3G-6 already has persisted state in the wild (I saw it on launch). Adding new fields needs append-at-end + version tag + sane defaults.
- **QStackedWidget with ~30 editor pages**. Pages built lazily on first selection. Swap cost is trivial. Memory cost negligible.
- **Dialog real estate**. 30+ per-type pages with full parity will have varying content heights. Each editor page wraps its content in a `QScrollArea` to handle the tallest pages (button-box items have ~25 controls).
- **Live edit repaint throttling**. Text-field edits apply on `editingFinished` (Enter or focus loss), not on every keystroke. Numeric spinboxes and color buttons apply immediately.

## Commit strategy

All commits GPG-signed. Imperative mood. Each is independently buildable and testable unless explicitly marked as part of a multi-commit landing.

### Rendering & quick wins (fast feedback loop)
1. `feat(3G-6): register ananMM + cross-needle pngs as qt resources`
2. `fix(3G-6): use resource paths for ANANMM and CrossNeedle backgrounds`
3. `feat(meters): add onlyWhenRx/Tx/displayGroup to MeterItem base class`
4. `fix(meters): honor display group filtering in NeedleScalePwrItem paint`
5. `feat(ItemGroup): wire display group on ANANMM NeedleScalePwr items`

### ContainerWidget / ContainerManager surface
6. `feat(containers): add lock/titleBarVisible/minimises state to ContainerWidget`
7. `feat(containers): add autoHeight/hidesWhenRxNotUsed/notes to ContainerWidget`
8. `feat(containers): highlighted runtime flag + paint outline`
9. `feat(containers): duplicate + allContainers + change signals in ContainerManager`
10. `feat(floating): title-bar-visible toggle and minimised state in FloatingContainer`

### Dialog rewrite (many commits)
11. `refactor(dialog): remove live preview panel from ContainerSettingsDialog`
12. `feat(dialog): Thetis 3-column layout skeleton`
13. `feat(dialog): container dropdown with auto-commit on switch`
14. `feat(dialog): snapshot + revert on Cancel`
15. `feat(dialog): categorized Available listbox (RX/TX/Special)`
16. `feat(dialog): item reorder up/down wired to in-use list`
17. `feat(dialog): container-level property bar with all 8 new controls`
18. `feat(dialog): MMIO Variables footer button (wired in commit 36)`
19. `feat(dialog): Save/Load/Presets footer buttons`

### Per-item property editors (one commit per editor)
20. `feat(editor): BaseItemEditor with common fields`
21. `feat(editor): BarItemEditor full Thetis parity`
22. `feat(editor): NeedleItemEditor full Thetis parity`
23. `feat(editor): ScaleItemEditor`
24. `feat(editor): TextItemEditor`
25. `feat(editor): ImageItemEditor + SolidColourItemEditor`
26. `feat(editor): SpacerItemEditor + FadeCoverItemEditor`
27. `feat(editor): LedItemEditor`
28. `feat(editor): HistoryGraphItemEditor`
29. `feat(editor): MagicEyeItemEditor + NeedleScalePwrItemEditor`
30. `feat(editor): SignalTextItemEditor + DialItemEditor`
31. `feat(editor): TextOverlayItemEditor + WebImageItemEditor`
32. `feat(editor): FilterDisplayItemEditor + RotatorItemEditor + ClockItemEditor`
33. `feat(editor): button-box editors (Band/Mode/Filter/Antenna/TuneStep/Other/VoiceRecordPlay/Discord)`
34. `feat(editor): VfoDisplayItemEditor + ClickBoxItemEditor + DataOutItemEditor`
35. `feat(dialog): Copy/Paste item settings clipboard`

### MMIO subsystem
36. `feat(mmio): ExternalVariableEngine + VariableDefinition + VariableCache skeleton`
37. `feat(mmio): TcpTransportWorker with reconnect`
38. `feat(mmio): UdpTransportWorker`
39. `feat(mmio): SerialTransportWorker behind HAVE_SERIALPORT`
40. `feat(mmio): ParseRule — ascii/scanf/hex-le/hex-be/regex/last-line`
41. `feat(mmio): MeterPoller integration (reserved binding id range)`
42. `feat(mmio): MmioVariablesDialog + MmioVariableEditDialog`
43. `feat(mmio): MmioVariablePickerPopup + Variable… button in BaseItemEditor`
44. `feat(mmio): persist variables in AppSettings`

### Menu restructure + verification
45. `feat(menu): Edit Container submenu populated from ContainerManager`
46. `fix(menu): verify Reset Default Layout recovery path`

### Polish + docs
47. `docs(3G-6): update CHANGELOG, CLAUDE.md phase table, README`
48. `chore(3G-6): mark debug-handoff findings as resolved`

Commits can be reordered if dependencies shake out differently during implementation — the rough ordering above follows "land quick rendering wins first, then surface, then editors, then MMIO" to keep visible progress flowing.

## How execution will proceed

1. You say "go" (or "iterate on X" and I revise this plan).
2. I ensure `resources/meters/cross-needle.png` and `cross-needle-bg.png` are in place (wait for you to provide).
3. Dispatch a targeted Explore agent on Thetis MMIO code paths for section 6 details before writing any MMIO code.
4. Execute commits 1–5 (rendering quick wins) and visually verify. Report back with screenshots.
5. Execute commits 6–10 (ContainerWidget/ContainerManager surface). Build + relaunch + screenshot checkpoint.
6. Execute commits 11–19 (dialog rewrite). Checkpoint after Thetis layout is visible.
7. Execute commits 20–35 (per-item editors and copy/paste). Checkpoint after each batch of ~5 editors.
8. Execute commits 36–44 (MMIO). Checkpoint after transports are individually tested.
9. Execute commits 45–48 (menu + polish + docs).
10. Full end-to-end acceptance-criteria run against the build. Report results.
11. You review the PR and decide on merge.

I will pause for your OK between each major block (5/10/19/35/44/48) so you can sanity-check the direction without reading 50 commits at once.
