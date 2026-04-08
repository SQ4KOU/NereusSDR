# Phase 1B: Thetis Architecture Analysis

## Status: Complete

---

## 1. Project Structure

Thetis is a C# WinForms application for OpenHPSDR Protocol 1 and Protocol 2
radios. The repository is organized around a single large Visual Studio project.

- **Main project:** `Project Files/Source/Console/Thetis.csproj`
- **Key directories:**
  - `Console/` — Main application code; `console.cs` is the central file at
    20,000+ lines containing the `Console` form class
  - `HPSDR/` — OpenHPSDR protocol implementation (discovery, framing, I/O)
  - `Memory/` — Memory channel storage and recall
  - `CAT/` — Computer-Aided Transceiver (serial/TCP control) interface
  - `CW/` — CW keying, sidetone, and iambic keyer logic
  - `Invoke/` — Cross-thread UI invocation helpers
  - `Andromeda/` — Hardware controller support (Andromeda front panel)

- **Entry point:** `console.cs::Main()` at approximately line 1462
  - Sets up global exception handlers (`Application.ThreadException`,
    `AppDomain.CurrentDomain.UnhandledException`)
  - Parses command-line arguments
  - Enforces single-instance via named `Mutex`
  - Sets 1 ms system timer resolution (`timeBeginPeriod(1)`)
  - Creates and runs the `Console` form (`Application.Run(new Console(args))`)

---

## 2. Core Classes

### Console (`console.cs`)

The main WinForms `Form` and application hub. This single class manages:

- All UI state: VFO frequencies, mode selectors, filter buttons, S-meter,
  band stacking registers, memory channels
- Over 1,000 properties tracking radio operating state
- Direct manipulation of UI controls (no MVVM or MVC separation)
- Initialization of all subsystems (Radio, Display, Audio, CAT, CW)
- Event handlers for every front-panel control
- Global hotkey and keyboard shortcut handling

### Radio (`radio.cs`)

Manages DSP engine instances and acts as the bridge between the Console UI and
WDSP.

- **RX paths:** `dsp_rx[2][2]` array — 2 threads x 2 sub-receivers = 4 RX paths
  - `dsp_rx[0][0]` = RX1 main receiver
  - `dsp_rx[0][1]` = RX1 diversity/sub-receiver
  - `dsp_rx[1][0]` = RX2 main receiver (note: indexed as thread 2 in WDSP,
    stored at array index 1)
  - `dsp_rx[1][1]` = RX2 diversity/sub-receiver
- **TX path:** `dsp_tx[1]` — single TX DSP instance (array of 1)
- Access pattern: `radio.GetDSPRX(thread, subrx)` returns the appropriate
  `RadioDSPRX` instance

### RadioDSPRX (`radio.cs`)

Per-receiver DSP state machine, indexed by `(thread, subrx)`. Each instance
holds independent state for:

- `DSPMode` — demodulation mode (LSB, USB, AM, FM, CW, DIGU, DIGL, etc.)
- `RXFilterLow` / `RXFilterHigh` — bandpass filter edges
- `RXOsc` — receiver offset frequency
- `RXPan` — stereo pan position
- `RXAGCMode` — AGC mode (Off, Long, Slow, Medium, Fast, Custom)
- `RXANF` — auto-notch filter enable
- `RXNR` / `RXNR2` — noise reduction enable
- `RXNB` / `RXNB2` — noise blanker enable
- `RXSquelch` — squelch threshold and enable
- Spectrum display settings (FFT size, averaging, window function)

Property setters call through to WDSP API functions (e.g., `SetRXAMode`,
`SetRXABandpassFreqs`, `SetRXAAGCMode`).

### RadioDSPTX (`radio.cs`)

Single TX DSP state instance managing:

- `DSPMode` — TX modulation mode
- `TXFilterLow` / `TXFilterHigh` — TX bandpass filter
- `TXEQEnabled` / EQ bands — transmit equalization
- `TXCompEnabled` / `TXCompLevel` — speech compression
- `TXVOXEnabled` / `TXVOXThreshold` — VOX detection
- CESSB (Controlled Envelope Single Sideband) settings
- PureSignal feedback parameters

