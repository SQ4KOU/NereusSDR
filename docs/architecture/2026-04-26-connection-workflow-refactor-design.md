# Connection workflow refactor — design

Date: 2026-04-26
Author: J.J. Boyd / Claude (brainstorm pair)
Phase: TBD (post v0.2.3 polish, sequenced before 3M-1 SSB TX)
Source-first stamps for cross-references: Thetis `[v2.10.3.13]` (commit `501e3f51`), AetherSDR TitleBar pattern (port already in tree), mi0bot-Thetis discovery profiles (already in tree).

---

## 1. Why we are doing this

A user (Miguel Martínez, public NereusSDR feedback, April 2026) reported: with NereusSDR running on a Mac mini in his city house, he could not connect to a Hermes Lite 2 at his beach house over a WireGuard tunnel even though the same setup works in Thetis. The HL2's LAN IP (192.168.8.3) is reachable across the tunnel, but:

- Discovery returns nothing (expected — WireGuard does not carry layer-2 broadcast).
- Manual entry of the IP "doesn't connect — nothing happens." The current Add Manually flow accepts the IP, the row appears, clicking Connect dives into a silent UDP timeout.
- Saved entries "clear out of the list" between sessions (or after the 15-second stale sweep).

That public report sits on top of issues already accumulating in our own code:

