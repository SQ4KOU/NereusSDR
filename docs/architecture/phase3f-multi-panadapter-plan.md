# Phase 3F: Multi-Panadapter Implementation Plan

**Status:** Planning
**Date:** 2026-04-09
**Author:** JJ Boyd ~KG4VCF, Co-Authored with Claude Code
**Prerequisite:** Phase 3E (VFO + multi-receiver I/Q pipeline foundation)

---

## Goal

Support 1-4 panadapters in configurable layouts with proper DDC-to-ADC mapping
and multiple active receivers. After Phase 3E delivers the multi-receiver I/Q
pipeline (per-receiver WDSP channels and FFTEngines), this phase adds:

1. The DDC assignment state machine (UpdateDDCs port from Thetis)
2. FFTRouter to map receivers to panadapters
3. PanadapterStack UI with 5 layout modes
4. RX2 enablement on DDC3

---

## Prerequisites from Phase 3E

Phase 3E must deliver these before 3F can begin:

- [x] I/Q routed through ReceiverManager (not hardcoded in RadioModel)
- [x] Per-receiver WDSP channels in WdspEngine
- [x] Per-receiver FFTEngine instances
- [x] SliceModel with slice-to-receiver mapping
- [x] ReceiverManager board-type-aware DDC mapping (DDC2=RX1 on 2-ADC)

---

## Implementation Steps

### Step 1: Port UpdateDDCs() from Thetis

**Source:** `console.cs:8186-8538` (353 lines)

Port the DDC assignment state machine to `ReceiverManager::updateDdcAssignment()`.
This function determines which DDCs are enabled, their sample rates, sync pairs,
and ADC assignments based on:

- Board type (1-ADC vs 2-ADC)
- MOX state (RX vs TX)
- Diversity enabled
- PureSignal enabled
- RX2 enabled

#### State Machine Summary (2-ADC boards: ANAN-G2, 7000DLE, 8000DLE, etc.)

| State | DDC0 | DDC1 | DDC2 | DDC3 | SyncEnable |
|-------|------|------|------|------|------------|
| Normal RX | idle | idle | RX1 | +RX2 | none |
| RX + Diversity | RX1 (ADC0) | sync (ADC1) | idle | +RX2 | DDC1 |
| TX, no PS | idle | idle | RX1 | +RX2 | none |
| TX + PureSignal | PS fwd | PS rev (ADC2) | RX1 | +RX2 | DDC1 |
| TX + Div + PS | PS fwd | PS rev (ADC2) | RX1 | +RX2 | DDC1 |
| TX + Diversity | RX1 (ADC0) | sync (ADC1) | idle | +RX2 | DDC1 |

#### State Machine Summary (1-ADC boards: Hermes, ANAN-10, ANAN-100)

| State | DDC0 | DDC1 | SyncEnable |
|-------|------|------|------------|
| Normal RX | RX1 | +RX2 | none |
| RX + Diversity | RX1 div | sync | DDC1 |
| TX + PureSignal | PS fwd | PS rev | DDC1 |

#### Output of updateDdcAssignment()

```cpp
struct DdcAssignment {
    int ddcEnable;      // Bitmask: which DDCs are active (DDC0=bit0..DDC6=bit6)
    int syncEnable;     // Bitmask: which DDCs are synced
    int rate[8];        // Per-DDC sample rate in Hz (0 = not set)
    int adcCtrl1;       // ADC assignment for DDC0-3 (2 bits each)
    int adcCtrl2;       // ADC assignment for DDC4-7 (2 bits each)
    int p1DdcConfig;    // P1-specific preset (0-6)
    int p1Diversity;    // P1 diversity flag
    int p1RxCount;      // P1 receiver count
    int nddc;           // Number of DDCs in use
};
```

#### Wiring to P2RadioConnection

After computing the assignment, call these on P2RadioConnection:

```cpp
// These map to sendCmdRx() byte layout (P2RadioConnection.cpp:510-549)
connection->setDdcEnabled(i, assignment.ddcEnable & (1 << i));
connection->setDdcSampleRate(i, assignment.rate[i]);
connection->setDdcAdcAssignment(i, getAdcForDdc(i, assignment));
connection->setDdcSync(i, assignment.syncEnable & (1 << i));
```

The P2RadioConnection already has `RxState` fields for `enable`, `samplingRate`,
`rxAdc`, and `sync` (P2RadioConnection.h:95-108). They just need to be
populated from the assignment.

