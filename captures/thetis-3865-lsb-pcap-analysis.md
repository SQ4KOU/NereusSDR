# Thetis PCAP Analysis: Connect + Tune to 3865 kHz LSB

**Capture:** `thetis-tune.pcapng` (70 MB, ~20s)
**Date:** 2026-04-08
**Radio:** ANAN-G2 (Orion MkII) at 192.168.109.45, MAC `2c:cf:67:ab:fc:f4`
**PC:** 192.168.109.19
**Protocol:** OpenHPSDR Protocol 2 (UDP, phase word mode)

---

## Discovery (Radio → PC)

| Field | Value |
|-------|-------|
| Port | UDP 1024 (broadcast response) |
| Board ID | 0x0a (10) = Orion MkII / Saturn |
| MAC | 2c:cf:67:ab:fc:f4 |
| Firmware | 43.27 |

The radio responds with a 120-byte discovery packet. Contains two identical radio entries (60 bytes each), both for the same MAC — single-radio response.

---

## Connection Sequence

Thetis fires 4 UDP commands in rapid succession (<130ms), then starts a 500ms keepalive timer:

### Step 1: RX Specific (port 1025, 1444 bytes) — t=0.180s

Sent FIRST, before general command. Sets DDC configuration:

| Field | Value | Notes |
|-------|-------|-------|
| Num ADC | 2 | ANAN-G2 has 2 ADCs |
| Dither | 0x07 | All 3 ADC inputs: dither ON |
| Random | 0x07 | All 3 ADC inputs: random ON |
| DDC Enable | 0x00 → 0x04 | Initially 0, then DDC2 enabled in frame 5 (20ms later) |

**Per-DDC configuration (7 DDCs, 6 bytes each starting at byte 17):**

| DDC | ADC | Rate | SPP | Bits | Status |
|-----|-----|------|-----|------|--------|
| DDC0 | 0 | 0 kHz | 0 | 24 | Disabled |
| DDC1 | 1 | 0 kHz | 0 | 24 | Disabled |
| **DDC2** | **0** | **768 kHz** | **0 (default 238)** | **24** | **ENABLED** |
| DDC3 | 0 | 0 kHz | 0 | 24 | Disabled |
| DDC4 | 0 | 48 kHz | 0 | 24 | Disabled |
| DDC5 | 0 | 48 kHz | 0 | 24 | Disabled |
| DDC6 | 0 | 48 kHz | 0 | 24 | Disabled |

**Key finding:** DDC2 is the primary receiver on ANAN-G2, not DDC0. DDC2 → ADC0 at 768 kHz sample rate. This matches Thetis `console.cs:8216 UpdateDDCs()` for OrionMkII/Saturn in non-diversity mode.

### Step 2: General Command (port 1024, 60 bytes) — t=0.308s

Keepalive / configuration command. Sent every 500ms while connected.

| Field | Byte | Value | Notes |
|-------|------|-------|-------|
| Command type | 4 | 0x00 | Standard command |
| Phase word mode | 37 | 0x08 (bit 3) | **Frequencies sent as NCO phase words, NOT Hz** |
| Watchdog timer | 38 | 1 | Enabled — radio requires this for streaming |
| PA enable | 58 | 0x01 | PA OFF (inverted logic: 0=ON, 1=OFF) |
| Wideband enable | 23 | 0 | Disabled |
| Wideband SPP | 24-25 | 512 | |
| Wideband bits | 26 | 16 | |
| Wideband rate | 27 | 70 ms | |

**Port assignments (all confirmed in pcap):**

| Port | Direction | Purpose |
|------|-----------|---------|
| 1024 | PC → Radio | General command / keepalive |
| 1025 | PC → Radio | RX specific command |
| 1025 | Radio → PC | High priority status (60 bytes) |
| 1026 | PC → Radio | TX specific command |
| 1026 | Radio → PC | Mic samples (132 bytes) |
| 1027 | PC → Radio | High priority command (1444 bytes) |
| 1027 | Radio → PC | Wideband ADC data |
| 1028 | — | RX audio (unused in pcap) |
| 1029 | PC → Radio | TX I/Q data |
| 1035 | Radio → PC | DDC0 I/Q (not active) |
| 1037 | Radio → PC | **DDC2 I/Q — primary RX data** |

### Step 3: TX Specific (port 1026, 60 bytes) — t=0.308s

| Field | Byte | Value |
|-------|------|-------|
| Num DAC | 4 | 1 |
| CW mode control | 5 | 0xB8 |
| Sidetone level | 6 | 63 |
| Sidetone freq | 7-8 | 600 Hz |
| Keyer speed | 9 | 25 WPM |
| Keyer weight | 10 | 50 |
| Hang delay | 11-12 | 310 ms |
| RF delay | 13 | 7 ms |
| TX sample rate | 14-15 | 192 kHz |
| CW edge length | 17 | 9 |
| Mic control | 50 | 0x0C |
| Line-in gain | 51 | 23 |
| ADC0 TX step attn | 59 | 31 dB |
| ADC1 TX step attn | 58 | 31 dB |
| ADC2 TX step attn | 57 | 31 dB |

### Step 4: High Priority Command (port 1027, 1444 bytes) — t=0.308s

The main state command. Contains frequencies, Alex filters, attenuators, preamp.

---

## Frequency Configuration (Phase Words)

**CRITICAL:** General command byte 37 bit 3 = 1 means frequencies are **NCO phase words**, not Hz.

**Phase word → Hz conversion:** `freq_hz = phase_word × 122,880,000 / 2^32`

