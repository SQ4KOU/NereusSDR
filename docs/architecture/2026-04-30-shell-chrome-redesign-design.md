# Shell-Chrome Redesign — Design Spec

**Date:** 2026-04-30
**Branch:** `feature/phase3q-connection-workflow-refactor` (continuation)
**Author:** J.J. Boyd / KG4VCF, with AI-assisted drafting via Claude (brainstorming session).

## Goal

Replace today's TitleBar / status-bar surface with a coherent, redundancy-free
chrome that:

1. Says "what radio am I on?" once, in one place (STATION block, bottom center).
2. Says "is the link healthy?" once, in one place (TitleBar segment).
3. Says "what is the radio doing right now?" as a dense glance dashboard
   (status-bar middle), packed with state that is otherwise hidden behind
   tabs and dialogs.
4. Restyles the existing right-side strip in place (CAT · TCI · PSU · CPU ·
   PA · TX · UTC) without changing its footprint or moving items around.
5. Removes the operator-confusing "PA — V" label (which today actually shows
   PSU supply voltage on most radios); names voltages honestly.
6. Wires every visible element to real state and every clickable element to
   a real action — **no NYI placeholders, no dead-end clicks**.

## Non-goals

- Multi-RX layout in the strip — this spec covers RX1 only. Multi-RX (RX2
  on second slice) is a follow-up once Phase 3F lands.
- New TX surface — TX detail (power %, profile, PROC, VOX) stays in the
  existing TX applet. The strip carries one TX indicator (right-side, MOX
  state) and nothing else.
- New chrome on the Spectrum/Waterfall area itself — outside scope.
- Skin overlay system — that's Phase 3H. The colors here are the default
  palette only.
- Window-title (OS chrome) — left as-is. The OS already says "NereusSDR
  0.2.3" and that's the brand mark.

## Hard constraints

1. **No NYI / no dead ends to userland.** Every visible element wires to
   real state. Every clickable element does something useful. Tooltips
   never say "TBD". Where the backing subsystem doesn't exist yet (CAT
   server / TCI server are Phase 3K), the corresponding badge **does not
   render** — the right-side strip simply re-flows to fit. When 3K lands,
   the badges appear naturally.
2. **Layout never shifts when state toggles.** Section boundaries (1px
   `#1a2030` separators) are fixed. Disabled badges don't render, so the
   middle section grows/shrinks, but section boundaries stay put. STATION
   is centered between two flex spacers and never moves left/right.
3. **One source of truth per fact.** Radio name lives only in STATION.
   Connection state lives only in the segment dot. Firmware lives only in
   the board+fw widget (and in the diagnostics tooltip). No region duplicates
   another region's data.
4. **Source-first for telemetry.** Voltage labels reflect what the radio
   actually reports (`supply_volts` → "PSU"; `user_adc0` → "PA" — the
   latter only on ORIONMKII / ANAN-8000D / ANAN-7000DLE per
   `RadioConnection.h:405-407`).

## Region-by-region spec

### TitleBar (top strip, height unchanged at 32 px)

```
[≡menu]  [segment]  ──stretch──  NereusSDR  ──stretch──  [Master Out gauge]  [💡]
```