### Display (`display.cs`)

Panadapter and waterfall rendering engine:

- Uses **SharpDX** (managed DirectX wrapper) for hardware-accelerated rendering
  - Direct2D1 for 2D drawing (grid lines, text, signal traces)
  - Direct3D11 for GPU-backed surfaces
- Separate rendering contexts for RX1 and RX2 panadapters
- Maintains independent state per display:
  - `m_dCentreFreqRX1` / `m_dCentreFreqRX2` — center frequencies
  - Filter overlay rectangles (passband shading)
  - Display range (dB scale, frequency span)
  - Waterfall color palette and speed
- Rendering loop driven by timer tick, not vsync

### NetworkIO (`HPSDR/NetworkIO.cs`)

Protocol abstraction layer interfacing with native C++ DLLs:

- **Protocol detection:** Examines first bytes of discovery response to
  determine P1 vs P2
- **P/Invoke interface:** Calls into `wdsp.dll` and protocol-handling native
  libraries
- Key methods:
  - `VFOfreq(id, freq, tx)` — set VFO frequency for receiver ID 0-3
  - `SetOutputPower(factor)` — set TX drive level
  - `Discover()` / `GetMetisIPAddr()` — radio discovery
  - `StartAudio()` / `StopAudio()` — start/stop I/Q streaming

---

## 3. Receiver Lifecycle

Receivers in Thetis are **pre-allocated at startup**, not dynamically created or
destroyed.

### Allocation

At `Radio` construction time, all 4 RX paths and 1 TX path are instantiated:

```
dsp_rx[0][0] → RX1 main       (WDSP thread 0, subrx 0) — Active by default
dsp_rx[0][1] → RX1 diversity   (WDSP thread 0, subrx 1) — Inactive by default
dsp_rx[1][0] → RX2 main       (WDSP thread 2, subrx 0) — Inactive by default
dsp_rx[1][1] → RX2 diversity   (WDSP thread 2, subrx 1) — Inactive by default
dsp_tx[0]    → TX              (single instance)
```

### Activation

- Only `dsp_rx[0][0].Active = true` at startup (RX1 main receiver)
- RX2 is activated when the user enables the second receiver via UI toggle
- Diversity sub-receivers are activated when diversity mode is enabled per-band
- The `Active` flag gates whether WDSP processes samples for that channel

### Access Pattern

Console accesses receivers through the Radio class:

```csharp
RadioDSPRX rx1 = radio.GetDSPRX(0, 0);  // RX1 main
RadioDSPRX rx1d = radio.GetDSPRX(0, 1); // RX1 diversity
RadioDSPRX rx2 = radio.GetDSPRX(2, 0);  // RX2 main  (note: thread ID 2)
RadioDSPRX rx2d = radio.GetDSPRX(2, 1); // RX2 diversity
```

Thread ID mapping: Thread 0 (indices 0,1) = RX1/VFOA path, Thread 2
(indices 0,1) = RX2/VFOB path. Threads 1 and 3 are unused.

### Maximum Active Paths

4 simultaneous RX paths: 2 independent receivers + 2 diversity sub-receivers.
This is not an arbitrary limit but a consequence of the dual-thread DSP
architecture inherited from the original PowerSDR design.

---

## 4. State Synchronization

State flows from UI controls through the Console class to the Radio/DSP layer
and out to the hardware via NetworkIO. The synchronization is one-directional
for most parameters.

### VFO Frequency

```
UI Knob/Click → Console.VFOAFreq property setter
  → NetworkIO.VFOfreq(id, freq, tx)
    → Protocol encoding:
        P1: phase_word = (long)(Math.Pow(2,32) * freq / 122880000)
        P2: frequency in Hz directly
  → radio.GetDSPRX(thread, subrx).RXOsc = offset (if needed)
```

VFO IDs:
- 0 = RX1 main
- 1 = RX1 sub
- 2 = RX2 main
- 3 = RX2 sub

### Mode Changes

```
UI Mode Button → Console sets mode variable
  → radio.GetDSPRX(thread, subrx).DSPMode = newMode
    → WDSP API: SetRXAMode(channel, mode_enum)
  → Filter defaults updated for new mode
  → Display overlay updated
```

