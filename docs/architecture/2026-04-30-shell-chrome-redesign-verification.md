# Shell-Chrome Redesign — Manual Verification Matrix

For each row, mark Pass/Fail with the date and tester initials. This
matrix turns the design spec's §Test scenarios into a checklist.

Spec: `docs/architecture/2026-04-30-shell-chrome-redesign-design.md`
Plan: `docs/architecture/2026-04-30-shell-chrome-redesign-plan.md`

| # | Scenario | Expected | Pass/Fail |
| --- | --- | --- | --- |
| 1 | Cold launch (no saved-radio) | Segment red+solid; STATION dashed-red "Click to connect"; right-side: only CPU + UTC visible (PSU hidden until connect) | |
| 2 | Click STATION → ConnectionPanel opens | Panel modal opens, focused. | |
| 3 | Probe + connect to ANAN-G2 | Segment blue-pulsing → green-pulsing; STATION fills with radio name; PSU shows ~13.8V; PA OK badge appears (if PA fitted) | |
| 4 | Send key (MOX) | Segment dot red-pulsing-fast; ▲ red; right-side TX badge **solid red, no flash** | |
| 5 | Toggle NR off in setup | NR badge disappears from dashboard; AGC badge stays put — no layout shift | |
| 6 | Disable spectrum window narrow → 800 px | SQL drops first, then APF, NB, NR, AGC in order. Filter width never drops. | |
| 7 | Hover segment dot for 1 s | Tooltip shows full diagnostic bundle (radio, IP, MAC, proto, fw, sample rate, throughput) | |
| 8 | Click "12 ms" RTT readout | NetworkDiagnosticsDialog opens. 4 sections populated. | |
| 9 | NetworkDiagnostics → Reset session stats | Max RTT clears to current; underrun count clears | |
| 10 | Right-click STATION | Menu: Disconnect / Edit radio… / Forget radio. Each lands on the right code path. | |
| 11 | Connect to ORIONMKII / 8000D / 7000DLE | Voltage strip shows BOTH "PSU 13.8V" and "PA <drainV>" | |
| 12 | Disconnect | STATION returns to dashed "Click to connect"; PSU/PA volts return to "—"; segment red-solid | |
| 13 | Force LinkLost (kill radio mid-stream) | Segment dot orange-pulsing; tooltip shows "Disconnected. Click STATION to connect." (or transient state) | |
| 14 | OS window title still says "NereusSDR 0.2.x" | Confirmed — brand-mark redundancy intentional, OS title unchanged | |
| 15 | No NYI / dead-end click anywhere in chrome | Click every visible element. None show "TBD" or do nothing. | |
| 16 | Audio engine pip ♪ — connect with audio flowing | Pip is solid blue. Pull plug or kill backend → pip turns yellow/red within 1 s | |
| 17 | Click ♪ audio pip | Opens Audio settings page (Setup → Audio) | |
| 18 | Right-click anywhere on segment | Menu: Disconnect / Connect to other / Diagnostics / Copy IP / Copy MAC | |
| 19 | Long-running connection — segment dot pulse stays smooth | No stuttering. 1.5 s slow pulse for streaming, no observable jitter | |
| 20 | Multi-radio switching while connected | Click STATION → select different radio → first radio disconnects, new one connects, dashboard rebinds to new slice automatically | |

## Test recording

When running this matrix, screen-capture the chrome at each step where
the visual difference is interesting (4, 6, 11, 14). Store screenshots
in `docs/architecture/screenshots/2026-04-30-shell-chrome/` (gitignored
by default; commit the matrix doc only).

## Sign-off

Once all 20 rows pass, the design intent is met and sub-PR review can begin.
The design spec's "no NYI / no dead ends to userland" hard constraint is
considered satisfied when row 15 passes with zero exceptions.