| Element | Source / Behavior |
| --- | --- |
| Menu bar (`≡ Radio · View · Setup · Help …`) | unchanged; current `m_menuBar` |
| **ConnectionSegment** (rebuilt, see below) | new layout per §ConnectionSegment |
| `NereusSDR` brand label | kept; `kAppNameStyle` (cyan #00b4d8, 14 px bold) |
| MasterOutputWidget | unchanged |
| 💡 feature-request button | unchanged |

### ConnectionSegment (top strip, position 1)

Replaces today's segment that hard-codes radio name + IP + ▲0/▼0.
New form (connected):

```
[●] ▲ 1.2  12 ms  ▼ 11.4  |  ♪
```

| Element | Color/Style | Source | Affordances |
| --- | --- | --- | --- |
| State+activity **dot** (single dot, replaces today's two) | see §State dot palette | `RadioModel::connectionState()` + activity gating from `RadioConnection::frameReceived` (10 Hz throttle) | hover=tooltip; right-click=segment menu |
| `▲ X.X` (TX Mbps) | `#a0d8a0` (or red on TX) | new `RadioConnection::txByteRate(int ms)` (1 s rolling) | inherits |
| `12 ms` RTT | green/yellow/red by threshold (see §RTT thresholds) | new `RadioConnection::pingRttMeasured(int)` signal | **left-click → opens NetworkDiagnosticsDialog** |
| `▼ X.X` (RX Mbps) | `#a0d8a0` | new `RadioConnection::rxByteRate(int ms)` (1 s rolling) | inherits |
| Vertical separator `\|` | `#304050` | static | — |
| `♪` audio engine pip | `#5fa8ff` healthy / `#ffd700` underrun / `#ff6060` dead | `AudioEngine::flowStateChanged(state)` (new signal) | left-click → Audio settings tab; right-click → backend menu |

Disconnected form:

```
[●]  Disconnected — click to connect
```

#### State dot palette

| State | Color | Animation | Trigger |
| --- | --- | --- | --- |
| Streaming (connected, packets in last 200 ms) | `#5fff8a` | pulse 1.5 s | `RadioConnection::frameReceived()` within window |
| Connected, stale (no packets ≥ 200 ms but state=Connected) | `#ffd700` | none (solid) | timer-based |
| Reconnecting / link lost (transient) | `#ff8c00` | pulse 1.5 s | `ConnectionState::LinkLost` |
| Disconnected | `#ff4040` | none (solid) | `ConnectionState::Disconnected` |
| Probing (during connect attempt) | `#5fa8ff` ring | pulse 0.8 s | `ConnectionState::Probing` |
| TX engaged (overrides streaming) | `#ff4040` | pulse 0.4 s | `MoxController::moxStateChanged(true)` |

#### RTT thresholds

| RTT | Color |
| --- | --- |
| < 50 ms | green `#5fff8a` |
| 50–149 ms | yellow `#ffd700` |
| ≥ 150 ms | red `#ff6060` |
| no measurement yet | gray `#607080`, text "— ms" |

#### Tooltip (segment hover)

```
ANAN-G2 (Saturn) — Connected · 14m32s
  192.168.109.45 · 2C:CF:67:AB:FC:F4
  Protocol 2 · Firmware v27 · 192 kHz
  Network: 12 ms RTT (max 87 ms) · 0.0% loss · 2 ms jitter
  Throughput: ▲ 1.2 Mbps · ▼ 11.4 Mbps
  Audio: PipeWire @ 48 kHz · 0 underruns · 1.2 ms gap

Right-click for actions · Click STATION to switch radios
```

Tooltip refreshes on hover-enter; no live updates while shown.

#### Right-click context menu

| Item | Action |
| --- | --- |
| Disconnect | `RadioModel::disconnectFromRadio()` |
| Reconnect | disconnect + auto-reconnect to `lastConnectedMac` |
| Connect to other radio… | open `ConnectionPanel` |
| Network diagnostics… | open `NetworkDiagnosticsDialog` |
| Copy IP address | clipboard |
| Copy MAC address | clipboard |

### Status bar — middle section (RX dashboard)

Replaces today's `m_statusConnInfo` (sample rate · proto · fw · MAC) with a
dense, RX-focused glance dashboard.

```
[▣ ☰ TNF CWX DVK FDX] | [Saturn / v27] | [RX1 / 14.150.000] | [USB][2.4k][S][NR2][NB][ANF][SQL]
```

| Section | Always shown | Source |
| --- | --- | --- |
| Toggle row (▣ ☰ TNF CWX DVK FDX) | yes | unchanged from today |
| Board + firmware widget (stacked: top=board, bottom=v + firmware) | only when connected | `RadioInfo::boardType` + `firmwareVersion` |
| RX1 + frequency block (small "RX1" label above big freq) | only when connected | `SliceModel(0)::frequency()` |
| Mode badge | yes (when connected) | `SliceModel(0)::mode()` |
| Filter width badge | yes (when connected) | `SliceModel(0)::filterWidth()` |
| AGC mode badge (S/M/F/L/Custom) | yes (when connected) | `SliceModel(0)::agcMode()` |
| NR badge | only when active | `SliceModel(0)::nrMode()` ≠ Off |
| NB badge | only when active | `SliceModel(0)::nbActive()` |
| ANF badge | only when active | `SliceModel(0)::anfActive()` |
| SQL badge | only when squelch open / active | `SliceModel(0)::squelchActive()` |

#### Badge rendering

- Always-shown badges (mode/filter/AGC) hold their position.
- Active-only badges (NR/NB/ANF/SQL) render in fixed order: NR → NB → ANF → SQL.
- All badges share the same dimensions (height 18 px, 6 px horizontal padding).
- Color rules:
  - **`b-info`** (blue, `rgba(95,168,255,*)`) — type/mode badges (USB, AGC-S)
  - **`b-on`** (green, `rgba(95,255,138,*)`) — active-on toggles (NR2, NB, ANF, SQL, filter when narrow)
  - **`b-warn`** (yellow, `rgba(255,215,0,*)`) — degraded states
  - **`b-tx`** (red, `rgba(255,96,96,*)`) — TX-related (used only on right-side TX badge)
- Each badge has an icon prefix (see §Icon legend) — text is supporting.

#### Icon legend

| Icon | Feature | Notes |
| --- | --- | --- |
| `~` | Mode (USB / LSB / CW / AM / FM / DIGU / DIGL / DSB / SAM / SPEC / DRM) | Always shown when connected |
| `⨎` | Filter width (e.g., 2.4k, 500, 100) | Always shown when connected |
| `⚡` | AGC mode (S / M / F / L / Custom) | Always shown when connected |
| `⌁` | Noise reduction (NR1/2/3/4/EMNR/RNNR/SBNR/DFNR/MNR — text shows active mode) | Active only |
| `∼` | Noise blanker (NB / NB2) | Active only |
| `⊘` | Auto-notch filter (ANF) | Active only |
| `▼` | Squelch open | Active only |
| `⌨` | CAT (Computer Aided Tuning) | Active only — does NOT render until Phase 3K lands |
| `⊞` | TCI (Transceiver Control Interface) | Active only — does NOT render until Phase 3K lands |
| `✓` | PA OK | Active only |
| `●` | TX (MOX) | Solid red when TX engaged |

Icons are not localizable; tooltips on each badge spell the meaning in plain
English ("Noise reduction NR2 active").

### STATION block (bottom center)

Bordered cyan box anchored by two flex spacers. Centerlines remain stable
regardless of RX/TX activity.

```
[ANAN-G2 (Saturn)]
```

| Source | `RadioInfo::name` (from saved-radio entry, falls back to discovery name) |
| --- | --- |
| When disconnected | dashed-red border, italic gray text "Click to connect" |
| Left click | open `ConnectionPanel` (always — even when connected, so user can switch radios) |
| Right click | menu: **Disconnect** / **Edit radio…** / **Forget radio** |
| Hover tooltip | same metadata bundle as segment tooltip (see §ConnectionSegment) |

### Right-side strip (existing, restyled in place)

Footprint stays the same as today. Only the visual treatment changes — items
become badges or labelled metrics in the hybrid styling.

```
[CAT][TCI] | PSU 13.8V · CPU 19% | [PA OK][TX] | 16:03 UTC
```

| Element | Source | Behavior |
| --- | --- | --- |
| `CAT` badge | `CatServer::clientConnected()` (Phase 3K) | green when client connected; **does not render until 3K lands** |
| `TCI` badge | `TciServer::isRunning()` (Phase 3K) | green when server running; **does not render until 3K lands** |
| `PSU 13.8V` metric | `RadioConnection::supplyVoltsChanged(float)` (existing) | label dynamic: "PSU" by default; "PA" only when `user_adc0` is the available signal (ORIONMKII / 8000D / 7000DLE) |
| `CPU 19%` metric | system stats poll | green < 60%, yellow 60–89%, red ≥ 90% |
| `PA OK` badge | Mercury PA fault status (existing) | hidden when no PA fitted |
| `TX` badge | `MoxController::isTx()` | **solid red** (no flash) when keyed; dim/off color when not |
| `16:03 UTC` | system clock, formatted | always shown |

#### Voltage label resolution

Per `RadioConnection.h:405-407` ([@501e3f5]):

- All P1/P2 radios: `supply_volts` from AIN6 → label = **"PSU"**
- ORIONMKII / ANAN-8000D / ANAN-7000DLE: also have `user_adc0` from AIN3
  reporting PA drain → label = **"PA"** (overrides PSU)
- Other radios: PA voltage not reported; only PSU shown

When both are available the diagnostic dialog (§Network Diagnostics) shows
both rows.

### Network Diagnostics dialog (new — opened by left-click "12 ms")

Modal `QDialog` parented to MainWindow, refreshes every 250 ms. Inspired by
AetherSDR's `NetworkDiagnosticsDialog` (`src/gui/NetworkDiagnosticsDialog.cpp`
@ `v0.8.19-10-g0cd4559`) but reimplemented for our model surface.

Sections (4-column grid):

1. **CONNECTION** — Status, Uptime, Radio, Protocol, IP, MAC
2. **NETWORK** — Latency (RTT), Max RTT, Jitter, Packet loss, ▲ TX rate, ▼ RX rate, Sample rate, UDP seen
3. **AUDIO ENGINE** — Backend, Buffer (current / target), Underruns, Packet gap (current / max)
4. **RADIO TELEMETRY** — PSU voltage, PA voltage (— when not reported), ADC overload state

Buttons: `[Reset session stats]` `[Close]`.

Reset wipes max-RTT, packet-loss counter, audio underrun counter (does not
disconnect or reconfigure).

## Affordances summary

| Element | Hover (tooltip) | Left click | Right click |
| --- | --- | --- | --- |
| Segment dot · ▲▼ | Diagnostic bundle | — | Disconnect / Reconnect / Connect to other / Diagnostics / Copy IP / Copy MAC |
| **`12 ms` RTT** | RTT max + jitter + loss | **Open NetworkDiagnosticsDialog** | inherits |
| `♪` audio pip | Backend, sample rate, underrun count | Open Audio settings tab | Audio backend menu |
| **STATION block** | Full metadata (name, IP, MAC, proto, fw, sample rate, RTT, uptime) | **Open ConnectionPanel (always — even when connected)** | Disconnect / Edit radio… / Forget radio |
| RX badges (mode/filter/AGC) | Feature name + value | Toggle / cycle (where applicable, e.g. NR cycles modes) | Configure (opens setup page for that feature) |
| RX badges (NR/NB/ANF/SQL) | Feature name + level | Toggle on/off | Configure |
| CAT / TCI badges | Connection state | Open CAT / TCI settings | Toggle on/off (3K) |
| PSU / PA / CPU metrics | Current value + (where useful) running max | — | — |
| PA OK badge | Mercury PA status text | — | — |
| TX badge | "TX engaged via {Mic / VOX / CAT / TCI / Manual}" | — | — |
| UTC clock | Local time + zone | Open Setup → Time | — |

## Layout-stability rules

1. Section boundaries are 1px `#1a2030` separators. Sections never overprint.
2. Disabled badges don't render at all (no dimmed placeholders).
3. Always-shown items hold their position: mode → filter → AGC. New active-
   only badges (NR/NB/ANF/SQL) render in fixed order to the right of AGC.
4. STATION sits between two `flex:1` spacers. Activity in the middle or
   right sections never moves it.
5. **Tight-space drop priority** (when window is narrow):
   - Right-of-RX badges drop in order: SQL → ANF → NB → NR → AGC
   - The filter-width badge **never drops** — it's the most operationally
     important indicator
   - Right-side strip drops in order: PA OK → CAT/TCI → PSU/PA → CPU → time
   - Tight-space ellipsis indicator: a small `…` chip that, when hovered,
     reveals the dropped badges in a popover
6. Dropped-badge popover: triggered when ≥ 1 badge has been hidden for layout
   reasons. Tooltip lists what was dropped + their current state.

## State transitions

The chrome must visibly track the underlying `ConnectionState` finite-state
machine (`RadioModel::connectionState()`):

| `ConnectionState` | Segment dot | Tooltip body | STATION text | Right-side TX badge |
| --- | --- | --- | --- | --- |
| `Disconnected` | red, solid | "Disconnected. Click STATION to connect." | dashed border, "Click to connect" | not rendered |
| `Probing` | blue, pulsing | "Probing {ip}…" | bordered, "Probing…" | not rendered |
| `Connecting` | blue, pulsing fast | "Connecting to {name}…" | bordered, "{name}" italic | not rendered |
| `Connected` (no recent traffic) | yellow, solid | full diagnostic | bordered, name (solid) | rendered, dim |
| `Connected` (streaming) | green, pulsing | full diagnostic | bordered, name (solid) | rendered, dim |
| `Connected` (TX) | red, pulsing | full diagnostic + TX flag | bordered, name | **solid red** |
| `LinkLost` | orange, pulsing | "Link lost — reconnecting…" | bordered, italic name | not rendered |

## Wiring inventory — every visible element → data source + click target

This table is the litmus test for the "no NYI / no dead ends" constraint. If
an element doesn't have both a data source and (where clickable) an action,
it doesn't ship.

| Element | Data source | Status | Click target | Status |
| --- | --- | --- | --- | --- |
| State dot color/animation | `RadioModel::connectionStateChanged` + `MoxController::moxStateChanged` + 200 ms staleness timer | **EXISTS** | (right-click only) | menu wired below |
| `▲ Mbps` | `RadioConnection::txByteRate(1000)` (NEW) | **NEW** — must add rolling counter | none | — |
| `12 ms` RTT | `RadioConnection::pingRttMeasured(int)` (NEW, inspired by `WanConnection::pingRttMeasured`) | **NEW** — adds periodic ping or piggybacks on C&C round-trip | open NetworkDiagnosticsDialog | **NEW** dialog class |
| `▼ Mbps` | `RadioConnection::rxByteRate(1000)` (NEW) | **NEW** — must add rolling counter | none | — |
| `♪` audio pip | `AudioEngine::flowStateChanged(state)` (NEW signal) | **NEW** — minor refactor | open Audio settings | EXISTS — Setup → Audio page |
| Saturn (board label) | `RadioInfo::boardType` → `boardName()` helper | **EXISTS** | none | — |
| v27 (firmware) | `RadioInfo::firmwareVersion` | **EXISTS** | none | — |
| RX1 label | `SliceModel::index()` (always 0 for RX1 today) | **EXISTS** | none | — |
| 14.150.000 (frequency) | `SliceModel::frequencyHz()` | **EXISTS** | open VFO/SliceModel inline editor (existing VfoWidget) | EXISTS |
| Mode badge | `SliceModel::mode()` | **EXISTS** | cycle mode | EXISTS via `SliceModel::setMode` |
| Filter badge | `SliceModel::filterWidthHz()` | **EXISTS** | cycle filter | EXISTS |
| AGC badge | `SliceModel::agcMode()` | **EXISTS** | cycle AGC | EXISTS |
| NR badge | `SliceModel::nrMode()` | **EXISTS** (Phase 3G-RX-Epic Sub-C) | cycle NR mode / right-click → DspParamPopup | EXISTS |
| NB badge | `SliceModel::nbActive()` | **EXISTS** | toggle | EXISTS |
| ANF badge | `SliceModel::anfActive()` | **EXISTS** | toggle | EXISTS |
| SQL badge | `SliceModel::squelchOpen()` | **EXISTS** | toggle squelch | EXISTS |
| STATION text | `RadioInfo::name` | **EXISTS** | open ConnectionPanel | **EXISTS** |
| STATION right-click → Disconnect | `RadioModel::disconnectFromRadio()` | **EXISTS** | — | — |
| STATION right-click → Edit | `ConnectionPanel::onEditClicked` w/ pre-selected MAC | **EXISTS** | — | — |
| STATION right-click → Forget | `AppSettings::forgetRadio` | **EXISTS** | — | — |
| CAT badge | `CatServer::clientConnected()` | **NOT YET (Phase 3K)** — badge omitted from layout | — | — |
| TCI badge | `TciServer::isRunning()` | **NOT YET (Phase 3K)** — badge omitted from layout | — | — |
| PSU 13.8V metric | **NEW** `RadioConnection::supplyVoltsChanged(float)` signal — raw bytes already parsed in P1/P2 (`P1RadioConnection.cpp:2180`, `P2RadioConnection.cpp:2111`); current code is `Q_UNUSED(supplyRaw)` per `RadioModel.cpp:2212` ("supply_volts surfaced via computeHermesDCVoltage in a later step"). This spec finishes that surfacing. | **NEW** wiring | none | — |
| PA voltage (when ORIONMKII/8000D/7000DLE) | **NEW** `RadioConnection::userAdc0Changed(float)` signal + `BoardCapabilities::reportsPaVoltage()` gating; data path exists per `RadioConnection.h:405` (`user_adc0` → P1 AIN3 / P2 bytes 53-54), conversion at `RadioModel.cpp:397-399` (`computeMKIIPaVoltage`). | **NEW** wiring | none | — |
| CPU% metric | Existing `getrusage()` polling timer (1.5 s) at `MainWindow.cpp:2690-2723` updates the existing CPU label. Reuse the timer; just expose the value to the new `StatusBadge` widget. | **EXISTS** (reuse existing poll) | none | — |
| PA OK badge | Existing `m_paStatusBadge` at `MainWindow.cpp:2630-2638`; `setPaTripped()` updater at `MainWindow.cpp:2739-2742`. | **EXISTS** (rewrap in new `StatusBadge` style; data path unchanged) | none | — |
| TX badge | `MoxController::moxStateChanged(bool)` | **EXISTS** | none | — |
| UTC clock | `QDateTime::currentDateTimeUtc()` polled at 1 Hz | **TRIVIAL** | open Setup → Time | EXISTS |

## New code required

| File | Purpose | Approx LOC |
| --- | --- | --- |
| `src/gui/NetworkDiagnosticsDialog.{h,cpp}` | Modal dialog port (4-section grid: Connection, Network, Audio, Radio Telemetry) | ~400 LOC |
| `src/core/RadioConnection.{h,cpp}` (additions) | `txByteRate()` / `rxByteRate()` rolling counters; `pingRttMeasured(int)` signal; periodic ping issuance; `supplyVoltsChanged(float)` + `userAdc0Changed(float)` signals (currently `Q_UNUSED`) | ~250 LOC |
| `src/core/AudioEngine.{h,cpp}` (additions) | `flowStateChanged(FlowState)` signal: `FlowState ∈ { Healthy, Underrun, Stalled, Dead }` derived from `QAudioSink::stateChanged()` + buffer-underrun watchdog | ~80 LOC |
| `src/gui/TitleBar.{h,cpp}` (rewrite of ConnectionSegment) | Single dot, RTT clickable, audio pip, restructured paint logic | ~300 LOC |
| `src/gui/MainWindow.cpp` (status-bar rebuild) | Drop `m_statusConnInfo` strip; new dense dashboard layout; restyled right-side strip | ~500 LOC delta |
| `src/gui/widgets/StatusBadge.{h,cpp}` (NEW shared widget) | Reusable badge with icon-prefix + on/off/info/warn/tx variants | ~150 LOC |
| `src/gui/widgets/StationBlock.{h,cpp}` (NEW) | Bordered cyan box, click-to-Connect, right-click menu, tooltip aggregation | ~180 LOC |
| Tests | Unit tests for rate counters, RTT signal, badge state mapping, STATION click routing | ~400 LOC across 4-5 test exes |

Total estimate: **~2,250 lines of code** across implementation + tests.

## Test scenarios (manual verification matrix)

The implementation plan will turn these into explicit checklist items.

1. Cold launch (no saved-radio): segment red+solid, STATION dashed-red
   "Click to connect", right-side strip shows only CPU + UTC.
2. Click STATION → ConnectionPanel opens. Probe + connect. Segment becomes
   blue-pulsing → green-pulsing. STATION fills with radio name. Right-side
   strip grows: PSU + CPU + (PA OK if fitted) + TX (dim) + UTC.
3. Send key (MOX): segment dot turns red-pulsing-fast, ▲ turns red,
   right-side TX badge **solid red**. RX1/freq/mode unchanged.
4. Toggle NR off → NR badge disappears; AGC badge stays put. No layout shift.
5. Disable spectrum, narrow window: SQL badge drops first, then ANF, etc.
   `…` overflow chip appears.
6. Hover the segment dot: tooltip body shows full diagnostic. Click the
   `12 ms` text: NetworkDiagnosticsDialog opens.
7. Right-click STATION: menu offers Disconnect / Edit / Forget. Each
   item lands on the right code path with no NYI message.
8. Connect to ORIONMKII (or 8000D simulator): voltage label flips from
   "PSU" to "PA"; diagnostic dialog shows both PSU and PA rows.
9. Disconnect: STATION returns to dashed "Click to connect"; right-side
   collapses; segment dot turns solid red.
10. Force LinkLost (kill radio mid-stream): dot turns orange-pulsing,
    tooltip says "Link lost — reconnecting…", right-side TX hides.

## Open questions / future

- **Multi-RX**: when RX2 active (Phase 3F), the strip shows two rows or
  two columns? Out of scope for this spec.
- **Skin overlay** (Phase 3H) will need to override the badge palette and
  metric label colors. The hard-coded values here are the default skin.
- **Spotter / DXCluster integration** (Phase 3J) may want a small spot-feed
  indicator in the strip — TBD when 3J is designed.
- **VPN / remote-network indicator** (suggested in brainstorm but not
  locked) — deferred. If a 🔒 icon is wanted next to the segment dot, it
  becomes a follow-up.

## Implementation phasing

Recommended sub-PR slicing for incremental review (the writing-plans skill
will turn this into a concrete task list):

1. **Sub-PR-1**: `StatusBadge` + `StationBlock` widgets (no integration yet,
   just the reusable building blocks + tests).
2. **Sub-PR-2**: `RadioConnection` rate counters + `pingRttMeasured` signal +
   tests. No UI consumption yet.
3. **Sub-PR-3**: `NetworkDiagnosticsDialog` (consumes signals from PR-2).
4. **Sub-PR-4**: TitleBar `ConnectionSegment` rebuild — single dot, RTT
   clickable, audio pip.
5. **Sub-PR-5**: Status-bar middle dashboard (RX dashboard with badges).
6. **Sub-PR-6**: Right-side strip restyle in place (CAT/TCI omitted until 3K).
7. **Sub-PR-7**: STATION block migration (from old `m_callsignLabel` to
   new `StationBlock`).
8. **Sub-PR-8**: Layout-stability rules + drop-priority + overflow chip.
9. **Sub-PR-9**: Manual verification + screen-capture matrix.

Each sub-PR is independently shippable behind feature flag if needed; final
combined commit gates the layout switch on a single setting (defaults on).

## Approval

This spec needs one round of human review before transitioning to
`writing-plans`. Open the file, verify nothing is misrepresented, then
either approve or list changes.
