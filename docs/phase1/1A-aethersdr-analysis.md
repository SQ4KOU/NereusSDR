# Phase 1A: AetherSDR Architecture Analysis

## Status: Complete

## Executive Summary

AetherSDR is a Qt6/C++20 multi-threaded SDR console with GPU-accelerated
rendering. The architecture uses thread isolation + auto-queued signals (no
shared mutexes). RadioModel is the central state hub owning all sub-models as
value members on the main thread.

---

## 1. Radio Abstraction Layer

### RadioConnection (TCP 4992)

- Lives on worker thread (`moveToThread` pattern)
- Sends sequenced commands: `C<seq>|<cmd>\n`
- Parses protocol messages (`V`, `H`, `R`, `S`, `M` lines)
- Measures RTT via kernel `TCP_INFO`
- 30-second heartbeat pings
- Key signals (auto-queued to main): `stateChanged`, `connected`,
  `disconnected`, `statusReceived`, `commandResponse`, `versionReceived`

### RadioDiscovery (UDP 4992)

- Listens for SSDP discovery broadcasts
- `RadioInfo` struct: name, model, serial, version, address, port (4992),
  status
- 5-second staleness timer
- Signals: `radioDiscovered`, `radioUpdated`, `radioLost`

### Connection Lifecycle

1. TCP connect to radio:4992
2. Radio sends `V<version>` then `H<handle>`
3. Subscribe: `sub slice all`, `sub pan all`, `sub tx all`, `sub atu all`,
   `sub meter all`
4. `client gui` + `client program AetherSDR`
5. Bind UDP socket, send `\x00` to radio:4992 (port registration)
6. `slice list` -- if empty, create default slice
7. `stream create type=remote_audio_rx`

---

## 2. State Management

### RadioModel (Central Hub)

- Owns sub-models as **value members** (not pointers): `MeterModel`,
  `TunerModel`, `TransmitModel`, `EqualizerModel`, `TnfModel`
- Worker threads: `RadioConnection` (on `m_connThread`),
  `PanadapterStream` (on `m_networkThread`)
- State containers: `QList<SliceModel*> m_slices`,
  `QMap<QString, PanadapterModel*> m_panadapters`
- Multi-Flex: `QSet<int> m_ownedSliceIds` for `client_handle` filtering

### Signal/Slot Command Chain

```
User tunes frequency
  -> SliceWidget::frequencyValueChanged
  -> SliceModel::setFrequency
  -> emit commandReady("slice tune ...")
  -> RadioModel::sendCmd
  -> QMetaObject::invokeMethod to conn thread
  -> RadioConnection::writeCommand
  -> QTcpSocket::write
```

### Status Processing Chain

```
Radio sends S<handle>|slice 0 RF_frequency=14.226...
  -> RadioConnection::processLine
  -> emit statusReceived (auto-queued to main)
  -> RadioModel::onStatusReceived
  -> handleSliceStatus
  -> SliceModel::applyStatus
  -> emit frequencyChanged
  -> GUI updates
```

### AppSettings (XML, NOT QSettings)

- File: `~/.config/AetherSDR/AetherSDR.settings`
- PascalCase keys, `"True"`/`"False"` booleans
- **Radio-authoritative policy:** NEVER persist radio-side settings (frequency,
  mode, filter, AGC, antennas, TX power)
- Only persist client-side settings (layout, display prefs, client DSP)

---

## 3. Audio Pipeline

### AudioEngine

- Lives on audio worker thread
- **RX path:** VITA-49 audio -> `PanadapterStream` -> `audioDataReady`
  (auto-queued) -> `AudioEngine` -> NR2/RN2/NR4/BNR/DFNR DSP ->
  `QAudioSink`
- **TX path:** `QAudioSource` -> `AudioEngine` -> Opus encode (if WAN) ->
  VITA-49 -> UDP to radio
- Lock-free parameter updates: `std::atomic` for DSP enables, no mutex in
  audio callback
- 200ms RX buffer cap to prevent unbounded growth
- DSP chain: `SpectralNR` (NR2), `RNNoiseFilter` (RN2),
  `SpecbleachFilter` (NR4), `NvidiaBnrFilter` (BNR),
  `DeepFilterFilter` (DFNR)

---

## 4. Spectrum/Waterfall Rendering

### SpectrumWidget (QRhiWidget)

