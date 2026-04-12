---
date: 2026-04-12
author: JJ Boyd (KG4VCF)
status: Reference — derived from HL2_Packet_Capture.pcapng
related:
  - docs/protocols/openhpsdr-protocol1.md
  - docs/architecture/radio-abstraction.md
  - docs/superpowers/specs/2026-04-12-p1-capture-reference-design.md
---

# OpenHPSDR Protocol 1 — Annotated Capture Reference (Hermes Lite 2)

This document is the byte-level ground-truth reference for OpenHPSDR
Protocol 1 as implemented by Hermes Lite 2 firmware, derived from a
single live capture of a Thetis-class host talking to an HL2 over a
direct link-local Ethernet connection. Every layout claim in this doc
is backed by a hex dump from the capture and a Thetis `NetworkIO.cs`
source citation. NereusSDR Phase 3L (P1 support) implements against
this document.

## 1. Capture Metadata

| Property | Value |
| --- | --- |
| File | `HL2_Packet_Capture.pcapng` (~324 MB) |
| Frames | 302,256 total (302,252 IPv4/UDP, 4 ARP) |
| Duration | ~55.7 s session (+ ~4 s DHCP tail) |
| Host | `169.254.105.135` (link-local) |
| Radio (HL2) | `169.254.19.221`, UDP port `1024` |
| Discovery | host `:50533` → broadcast `:1024`, reply from radio |
| Session | host `:50534` ↔ radio `:1024` |
| Direction split | 281,195 frames radio→host (EP6), 21,049 frames host→radio (EP2) |

The capture is a single clean session: discovery → start → steady-state
RX → stop. Subsequent sections walk through each phase.

<!-- Sections 3-10 and Appendix A added by later tasks -->

## 2. Discovery Exchange

P1 discovery is a one-shot broadcast UDP exchange on port 1024. The host
sends a 63-byte packet to the subnet broadcast address; the radio replies
from its own port 1024 to the host's ephemeral source port. This handshake
precedes every session: no start command is sent until at least one valid
reply is received.

### 2.1 Discovery REQUEST (host → broadcast :1024)

UDP payload (63 bytes, frames 1 and 4 in the capture):

```text
Offset  Hex                                               ASCII
------  ------------------------------------------------  -----
00      ef fe 02 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
10      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
20      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ........
30      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00      ...............
```

Field legend:

- **bytes 0–1** `EF FE` — P1 sync / frame marker
- **byte 2** `02` — command: discovery request
- **bytes 3–62** `00 * 60` — padding (all zero); no additional fields defined
  for the request direction

Thetis send: `clsRadioDiscovery.cs:1301` — `buildDiscoveryPacketP1()`

### 2.2 Discovery REPLY (radio :1024 → host :50533)

UDP payload (60 bytes, frames 2 and 5 in the capture; both identical):

```text
Offset  Hex                                               ASCII
------  ------------------------------------------------  -----
00      ef fe 02 00 1c c0 a2 13 dd 4a 06 00 00 00 00 00  .........J......
10      00 00 00 04 45 02 00 00 00 00 00 03 03 ef 00 00  ....E...........
20      00 00 00 00 80 16 46 36 5e 83 00 00 00 00 00 00  ......F6^.......
30      00 00 00 00 00 00 00 00 00 00 00 00              ............
```

Field legend (P1 parser: `clsRadioDiscovery.cs:1122` — `parseDiscoveryReply()`):

- **byte 0** `EF` — sync (matches `data[0] == 0xef`)
- **byte 1** `FE` — sync (matches `data[1] == 0xfe`)
- **byte 2** `02` — status: `02` = available, `03` would mean busy
  (`r.IsBusy = (data[2] == 0x3)` — `clsRadioDiscovery.cs:1147`)
- **bytes 3–8** `00 1C C0 A2 13 DD` — MAC address of radio
  (`Array.Copy(data, 3, mac, 0, 6)` — `clsRadioDiscovery.cs:1150`);
  HL2 MAC seen in this capture: **00:1C:C0:A2:13:DD**
- **byte 9** `4A` — firmware / code version = `0x4A` (decimal 74)
  (`r.CodeVersion = data[9]` — `clsRadioDiscovery.cs:1155`)
