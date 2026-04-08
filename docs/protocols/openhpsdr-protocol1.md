# OpenHPSDR Protocol 1

## Status: Reference placeholder

## Overview

Protocol 1 is the original OpenHPSDR protocol used by Metis, Hermes, Angelia,
Orion, and Hermes Lite 2 boards. It uses UDP-only communication on port 1024.

## Key Characteristics

- **Transport:** UDP only, port 1024
- **Frame size:** 1032 bytes (Metis frame)
- **Frame structure:** 8-byte header + two 512-byte USB frames
- **I/Q format:** 24-bit big-endian, interleaved
- **Control:** C&C (Command & Control) bytes in USB frame headers
- **Discovery:** UDP broadcast to port 1024

## Frame Format

```
Bytes 0-1:   Sync (0xEF 0xFE)
Byte 2:      Endpoint (0x06 = EP6 data from radio)
Byte 3:      Sequence number
Bytes 4-7:   Sequence number (32-bit)
Bytes 8-519: USB Frame 1 (5 C&C bytes + 504 I/Q/audio bytes)
Bytes 520-1031: USB Frame 2 (5 C&C bytes + 504 I/Q/audio bytes)
```

## Official Specification

See: https://openhpsdr.org/wiki/index.php?title=Protocol_1