- The Radio menu has four items — `Discover…`, `Connect`, `Disconnect`, `Radio Setup…` — three of which are aliases for "open the Connection panel." Only `Disconnect` actually does its own action ([src/gui/MainWindow.cpp:1197-1249](../../src/gui/MainWindow.cpp)).
- On disconnect, the spectrum and waterfall just freeze. There is no explicit "you are now disconnected" indication. `clearWaterfallHistory()` is called ([src/gui/MainWindow.cpp:741-746](../../src/gui/MainWindow.cpp)) but the user sees only "data stopped."
- Discovery is event-driven and **broadcast-only**. There is no unicast probe. A manual IP is never validated before the connection is attempted.
- The stale timeout (`kStaleTimeoutMs = 15000`, [src/core/RadioDiscovery.h:208](../../src/core/RadioDiscovery.h)) ages out any radio not heard from on broadcast — the *currently connected* MAC is exempt, but everyone else gets dropped from `m_radios`/`m_lastSeen` after 15 s.
- The `AddCustomRadioDialog` board picker presents 9 silicon families ([src/gui/AddCustomRadioDialog.cpp:294-302](../../src/gui/AddCustomRadioDialog.cpp)). The user-facing models number 16 ([displayName(HPSDRModel) at src/core/HpsdrModel.h:160-182](../../src/core/HpsdrModel.h)) and crucially many models share silicon — five products on Orion MkII alone. The current dialog asks for the wrong concept (silicon, not product).
- "Add Manually…" and "Connect to IP" should not coexist as separate surfaces (they did briefly in early mockup rounds — they're the same thing said two ways).

The connection lifecycle has been growing scar tissue across multiple phases. This design takes one pass at making it coherent.

## 2. Design principles

These guide every decision below; cite them when reviewing:

1. **State visibility** — at any moment the user can answer: am I connected? to what radio? at what IP? since when?
2. **One connection surface** — collapse menu aliasing and parallel "manual IP / Add Manually" entry surfaces into a single coherent set.
3. **Disconnect must announce itself** — frozen spectrum is not feedback. Multi-cue: TitleBar dot, spectrum overlay, status-bar glyph, panel auto-open.
4. **Errors are visible** — silent UDP timeouts get surfaced in plain English with the likely causes ("radio off, IP wrong, or VPN tunnel down").
5. **Recognition over recall** — saved radios stay in the panel across sessions and never age out. The user picks from a list, not from memory.
6. **Progressive disclosure** — broadcast scan is one click for "what's on the LAN?" The dialog is the place for "I have an IP and a SKU, save this for me."

## 3. Connection lifecycle (state machine)

One state machine, two triggers (broadcast scan + unicast probe). Both feed the same Probing → Connecting → Connected path:

```
                          ┌───────────────┐
                          │ Disconnected  │
                          └───────┬───────┘
                                  │ user picks radio / types IP
                                  ▼
                          ┌───────────────┐
                          │   Probing     │   (unicast or broadcast)
                          └───┬───────┬───┘
                  probe ok ───┘       └─── probe fails
                              │           │
                              ▼           ▼
                      ┌───────────────┐  ┌──────────────────────┐
                      │  Connecting   │  │ Inline failure form  │
                      └───────┬───────┘  │ (Save offline · Retry │
                              │          │  · Cancel)            │
                first ep6 frame received │                       │
                              ▼          └────────┬─────────────┘
                      ┌───────────────┐           │
                      │   Connected   │           ▼
                      └───┬───────┬───┘    ┌───────────────┐
       user disconnects ──┘       │        │ Disconnected  │
                                  │        │  (saved row   │
                no frames > 5 s ──┤        │  red pill)    │
                                  ▼        └───────────────┘
                          ┌───────────────┐
                          │   Link lost   │  (drawer reopens; Reconnect on row)
                          └───────────────┘
```

State definitions:

| State | TitleBar dot | Notes |
|---|---|---|
| Disconnected | grey | "Disconnected — click to connect"; spectrum overlay; ConnectionPanel auto-opens on launch and after disconnect |
| Probing | amber pulse (slow) | "Probing 192.168.8.3…"; ~1.5 s budget |
| Connecting | amber pulse (fast) | "Connecting to ANAN-G2…"; metis-start frames sent, waiting for first ep6 |
| Connected | green | radio name · IP · ▲ Mbps ▼ Mbps · activity LED on each frame |
| Link lost | red pulse | "Link lost — last frame Xs ago"; drawer reopens with Reconnect button |

## 4. UI components

### 4.1 TitleBar connection indicator

Lives in the existing 32 px [TitleBar](../../src/gui/TitleBar.h) widget, between the menu bar and the right-edge MasterOutput / 💡. Dropped in as a new segment (the file header already notes connection-state UI was deliberately deferred — this is the now-fill).

Disconnected state:
```
[menu]  ⚪ Disconnected — click to connect          🔊 [master]  💡
```

Probing state:
```
[menu]  🟡 Probing 192.168.8.3…                    🔊 [master]  💡
```

Connected state:
```
[menu]  🟢 ANAN-G2  192.168.1.42  ▲ 1.4 Mb/s  ▼ 2 kb/s  ●flash  🔊 [master]  💡
```

Click target = the whole connection segment (state dot through rates). On click → opens the ConnectionPanel. Right-click → context menu with Reconnect / Disconnect / Manage Radios… as appropriate to state.

Activity LED is a small green dot to the right of the rate readout that flashes on each incoming ep6 frame (P1) or DDC frame (P2). Implementation: a `frameReceived()` signal off `RadioConnection` driving a 10 Hz refresh on the LED — never more than 10 visible flashes per second regardless of actual frame rate, so it pulses cleanly instead of seizing.

Tooltip (on hover): packets/sec · drops · sample rate · firmware · MAC · command latency.

### 4.2 Status bar verbose strip

The existing 46 px [status bar](../../src/gui/MainWindow.cpp#L1939) (`buildStatusBar()`) already has the height. Add a persistent verbose-mode strip that mirrors the TitleBar with more detail:

Connected:
```
RX1 · 14.205 MHz · USB · 2400 Hz · Sample 384 kHz · P2 · fw 75 · MAC 00:1C:… · 1492 pkt/s · drops 0          ● live
```

Disconnected:
```
🔴  No radio connected — click the connection indicator or the Connect menu item     last connected ANAN-G2 · 14:23 today
```

The "last connected" breadcrumb on the right makes the most-recent connection one-glance recoverable even when the panel is closed.

### 4.3 ConnectionPanel

Existing [`QDialog`](../../src/gui/ConnectionPanel.h) layout is preserved — table + detail panel + button strip. Changes:

- **Stays modal** (`Qt::WindowModality::ApplicationModal`, today's behavior preserved). The auto-open-on-launch and auto-open-on-disconnect cases are exactly when modal pays off — there's nothing useful to do without a connection, and modality forces the user to engage instead of leaving an easily-missed floating widget in the corner. See §7.6.
- **Add a connection-status strip at the top** — replaces the bare `m_statusLabel`. When connected: state pill + radio name + IP + "since 14:23 (00:42:18)" + protocol/firmware/sample rate + RX/TX rates + Disconnect button on the right. When disconnected: state pill + "Disconnected" + "Reconnect to last radio" link if a last-connected MAC is known.
- **State pills** (in the existing `●` column) replace the bare glyph. Three states with thresholds:
  - 🟢 **Online** — last seen <60 s ago (broadcast or probe succeeded recently).
  - 🟡 **Stale** — last seen 60 s to 5 min ago. Probably still online; info aging.
  - 🔴 **Offline** — last seen >5 min ago, or last probe failed, or never seen this session (e.g. a freshly-saved entry).
- **Replace MAC column with "Last seen"** showing relative time ("just now", "4 m ago", "2 h ago", "never seen", or "connected · since 14:23"). MAC stays in the detail panel where it's actually consulted.
- **Drop `Start Discovery` / `Stop Discovery` buttons** — replaced by a single `↻ Scan` at the top-right of the table header (where it belongs).
- **Drop `Disconnect` from the bottom button strip** — moved into the status strip (it's a state action, not a list action).
- **Bottom button strip becomes** `[Connect]` `[Add Manually…]` `[Forget]` (stretch) `[Close]`. "Add Manually…" stays where it is today.
- **Add an "Auto-connect on launch" checkbox** to the detail panel.
- **Detail panel "Radio Model" dropdown** uses the same `<optgroup>`-organized model list as the dialog (see §4.4).
- **Auto-open behavior:**
  - On app launch (when no auto-connect target is set and reachable).
  - On user-initiated or unexpected disconnect.
  - On user click of the TitleBar connection segment.
- **Auto-close behavior:** ~1 second after a connection settles into the Connected state, the panel auto-closes. The user is back to operating the radio. Re-open is one click on the TitleBar.

### 4.4 Add Radio dialog

A rebuild of the existing `AddCustomRadioDialog`. Same fields, but the **Model** dropdown becomes the load-bearing input.

Fields:

| Field | Default | Notes |
|---|---|---|
| Name | empty | Defaults to "Model · IP suffix" if left blank ("Hermes Lite 2 @ .3") |
| IP / Port | empty / 1024 | 1024 is correct for both P1 and P2 — the field is the override |
| Model | "Auto-detect (probe will fill this in)" | 16 SKUs in `<optgroup>`s by silicon family; see §5.1 |
| Protocol | "Auto-detect" | Probe tries both; user picks explicitly only when saving offline |
| MAC | empty | Optional; required for "Pin to MAC" |
| Pin to MAC | checked | Survives DHCP IP changes |
| Auto-connect on launch | unchecked | Per-radio flag |

Two action buttons:

- `Probe and connect now` (primary) — sends a unicast probe, on success fills the silicon/firmware/MAC from the reply, picks the user-selected Model (or family default if Auto-detect), saves the entry, and proceeds straight into the Connecting → Connected path.
- `Save offline` (secondary) — skips the probe entirely. User must set Protocol explicitly (we surface a small inline note when they click Save offline with Protocol = Auto-detect, asking them to pick).

Failure handling: probe times out → dialog **stays open**, form preserved, red error band drops in above the buttons explaining the likely causes ("Couldn't reach 192.168.8.3 after 1.5 s. Radio off, IP wrong, or VPN tunnel down. Your form is preserved — change a field and retry, or save offline for later."). Buttons re-enable; primary becomes "Retry probe", secondary stays "Save offline".

Why a dialog instead of inline strip: many radios share silicon. The user must specify the SKU. Inlining the manual-IP path hid that requirement. The dialog makes the SKU input a first-class field rather than a side-effect of probing.

### 4.5 Spectrum disconnect overlay

When the connection state goes from Connected → not-Connected, the spectrum trace and waterfall fade to ~40 % opacity over 800 ms. A semi-transparent **DISCONNECTED** label drops in centered on the spectrum area, with a subtle pulse so the eye picks it up. Click anywhere on the spectrum → opens the ConnectionPanel.

The waterfall already gets cleared on disconnect via [`clearWaterfallHistory()` at MainWindow.cpp:741-746](../../src/gui/MainWindow.cpp). The fade is the new part — it preserves the last spectrum frame at reduced visibility (so the user remembers what they were looking at) while making it unmistakable that this is not live data.

### 4.6 Radio menu

Today's menu has four items that are aliases. New menu, role-based:

```
Radio
├── Connect              ⌘K       (disabled when connected — opens panel if no last radio)
├── Disconnect           ⌘⇧K      (disabled when disconnected)
├── ──────────────────
├── Discover Now                  (sends broadcast scan; no modal opens)
├── Manage Radios…                (opens the ConnectionPanel)
├── ──────────────────
├── Antenna Setup…       (NYI)
├── Transverters…        (NYI)
├── ──────────────────
└── Protocol Info                 (disabled when disconnected)
```

Each item does one thing, and the enabledness reflects connection state. Connect/Disconnect are mutually exclusive — never both enabled. Discover Now mid-session brings the panel up if it's closed (so the user sees the new results) but does not pop a modal otherwise — results flow into the panel/table and into the TitleBar dot directly.

## 5. Data model

### 5.1 Model dropdown contents

The dropdown lists all 16 user-facing models from [displayName(HPSDRModel) at src/core/HpsdrModel.h:160-182](../../src/core/HpsdrModel.h), organized by silicon family in `<optgroup>` sections so the relationship between product and ASIC is visible:

```
Auto-detect (probe will fill this in)             ← default
─────────
Atlas / Metis
  HPSDR (Atlas / Metis)
Hermes (1 ADC)
  Hermes
  ANAN-10
  ANAN-100
Hermes II (1 ADC)
  ANAN-10E
  ANAN-100B
Angelia (2 ADC)
  ANAN-100D
Orion (2 ADC · 50 V)
  ANAN-200D
Orion MkII (2 ADC · MKII BPF)
  Orion MkII (generic)
  ANAN-7000DLE
  ANAN-8000DLE
  Anvelina Pro 3
  Red Pitaya
Hermes Lite 2
  Hermes Lite 2
Saturn (ANAN-G2)
  ANAN-G2
  ANAN-G2 1K
```

Note: HL2 has additional variants in the wild (HL Plus, HL Plus 2x4, HL RX-only, etc.) that we do not yet model as separate `HPSDRModel` enum values. Adding them is a separate task that extends the enum + `boardForModel` + `profileForModel` + `BoardCapsTable` — see §10.

### 5.2 RadioInfo / HardwareProfile / BoardCapabilities

No struct changes required. The existing types ([RadioInfo at src/core/RadioDiscovery.h:77](../../src/core/RadioDiscovery.h), [HardwareProfile at src/core/HardwareProfile.h:72](../../src/core/HardwareProfile.h), [BoardCapabilities at src/core/BoardCapabilities.h](../../src/core/BoardCapabilities.h)) already carry everything we need:

- `RadioInfo.modelOverride` — the per-MAC SKU pick (already persisted in AppSettings under `radios/<macKey>/modelOverride`).
- `HardwareProfile` — derived from the model via `profileForModel()`.
- `BoardCapabilities` — looked up from the silicon via `BoardCapsTable::forBoard()`, with model refinements layered on top in `RadioModel::connectToRadio()`.

### 5.3 AppSettings keys

The schema at [src/core/AppSettings.h:174-221](../../src/core/AppSettings.h) is preserved. New behavior:

- **`radios/<macKey>/lastSeen` is updated** on each successful broadcast hit *and* successful unicast probe, so the state-pill computation has a current source. Today the field exists but is only set on broadcast.
- **`radios/<macKey>/lastSeen` is consulted** for the state pill on panel open (cached) — not for any active polling.
- **Saved entries never get aged out** of the panel regardless of stale timeout, regardless of broadcast/probe results. They go into Offline pill state when last-seen is >5 min, but the row stays.

### 5.4 macKey for offline entries

[AppSettings.h:191](../../src/core/AppSettings.h) already uses a `manual-<ip>-<port>` synthetic key when no MAC is known. This stays. When the user later connects to that entry and the probe returns a real MAC, the entry can be migrated under the real-MAC key (preserving Name, Model, Auto-connect flag) — that migration is a small piece of `RadioModel::connectToRadio()` work.

## 6. Workflows

The full set, end-to-end. Each is documented in mockup form at `.superpowers/brainstorm/67485-1777249005/content/connect-flows-r5.html` and `add-radio-flows-r7.html` — this section is the textual spec.

### 6.1 Cold launch, no auto-connect target set

1. App window appears. TitleBar grey dot. Spectrum + waterfall render with DISCONNECTED overlay. Status bar reads disconnected with last-connected breadcrumb if any.
2. ConnectionPanel auto-opens (modal, centered) — the spectrum is dimmed behind it until the user picks something or cancels.
3. Background launch broadcast scan kicks off ([RadioDiscovery::startDiscovery()](../../src/core/RadioDiscovery.cpp)). Panel header reads "Scanning…"; results stream in as radios reply.
4. User clicks a row → detail panel populates (with model dropdown for refinement) → clicks Connect → state transitions Probing → Connecting → Connected.
5. TitleBar settles into connected state. Spectrum overlay clears. Waterfall starts scrolling. Panel auto-closes 1 s after Connected stabilizes.

### 6.2 Cold launch, auto-connect target set

1. App window appears. TitleBar dot amber, "Connecting to ANAN-G2 (auto-connect)…". No panel.
2. Connection proceeds as normal. Same end-state as 6.1 step 5.
3. **On auto-connect failure:** TitleBar dot goes red briefly, ConnectionPanel auto-opens with the auto-connect target highlighted in offline state. Status bar reads: "Auto-connect target ANAN-G2 isn't reachable from this network. Pick a different radio or update the address."

### 6.3 Manual IP, probe success (Workflow A in mockups)

1. User clicks `Add Manually…` in the bottom button strip → Add Radio dialog opens.
2. Fills Name, IP/Port, picks Model (or leaves Auto-detect).
3. Clicks `Probe and connect now`. Dialog shows probing overlay (~1.5 s).
4. Probe replies. Dialog auto-closes. Saved entry lands in the table tagged "just now (probe)". Connection proceeds → Connected. TitleBar lights up.

### 6.4 Manual IP, probe fails, user pivots to Save offline (Workflow B)

1. Same as 6.3 steps 1–3.
2. Probe times out (~1.5 s). Dialog stays open. Red error band above the buttons. Form preserved. Buttons swap to `Retry probe` / `Save offline` / `Cancel`.
3. User clicks `Save offline`. Dialog closes. Entry lands in the table with red Offline pill, "never seen" timestamp.
4. Later, when the radio is reachable, double-click the row → same Probing → Connecting → Connected path as 6.3 from step 3 onward, no need to reopen the dialog.

### 6.5 Save offline directly (Workflow C)

1. User opens dialog. Knows the radio is unreachable right now.
2. Fills Name, IP, Model. **Sets Protocol explicitly** (not Auto-detect, since no probe).
3. Clicks `Save offline`. If Protocol is still Auto-detect, dialog drops a blue info band ("Saving without probing — protocol must be set explicitly because there's no reply to learn from. Pick P1 or P2.") and the user picks; they can then re-click Save offline.
4. Dialog closes. Same end-state as 6.4 step 3.

### 6.6 User-initiated disconnect

1. User clicks Disconnect in the status strip (or `Radio → Disconnect`, ⌘⇧K).
2. Brief amber TitleBar pulse during teardown (UDP stop frames, WDSP channel exchange). Usually <200 ms.
3. Multi-cue Disconnected feedback:
   - TitleBar dot grey + "Disconnected — click to connect."
   - Spectrum trace + waterfall fade to ~40 % over 800 ms; DISCONNECTED overlay drops in.
   - Applet strip dims to ~45 %.
   - Status bar: red dot + "No radio connected" + last-connected breadcrumb.
   - ConnectionPanel auto-opens with the just-disconnected radio highlighted at the top + Reconnect button.
4. User clicks Reconnect → goes through Probing → Connecting → Connected (6.3 from step 3 onward).

### 6.7 Link lost (unexpected)

1. Connection is up. No frames received for 5+ seconds.
2. TitleBar dot goes red-pulse. Status text: "Link lost — last frame 5 s ago."
3. ConnectionPanel auto-opens. The affected row shows red pulse pill + "Link lost · X s ago" in the Last Seen column. Inline `Reconnect` button.
4. **No silent auto-retry.** The user explicitly clicks Reconnect; the same Probing → Connecting → Connected path runs. (Auto-retry is in §10 as a deferred decision.)

## 7. Behaviors and policies

### 7.1 Discovery cadence

**One-shot, user-triggered. No background polling.**

- App launch: one broadcast scan ([MainWindow.cpp:474](../../src/gui/MainWindow.cpp)).
- User clicks ↻ Scan in the panel header: one broadcast scan + parallel unicast probes for any saved radios that did not appear in the broadcast result. Total time ≈ broadcast scan duration (4.5 s default profile) since unicast probes run concurrently and finish faster.
- Otherwise: nothing. No timer-driven re-scans, no daemon thread.

This matches Thetis upstream behavior (`tryDiscoverRadios()` at `setup.cs:35733` in [v2.10.3.13]) and avoids re-introducing the 15–20 s main-thread stall that the prior continuous-scan attempt caused (constructor comment at [RadioDiscovery.cpp:136-139](../../src/core/RadioDiscovery.cpp)).

### 7.2 Unicast probe

**New code path.** Currently absent from `RadioDiscovery`.

- Triggered by: user types IP + clicks Connect in the dialog, or double-clicks a saved row, or auto-connect-on-launch path.
- Sends a P1 probe (`0xEF 0xFE 0x02`) and a P2 probe (`0x00 0x00 0x00 0x00 0x02`) **in parallel** to the target IP:port.
- Timeout: 1.5 s.
- On reply: parses with the existing `parseP1Reply()` / `parseP2Reply()` ([src/core/RadioDiscovery.cpp:188](../../src/core/RadioDiscovery.cpp)) — no new parsing needed. Updates the radio's `lastSeen`, fills board / firmware / MAC.
- On timeout: emits a structured failure (typed, not a string) — sufficient for the dialog to render the red error band with a specific cause if we can detect one (UDP unreachable vs no reply vs malformed reply).

### 7.3 State pill thresholds

| Pill | Condition |
|---|---|
| 🟢 Online | last seen <60 s ago |
| 🟡 Stale | last seen 60 s – 5 min ago |
| 🔴 Offline | last seen >5 min ago, OR last probe failed, OR never seen this session |

Clicking Connect always sends a fresh probe regardless of pill color. The pill is information about *probability of success*, not a gatekeeper.

### 7.4 Stale timeout

**Saved radios never age out.** They appear in the panel's table forever, until the user clicks Forget. Their state pill reflects the most recent broadcast/probe result.

Discovered-only radios (never saved) age out at **60 s** after their last broadcast (raised from 15 s — long enough not to flap on a single-NIC scan; short enough to clean up after the user closes the panel).

The currently connected radio is exempt (today's behavior preserved — [`m_connectedMac` exemption at RadioDiscovery.cpp:494](../../src/core/RadioDiscovery.cpp)).

### 7.5 Auto-connect on launch

The per-radio `autoConnect` flag at [src/core/AppSettings.h:185](../../src/core/AppSettings.h) is the existing mechanism. The new layer:

- If exactly one radio has `autoConnect = true`: app launches, immediately probes it, transitions Probing → Connecting → Connected. Panel does **not** auto-open. Status is communicated via the TitleBar.
- If the auto-connect target fails to probe within 1.5 s: panel auto-opens with the target highlighted offline + status-bar diagnostic. User picks something else or fixes the target.
- If multiple radios have `autoConnect = true`: pick the one most recently connected (`radios/lastConnected` MAC). Surface a one-time setup-bar warning: "Multiple radios marked Auto-connect on launch. Using ANAN-G2 (most recent). Adjust in Manage Radios."
- If no radio has `autoConnect = true`: regular cold-launch flow (panel auto-opens, broadcast scan).

### 7.6 Modality

- **ConnectionPanel: modal** (`Qt::WindowModality::ApplicationModal`). Today's behavior preserved. Auto-open-on-launch and auto-open-on-disconnect are the load-bearing cases for the panel — and exactly the cases where modality is right: there's no useful work to do without a connection, so blocking the rest of the UI costs nothing and forces engagement instead of leaving the panel as an easily-missed floating widget. The "let me poke at Setup while picking a radio" case is rare enough to handle by closing the panel first.
- **Add Radio dialog: modal.** A small focused form; modal is right.

Both modals dim the parent window slightly (the spectrum, the applet strip) while open — the existing dimming behavior of `QDialog`, no new code required.

### 7.7 Auto-close on connect

ConnectionPanel auto-closes ~1 s after the connection state settles into Connected. Re-open is one click on the TitleBar segment. The 1 s buffer lets the user see the green pill / status flip before the panel disappears (rather than feeling like the panel is yanked away).

### 7.8 "Discover Now" mid-session

If the panel is closed, `Radio → Discover Now` opens it (so the user sees the new results). If the panel is already open, it just runs the scan. Either way, results land in the panel's table; the TitleBar dot doesn't change for non-connected discovery results, but a transient status-bar message ("Found 1 new radio: Hermes Lite 2 at 192.168.1.55") confirms the action.

## 8. Implementation surface

Files that change, with one-line summaries. No code in this doc — implementation belongs in the plan.

| File | Change |
|---|---|
| [src/gui/TitleBar.h/.cpp](../../src/gui/TitleBar.h) | Add `ConnectionSegment` widget — state dot + radio name + IP + RX/TX rates + activity LED. Click handler emits `connectionSegmentClicked()`. Tooltip with verbose detail. |
| [src/gui/MainWindow.cpp](../../src/gui/MainWindow.cpp) | Radio menu rework (replace 4-item alias set with 6-item role-based set + state-aware enablement). Wire TitleBar `connectionSegmentClicked()` → ConnectionPanel show. Add status-bar verbose strip. Change ConnectionPanel auto-open to fire on launch + on disconnect. |
| [src/gui/ConnectionPanel.h/.cpp](../../src/gui/ConnectionPanel.h) | Modeless. Status strip up top with inline Disconnect. State-pill column (replaces `●` glyph). Last-seen column replaces MAC. Drop Start/Stop Discovery buttons → single ↻ Scan in table header. Drop Disconnect from bottom strip. Add Auto-connect on launch checkbox to detail panel. Detail-panel model dropdown gets `<optgroup>` reorganization. Auto-close-on-connect timer. |
| [src/gui/AddCustomRadioDialog.h/.cpp](../../src/gui/AddCustomRadioDialog.h) | Replace board dropdown with model dropdown (16 SKUs in optgroups, "Auto-detect" default). Two action buttons: `Probe and connect now`, `Save offline`. Probing overlay (spinner). Failure error band. Form preservation across failure. Protocol-required note for Save offline + Auto-detect protocol case. |
| [src/gui/SpectrumWidget.h/.cpp](../../src/gui/SpectrumWidget.h) | Disconnect overlay rendering: 800 ms fade-to-40 %, DISCONNECTED label, click-anywhere-to-open-panel handler. |
| [src/core/RadioDiscovery.h/.cpp](../../src/core/RadioDiscovery.h) | Add `probeAddress(QHostAddress, quint16, std::chrono::milliseconds)` for unicast probe. Update stale policy: saved radios never age out, discovered-only radios age out at 60 s. |
| [src/core/RadioConnection.h/.cpp](../../src/core/RadioConnection.h) | Probe-then-connect flow. Structured connect-failure errors (typed: `Unreachable`, `Timeout`, `MalformedReply`, etc.). |
| [src/core/AppSettings.h/.cpp](../../src/core/AppSettings.h) | `lastSeen` updated on probe success (today only updated on broadcast). MAC-key migration for offline entries on first probe success. |
| [src/models/RadioModel.h/.cpp](../../src/models/RadioModel.h) | Connection lifecycle state exposure (`connectionState()` returning `Disconnected | Probing | Connecting | Connected | LinkLost`). Wire up auto-connect-on-launch with the new failure path. Frame-received signal for activity LED. |

## 9. Migration / backward-compat

- Existing `radios/<macKey>/*` entries continue to work. The schema is unchanged — we just write `lastSeen` more often.
- Existing `modelOverride(mac)` per-MAC stays. The detail-panel dropdown becomes the SKU-refinement path for already-discovered radios.
- The `manual-<IP>-<port>` synthetic macKey for offline entries continues. First successful probe (or connect) migrates the entry under the real MAC, preserving Name / Model / Auto-connect / Pin-to-MAC settings.
- Users with multiple `autoConnect = true` radios get one warning + the most-recent-MAC pick, which preserves their session expectations until they curate the flag in Manage Radios.

## 10. Open questions and deferred decisions

These are flagged for spec-review attention, with a current default in each case:

1. **HL2 variants beyond stock HL2** (HL Plus, HL Plus 2x4, HL RX-only). Not yet in `HPSDRModel`. **Default:** track as a separate follow-up task; the dialog model dropdown shows only what's in the enum until that lands. The Hermes Lite 2 optgroup will gain entries once added.
2. **Async discovery rewrite.** The current synchronous ~4.5 s NIC walk blocks the main thread. **Default:** ship this design with the synchronous path (matching today + Thetis upstream); cover the freeze with a clear "Scanning…" progress overlay; book the async rewrite as a follow-up phase (the dead `m_continuousTimer` field at [RadioDiscovery.h:220](../../src/core/RadioDiscovery.h) gets wired up then).
3. **Background unicast pings of saved radios.** Could update state pills in real time without user action. **Default:** not in this design; pills update on Scan and on Connect attempts only. Add as opt-in toggle in a future phase if requested.
4. **Link-lost auto-retry policy.** Today: nothing. Proposed: nothing — drawer reopens, user explicitly clicks Reconnect. Alternative: one silent retry within 5 s before showing the drawer. **Default:** no auto-retry. Open for review.
5. **Auto-close on connect.** **Default:** 1 s after Connected stabilizes. Open for review (some users may prefer it stays open until they explicitly close).
6. **Multi-auto-connect-radio behavior.** **Default:** pick most-recent-connected, surface a setup-bar warning. Alternative: a startup dialog that asks. Default chosen because the warning is reversible without blocking startup.

## 11. Testing strategy

Unit tests:

- `RadioDiscovery::probeAddress()` — happy path P1 reply, happy path P2 reply, timeout, malformed reply, unreachable host.
- State-pill computation given various `lastSeen` deltas and probe-result histories.
- macKey migration: offline entry with `manual-IP-port` key gets migrated to real-MAC key on first probe success while preserving Name / Model / flags.
- `displayName(HPSDRModel)` enumeration matches the dialog's `<optgroup>` contents (catches drift if a model is added to the enum but not the dropdown).

Integration / manual:

- Walk through each of the seven workflows in §6 against a real HL2 (LAN), a real HL2 across a working VPN tunnel (Miguel scenario), and a configured-but-unreachable manual entry (offline-save case).
- Disconnect during active streaming — verify multi-cue feedback (TitleBar grey, spectrum fade, overlay, drawer).
- Cold launch with an unreachable auto-connect target — verify the panel auto-open + status-bar message.
- Scan with three saved radios where one is reachable via broadcast and two are L3-only — verify the parallel unicast probes succeed/fail correctly and pills update.

## 12. Related material

- Mockups (gitignored — local-only): `.superpowers/brainstorm/67485-1777249005/content/`
  - `connect-ux-r1.html` — initial drawer + TitleBar exploration
  - `connect-ux-r2.html` — polished existing-panel layout (the basis for §4.3)
  - `connect-ux-r3.html` — SKU disambiguation, model dropdown design
  - `connect-flows-r4.html` — first end-to-end workflow walkthrough
  - `connect-flows-r5.html` — corrected workflows (dialog-driven manual IP, no inline strip)
  - `menu-and-states-r6.html` — Radio menu states, full-app connected/disconnected
  - `add-radio-flows-r7.html` — dialog and its three workflows (the §6.3-6.5 spec)
- Code grounding (existing source the design builds on):
  - [src/gui/TitleBar.h](../../src/gui/TitleBar.h) — the host strip the connection segment goes into
  - [src/gui/ConnectionPanel.h](../../src/gui/ConnectionPanel.h) — existing 8-column table + detail panel pattern
  - [src/gui/AddCustomRadioDialog.h](../../src/gui/AddCustomRadioDialog.h) — existing manual-entry dialog being rebuilt
  - [src/core/RadioDiscovery.h](../../src/core/RadioDiscovery.h) — broadcast-only discovery being extended with unicast probe
  - [src/core/HpsdrModel.h](../../src/core/HpsdrModel.h) — `displayName(HPSDRModel)` for the model dropdown source
  - [src/core/HardwareProfile.h](../../src/core/HardwareProfile.h) / [src/core/BoardCapabilities.h](../../src/core/BoardCapabilities.h) — silicon/SKU mapping referenced in §5
- Upstream references:
  - Thetis `[v2.10.3.13]` `clsRadioDiscovery.cs`, `clsDiscoveredRadioPicker.cs`, `frmAddCustomRadio.cs`, `setup.cs:35594-36051`
  - mi0bot-Thetis discovery profiles (already integrated in [RadioDiscovery.h:135-161](../../src/core/RadioDiscovery.h))
  - AetherSDR TitleBar pattern (already ported in [TitleBar.h](../../src/gui/TitleBar.h))
- Public report that triggered this work: NereusSDR community feedback (Miguel Martínez, April 2026) on WireGuard-tunneled HL2 connectivity.
