# Phase 3O — Linux PipeWire Audio Bridge Verification Matrix

> **Status:** Structure drafted. Rows get filled in during shakedown
> on each host. The Ubuntu 25.10 row is the release-gating one;
> additional rows can be filled by other contributors as they
> exercise the bridge on their distros.

## How to use

1. On each target host, run the build and launch:
   ```
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
   cmake --build build -j$(nproc)
   ./build/NereusSDR
   ```
2. Capture the **detected backend** from the log (`Linux audio
   backend detected: …`).
3. Open **Setup → Audio** and confirm the AudioBackendStrip header
   matches the detected backend.
4. Walk the scenarios for the row.
5. For each filled scenario, record measured latency, xrun count
   after a 10-minute soak, and a screenshot of the telemetry strip
   (when applicable). Drop screenshots into this directory keyed
   by row letter (e.g. `A-vax-wsjtx.png`).
6. Reference this matrix from the release PR description.

---

## Distro coverage matrix

| # | Distro | PipeWire | pactl | Expected detection | Scenarios |
|---|--------|----------|-------|--------------------|-----------|
| A | Ubuntu 25.10 (bare metal) | ✓ 1.4.7 | ✗ | PipeWire | VAX → WSJT-X, sidetone on USB headphones, per-slice routing, reconnect on device replug |
| B | Ubuntu 22.04 LTS | 0.3.48 (under 0.3.50 floor) | ✓ | Pactl | VAX → WSJT-X, single speakers sink |
| C | Ubuntu 20.04 LTS (VM) | ✗ | ✓ | Pactl | VAX → WSJT-X, single speakers sink |
| D | Fedora 41 | ✓ | maybe ✓ | PipeWire | Full scenarios (A's set) |
| E | Debian 12 | ✓ 1.0.5 | ✗ | PipeWire | Full scenarios (A's set) |
| F | Arch current | ✓ | ✓ | PipeWire | Full scenarios (A's set) |
| G | Container, no audio | ✗ | ✗ | None + first-run dialog | Dismiss, Rescan after install, first-run persist flag |

**Release gate:** at least rows A, one Ubuntu LTS (B or C), and one
container (G) must be filled before merging `ubuntu-dev` to `main`.

---

## Per-row scenario detail

### Row A — Ubuntu 25.10 (PipeWire path, primary target)

| Scenario | Expected | Result | Notes |
|---|---|---|---|
| Backend detection | `PipeWire` (server version 1.4.7) | _pending_ | |
| `nereussdr.vax-1` visible to WSJT-X | Selectable as input source | _pending_ | |
| `nereussdr.vax-2/3/4` visible to fldigi / VARA / arbitrary capture | All four selectable | _pending_ | |
| VAX 1 → WSJT-X round-trip decode | Decodes a known FT8 capture | _pending_ | |
| Primary sink default routing | Speakers + master mix audible | _pending_ | |
| CW sidetone on USB headphones | Separate stream visible in `pw-cli`, audible in headphones only | _pending_ | |
| TX MON on third sink | Separate stream, audible | _pending_ | |
| Per-slice sinkNodeName routing | Slice 2 routed to alt sink, audio audibly splits | _pending_ | |
| Device replug recovery | Unplug + replug headphones; sidetone reroutes | _pending_ | |
| 10-min soak — latency | Measured (target ≤ 15 ms total RX → speakers) | _pending_ | |
| 10-min soak — xrun count | Target 0; record actual | _pending_ | |
| RT scheduling probe | Telemetry strip shows policy + priority | _pending_ | |
| First-run dialog (intentional) | Help → Diagnose audio backend opens it | _pending_ | |

### Row B — Ubuntu 22.04 LTS (Pactl fallback)

| Scenario | Expected | Result | Notes |
|---|---|---|---|
| Backend detection | `Pactl` (PipeWire 0.3.48 below 0.3.50 floor) | _pending_ | |
| `nereussdr-vax-1..4` (LinuxPipeBus) visible | Selectable in WSJT-X | _pending_ | |
| VAX 1 → WSJT-X round-trip | Decodes | _pending_ | |
| Single speakers sink (no PipeWire split) | Audible | _pending_ | |
| 10-min soak — latency | Measured | _pending_ | |
| 10-min soak — xrun count | Recorded | _pending_ | |

### Row C — Ubuntu 20.04 LTS VM (Pactl-only)

Same scenarios as Row B; PipeWire absent so detection should fall
straight to Pactl without a connect attempt log line.

### Row D — Fedora 41

Same scenarios as Row A. If Fedora ships pactl alongside PipeWire,
detection should still pick PipeWire (per the spec §4.1 ordering).

### Row E — Debian 12

Same scenarios as Row A. PipeWire 1.0.5 is above the 0.3.50 floor.

### Row F — Arch current

Same scenarios as Row A.

### Row G — Container without audio

| Scenario | Expected | Result | Notes |
|---|---|---|---|
| Backend detection | `None` | _pending_ | |
| First-run dialog auto-opens | Yes, on first launch | _pending_ | |
| Detection-state section | "PipeWire socket: not found at /run/user/N/pipewire-0" + "pactl binary: not in $PATH" | _pending_ | |
| Copy commands button | Three install lines on clipboard | _pending_ | |
| Dismiss | Sets `Audio/LinuxFirstRunSeen=True`; doesn't reopen on next launch | _pending_ | |
| Rescan after install | After `apt install pipewire pipewire-pulse`, re-enters PipeWire path without restart | _pending_ | |
| Help → Diagnose audio backend | Re-opens dialog with current state | _pending_ | |

---

## Reference

- Design spec: [`../2026-04-23-linux-audio-pipewire-design.md`](../2026-04-23-linux-audio-pipewire-design.md)
- Implementation plan: [`../2026-04-23-linux-audio-pipewire-plan.md`](../2026-04-23-linux-audio-pipewire-plan.md)
- Sister phase verification matrix (3G-8): [`../phase3g8-verification/README.md`](../phase3g8-verification/README.md)