### Filter Updates

```
UI Filter Button/Drag → Console computes (low, high) edges
  → radio.GetDSPRX(thread, subrx).SetRXFilter(low, high)
    → WDSP API: SetRXABandpassFreqs(channel, low, high)
  → Display filter overlay redrawn
```

### Feedback Prevention

- The protocol is **one-way / fire-and-forget**: commands are sent to the radio
  with no acknowledgment or response
- Local state variables track the last-sent value; commands are only re-sent
  when the value actually changes
- UI updates from internal state changes use guard flags to prevent re-entrant
  property setter calls (similar to NereusSDR's `m_updatingFromModel` pattern)

### AGC, NR, NB, ANF

Each DSP parameter follows the same pattern:

```
UI Toggle/Slider → Console property
  → radio.GetDSPRX(thread, subrx).[Parameter] = value
    → WDSP API: Set[Parameter](channel, value)
```

All DSP parameters are fully independent per receiver — changing NR on RX1
has no effect on RX2.

### Global State

A few parameters are global (not per-receiver):
- Sample rate: 48 kHz default, 192 kHz required for FM broadcast mode
- TX drive level and SWR protection factor
- Frequency correction factor (applied to all frequency commands)

---

## 5. Two-Panadapter Limitation

### UI Layout

In `console.Designer.cs`, two panel controls host the spectrum displays:

- `panelDisplay` — RX1 panadapter/waterfall (primary)
- `panelRX2Display` — RX2 panadapter/waterfall (secondary)

These are statically defined in the form designer, not dynamically created.

### Display State

`display.cs` maintains separate state for each panadapter:

- `m_dCentreFreqRX1` / `m_dCentreFreqRX2` — independent center frequencies
- Separate filter rectangle overlays per RX
- Independent zoom levels and dB ranges
- Independent waterfall speed and color palettes

### Why Two?

The two-panadapter limit is **not a hard-coded constant** but a natural
consequence of the dual-thread DSP architecture:

- Thread 0 = RX1 signal path → provides I/Q data for RX1 panadapter
- Thread 2 = RX2 signal path → provides I/Q data for RX2 panadapter
- No additional DSP threads exist to feed additional panadapters
- The WDSP channel allocation is fixed at startup

Adding more panadapters would require:
1. Expanding the DSP backend to create additional WDSP channels
2. Allocating additional I/Q data streams from the radio
3. Adding new display panels and rendering contexts
4. Modifying the protocol layer to request additional receivers from the radio

### NereusSDR Implication

NereusSDR's architecture should **not** hard-code a two-panadapter limit.
Instead, the number of panadapters should be driven by the number of active
WDSP channels/receivers, which in turn depends on radio capability and
available bandwidth. The display layer should support N panadapters.

---

## 6. Multi-Slice Management

### Slice Architecture

Thetis supports **2 independent slices** (receiver paths):

| Slice | VFO   | DSP Thread | Main RX          | Diversity RX      |
|-------|-------|------------|-------------------|-------------------|
| 1     | VFOA  | Thread 0   | `dsp_rx[0][0]`   | `dsp_rx[0][1]`   |
| 2     | VFOB  | Thread 2   | `dsp_rx[1][0]`   | `dsp_rx[1][1]`   |

### Independence

Each slice has fully independent:
- Frequency (VFO)
- Mode (LSB, USB, AM, FM, CW, etc.)
- Bandpass filter width and position
- AGC mode and parameters
- Noise reduction (NR/NR2) settings
- Noise blanker (NB/NB2) settings
- Auto-notch filter (ANF)
- Squelch level and enable
- Audio pan (stereo position)
- Spectrum display parameters

### Diversity Mode

Each slice can optionally enable a diversity sub-receiver:
- Diversity settings are stored **per-band** (different diversity config for
  each amateur band)
- When enabled, the sub-receiver (`subrx=1`) is activated alongside the main
  receiver (`subrx=0`)
- Diversity combining is done in WDSP
- This effectively doubles the active DSP channels when both slices use
  diversity

### Band Stacking Registers

Thetis maintains band stacking registers that save and restore per-band state:
- 3 stacking registers per band (cycle through with band button)
- Each register stores: frequency, mode, filter, and other operating parameters
- Implemented as arrays indexed by band enum

---

## 7. Legacy Skin System

### Overview

Thetis supports downloadable skins that change the visual appearance of the
console. Skins are distributed as ZIP archives from remote repositories.

### Skin Server Infrastructure

- Skin sources configured via `skin_servers.json`
- Management class: `clsThetisSkinService`
- Default repositories:
  - `github.com/ramdor/ThetisSkins` (primary)
  - `oe3ide.com` (community)
  - `w1aex.com` (community)
  - `die-jetzis.de` (community)

### Manifest Format

Each skin repository serves a JSON manifest with skin metadata:

```json
{
  "SkinName": "DarkPanel",
  "SkinUrl": "https://example.com/skins/DarkPanel.zip",
  "SkinVersion": "1.2",
  "FromThetisVersion": "2.10.3",
  "ThumbnailUrl": "https://example.com/skins/DarkPanel_thumb.png"
}
```

### File Structure

After extraction, a skin directory contains:

```
SkinName/
├── SkinName.xml          — Layout and property definitions
├── Console/
│   └── Console.png       — Main console background image
└── [ControlName]/
    ├── up.png            — Button state: normal/up
    ├── down.png          — Button state: pressed/down
    ├── over.png          — Button state: hover
    ├── disabled.png      — Button state: disabled
    ├── checked.png       — Button state: checked
    ├── unchecked.png     — Button state: unchecked
    ├── checked_over.png  — Button state: checked + hover
    └── unchecked_over.png— Button state: unchecked + hover
```

Up to **8 button states** are supported per control.

### XML Skin Format

The `SkinName.xml` file defines form and control properties:

- **Form-level properties:**
  - `BackColor` — form background color
  - `Font` — default font family, size, style
  - `Size` — form dimensions
  - `TransparencyKey` — color used for transparent regions

- **Control-level properties:**
  - `Position` (X, Y) — control location
  - `Size` (Width, Height) — control dimensions
  - `ForeColor` / `BackColor` — control colors
  - `Text` — label/button text
  - `Font` — per-control font override
  - `Checked` — initial checked state (for checkboxes/radio buttons)

- **Supported control types:**
  - `Button` — 8-state image button
  - `CheckBox` — toggle with checked/unchecked images
  - `RadioButton` — exclusive selection group
  - `Label` — static text
  - `TextBox` — text input
  - `ComboBox` — dropdown selector
  - `NumericUpDown` — numeric spinner
  - `PrettyTrackBar` — custom slider/trackbar
  - `PictureBox` — static image display
  - `GroupBox` — labeled container
  - `Panel` — layout container

### Loading Process

1. Fetch JSON manifest from configured skin servers
2. Download ZIP archive from `SkinUrl`
3. Extract ZIP to local skin directory
4. Parse `SkinName.xml` for layout and property definitions
5. Restore form properties from XML
6. Load PNG images for each control from extracted directories
7. Apply images and properties to WinForms controls

---

## 8. OpenHPSDR Protocol Usage

### Protocol Detection

NetworkIO determines the protocol version from the discovery response:

- **Protocol 1 (P1):** Response begins with `0xEF 0xFE` (Metis framing)
- **Protocol 2 (P2):** Response begins with `0x00` prefix

The board type is extracted from:
- P1: byte 10 of the discovery response
- P2: byte 11 of the discovery response

### Frequency Encoding

The two protocols encode VFO frequencies differently:

**Protocol 1 (phase word):**
```
phase_word = (long)(Math.Pow(2, 32) * frequency_hz / 122880000)
```
The 32-bit phase word is sent as 4 bytes in the C&C data. The divisor
`122880000` is the P1 master clock frequency (122.88 MHz).

**Protocol 2 (direct Hz):**
```
frequency_bytes = frequency_in_hz  (32-bit integer, Hz)
```
Sent directly as a 4-byte integer — no phase word conversion needed.

### VFO Addressing

`NetworkIO.VFOfreq(id, freq, tx)` sends frequency commands where:

| ID | Receiver      | Description        |
|----|---------------|--------------------|
| 0  | RX1 main      | Primary receiver   |
| 1  | RX1 sub       | RX1 diversity/sub  |
| 2  | RX2 main      | Secondary receiver |
| 3  | RX2 sub       | RX2 diversity/sub  |

The `tx` parameter indicates whether this VFO is currently the TX frequency.

### TX Power Control

```
NetworkIO.SetOutputPower(factor)
  → int power = (int)(255 * factor * swr_protect_factor)
```

The drive level is a 0-255 value derived from the user's power slider (0.0-1.0)
multiplied by an SWR protection reduction factor.

### Frequency Correction

A global frequency correction factor is applied to all frequency commands to
compensate for oscillator drift or calibration offset. This is applied
multiplicatively before protocol encoding.

### Protocol Characteristics

- **One-way commands:** All commands from PC to radio are fire-and-forget with
  no acknowledgment. The PC assumes commands were received.
- **No bidirectional handshake:** The radio does not confirm frequency changes,
  mode changes, or any other command.
- **Streaming data:** The radio continuously streams I/Q data to the PC once
  started; the PC continuously streams C&C data and (during TX) modulated I/Q
  to the radio.

---

## 9. Key Insights for NereusSDR Port

### Architecture

1. **Dual-thread DSP architecture is fundamental.** RX1 uses WDSP thread 0,
   RX2 uses WDSP thread 2. This is deeply embedded in Thetis and influences
   every subsystem. NereusSDR should abstract this into a dynamic receiver
   allocation model rather than hard-coding thread indices.

2. **RadioDSPRX/TX are stateful objects synced via property setters with WDSP
   callbacks.** Each property setter immediately calls the corresponding WDSP
   API function. NereusSDR's `WdspEngine` should provide the same immediate
   update behavior, likely via Qt properties with signal/slot notification.

3. **The Radio class is independent of the UI (Console).** Console accesses
   Radio via `GetDSPRX()` — there is a clear (if informal) separation. NereusSDR
   should formalize this into a proper model/view split with `RadioModel`.

4. **Receivers are pre-allocated; the `Active` flag gates processing.** NereusSDR
   can follow a similar pattern: pre-create WDSP channels at startup, activate/
   deactivate as needed, avoiding the overhead of dynamic channel creation.

### Protocol

5. **One-way protocol: fire-and-forget commands, no bidirectional handshake.**
   NereusSDR's `RadioConnection` must be resilient to commands being silently
   dropped. Consider periodic re-send of critical state (frequency, mode) as
   a robustness measure.

6. **Support both phase-word (P1) and Hz (P2) frequency encoding.** The
   `RadioConnection` class must abstract this difference so higher layers
   always work in Hz, with protocol-specific encoding handled internally.

7. **Frequency correction factor must be applied globally.** A single
   calibration offset applied to all outgoing frequency commands — implement
   this in `RadioConnection` so it is transparent to the rest of the app.

### Display

8. **Display uses separate panadapters per RX path — logical, not hard-limited.**
   NereusSDR should support N panadapters driven by the number of active WDSP
   channels, not a fixed count of 2.

9. **SharpDX (DirectX) rendering must be replaced.** NereusSDR uses Qt6 with
   potential GPU acceleration via QRhi or custom OpenGL/Vulkan. The display
   architecture should separate data computation (FFT bins) from rendering
   (panadapter/waterfall drawing).

### Skin System

10. **XML + PNG skin model is extensible.** NereusSDR should support importing
    Thetis skins for familiarity, while also offering a modern theming system
    (Qt Style Sheets or custom QML themes) for new skins.

### State Management

11. **Console.cs is a 20,000+ line monolith.** This is the primary maintenance
    burden in Thetis. NereusSDR must avoid this by distributing state across
    focused model classes (`SliceModel`, `DisplayModel`, `AudioModel`, etc.)
    connected via signals/slots.

12. **Band stacking registers and per-band state** are important operator
    workflow features that must be preserved in the port. These map naturally
    to per-band entries in `AppSettings`.