#### ADC Override for PureSignal

From Thetis console.cs:8264:
```csharp
cntrl1 = (rx_adc_ctrl1 & 0xf3) | 0x08;
// Mask 0xf3 = 11110011: clears DDC1 bits [3:2]
// OR 0x08 = 00001000: sets DDC1 to ADC2 (PA feedback)
```

#### Files to Modify

- `src/core/ReceiverManager.h` — add `DdcAssignment` struct, `updateDdcAssignment()`,
  `rxAdcCtrl1()`, `rxAdcCtrl2()`, `getAdcInUse(int ddc)` methods
- `src/core/ReceiverManager.cpp` — implement UpdateDDCs state machine
  (port from Thetis console.cs:8186-8538)
- `src/core/P2RadioConnection.h` — add `setDdcEnabled()`, `setDdcSampleRate()`,
  `setDdcAdcAssignment()`, `setDdcSync()` slots
- `src/core/P2RadioConnection.cpp` — implement setters that update `m_rx[i]` fields

#### Thetis Source Reference

| What to Port | Source File | Lines |
|-------------|------------|-------|
| UpdateDDCs() state machine | console.cs | 8186-8538 |
| GetDDC() lookup | console.cs | 8540-8640 |
| rx_adc_ctrl properties | console.cs | 15072-15130 |
| GetADCInUse() | console.cs | 15083-15106 |

---

### Step 2: Implement FFTRouter

**Design:** `docs/architecture/multi-panadapter.md` lines 599-632

FFTRouter sits between FFTEngine instances and PanadapterApplet widgets.
It receives FFT frames from receivers and fans them out to subscribed pans.

```cpp
// src/core/FFTRouter.h
class FFTRouter : public QObject {
    Q_OBJECT
public:
    void mapPanToReceiver(int panId, int receiverId);
    void removePan(int panId);
    void removeReceiver(int receiverId);
    void setPanWidget(int panId, PanadapterApplet* applet);

public slots:
    // Connected to FFTEngine::fftReady for each receiver
    void onFftFrame(int receiverId, const QVector<float>& binsDbm);

private:
    QMap<int, QList<int>> m_receiverToPans;    // receiverId -> panIds
    QMap<int, PanadapterApplet*> m_panWidgets;  // panId -> applet
};
```

**Key behaviors:**
- Multiple pans can subscribe to the same receiver (different zoom levels)
- Each pan extracts its visible portion from the full FFT bins
- Adding/removing pans is dynamic (runtime layout changes)

#### Files to Create
- `src/core/FFTRouter.h` — **new**
- `src/core/FFTRouter.cpp` — **new**

---

### Step 3: Implement PanadapterStack

**Design:** `docs/architecture/multi-panadapter.md` lines 82-161

PanadapterStack manages N PanadapterApplet instances using nested QSplitters.

#### 5 Layout Configurations

```
Layout "1":     Single full-size pan
Layout "2v":    QSplitter(V) — two pans stacked
Layout "2h":    QSplitter(H) — two pans side by side
Layout "2x2":   QSplitter(V) > 2x QSplitter(H) — four pans in grid
Layout "12h":   QSplitter(V) > [Pan A stretch=2] + QSplitter(H) [Pan B, Pan C]
```

#### Class Interface

```cpp
// src/gui/PanadapterStack.h
class PanadapterStack : public QWidget {
    Q_OBJECT
public:
    PanadapterApplet* addPanadapter(int panId);
    void removePanadapter(int panId);
    void removeAll();
    void applyLayout(const QString& layoutId, const QList<int>& panIds);
    void equalizeSizes();
    void saveSplitterState();
    void restoreSplitterState();

    PanadapterApplet* panadapter(int panId) const;
    SpectrumWidget* spectrum(int panId) const;
    int count() const;
    void setActivePan(int panId);
    PanadapterApplet* activeApplet() const;

signals:
    void activePanChanged(int panId);
    void countChanged(int count);

private:
    void rebuildSplitters(const QString& layoutId, const QList<int>& panIds);
    void clearSplitters();

    QSplitter* m_rootSplitter{nullptr};
    QMap<int, PanadapterApplet*> m_pans;
    int m_activePanId{0};
    QString m_currentLayoutId{"1"};
};
```

#### Layout Persistence (AppSettings)

