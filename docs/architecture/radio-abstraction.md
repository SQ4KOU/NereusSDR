# Radio Abstraction Layer

**Status:** Phase 2A — Design pending

## Overview

The radio abstraction layer handles:
- Radio discovery on the local network
- Connection management (Protocol 1 and Protocol 2)
- Multiple independent receivers per radio
- C&C (command and control) data exchange

## Design Goals

- Support Protocol 1 and Protocol 2 behind a unified interface
- Handle radio discovery, connection lifecycle, and reconnection
- Manage multiple independent receivers/slices per radio
- Expose a clean Qt6 signal/slot interface for UI binding
- Follow AetherSDR's pattern: radio is authoritative for hardware state

## Key Classes

- `RadioDiscovery` — UDP broadcast discovery on port 1024
- `RadioConnection` — Protocol-agnostic connection management
- `RadioModel` — Central state model

## Protocol Differences

| Aspect | Protocol 1 | Protocol 2 |
|--------|-----------|-----------|
| Transport | UDP only | TCP + UDP |
| Discovery | UDP broadcast | UDP broadcast/multicast |
| Data format | 1032-byte Metis frames | Structured packets |
| Control | C&C bytes in frame headers | TCP commands |
| Max receivers | 7 (multiplexed in EP6) | Hardware-limited |