- **byte 10** `06` — board ID = `6` → maps to `HPSDRHW.HermesLite` (MI0BOT)
  (`r.DeviceType = mapP1DeviceType(data[10])` — `clsRadioDiscovery.cs:1153`;
  enum value at `enums.cs:396`: `HermesLite = 6`)
- **bytes 11–13** `00 00 00` — unknown — investigate before implementing
  (not read by the P1 parser branch)
- **byte 14** `00` — `MercuryVersion0` (`data[14]` — `clsRadioDiscovery.cs:1160`)
- **byte 15** `00` — `MercuryVersion1` (`data[15]` — `clsRadioDiscovery.cs:1161`)
- **byte 16** `00` — `MercuryVersion2` (`data[16]` — `clsRadioDiscovery.cs:1162`)
- **byte 17** `00` — `MercuryVersion3` (`data[17]` — `clsRadioDiscovery.cs:1163`)
- **byte 18** `00` — `PennyVersion` (`data[18]` — `clsRadioDiscovery.cs:1164`)
- **byte 19** `04` — `MetisVersion` = 4 (`data[19]` — `clsRadioDiscovery.cs:1165`)
- **byte 20** `45` — `NumRxs` = 69 (`data[20]` — `clsRadioDiscovery.cs:1166`);
  raw value 0x45 as reported by HL2 firmware — unknown — investigate before implementing
- **bytes 21–59** `02 00 00 ... 83 00 ...` — unknown — investigate before implementing
  (not read by the P1 parser branch; 39 bytes total)

**Thetis source:** `clsRadioDiscovery.cs:1301` (send — `buildDiscoveryPacketP1`), `:1122` (parse — `parseDiscoveryReply`)

## 3. Start / Stop Commands

P1 start/stop is a small 64-byte Metis frame sent to the radio. Byte 2 is the
command (`04`), byte 3 selects start/stop and which data streams to enable.
The frame is NOT a full 1032-byte Metis frame — it carries no C&C or I/Q
payload, just the 4-byte command header followed by 60 zero bytes.

### 3.1 Start Command (host → radio :1024)

Captured frame 10, relative timestamp 1.007829900 s (first confirmed start
after IQ stream begins flowing).

UDP payload (64 bytes, offsets shown relative to UDP payload start):

```text
Offset  Hex                                               ASCII
------  ------------------------------------------------  -----
00      ef fe 04 01 00 00 00 00 00 00 00 00 00 00 00 00  ................
10      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
20      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
30      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

Field legend:

- **bytes 0–1** `EF FE` — P1 sync / frame marker
- **byte 2** `04` — command: start/stop
- **byte 3** `01` — mode: start with IQ data stream enabled
  (`outpacket.packetbuf[3] = 0x01` — `networkproto1.c:50`)
- **bytes 4–63** `00 * 60` — zero padding

Note: frame 7 (t=0.944 s) contains byte 3 = `00` (a stop command sent before
the first start — consistent with `SendStartToMetis()` calling
`ForceCandCFrame(1)` then immediately sending start, with the radio possibly
echoing a cleanup stop first).

### 3.2 Stop Command (host → radio :1024)

Captured frame 302249, relative timestamp 56.658037300 s (end of session).

UDP payload (64 bytes):

```text
Offset  Hex                                               ASCII
------  ------------------------------------------------  -----
00      ef fe 04 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
10      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
20      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
30      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

Field legend:

- **bytes 0–1** `EF FE` — P1 sync / frame marker
- **byte 2** `04` — command: start/stop
- **byte 3** `00` — mode: stop all data streams
  (`outpacket.packetbuf[3] = 0x00` — `networkproto1.c:85`)
- **bytes 4–63** `00 * 60` — zero padding

### 3.3 Mode Byte (byte 3) Decode Table

Values taken directly from `networkproto1.c` (`SendStartToMetis` and
`SendStopToMetis`). No other values appear in this source file.

| Value | Meaning | Source |
| --- | --- | --- |
| `0x00` | Stop — all data streams off | `networkproto1.c:85` |
| `0x01` | Start — IQ data stream enabled | `networkproto1.c:50` |