| Key | Example | Description |
|-----|---------|-------------|
| `PanLayoutId` | `"2v"` | Current layout |
| `PanCount` | `"2"` | Number of active pans |
| `PanSplitter0Sizes` | `"300,300"` | Root splitter sizes |
| `Pan0CenterMhz` | `"14.225"` | Per-pan center frequency |
| `Pan0BandwidthMhz` | `"0.200"` | Per-pan display bandwidth |
| `Pan0FftSize` | `"4096"` | Per-pan FFT size |

#### Files to Create
- `src/gui/PanadapterStack.h` — **new**
- `src/gui/PanadapterStack.cpp` — **new**

---

### Step 4: Implement PanadapterApplet

**Design:** `docs/architecture/multi-panadapter.md` lines 229-337

Container for a single panadapter view: title bar + SpectrumWidget + optional
CW decode panel.

```
+-----------------------------------------------+
| Title Bar (16px gradient)    [Rx1: 14.225 USB] |
+-----------------------------------------------+
|                                                |
|  SpectrumWidget                                |
|  (FFT spectrum ~40% + waterfall ~60%)          |
|                                                |
+-----------------------------------------------+
| CW Decode Panel (optional, collapsible)        |
+-----------------------------------------------+
```

#### Per-Pan State

| State | Type | Default | Notes |
|-------|------|---------|-------|
| `m_panId` | int | 0 | Client-assigned index |
| `m_centerMhz` | double | 14.225 | Pan center frequency |
| `m_bandwidthMhz` | double | 0.200 | Display bandwidth (limited by DDC sample rate) |
| `m_fftSize` | int | 4096 | FFT size |
| `m_activeSliceId` | int | -1 | Which slice receives tune commands |
| `m_associatedSlices` | QSet<int> | {} | Slice IDs overlaid on this pan |

#### Key Behaviors
- Clicking anywhere emits `activated(panId)` -> PanadapterStack::setActivePan()
- Each pan has an independent SpectrumWidget with its own display state
- Bandwidth is client-side state but capped by DDC sample rate (~384 kHz max)
- Auto-center tracking when active slice VFO moves near edge (10% margin)

#### Files to Create
- `src/gui/PanadapterApplet.h` — **new**
- `src/gui/PanadapterApplet.cpp` — **new**

---

### Step 5: Implement wirePanadapter()

**Design:** `docs/architecture/multi-panadapter.md` lines 354-491

Private method on MainWindow that connects all signals between a new
PanadapterApplet and the rest of the application.

#### Signal Routing Map

```
PanadapterApplet::activated(panId)
  -> PanadapterStack::setActivePan()

PanadapterApplet::closeRequested(panId)
  -> MainWindow: remove pan if count > 1, disconnect first

SpectrumWidget::frequencyClicked(hz)
  -> SliceModel::setFrequency(applet->activeSliceId(), hz)

SpectrumWidget::filterChangeRequested(lo, hi)
  -> SliceModel::setFilterWidth(applet->activeSliceId(), lo, hi)

SpectrumWidget::bandwidthChangeRequested(bw)
  -> PanadapterApplet::setBandwidthMhz(bw)  [client-side only]

SpectrumWidget::centerChangeRequested(center)
  -> PanadapterApplet::setCenterMhz(center)  [client-side only]
```

#### Disconnect-Before-Removal (from AetherSDR issue #242)

When removing a pan, ALL signals must be disconnected BEFORE widget destruction:

```cpp
if (auto* applet = m_panStack->panadapter(panId)) {
    if (auto* sw = applet->spectrumWidget()) {
        sw->disconnect(this);
        sw->disconnect(m_panStack);
    }
    applet->disconnect(this);
    applet->disconnect(m_panStack);
}
m_panStack->removePanadapter(panId);
```

#### Files to Modify
- `src/gui/MainWindow.h` — replace single `SpectrumWidget*` with `PanadapterStack*`
- `src/gui/MainWindow.cpp` — implement wirePanadapter(), layout menu actions,
  startup/restore sequence

---

### Step 6: Enable RX2

With all plumbing in place, enable the second receiver:

1. ReceiverManager creates receiver 1 and activates it
2. updateDdcAssignment() enables DDC3 for RX2
3. P2RadioConnection sends updated CmdRx with DDC3 enabled
4. Radio starts streaming I/Q from port 1038 (DDC3)
5. ReceiverManager routes DDC3 I/Q to receiver 1's WDSP channel
6. FFTEngine[1] computes spectrum for receiver 1
7. FFTRouter sends FFT data to pans subscribed to receiver 1

