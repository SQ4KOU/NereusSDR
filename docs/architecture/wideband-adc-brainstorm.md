# Wideband ADC Stream — Discovery & Future Implementation Brainstorm

**Date:** 2026-04-09
**Status:** Research complete, implementation deferred (Phase 2)
**Prerequisite:** DDC2 at 768 kHz (Phase 1) must be completed first

---

## Discovery Summary

The ANAN-G2 (Orion MkII / Saturn) FPGA provides two independent data paths:

### Path 1: DDC I/Q Streams (currently used)
- 7 DDCs (DDC0-6), each independently tunable
- Complex I/Q output at configurable rates: 48k, 96k, 192k, 384k, 768k, 1536k
- Ports 1035-1041 (one per DDC)
- Feeds both WDSP (audio) and FFTEngine (spectrum display)
- DDC2 is the primary RX for ANAN-G2 (non-diversity mode)

### Path 2: Wideband ADC Streams (not yet implemented)
- Raw 16-bit ADC samples at 122.88 MHz
- REAL data only (not I/Q complex)
- Per-ADC: ADC0 on port 1027, ADC1 on port 1028
- 512 samples per packet, 32 packets per frame = 16,384 samples
- Covers 0 to 61.44 MHz — entire HF spectrum in one shot
- Completely independent from DDC streams
- Thetis keeps this OFF by default (pcap `wb_enable = 0`)

---

## Thetis Wideband Implementation

### Enable
```
CmdGeneral byte 23: wb_enable bits (bit 0 = ADC0, bit 1 = ADC1)
SetWBEnable(0, 1)  — turns on ADC0 wideband
```

### Configuration (CmdGeneral bytes 23-28)
| Byte | Field | Default | Notes |
|------|-------|---------|-------|
| 23 | wb_enable | 0 | Per-ADC bit field |
| 24-25 | wb_samples_per_packet | 512 | Big-endian |
| 26 | wb_sample_size | 16 | Bits per sample |
| 27 | wb_update_rate | 70 | Milliseconds between frames |
| 28 | wb_packets_per_frame | 32 | 32 × 512 = 16384 samples |

### Packet Format (port 1027 for ADC0)
```
Bytes 0-3:   Sequence number (big-endian uint32)
Bytes 4-1027: 512 × 16-bit signed samples (big-endian)
Total: 1028 bytes per packet
```

### Decoding (from Thetis network.c:569-571)
```c
for (ii = 0, jj = 4; ii < wb_spp; ii++, jj += 2)
    wb_buff[ii] = const_1_div_2147483648_ *
        (double)(readbuf[jj + 0] << 24 | readbuf[jj + 1] << 16);
```
16-bit sample shifted to 32-bit range, then normalized to ±1.0 float.

### FFT Processing (from Thetis wbDisplay.cs + analyzer.c)
- FFT size: 16,384 (real-to-complex)
- FFTW plan: `fftw_plan_dft_r2c_1d(16384, ...)`
- Output: 8,192 complex bins (real FFT symmetry)
- Window: Kaiser (type 6 in WDSP analyzer)
- Bin width: 122,880,000 / 16,384 ≈ 7,500 Hz
- Display range: centered at sample_rate/4 = 30.72 MHz, spanning ±30.72 MHz
- Display ID: 32 + ADC number (separate from DDC display IDs)

### Frequency Mapping
```
bin_width = 122880000 / 16384 = 7500 Hz
useable_bins = 16384 / 2 - 2 * clip
total_bandwidth = useable_bins * bin_width
low_freq = 30720000 - bandwidth/2
high_freq = 30720000 + bandwidth/2
```

---

## How It Could Be Implemented in NereusSDR

### Architecture Overview

```
Radio ADC0 ─── port 1027 ──→ WbReceiver ──→ WbFFTEngine ──→ WbSpectrumWidget
                                                                   │
                                                              Full HF bandscope
                                                              (0-61 MHz)

Radio DDC2 ─── port 1037 ──→ ReceiverManager ──→ WDSP ──→ AudioEngine
                                    │
                                    └──→ FFTEngine ──→ SpectrumWidget
                                                           │
                                                      Zoomed panadapter
                                                      (±384 kHz at 768k rate)
```

