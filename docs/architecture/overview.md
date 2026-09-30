# NereusSDR Architecture Overview

## Layer Diagram

```
+---------------------------------------------+
|                  GUI Layer                    |
|  MainWindow, SpectrumWidget, VfoWidget, etc. |
+---------------------------------------------+
|                Model Layer                    |
|  RadioModel, SliceModel, PanadapterModel     |
+---------------------------------------------+
|                 Core Layer                    |
|  RadioDiscovery, RadioConnection, AudioEngine |
|  WdspEngine, AppSettings                     |
+---------------------------------------------+
|              Protocol Layer                   |
|  OpenHPSDR P1 (UDP 1024), P2 (UDP multi-port)|
+---------------------------------------------+
|                DSP Layer                      |
|  WDSP (demod, AGC, NR, NB, ANF, PureSignal) |
|  FFTW3 (spectrum FFT)                        |
+---------------------------------------------+
```

## Key Architectural Difference from AetherSDR

In AetherSDR, the FlexRadio does most DSP and sends:
- Decoded audio (PCM) for speakers
- FFT bin data for spectrum display
- Waterfall tile images for waterfall display

In NereusSDR, the OpenHPSDR radio sends only:
- Raw I/Q samples (24-bit, from the ADC)
- Feedback data (forward power, SWR, etc.)

**The client does ALL signal processing:**
1. I/Q samples -> WDSP -> Demodulated audio
2. I/Q samples -> FFTW3 -> FFT bins -> GPU rendering -> Spectrum/Waterfall
3. I/Q samples -> WDSP -> PureSignal feedback processing

This means:
- Client CPU/GPU usage is significantly higher than AetherSDR
- Multiple receivers require multiple WDSP channels (each is an independent DSP pipeline)
- Spectrum/waterfall quality is limited by client FFT size and GPU rendering performance
- PureSignal feedback loop runs entirely on the client

## Thread Architecture

| Thread | Components |
|--------|-----------|
| **Main** | GUI rendering, RadioModel, all sub-models, user input |
| **Connection** | RadioConnection (UDP I/O, protocol framing) |
| **Audio** | AudioEngine + WdspEngine (I/Q processing, DSP, audio output) |
| **Spectrum** | FFT computation, waterfall data generation |

Cross-thread communication uses auto-queued signals exclusively.

## Data Flow: RX Path

```
Radio (ADC) --UDP--> RadioConnection --signal--> RadioModel
                                                      |
                                                      v
                                                 WdspEngine (per-channel)
                                                 |          |
                                                 v          v
                                          AudioEngine   FFT/Spectrum
                                              |              |
                                              v              v
                                          Speakers      SpectrumWidget
```

### RX path in detail (P2, CTUN + zoom)

Moved from CLAUDE.md.

```
Radio (ADC) → UDP port 1037 (DDC2) → P2RadioConnection
    ↓ iqDataReceived(ddcIndex=2, interleaved float I/Q)
ReceiverManager::feedIqData(2) → maps DDC2 → receiver 0
    ↓ iqDataForReceiver(0, samples)
RadioModel lambda:
    ├── emit rawIqData(samples) → FFTEngine → SpectrumWidget
    ├── Deinterleave I/Q, accumulate 238 → 1024 samples
    └── RxChannel::processIq() → fexchange2() → decoded audio
        ↓
    AudioEngine::feedAudio() → float→int16 → m_rxBuffer
        ↓ 10ms timer drain
    QAudioSink (48kHz stereo Int16) → Speakers

FFT → Display (with zoom):
    FFTEngine emits N bins (full DDC bandwidth)
    → SpectrumWidget::updateSpectrum() stores in m_smoothed
    → visibleBinRange(N) maps m_centerHz ± m_bandwidthHz/2 to bin indices
      using m_ddcCenterHz + m_sampleRateHz for bin-to-frequency mapping
    → GPU/CPU renderer iterates only [firstBin..lastBin], stretched to full display
    → pushWaterfallRow() writes only visible bin subset to waterfall texture

User zooms (freq scale drag or Ctrl+scroll):
    m_bandwidthHz changes → visibleBinRange() narrows → immediate visual zoom
    On mouse release → bandwidthChangeRequested → MainWindow replans FFT size
    → FFTEngine delivers more bins → sharper resolution at new zoom level

User tunes VFO:
    VfoWidget (wheel/click/edit) → emit frequencyChanged(hz)
    → SliceModel::setFrequency(hz)
    → ReceiverManager::setReceiverFrequency(0, hz)
      → hardwareFrequencyChanged(DDC2, hz)
      → P2RadioConnection::setReceiverFrequency(2, hz) + Alex HPF/LPF update
      → sendCmdHighPriority() → radio retunes DDC NCO
```

## Data Flow: TX Path

```
Microphone --> AudioEngine --> WdspEngine (TX channel)
                                    |
                                    v
                              Modulated I/Q
                                    |
                                    v
                              RadioConnection --UDP--> Radio (DAC)
```