#### Verification

- Pan 0 shows RX1 (DDC2) spectrum on 20m
- Pan 1 shows RX2 (DDC3) spectrum on 40m
- Both pans have independent frequency, mode, filter
- Both audio streams are independently demodulated

---

### Step 7: Layout Menu and Runtime Changes

Add View menu entries:

```
View > Pan Layout > Single (1)
                    Stacked (2v)
                    Side by Side (2h)
                    Grid 2x2
                    Wide + 2 (12h)
View > Add Panadapter
View > Remove Panadapter
```

Runtime layout change sequence:
1. Save current splitter state
2. If new layout needs more pans: create additional PanadapterApplets
3. If new layout needs fewer pans: disconnect + remove excess
4. PanadapterStack::applyLayout(newLayoutId, panIds)
5. Save new layout ID to AppSettings

---

## Startup and Layout Restore Sequence

```
1. MainWindow::init()
   |
2. Read AppSettings: PanLayoutId, PanCount, per-pan state
   |
3. Create PanadapterStack (replaces single SpectrumWidget)
   |
4. For each saved pan (0..PanCount-1):
   |   a. PanadapterStack::addPanadapter(panId)
   |   b. wirePanadapter(applet)
   |   c. Restore per-pan settings (center, bandwidth, FFT size)
   |   d. Restore slice associations from AppSettings
   |
5. PanadapterStack::applyLayout(layoutId, panIds)
   |
6. PanadapterStack::restoreSplitterState()
   |
7. FFTRouter: map each pan to its receiver
   |
8. If no saved state: create single pan with slice 0, layout "1"
```

---

## Files Summary

### New Files (6)
| File | Lines (est) | Description |
|------|------------|-------------|
| `src/core/FFTRouter.h` | 50 | Receiver-to-pan FFT routing |
| `src/core/FFTRouter.cpp` | 80 | Fan-out FFT frames to subscribed pans |
| `src/gui/PanadapterStack.h` | 60 | QSplitter layout manager interface |
| `src/gui/PanadapterStack.cpp` | 250 | Layout algorithms, splitter management |
| `src/gui/PanadapterApplet.h` | 80 | Single pan container interface |
| `src/gui/PanadapterApplet.cpp` | 200 | Title bar, SpectrumWidget host, per-pan state |

### Modified Files (4)
| File | Changes |
|------|---------|
| `src/core/ReceiverManager.h/.cpp` | UpdateDDCs port, rx_adc_ctrl management, GetADCInUse |
| `src/core/P2RadioConnection.h/.cpp` | DDC setter slots (enable, rate, ADC, sync) |
| `src/gui/MainWindow.h/.cpp` | PanadapterStack replaces SpectrumWidget, wirePanadapter(), layout menu |
| `CMakeLists.txt` | Add new source files |

---

## Verification Checklist

- [ ] Single pan layout works (regression test from 3E)
- [ ] 2 pans stacked (2v): RX1 on 20m, RX2 on 40m, independent spectrums
- [ ] 2 pans same receiver: same data, different zoom levels
- [ ] 4 pans in 2x2: 2 on RX1, 2 on RX2
- [ ] Layout switch at runtime (2v -> 2x2 -> 1)
- [ ] Layout persistence across restart
- [ ] Per-pan display settings persist (center, bandwidth, ref level)
- [ ] Click-to-tune routes to correct slice on correct pan
- [ ] DDC2=RX1 on ADC0, DDC3=RX2 on ADC1 (verified via CmdRx packet)
- [ ] DDC enable bitmask correct in sendCmdRx() byte 7
- [ ] Per-DDC sample rates correct in sendCmdRx()
- [ ] Disconnect-before-removal: no crash when closing a pan
- [ ] Build green on all platforms (Ubuntu, Windows, macOS)

---

## Key Design References

- `docs/architecture/multi-panadapter.md` — PanadapterStack, PanadapterApplet,
  FFTRouter, wirePanadapter() interfaces
- `docs/architecture/adc-ddc-panadapter-mapping.md` — DDC assignment strategy,
  ADC control registers, bandwidth limits, Thetis source reference
- `docs/architecture/radio-abstraction.md` — ReceiverManager, P2RadioConnection,
  CmdRx packet format
- Thetis `console.cs:8186-8538` — UpdateDDCs() source (READ FIRST)
- Thetis `console.cs:8540-8640` — GetDDC() source