The capture contains only `0x00` and `0x01`. The values `0x02` (wideband only)
and `0x03` (IQ + wideband) are referenced in the OpenHPSDR P1 specification
but do NOT appear in `SendStartToMetis()` or `SendStopToMetis()`. They are not
documented here as they cannot be verified against this source or capture.

**Thetis source:** `networkproto1.c:50` (start send — `SendStartToMetis`),
`networkproto1.c:85` (stop send — `SendStopToMetis`)

## 4. EP6 — Radio → Host Metis Frames

EP6 is the primary data stream from the radio. Each frame is 1032 bytes of
UDP payload containing an 8-byte Metis header and two 512-byte USB frames,
each carrying 5 C&C status bytes and 504 bytes of interleaved 24-bit BE I/Q
plus 16-bit mic audio. The C0 status index is round-robin: each USB frame
carries one slot of status, and the host reassembles the full snapshot across
frames. The decode switch on `ControlBytesIn[0] & 0xF8` means bits [7:3]
select the status slot while bits [2:0] carry PTT/dot/dash inputs.

### 4.1 Frame Layout

```
Offset  Length  Field
------  ------  -----
0       2       Metis sync 'EF FE'
2       1       Direction marker: 0x01 (data frame from radio)
3       1       Endpoint: 0x06 (EP6 — I/Q data to host)
4       4       Sequence number, big-endian uint32 (monotonically incrementing)
8       3       USB1 sync '7F 7F 7F'
11      1       USB1 C0 — status index (bits [7:3]) + PTT/dot/dash (bits [2:0])
12      4       USB1 C1..C4 — status payload (meaning depends on C0[7:3])
16      504     USB1 I/Q + mic samples (see §4.2)
520     3       USB2 sync '7F 7F 7F'
523     1       USB2 C0 — same encoding as USB1 C0 (carries next status slot)
524     4       USB2 C1..C4
528     504     USB2 I/Q + mic samples
```

Verification from hex dump (frame 11, first EP6 frame in capture):

```text
Offset  Hex                                        Field
------  -----------------------------------------  ----------------------------
00      EF FE                                      Metis sync
02      01                                         Direction marker (data frame)
03      06                                         Endpoint (EP6)
04      00 00 00 00                                Seq# = 0 (first frame)
08      7F 7F 7F                                   USB1 sync
0B      18                                         USB1 C0 = 0x18 (status slot 3)
0C      00 00 00 00                                USB1 C1-C4 (slot 3: user_adc1=0, supply_volts=0)
10      01 27 4F FF 70 B6 FF FE 65 FF F8 0C ...   USB1 I/Q samples begin
          (504 bytes total, continuing to offset 0x0207)
208     7F 7F 7F                                   USB2 sync
20B     00                                         USB2 C0 = 0x00 (status slot 0)
20C     1F 00 80 4A                                USB2 C1-C4 (slot 0: ADC overload flags + dig I/O)
210     ...                                        USB2 I/Q samples begin
```

Note: Metis byte 3 is `0x06`, confirming EP6 endpoint. The direction marker
(byte 2) is `0x01` for data frames in both directions; the endpoint byte
distinguishes EP6 (radio→host data) from EP2 (host→radio C&C).
Verified at `networkproto1.c:174–181` (`MetisReadDirect`).

### 4.2 Sample Format

Each USB frame carries 504 bytes of interleaved samples. For nddc=1 (one
receiver, as in this capture), samples per USB frame:

```
spr = 504 / (6 * nddc + 2) = 504 / 8 = 63 samples
```

Layout within each 8-byte sample group:

```
Byte 0-2   I-sample, 24-bit big-endian (sign-extended via << 8 shift)
Byte 3-5   Q-sample, 24-bit big-endian
Byte 6-7   Mic audio, 16-bit big-endian (one sample, decimated)
```

Thetis decodes I/Q as:
```c
// networkproto1.c:364-371 (MetisReadThreadMainLoop)
prn->RxBuff[iddc][2*isample+0] = const_1_div_2147483648_ *
    (double)(bptr[k+0] << 24 | bptr[k+1] << 16 | bptr[k+2] << 8);  // I
prn->RxBuff[iddc][2*isample+1] = const_1_div_2147483648_ *
    (double)(bptr[k+3] << 24 | bptr[k+4] << 16 | bptr[k+5] << 8);  // Q
```