| Receiver | Phase Word | Frequency | Notes |
|----------|-----------|-----------|-------|
| RX[0] (DDC0) | 0x080B6967 | 3,861,397 Hz | Not active, set same as RX2 |
| RX[1] (DDC1) | 0x080B6967 | 3,861,397 Hz | Not active, set same as RX2 |
| **RX[2] (DDC2)** | **0x080B6967** | **3,861,397 Hz** | **Primary RX — NCO center for 768 kHz span** |
| RX[3] (DDC3) | 0x081A67C3 | 3,889,510 Hz | Secondary (sub-RX or RX2) |
| **TX[0]** | **0x080D5555** | **3,865,000 Hz** | **Exact 3865.000 kHz** |

**Why RX DDC NCO ≠ dial frequency:**
- Dial: 3,865,000 Hz (3865 kHz)
- DDC2 NCO: 3,861,397 Hz (~3.6 kHz below dial)
- The 768 kHz DDC bandwidth captures ~3477–4245 kHz centered on the NCO
- WDSP handles final tuning offset, filtering, and demodulation in software
- The NCO offset from dial is a Thetis implementation detail (LO offset for image rejection or IF centering)

---

## ADC / Antenna / Filter Configuration

### ADC Assignment

| Component | Setting |
|-----------|---------|
| DDC2 (primary RX) → | **ADC0** |
| DDC1 → | ADC1 (not active) |
| Num ADCs | 2 |
| ADC0 RX step attn | **0 dB** |
| ADC1 RX step attn | **0 dB** |
| RX0 preamp | **OFF** |
| RX1 preamp | **OFF** |

### Alex Filter Registers

Both Alex0 and Alex1 are identical: **0x01400020**

```
Binary: 00000001 01000000 00000000 00100000
         ||||||||  ||||||||  ||||||||  ||||||||
         bit 31-24 bit 23-16 bit 15-8  bit 7-0
```

| Bit | Value | Meaning |
|-----|-------|---------|
| 24 | 1 | **ANT1 selected** (RX/TX antenna) |
| 22 | 1 | Alex routing (HPF/antenna path for Orion MkII) |
| 5 | 1 | **80/60m band filter engaged** |

**Summary for 80m (3.865 MHz):**
- Antenna: **ANT1**
- Band filter: **80/60m BPF** (provides bandpass filtering appropriate for 3.5–4.0 MHz)
- Attenuator: 0 dB (no attenuation)
- Preamp: OFF

---

## Disconnect Sequence

| Event | Time | Details |
|-------|------|---------|
| Stop command | t=7.87s (session 1) | HP packet with byte 4 = 0x00 (Run=0) |
| Same frequencies maintained | — | All NCO values unchanged in stop packet |
| Keepalive stops | — | No more General commands on port 1024 |

The disconnect is a single HP command with `Run=0`. Frequencies and filter settings remain in the stop packet.

---

## Data Flow Summary

```
PC (192.168.109.19)                    Radio (192.168.109.45)
       |                                       |
       |── Discovery req (port 1024) ─────────>|
       |<── Discovery resp (120 bytes) ────────|
       |                                       |
       |── RX Specific (port 1025, 1444B) ────>|  DDC2 on ADC0, 768kHz
       |── General (port 1024, 60B) ──────────>|  Phase word mode, WDT=1
       |── TX Specific (port 1026, 60B) ──────>|  DAC config, CW params
       |── High Priority (port 1027, 1444B) ──>|  NCO=3861kHz, TX=3865kHz
       |                                       |  Alex: ANT1, 80m BPF
       |                                       |
       |<── DDC2 I/Q (port 1037, 1444B) ──────|  238 samples/pkt, 24-bit
       |    ~2200 packets/sec (768kHz/238+hdr) |  I/Q → WDSP → audio
       |<── HP Status (port 1025, 60B) ───────|  ADC overflow, fwd/rev pwr
       |<── Mic samples (port 1026, 132B) ────|
       |                                       |
       |── TX I/Q (port 1029, ~1500B) ────────>|  Silence frames (no TX)
       |── General keepalive (every 500ms) ───>|
       |── HP updates (on freq/setting change) >|
       |                                       |
       |── HP Run=0 (port 1027) ──────────────>|  Disconnect
```

---

## Implications for NereusSDR

### Bug: NereusSDR sends Hz, Thetis sends phase words
NereusSDR `P2RadioConnection::sendCmdHighPriority()` writes raw Hz values:
```cpp
writeBE32(buf, offset, static_cast<quint32>(m_rx[i].frequency));
```
But General command byte 37 = 0x08 tells the radio to expect **phase words**.
Either:
1. Convert Hz → phase words before sending: `pw = freq_hz * 2^32 / 122880000`
2. Or clear bit 3 of byte 37 to request Hz mode (if radio supports it)

### DDC2 is the primary receiver
Thetis enables DDC2 (not DDC0) for ANAN-G2. NereusSDR already handles this (`m_rx[2].enable = 1`), confirmed correct.

### RX command sent BEFORE General command
Thetis sends RX Specific (port 1025) ~130ms before General (port 1024). The initial RX command has DDC enable = 0x00, followed 20ms later by a second RX command with DDC2 enabled (0x04). This two-step enable is important.

### Alex registers need implementation
NereusSDR's `setAntenna()` is currently stubbed. The pcap shows Alex0/Alex1 at HP packet bytes 1428-1435 with band-appropriate filter settings (ANT1 + 80/60m BPF for 80m).

### TX I/Q flows even in RX-only mode
Port 1029 carries TX I/Q data from PC → Radio even without MOX. Thetis sends ~800 packets/sec of silence frames. This may be required by the radio protocol.
