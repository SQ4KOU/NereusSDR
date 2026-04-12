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