Mic (16-bit):
```c
// networkproto1.c:401-403
prn->TxReadBufp[2*mic_sample_count+0] = const_1_div_2147483648_ *
    (double)(bptr[k+0] << 24 | bptr[k+1] << 16);  // 16-bit mic, upper bytes
```

**`networkproto1.c:358`** — `spr = 504 / (6 * nddc + 2)` — sample count formula.

### 4.3 Observed C0 Status Indices

The switch on `ControlBytesIn[0] & 0xF8` (networkproto1.c:332) defines 5 status
slots. The USB1 and USB2 C0 bytes in this capture are interleaved to carry
different slots per EP6 frame.

#### USB1 C0 counts (281,195 EP6 frames):

| C0 (hex) | Count   | C0[7:3] slot | Sample C1-C4    |
| -------- | ------- | ------------ | --------------- |
| `08`     | 140,597 | 1 (0x08)     | `03 F0 00 01`   |
| `18`     | 140,598 | 3 (0x18)     | `00 00 00 00`   |

#### USB2 C0 counts (281,195 EP6 frames):

| C0 (hex) | Count   | C0[7:3] slot | Sample C1-C4    |
| -------- | ------- | ------------ | --------------- |
| `00`     | 140,437 | 0 (0x00)     | `1F 00 80 4A`   |
| `10`     | 140,435 | 2 (0x10)     | `00 00 00 00`   |
| `FA`     | 323     | — (0xF8)     | `02 00 00 01`   |

C0=`FA` (`0xFA & 0xF8 = 0xF8`) falls outside all five cases in the
`networkproto1.c:332` switch. It appears throughout the session (~every 870
frames in USB2) with consistent payload `02 00 00 01`. This is an HL2-specific
status extension not decoded by Thetis's networkproto1.c switch —
**observed but unmapped — investigate** (likely HL2 firmware-version or
board-status slot; compare against HL2 FPGA source `radioberry.v` or
`hermeslite.v`).

#### C0 status slot decode table

| C0 & 0xF8 | Slot | C1-C4 Meaning | Source |
| ---------- | ---- | ------------- | ------ |
| `0x00`     | 0    | C1 bit0=ADC0 overload; C1 bits[4:1]=user digital inputs | networkproto1.c:334–336 |
| `0x08`     | 1    | C1-C2=exciter power (AIN5); C3-C4=fwd PA power (AIN1) | networkproto1.c:338–341 |
| `0x10`     | 2    | C1-C2=rev PA power (AIN2); C3-C4=user_adc0 (AIN3, PA volts) | networkproto1.c:343–346 |
| `0x18`     | 3    | C1-C2=user_adc1 (AIN4, PA amps); C3-C4=supply_volts (AIN6) | networkproto1.c:348–350 |
| `0x20`     | 4    | C1 bit0=ADC0 overload; C2 bit0=ADC1 overload; C3 bit0=ADC2 overload | networkproto1.c:352–355 |
| `0xF8`     | —    | **HL2-specific — not in networkproto1.c switch — investigate** | — |

**This capture exercises slots 0, 1, 2, 3 (C0=`00`, `08`, `10`, `18`) across
USB1+USB2. Slot 4 (C0=`20`) is NOT observed in this capture — the HL2
may not implement it, or the round-robin does not reach it in this
2-slot-per-frame pattern.**

#### Decoded sample values (first EP6 frame, frame 11):

- **USB1 C0=`18`, C1-C4=`00 00 00 00`**: user_adc1=0x0000 (AIN4 amps=0),
  supply_volts=0x0000 (AIN6=0) — radio just started, ADC rails not yet settling.
- **USB2 C0=`00`, C1-C4=`1F 00 80 4A`**: C1=`1F`=`0001 1111`→
  ADC0 overload=1 (bit0), user_dig_in=`0x0F` (bits[4:1]=`1111` — IO4-IO8 all
  high). C2-C4=`00 80 4A` are not used by slot 0.

**Thetis source:** `networkproto1.c:273–416` (`MetisReadThreadMainLoop` — RX
parser, C0 switch, I/Q decode, mic decode).