### New Components Needed

1. **WbPacketParser** (`src/core/WbPacketParser.h/cpp`)
   - Listens on port 1027 (configurable per-ADC)
   - Parses 1028-byte packets: 4-byte seq + 512 × 16-bit samples
   - Accumulates 32 packets into 16,384 sample frame
   - Validates sequence numbers, handles drops
   - Emits `wbFrameReady(int adcIndex, QVector<float>& samples)`

2. **WbFFTEngine** (`src/core/WbFFTEngine.h/cpp`)
   - Real-to-complex FFT: `fftwf_plan_dft_r2c_1d(16384, ...)`
   - Kaiser window (match Thetis analyzer default)
   - Output: 8192 magnitude bins in dBm
   - Rate limited to ~14 FPS (70ms update rate)
   - Runs on its own thread (separate from DDC FFTEngine)

3. **WbSpectrumWidget** (`src/gui/WbSpectrumWidget.h/cpp`)
   - Full HF spectrum display (0-61 MHz)
   - Click to tune: click on wideband → VFO jumps to that frequency
   - Band annotations (160m, 80m, 40m, etc.)
   - Signal strength heat map
   - Optional: zoom capability within the wideband view
   - Could be a dockable panel below the main panadapter

4. **P2RadioConnection additions**
   - `setWbEnable(int adc, bool enabled)` — sets bit in CmdGeneral byte 23
   - Socket listener for port 1027 (already have the port configured)
   - Route wideband packets to WbPacketParser

### Configuration via AppSettings
```
WbEnabled: True/False (default False)
WbUpdateRate: 70 (ms)
WbFftSize: 16384
WbWindowType: Kaiser
```

### UI Integration Options

**Option A: Split view**
```
┌─────────────────────────────────────┐
│  Wideband Bandscope (0-61 MHz)     │  ← WbSpectrumWidget
│  ▁▂▃▅▇█▅▃▂▁  ▂▅▇█▅▂  ▁▃▅▇▅▃▁    │
├─────────────────────────────────────┤
│  Zoomed Panadapter (±384 kHz)      │  ← SpectrumWidget (existing)
│  ████████████                       │
│  ▓▓▓▓ waterfall ▓▓▓▓               │
└─────────────────────────────────────┘
```

**Option B: Tab or toggle**
- Menu item: View → Wideband Scope
- Toggle between zoomed panadapter and full HF view
- Or a floating/dockable panel

**Option C: Overlay indicator**
- Small minimap at top of existing panadapter showing full HF spectrum
- Highlighted box shows current zoomed view position

### Performance Considerations
- Wideband packets: ~457 packets/sec (32 packets × ~14 frames/sec)
- Each packet: 1028 bytes → ~458 KB/sec network bandwidth
- FFT computation: 16384-point real FFT ~14 times/sec — negligible CPU
- Memory: 16384 × 4 bytes (float) × 2 (in+out) ≈ 128 KB per FFT buffer
- Display rendering: 8192 bins → downsample to pixel width

### Data Flow Timing
```
32 packets arrive over ~2.1ms (512 samples × 32 / 122.88MHz × 1000)
  → WbPacketParser accumulates into 16384-sample buffer
  → WbFFTEngine runs real-to-complex FFT
  → 8192 bins converted to dBm
  → WbSpectrumWidget updates display
  → Rate limited to 70ms (configurable)
```

---

## References

- Thetis source: `Project Files/Source/ChannelMaster/network.c` lines 550-603
- Thetis source: `Project Files/Source/ChannelMaster/netInterface.c` lines 1343-1349, 1461-1466
- Thetis source: `Project Files/Source/wdsp/analyzer.c` lines 1703-1831
- Thetis source: `Project Files/Source/Console/wbDisplay.cs` lines 4651-4730
- Thetis source: `Project Files/Source/ChannelMaster/network.h` lines 110-140
- NereusSDR pcap analysis: `captures/thetis-3865-lsb-pcap-analysis.md`
- OpenHPSDR Protocol 2 specification