- GPU rendering via Qt RHI (Vulkan/DX12/Metal/OpenGL)
- CPU fallback with `QPainter`
- **FFT data flow:** VITA-49 PCC 0x8003 -> `PanadapterStream::decodeFFT` ->
  emit `spectrumReady` -> `SpectrumWidget::updateSpectrum` -> exponential
  smoothing -> GPU vertex upload
- **Waterfall:** native tiles from radio (PCC 0x8004) OR derived from FFT
  frames -> ring buffer `QImage` -> GPU texture upload
- **Shaders:** waterfall (ring-buffer `fract()` offset), spectrum
  (per-vertex color), overlay (grid/markers/spots)
- **Overlay system:** slice VFO lines, filter passbands, TNF markers, DX
  spots with DXCC coloring

---

## 5. Panadapter Composition

### PanadapterStack

- Manages N `PanadapterApplet` instances in a vertical `QSplitter`
- Layout IDs: `"1"`, `"2v"`, `"2h"`, `"2h1"`, `"2x2"`, `"12h"`
- Tracks active panadapter

### wirePanadapter() in MainWindow

- Connects each pan's `SpectrumWidget` signals to `RadioModel` +
  `MainWindow`
- Slice overlay syncing per-pan
- Click-to-tune with panId context
- Filter edge drag
- Display settings (bandwidth change)
- Overlay menu per-pan
- **Critical:** disconnect dying pan widgets BEFORE removal to prevent lambda
  crashes

---

## 6. Multi-Slice Architecture

### Slice Ownership

- Created via `"slice create freq=... mode=... antenna=..."`
- Radio assigns ID, sends status with `client_handle`
- `m_ownedSliceIds` tracks which slices are ours
- Multi-Flex: other clients' slices removed from local model when
  `client_handle` doesn't match
- Early status messages arrive WITHOUT `client_handle` -- create for all
  initially, filter later

### Slice-to-Panadapter Assignment

- Radio assigns via `pan=` field in creation status
- Cross-pan: `"slice m <freq> pan=<panId>"`
- `autopan=0` prevents radio from recentering

---

## 7. Patterns for NereusSDR Reuse

### Adopt As-Is

- `RadioModel` as central hub (owns sub-models on main thread)
- Worker threads + auto-queued signals
- `AppSettings` for UI state persistence (NOT radio state)
- GPU rendering with `QRhiWidget`
- Multi-pan `PanadapterStack` + `wirePanadapter()`
- `SliceModel` signal-driven design (`emit commandReady`, `RadioModel`
  sends)

### Must Adapt for OpenHPSDR

- **Status message format** -- OpenHPSDR protocol, not SmartSDR
- **FFT computation** -- move from radio to client (`LocalFFTEngine`)
- **Waterfall generation** -- from FFT frame history, not native tiles
- **Metering** -- compute from FFT or audio, not separate VITA-49 packets
- **Command format** -- adapt to OpenHPSDR protocol
- **VITA-49 packet parsing** -- replace with OpenHPSDR framing

### Do Not Copy

- VITA-49 FFT/waterfall tiles
- Firmware-specific quirks
- Radio-side DSP state (NB, NR, ANF on slice)
- `GUIClientID` session restore

---

## 8. Thread Architecture

| Thread | Components |
|--------|-----------|
| Main | GUI rendering, RadioModel, all sub-models, user input |
| Connection | RadioConnection (TCP 4992 I/O) |
| Audio | AudioEngine (RX/TX audio, NR2/RN2/BNR DSP) |
| Network | PanadapterStream (VITA-49 UDP parsing) |
| ExtControllers | FlexControl, MIDI, SerialPort |
| Spot | DX Cluster, RBN, WSJT-X, POTA, FreeDV |

Cross-thread communication uses auto-queued signals exclusively. Never hold a
mutex in the audio callback.

---

## 9. Key Files Reference

| File | Purpose |
|------|---------|
| `src/core/RadioConnection.h/.cpp` | TCP protocol, command/response |
| `src/core/RadioDiscovery.h/.cpp` | SSDP discovery |
| `src/models/RadioModel.h/.cpp` | Central state hub |
| `src/core/AppSettings.h/.cpp` | XML settings persistence |
| `src/core/PanadapterStream.h/.cpp` | VITA-49 UDP demux |
| `src/core/AudioEngine.h/.cpp` | Audio pipeline + DSP |
| `src/gui/SpectrumWidget.h/.cpp` | GPU rendering |
| `src/gui/PanadapterStack.h/.cpp` | Multi-pan layout |
| `src/models/SliceModel.h/.cpp` | Per-slice state |
| `docs/multi-pan-pitfalls.md` | 20 lessons learned |
